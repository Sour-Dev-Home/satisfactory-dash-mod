import { execFile } from "node:child_process";
import { createHash, randomBytes, scrypt } from "node:crypto";
import { promisify } from "node:util";
import { LOOPBACK_HOSTS } from "./config";

export type Exec = (cmd: string, args: string[], opts: { cwd: string; env?: Record<string, string> }) => Promise<{ code: number; stdout: string; stderr: string }>;

/** Runs a command with no shell (execFile), the compose env layered over the process's own. */
export const realExec: Exec = (cmd, args, o) => new Promise((resolve) =>
  execFile(cmd, args, { cwd: o.cwd, env: { ...process.env, ...o.env }, maxBuffer: 16 * 1024 * 1024 },
    (err, stdout, stderr) => resolve({ code: err ? (typeof err.code === "number" ? err.code : 1) : 0, stdout, stderr })));
/** servers.servers.public_id: ^[a-z0-9-]{1,32}$ (1790294400000_core_tables.sql:71). */
const PUBLIC_IDS = { mod: "core-mod", node: "core-node", s5: "spike-s5" } as const;
const DISPLAY_NAMES = { mod: "Core mod", node: "Core node agent", s5: "S5 spike" } as const;
export type BackendServer = keyof typeof PUBLIC_IDS;
export interface BackendState { codeConsumed: boolean; agentVersion: string | null; lastSeen: boolean; revoked: boolean }
export interface CommandState { status: string; resultCode: string | null }

export interface Backend {
  url: string;
  seedCode(server: BackendServer, code: string): Promise<void>;
  /** The server's newest code, and its credential (agent_credentials has one row per server). */
  state(server: BackendServer): Promise<BackendState>;
  revoke(server: BackendServer): Promise<void>;
  /** Queues a set_auto_pause command (the only type, 1790985600000_agent_commands.sql) for 5 minutes; returns its id. */
  queueCommand(server: BackendServer, params: Record<string, unknown>): Promise<string>;
  command(id: string): Promise<CommandState>;
  stopRelay(): Promise<void>;
  startRelay(): Promise<void>;
  down(): Promise<void>;
}

export const UUID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;

/** A JSON literal for SQL: one { enabled } key at most, serialised by JSON.stringify, single quotes doubled. */
function paramsLiteral(params: Record<string, unknown>): string {
  const keys = Object.keys(params);
  if (typeof params !== "object" || params === null || Array.isArray(params) || keys.length > 1 || keys.some((k) => k !== "enabled")) {
    throw new Error("command params must be one { enabled } object");
  }
  return `'${JSON.stringify(params).replaceAll("'", "''")}'::jsonb`;
}

const PROJECT = "satis-s5";
const BASE32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

