import { describe, expect, it } from "vitest";
import { assertLoopbackOnly, composeEnv, type Exec, randomCode, startBackend } from "../src/backend";

// "{}" is an empty `compose config --format json`; up, down and the seed INSERT ignore stdout.
const ok = { code: 0, stdout: "{}", stderr: "" };

describe("throwaway backend", () => {
  it("makes codes the backend accepts (agent.ts EnrollmentCodeSchema)", () => {
    expect(randomCode()).toMatch(/^[A-Z2-7]{4}-[A-Z2-7]{4}$/);
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
    const exec: Exec = async (cmd, args) => {
      seen.push([cmd, ...args].join(" "));
      if (args.includes("psql") && args.join(" ").includes("SELECT")) return { ...ok, stdout: "t|spike-s5|t\n" };
      return ok;
    };
    const backend = await startBackend({ checkout: "D:/dash", relayFile: "D:/mod/harness/compose/s5-relay.yaml", port: 13101, exec });
    expect(backend.url).toBe("http://127.0.0.1:13101");
    await backend.seedCode("AB3D-7XQ2");
    expect(await backend.state()).toEqual({ codeConsumed: true, agentVersion: "spike-s5", lastSeen: true });
    await backend.down();
    expect(seen[0]).toMatch(/^docker compose -p satis-s5 -f compose\.yaml -f D:\/mod\/harness\/compose\/s5-relay\.yaml down -v$/);
    expect(seen.findIndex((s) => / up -d --wait/.test(s))).toBeGreaterThan(seen.findIndex((s) => / config --format json/.test(s)));
    expect(seen.some((s) => s.includes("INSERT INTO agents.enrollment_codes"))).toBe(true);
    expect(seen.at(-1)).toMatch(/down -v$/);
  });

  it("starts fresh: down -v runs before up, so a killed run's database is never reused (final review, finding 2)", async () => {
    const seen: string[] = [];
    const exec: Exec = async (_cmd, args) => (seen.push(args.slice(7).join(" ")), ok);
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

  it("runs down -v when compose up fails", async () => {
    const seen: string[] = [];
    const exec: Exec = async (_cmd, args) => (seen.push(args.join(" ")), args.includes("up") ? { code: 1, stdout: "", stderr: "boom" } : ok);
    await expect(startBackend({ checkout: "D:/dash", relayFile: "r.yaml", port: 13101, exec })).rejects.toThrow(/compose up failed/);
    expect(seen.at(-1)).toMatch(/down -v$/);
  });
});
