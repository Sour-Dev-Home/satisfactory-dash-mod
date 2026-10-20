import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import type { Backend } from "../src/backend";
import { connectBackendHost, serveBackendHost } from "../src/backend-host";

const ID = "6f1c2b9e-1d2a-4c3b-9e8f-0a1b2c3d4e5f";

function fakeBackend() {
  const calls: string[] = [];
  const backend: Backend = {
    url: "http://127.0.0.1:13101",
    seedCode: async (s, c) => void calls.push(`seed ${s} ${c}`),
    state: async (s) => (calls.push(`state ${s}`), { codeConsumed: true, agentVersion: "mod/0.1.0", lastSeen: true, revoked: false }),
    revoke: async (s) => void calls.push(`revoke ${s}`),
    queueCommand: async (s, p) => (calls.push(`queue ${s} ${JSON.stringify(p)}`), ID),
    command: async (id) => (calls.push(`command ${id}`), { status: "succeeded", resultCode: null }),
    stopRelay: async () => void calls.push("relay stop"),
    startRelay: async () => void calls.push("relay start"),
    down: async () => void calls.push("down"),
  };
  return { backend, calls };
}

let dirs: string[] = [];
let closers: Array<() => Promise<void>> = [];
afterEach(async () => {
  for (const c of closers) await c();
  closers = [];
  for (const d of dirs) rmSync(d, { recursive: true, force: true });
  dirs = [];
});
const tempDir = () => {
  const d = mkdtempSync(join(tmpdir(), "host-"));
  dirs.push(d);
  return d;
};

async function serve() {
  const dir = tempDir();
  const file = join(dir, "backend-host.json");
  const fake = fakeBackend();
  const host = await serveBackendHost({ backend: fake.backend, file, port: 0 });
  closers.push(() => host.close());
  const info = JSON.parse(readFileSync(file, "utf8")) as { relayUrl: string; controlUrl: string; token: string };
  const call = (method: string, path: string, body?: unknown, token: string | null = info.token) =>
    fetch(new URL(path, info.controlUrl), {
      method,
      headers: { ...(token === null ? {} : { authorization: `Bearer ${token}` }), ...(body === undefined ? {} : { "content-type": "application/json" }) },
      ...(body === undefined ? {} : { body: typeof body === "string" ? body : JSON.stringify(body) }),
    });
  return { ...fake, host, file, info, call };
}