/** XXXX-XXXX-XXXX in base32, 60 bits (agent.ts EnrollmentCodeSchema since dash #524, ADR-0046). */
export function randomCode(): string {
  const c = [...randomBytes(12)].map((b) => BASE32[b % 32]).join("");
  return `${c.slice(0, 4)}-${c.slice(4, 8)}-${c.slice(8)}`;
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

interface ComposeConfig {
  services?: Record<string, { ports?: Array<{ host_ip?: string; published?: string | number }>; environment?: Record<string, string | null> }>;
}

/** Every published port binds to loopback (no host_ip means 0.0.0.0), and no resolved env value is a non-loopback URL. */
function assertComposeLoopbackOnly(config: ComposeConfig): void {
  for (const [name, service] of Object.entries(config.services ?? {})) {
    for (const p of service.ports ?? []) {
      if (!p.host_ip || !LOOPBACK_HOSTS.has(p.host_ip)) throw new Error(`${name} publishes port ${p.published} on ${p.host_ip ?? "0.0.0.0"}, not loopback: S5's backend is local only`);
    }
    const env = Object.fromEntries(Object.entries(service.environment ?? {}).map(([k, v]) => [k, v ?? ""]));
    assertLoopbackOnly(env);
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

  // A run killed before its finally leaves its volumes: start from nothing every time (final review, finding 2).
  await down();
  // Review Focus 3 on what compose will actually run: the checkout's .env and the host env feed interpolation too.
  const resolved = await compose("config", "--format", "json");
  if (resolved.code !== 0) throw new Error(`compose config failed: ${resolved.stderr.trim()}`);
  assertComposeLoopbackOnly(JSON.parse(resolved.stdout) as ComposeConfig);

  const up = await compose("up", "-d", "--wait");
  if (up.code !== 0) {
    await down();
    throw new Error(`compose up failed: ${up.stderr.trim().split("\n").slice(-20).join("\n")}`);
  }
  // Every value spliced into SQL below is a fixed public id, a hex hash, a checked UUID or a checked JSON literal.
  const credential = (server: BackendServer, column: string) =>
    `(SELECT ${column} FROM agents.agent_credentials a JOIN servers.servers s ON s.id = a.server_id WHERE s.public_id = '${PUBLIC_IDS[server]}')`;
  return {
    url: `http://127.0.0.1:${opts.port}`,
    async seedCode(server, code) {
      // Columns from 1790294400000_core_tables.sql:68-74 and 1790899200000_agents.sql:19-28. created_by may be NULL.
      // A server seeded twice (a later phase's new code) keeps its row: public_id is UNIQUE (core_tables.sql:71).
      const id = PUBLIC_IDS[server];
      await psql(
        `INSERT INTO servers.servers (public_id, display_name) VALUES ('${id}', '${DISPLAY_NAMES[server]}') ON CONFLICT (public_id) DO NOTHING; ` +
          `INSERT INTO agents.enrollment_codes (server_id, code_hash, expires_at) ` +
          `SELECT id, decode('${hex(code)}', 'hex'), now() + interval '10 minutes' FROM servers.servers WHERE public_id = '${id}';`,
      );
    },
    async state(server) {
      const out = await psql(
        `SELECT (SELECT c.consumed_at IS NOT NULL FROM agents.enrollment_codes c JOIN servers.servers s ON s.id = c.server_id WHERE s.public_id = '${PUBLIC_IDS[server]}' ORDER BY c.created_at DESC LIMIT 1), ` +
          `${credential(server, "a.agent_version")}, ${credential(server, "a.last_seen_at IS NOT NULL")}, ${credential(server, "a.revoked_at IS NOT NULL")};`,
      );
      const [consumed, version, seen, revoked] = out.trim().split("|");
      return { codeConsumed: consumed === "t", agentVersion: version ? version : null, lastSeen: seen === "t", revoked: revoked === "t" };
    },
    async revoke(server) {
      // 1790899200000_agents.sql:37 (revoked_at).
      await psql(
        `UPDATE agents.agent_credentials SET revoked_at = now() WHERE server_id = (SELECT id FROM servers.servers WHERE public_id = '${PUBLIC_IDS[server]}');`,
      );
    },
    async queueCommand(server, params) {
      const literal = paramsLiteral(params);
      const out = await psql(
        `INSERT INTO agents.commands (server_id, type, params, expires_at) SELECT id, 'set_auto_pause', ${literal}, now() + interval '5 minutes' ` +
          `FROM servers.servers WHERE public_id = '${PUBLIC_IDS[server]}' RETURNING id;`,
      );
      // psql prints RETURNING's row, then the command tag (INSERT 0 1) unless quiet: the id is the first line.
      const id = out.trim().split("\n")[0]!.trim();
      if (!UUID.test(id)) throw new Error("queueing the command returned no id (is the server seeded?)");
      return id;
    },
    async command(id) {
      if (!UUID.test(id)) throw new Error("a command id must be a UUID");
      const out = await psql(`SELECT status, result_code FROM agents.commands WHERE id = '${id}';`);
      const [status, resultCode] = out.trim().split("|");
      if (!status) throw new Error("no such command");
      return { status, resultCode: resultCode ? resultCode : null };
    },
    async stopRelay() {
      const r = await compose("stop", "spike-relay");
      if (r.code !== 0) throw new Error(`stopping the relay failed: ${r.stderr.trim()}`);
    },
    async startRelay() {
      const r = await compose("start", "spike-relay");
      if (r.code !== 0) throw new Error(`starting the relay failed: ${r.stderr.trim()}`);
    },
    down,
  };
}
