import { readFileSync } from "node:fs";
import { join } from "node:path";
import { describe, expect, it } from "vitest";
import { assertLoopbackOnly, composeEnv, contractPin, type Exec, randomCode, startBackend } from "../src/backend";
import { RefusalError } from "../src/refusal";

// "{}" is an empty `compose config --format json`; up, down and the seed INSERT ignore stdout.
const ok = { code: 0, stdout: "{}", stderr: "" };
const PIN = (JSON.parse(readFileSync(join(import.meta.dirname, "..", "contract", "manifest.json"), "utf8")) as { commit: string }).commit;
const NEWER = "0123456789abcdef0123456789abcdef01234567";
const APP = "ghcr.io/sour-dev-home/satisfactory-dash-backend:main";
const OPS = "ghcr.io/sour-dev-home/satisfactory-dash-ops:main";
const CONFIG = { services: { backend: { image: APP }, migrate: { image: OPS }, backup: { image: OPS } } };
const labels = (revision: string) => JSON.stringify({ "org.opencontainers.image.revision": revision, "org.opencontainers.image.source": "x" });
/** What a healthy machine answers: the resolved config names both images, each carries NEWER, which descends from the pin. */
const base: Exec = async (cmd, args) => {
  if (cmd === "docker" && args[0] === "image" && args[1] === "inspect") return { ...ok, stdout: `${labels(NEWER)}\n` };
  if (cmd === "git") return { ...ok, stdout: "" };
  if (args.includes("config")) return { ...ok, stdout: JSON.stringify(CONFIG) };
  return ok;
};