describe("backend host (ruling 10)", () => {
  it("listens on loopback only and writes the host file with a 64-hex token", async () => {
    const { info } = await serve();
    expect(info.relayUrl).toBe("http://127.0.0.1:13101");
    expect(new URL(info.controlUrl).hostname).toBe("127.0.0.1");
    expect(info.token).toMatch(/^[0-9a-f]{64}$/);
    expect(Object.keys(info).sort()).toEqual(["controlUrl", "relayUrl", "token"]);
  });

  it("answers 401 to a call with no token or a wrong one, and touches nothing", async () => {
    const { call, calls, info } = await serve();
    expect((await call("POST", "/revoke", { server: "mod" }, null)).status).toBe(401);
    expect((await call("POST", "/revoke", { server: "mod" }, "0".repeat(64))).status).toBe(401);
    expect((await call("POST", "/revoke", { server: "mod" }, info.token.slice(1))).status).toBe(401);
    expect((await call("GET", "/state/mod", undefined, "")).status).toBe(401);
    expect(calls).toEqual([]);
  });

  it("serves the fixed operations", async () => {
    const { call, calls } = await serve();
    expect((await call("POST", "/seed", { server: "node", code: "AB3D-7XQ2" })).status).toBe(204);
    expect((await call("POST", "/revoke", { server: "mod" })).status).toBe(204);
    const queued = await call("POST", "/command", { server: "mod", params: { enabled: false } });
    expect(await queued.json()).toEqual({ id: ID });
    expect(await (await call("GET", `/command/${ID}`)).json()).toEqual({ status: "succeeded", resultCode: null });
    expect(await (await call("GET", "/state/mod")).json()).toEqual({ codeConsumed: true, agentVersion: "mod/0.1.0", lastSeen: true, revoked: false });
    expect((await call("POST", "/relay/stop")).status).toBe(204);
    expect((await call("POST", "/relay/start")).status).toBe(204);
    expect(calls).toEqual(["seed node AB3D-7XQ2", "revoke mod", 'queue mod {"enabled":false}', `command ${ID}`, "state mod", "relay stop", "relay start"]);
  });

  it("refuses loose bodies, other servers and unknown routes, touching nothing", async () => {
    const { call, calls } = await serve();
    expect((await call("POST", "/seed", { server: "mod", code: "ab3d-7xq2" })).status).toBe(400);
    expect((await call("POST", "/seed", { server: "mod", code: "AB3D-7XQ1" })).status).toBe(400);
    expect((await call("POST", "/seed", { server: "s5", code: "AB3D-7XQ2" })).status).toBe(400);
    expect((await call("POST", "/seed", { server: "mod", code: "AB3D-7XQ2", sql: "x" })).status).toBe(400);
    expect((await call("POST", "/revoke", "not json")).status).toBe(400);
    expect((await call("POST", "/command", { server: "mod", params: { enabled: true, extra: 1 } })).status).toBe(400);
    expect((await call("GET", "/command/1' OR 1=1")).status).toBe(404);
    expect((await call("GET", "/command/not-a-uuid")).status).toBe(404);
    expect((await call("GET", "/state/s5")).status).toBe(404);
    expect((await call("GET", "/relay/stop")).status).toBe(404);
    expect((await call("POST", "/sql", { q: "SELECT 1" })).status).toBe(404);
    expect((await call("POST", "/down")).status).toBe(404);
    expect(calls).toEqual([]);
  });

  it("answers 500 when the backend fails, and keeps serving", async () => {
    const { call, backend } = await serve();
    backend.revoke = async () => { throw new Error("psql failed"); };
    expect((await call("POST", "/revoke", { server: "mod" })).status).toBe(500);
    expect((await call("GET", "/state/mod")).status).toBe(200);
  });

  it("on close, runs down and deletes the host file", async () => {
    const { host, file, calls } = await serve();
    closers = [];
    await host.close();
    expect(calls).toContain("down");
    expect(existsSync(file)).toBe(false);
  });

  it("connectBackendHost reads the file and returns a client whose calls carry the token", async () => {
    const { file, calls } = await serve();
    const client = await connectBackendHost(file);
    expect(client.relayUrl).toBe("http://127.0.0.1:13101");
    await client.seedCode("mod", "AB3D-7XQ2");
    expect(await client.queueCommand("mod", { enabled: true })).toBe(ID);
    expect(await client.command(ID)).toEqual({ status: "succeeded", resultCode: null });
    expect(await client.state("mod")).toEqual({ codeConsumed: true, agentVersion: "mod/0.1.0", lastSeen: true, revoked: false });
    await client.revoke("mod");
    await client.stopRelay();
    await client.startRelay();
    expect(calls).toEqual(["seed mod AB3D-7XQ2", 'queue mod {"enabled":true}', `command ${ID}`, "state mod", "revoke mod", "relay stop", "relay start"]);
  });

  it("connectBackendHost refuses a host file that points beyond loopback, and a client call that fails throws", async () => {
    const bad = join(tempDir(), "bad.json");
    writeFileSync(bad, JSON.stringify({ relayUrl: "http://10.0.0.5:1", controlUrl: "http://127.0.0.1:1", token: "a".repeat(64) }));
    await expect(connectBackendHost(bad)).rejects.toThrow(/loopback/);
    const { file, backend } = await serve();
    backend.revoke = async () => { throw new Error("psql failed"); };
    const client = await connectBackendHost(file);
    await expect(client.revoke("mod")).rejects.toThrow(/500/);
  });
});
