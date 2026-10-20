import { createHash, randomBytes, scrypt } from "node:crypto";
import { promisify } from "node:util";
import { LOOPBACK_HOSTS } from "./config";

export type Exec = (cmd: string, args: string[], opts: { cwd: string; env?: Record<string, string> }) => Promise<{ code: number; stdout: string; stderr: string }>;
export interface Backend {
  url: string;
  seedCode(code: string): Promise<void>;
  state(): Promise<{ codeConsumed: boolean; agentVersion: string | null; lastSeen: boolean }>;
  down(): Promise<void>;
}

const PROJECT = "satis-s5";
const SERVER_PUBLIC_ID = "spike-s5"; // servers.servers.public_id: ^[a-z0-9-]{1,32}$ (1790294400000_core_tables.sql:71)
const BASE32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

/** XXXX-XXXX in base32 (agent.ts EnrollmentCodeSchema). */
export function randomCode(): string {
  const c = [...randomBytes(8)].map((b) => BASE32[b % 32]).join("");
  return `${c.slice(0, 4)}-${c.slice(4)}`;
}

const scryptAsync = promisify(scrypt) as (p: string, s: Buffer, k: number, o: object) => Promise<Buffer>;

/** Throwaway values for compose.yaml's required variables (compose.yaml:23-63). The hash is the backend's own format,
 *  scrypt$N$r$p$salt$hash with N=2^15, r=8, p=1, a 16-byte salt and a 64-byte key (passwordHash.ts:9, DEFAULT_PARAMS,
 *  KEY_LENGTH, SALT_BYTES); the session secret is 64 random base64 characters (authConfig.ts MIN_SECRET_LENGTH = 43). */
export async function composeEnv(): Promise<Record<string, string>> {
  const salt = randomBytes(16);
  const key = await scryptAsync(randomBytes(24).toString("base64"), salt, 64, { N: 2 ** 15, r: 8, p: 1, maxmem: 64 * 1024 * 1024 });
  const pw = () => randomBytes(24).toString("base64url");
  return {
    POSTGRES_PASSWORD: pw(),
    DB_MIGRATOR_PASSWORD: pw(),
    DB_APP_PASSWORD: pw(),
    DASHBOARD_ADMIN_USER: "spike",
    DASHBOARD_ADMIN_PASSWORD_HASH: ["scrypt", 32768, 8, 1, salt.toString("base64"), key.toString("base64")].join("$"),
    SESSION_SECRET: randomBytes(48).toString("base64"),
  };
}

/** Review Focus 3: every URL-looking value must point at loopback. */
export function assertLoopbackOnly(env: Record<string, string>): void {
  for (const [key, value] of Object.entries(env)) {
    for (const part of value.split(",")) {
      const v = part.trim();
      if (!/^https?:\/\//.test(v)) continue;
      if (!LOOPBACK_HOSTS.has(new URL(v).hostname)) throw new Error(`${key} is not loopback: S5's backend is local only`);
    }
  }
}

const hex = (code: string) => createHash("sha256").update(code, "utf8").digest("hex"); // agentRepository.ts:14

export async function startBackend(opts: { checkout: string; relayFile: string; port: number; exec: Exec }): Promise<Backend> {
  const env = { ...(await composeEnv()), SPIKE_BACKEND_PORT: String(opts.port) };
  assertLoopbackOnly(env);
  const compose = (...args: string[]) =>
    opts.exec("docker", ["compose", "-p", PROJECT, "-f", "compose.yaml", "-f", opts.relayFile, ...args], { cwd: opts.checkout, env });
  const psql = async (sql: string) => {
    const r = await compose("exec", "-T", "postgres", "psql", "-U", "postgres", "-d", "satis", "-v", "ON_ERROR_STOP=1", "-At", "-F", "|", "-c", sql);
    if (r.code !== 0) throw new Error(`psql failed: ${r.stderr.trim()}`);
    return r.stdout;
  };
  const down = async () => void (await compose("down", "-v"));

  const up = await compose("up", "-d", "--wait");
  if (up.code !== 0) {
    await down();
    throw new Error(`compose up failed: ${up.stderr.trim().split("\n").slice(-20).join("\n")}`);
  }
  return {
    url: `http://127.0.0.1:${opts.port}`,
    async seedCode(code) {
      // Columns from 1790294400000_core_tables.sql:68-74 and 1790899200000_agents.sql:19-28. created_by may be NULL.
      await psql(
        `INSERT INTO servers.servers (public_id, display_name) VALUES ('${SERVER_PUBLIC_ID}', 'S5 spike'); ` +
          `INSERT INTO agents.enrollment_codes (server_id, code_hash, expires_at) ` +
          `SELECT id, decode('${hex(code)}', 'hex'), now() + interval '10 minutes' FROM servers.servers WHERE public_id = '${SERVER_PUBLIC_ID}';`,
      );
    },
    async state() {
      const out = await psql(
        `SELECT (SELECT c.consumed_at IS NOT NULL FROM agents.enrollment_codes c JOIN servers.servers s ON s.id = c.server_id WHERE s.public_id = '${SERVER_PUBLIC_ID}'), ` +
          `(SELECT a.agent_version FROM agents.agent_credentials a JOIN servers.servers s ON s.id = a.server_id WHERE s.public_id = '${SERVER_PUBLIC_ID}'), ` +
          `(SELECT a.last_seen_at IS NOT NULL FROM agents.agent_credentials a JOIN servers.servers s ON s.id = a.server_id WHERE s.public_id = '${SERVER_PUBLIC_ID}');`,
      );
      const [consumed, version, seen] = out.trim().split("|");
      return { codeConsumed: consumed === "t", agentVersion: version ? version : null, lastSeen: seen === "t" };
    },
    down,
  };
}