describe("throwaway backend", () => {
  it("makes codes the backend accepts (agent.ts EnrollmentCodeSchema)", () => {
    expect(randomCode()).toMatch(/^[A-Z2-7]{4}-[A-Z2-7]{4}-[A-Z2-7]{4}$/);
  });

  it("makes env values the backend's checks accept: a scrypt hash and a long random session secret", async () => {
    const env = await composeEnv();
    expect(env.DASHBOARD_ADMIN_PASSWORD_HASH).toMatch(/^scrypt\$32768\$8\$1\$[A-Za-z0-9+/=]{24}\$[A-Za-z0-9+/=]{88}$/);
    expect(env.SESSION_SECRET!.length).toBeGreaterThanOrEqual(43);
    expect(env.POSTGRES_PASSWORD).not.toBe(env.DB_APP_PASSWORD);
  });

  it("refuses any address that isn't loopback (Review Focus 3)", () => {
    expect(() => assertLoopbackOnly({ FRONTEND_ORIGIN: "http://0.0.0.0:3000" })).toThrow(/loopback/);
    expect(() => assertLoopbackOnly({ CORS_ALLOWED_ORIGINS: "http://192.168.1.20" })).toThrow(/loopback/);
    expect(() => assertLoopbackOnly({ FRONTEND_ORIGIN: "http://127.0.0.1:3000", LOG_LEVEL: "info" })).not.toThrow();
  });

  it("runs compose up with both files, seeds a code, reads the state, and always runs down -v (Review Focus 2)", async () => {
    const seen: string[] = [];
    const exec: Exec = async (cmd, args, o) => {
      seen.push([cmd, ...args].join(" "));
      if (args.includes("psql") && args.join(" ").includes("SELECT")) return { ...ok, stdout: "t|spike-s5|t|f\n" };
      return base(cmd, args, o);
    };
    const backend = await startBackend({ checkout: "D:/dash", relayFile: "D:/mod/harness/compose/s5-relay.yaml", port: 13101, exec });
    expect(backend.url).toBe("http://127.0.0.1:13101");
    await backend.seedCode("s5", "AB3D-7XQ2-K5MN");
    expect(await backend.state("s5")).toEqual({ codeConsumed: true, agentVersion: "spike-s5", lastSeen: true, revoked: false });
    expect(seen.find((s) => s.includes("INSERT INTO servers.servers"))).toContain("'spike-s5'");
    await backend.down();
    expect(seen[0]).toMatch(/^docker compose -p satis-s5 -f compose\.yaml -f D:\/mod\/harness\/compose\/s5-relay\.yaml down -v$/);
    expect(seen.findIndex((s) => / up -d --wait/.test(s))).toBeGreaterThan(seen.findIndex((s) => / config --format json/.test(s)));
    expect(seen.some((s) => s.includes("INSERT INTO agents.enrollment_codes"))).toBe(true);
    expect(seen.at(-1)).toMatch(/down -v$/);
  });

  it("starts fresh: down -v runs before up, so a killed run's database is never reused (final review, finding 2)", async () => {
    const seen: string[] = [];
    const exec: Exec = async (cmd, args, o) => (seen.push(args.slice(7).join(" ")), base(cmd, args, o));
    await startBackend({ checkout: "D:/dash", relayFile: "r.yaml", port: 13101, exec });
    expect(seen.indexOf("down -v")).toBeLessThan(seen.indexOf("up -d --wait"));
    expect(seen.indexOf("down -v")).toBeGreaterThanOrEqual(0);
  });

  const withConfig = (config: unknown) => {
    const seen: string[] = [];
    const exec: Exec = async (_cmd, args) => {
      seen.push(args.join(" "));
      return args.includes("config") ? { ...ok, stdout: JSON.stringify(config) } : ok;
    };
    return { seen, exec };
  };

  it("refuses before up when the resolved compose config publishes a port beyond loopback (final review, finding 1)", async () => {
    const { seen, exec } = withConfig({ services: { backend: { ports: [{ host_ip: "0.0.0.0", target: 3101, published: "13101" }] } } });
    await expect(startBackend({ checkout: "D:/dash", relayFile: "r.yaml", port: 13101, exec })).rejects.toThrow(/loopback/);
    expect(seen.some((s) => s.includes(" up "))).toBe(false);
    const noHost = withConfig({ services: { backend: { ports: [{ target: 3101, published: "13101" }] } } });
    await expect(startBackend({ checkout: "D:/dash", relayFile: "r.yaml", port: 13101, exec: noHost.exec })).rejects.toThrow(/loopback/);
  });

  it("refuses before up when the resolved environment names a non-loopback URL (final review, finding 1)", async () => {
    const { seen, exec } = withConfig({ services: { backend: { environment: { FRONTEND_ORIGIN: "https://dash.example.com" } } } });
    await expect(startBackend({ checkout: "D:/dash", relayFile: "r.yaml", port: 13101, exec })).rejects.toThrow(/FRONTEND_ORIGIN is not loopback/);
    expect(seen.some((s) => s.includes(" up "))).toBe(false);
  });

  /** A started backend whose psql answers with `reply`; `sql` holds each psql -c argument, `all` every command. */
  const started = async (reply = "") => {
    const sql: string[] = [];
    const all: string[] = [];
    const exec: Exec = async (cmd, args, o) => {
      all.push(args.join(" "));
      if (args.includes("psql")) {
        sql.push(args.at(-1)!);
        return { ...ok, stdout: reply };
      }
      return base(cmd, args, o);
    };
    const backend = await startBackend({ checkout: "D:/dash", relayFile: "r.yaml", port: 13101, exec });
    return { backend, sql, all };
  };

  it("seeds a code for the node agent's server, adding the server only if it isn't there (public_id is UNIQUE)", async () => {
    const { backend, sql } = await started();
    await backend.seedCode("node", "AB3D-7XQ2-K5MN");
    expect(sql).toHaveLength(1);
    expect(sql[0]).toMatch(/INSERT INTO servers\.servers \(public_id, display_name\) VALUES \('core-node', '[^']+'\) ON CONFLICT \(public_id\) DO NOTHING;/);
    expect(sql[0]).toMatch(/INSERT INTO agents\.enrollment_codes .* WHERE public_id = 'core-node';/);
    expect(sql[0]).not.toContain("AB3D-7XQ2-K5MN"); // only the hash reaches the database
  });

  it("revokes the mod's credential (agent_credentials.revoked_at)", async () => {
    const { backend, sql } = await started();
    await backend.revoke("mod");
    expect(sql[0]).toMatch(/^UPDATE agents\.agent_credentials SET revoked_at = now\(\) .*public_id = 'core-mod'/);
  });

  it("queues a set_auto_pause command for five minutes and returns its id", async () => {
    const id = "6f1c2b9e-1d2a-4c3b-9e8f-0a1b2c3d4e5f";
    const { backend, sql } = await started(`${id}\nINSERT 0 1\n`);
    expect(await backend.queueCommand("mod", { enabled: true })).toBe(id);
    expect(sql[0]).toMatch(/^INSERT INTO agents\.commands \(server_id, type, params, expires_at\) SELECT id, 'set_auto_pause', '\{"enabled":true\}'::jsonb, now\(\) \+ interval '5 minutes' FROM servers\.servers WHERE public_id = 'core-mod' RETURNING id;$/);
  });

  it("refuses command params that aren't one { enabled } object before any SQL runs", async () => {
    const { backend, sql } = await started();
    await expect(backend.queueCommand("mod", { enabled: true, x: 1 })).rejects.toThrow(/params/);
    await expect(backend.queueCommand("mod", { other: "x'; DROP TABLE x; --" })).rejects.toThrow(/params/);
    expect(sql).toHaveLength(0);
  });

  it("doubles single quotes inside the params literal", async () => {
    const { backend, sql } = await started("6f1c2b9e-1d2a-4c3b-9e8f-0a1b2c3d4e5f\n");
    await backend.queueCommand("mod", { enabled: "it's" });
    expect(sql[0]).toContain(`'{"enabled":"it''s"}'::jsonb`);
  });

  it("reads a command's status and result code", async () => {
    const { backend, sql } = await started("failed|upstream_error\n");
    expect(await backend.command("6f1c2b9e-1d2a-4c3b-9e8f-0a1b2c3d4e5f")).toEqual({ status: "failed", resultCode: "upstream_error" });
    expect(sql[0]).toContain("'6f1c2b9e-1d2a-4c3b-9e8f-0a1b2c3d4e5f'");
  });

  it("reads a pending command's result code as null", async () => {
    const { backend } = await started("pending|\n");
    expect(await backend.command("6f1c2b9e-1d2a-4c3b-9e8f-0a1b2c3d4e5f")).toEqual({ status: "pending", resultCode: null });
  });

  it("refuses a command id that isn't a UUID before any SQL runs", async () => {
    const { backend, sql } = await started();
    await expect(backend.command("1' OR '1'='1")).rejects.toThrow(/UUID/);
    expect(sql).toHaveLength(0);
  });

  it("reads revoked in the state and the newest code only", async () => {
    const { backend, sql } = await started("t|mod/0.1.0|t|t\n");
    expect(await backend.state("mod")).toEqual({ codeConsumed: true, agentVersion: "mod/0.1.0", lastSeen: true, revoked: true });
    expect(sql[0]).toContain("revoked_at IS NOT NULL");
    expect(sql[0]).toContain("ORDER BY c.created_at DESC LIMIT 1");
    expect(sql[0]).toContain("'core-mod'");
  });

  it("stops and starts the relay", async () => {
    const { backend, all } = await started();
    await backend.stopRelay();
    await backend.startRelay();
    expect(all.slice(-2).map((s) => s.split(" ").slice(7).join(" "))).toEqual(["stop spike-relay", "start spike-relay"]);
  });

  it("runs down -v when compose up fails", async () => {
    const seen: string[] = [];
    const exec: Exec = async (cmd, args, o) => (seen.push(args.join(" ")), args.includes("up") ? { code: 1, stdout: "", stderr: "boom" } : base(cmd, args, o));
    await expect(startBackend({ checkout: "D:/dash", relayFile: "r.yaml", port: 13101, exec })).rejects.toThrow(/compose up failed/);
    expect(seen.at(-1)).toMatch(/down -v$/);
  });
});

describe("the backend image guard (enrol run 39609914480: a stale local image)", () => {
  /** base, with one command's answer replaced; every command is kept with its cwd. */
  const machine = (answer: (cmd: string, args: string[]) => { code: number; stdout: string; stderr: string } | undefined) => {
    const seen: Array<{ line: string; cwd: string }> = [];
    const exec: Exec = async (cmd, args, o) => {
      seen.push({ line: [cmd, ...args].join(" "), cwd: o.cwd });
      return answer(cmd, args) ?? base(cmd, args, o);
    };
    return { seen, exec };
  };
  const start = (exec: Exec) => startBackend({ checkout: "D:/dash", relayFile: "r.yaml", port: 13101, exec });
  const booted = (seen: Array<{ line: string }>) => seen.some((s) => / up -d --wait/.test(s.line));

  it("pins the contract's commit (harness/contract/manifest.json)", () => {
    expect(contractPin()).toBe(PIN);
  });

  it("reads each image's revision label, checks it descends from the pin with the checkout's git, and records both", async () => {
    const { seen, exec } = machine(() => undefined);
    const backend = await start(exec);
    expect(backend.images).toEqual({ app: { image: APP, revision: NEWER }, ops: { image: OPS, revision: NEWER } });
    const inspect = seen.filter((s) => s.line.startsWith("docker image inspect"));
    expect(inspect.map((s) => s.line)).toEqual([
      `docker image inspect --format {{json .Config.Labels}} ${APP}`,
      `docker image inspect --format {{json .Config.Labels}} ${OPS}`,
    ]);
    const git = seen.filter((s) => s.line.startsWith("git "));
    expect(git.map((s) => s.line)).toEqual([`git merge-base --is-ancestor ${PIN} ${NEWER}`, `git merge-base --is-ancestor ${PIN} ${NEWER}`]);
    expect(git.every((s) => s.cwd === "D:/dash")).toBe(true);
    // All of it before the stack boots.
    const up = seen.findIndex((s) => / up -d --wait/.test(s.line));
    expect(seen.findIndex((s) => s.line.startsWith("git "))).toBeLessThan(up);
  });

  it("accepts an image built from the pin itself", async () => {
    const { exec } = machine((cmd, args) => (cmd === "docker" && args[1] === "inspect" ? { ...ok, stdout: labels(PIN) } : undefined));
    expect((await start(exec)).images.app.revision).toBe(PIN);
  });

  it("refuses before any boot when an image's revision isn't the pin or a descendant of it", async () => {
    const { seen, exec } = machine((cmd) => (cmd === "git" ? { code: 1, stdout: "", stderr: "" } : undefined));
    const err = await start(exec).catch((e: unknown) => e);
    expect(err).toBeInstanceOf(RefusalError);
    expect(String(err)).toContain(`${APP} was built from ${NEWER}`);
    expect(String(err)).toContain(`not ${PIN} or a descendant of it`);
    expect(String(err)).toMatch(/docker pull/);
    expect(booted(seen)).toBe(false);
  });

  it("refuses when the checkout's git doesn't know the image's commit (fetch the checkout, no network from here)", async () => {
    const { seen, exec } = machine((cmd) => (cmd === "git" ? { code: 128, stdout: "", stderr: "fatal: Not a valid commit name" } : undefined));
    const err = await start(exec).catch((e: unknown) => e);
    expect(err).toBeInstanceOf(RefusalError);
    expect(String(err)).toMatch(/git fetch/);
    expect(booted(seen)).toBe(false);
  });

  it("refuses when an image isn't on this machine", async () => {
    const { seen, exec } = machine((cmd, args) => (cmd === "docker" && args[1] === "inspect" && args.at(-1) === OPS ? { code: 1, stdout: "", stderr: "No such image" } : undefined));
    const err = await start(exec).catch((e: unknown) => e);
    expect(err).toBeInstanceOf(RefusalError);
    expect(String(err)).toContain(`${OPS} is not on this machine`);
    expect(booted(seen)).toBe(false);
  });

  it("refuses an image with no revision label, or one that isn't a full commit, without running git on it", async () => {
    for (const stdout of ["null\n", JSON.stringify({ other: "x" }), labels("main"), labels(`${NEWER} --upload-pack=x`)]) {
      const { seen, exec } = machine((cmd, args) => (cmd === "docker" && args[1] === "inspect" ? { ...ok, stdout } : undefined));
      const err = await start(exec).catch((e: unknown) => e);
      expect(err).toBeInstanceOf(RefusalError);
      expect(String(err)).toMatch(/revision/);
      expect(seen.some((s) => s.line.startsWith("git "))).toBe(false);
      expect(booted(seen)).toBe(false);
    }
  });

  it("refuses when the resolved config names no image for the backend or the migrations", async () => {
    const { seen, exec } = machine((_cmd, args) => (args.includes("config") ? { ...ok, stdout: JSON.stringify({ services: { backend: { image: APP } } }) } : undefined));
    await expect(start(exec)).rejects.toThrow(/migrate/);
    expect(booted(seen)).toBe(false);
  });
});
