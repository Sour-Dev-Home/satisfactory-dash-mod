import { createServer, type IncomingMessage, request, type Server } from "node:http";
import type { AddressInfo } from "node:net";
import { gzipSync } from "node:zlib";
import { afterEach, describe, expect, it } from "vitest";
import { startRecorder, type Recorder } from "../src/recorder";
import { sessionHash } from "../src/status";
import { compareRound } from "../../runs/enrol/pass";

const SECRET = "sec_" + "x".repeat(40);
const CODE = "AB3D-7XQ2-K5MN";
const MOD_UA = "satisfactory-dash-mod/0.1.0";

interface Seen { method: string; url: string; headers: IncomingMessage["headers"]; body: Buffer }

let closers: Array<() => Promise<void>> = [];
afterEach(async () => {
  for (const c of closers.reverse()) await c();
  closers = [];
});

async function upstream(answer: (s: Seen) => { status: number; body: string | Buffer; headers?: Record<string, string> }) {
  const seen: Seen[] = [];
  const server: Server = createServer((req, res) => {
    const chunks: Buffer[] = [];
    req.on("data", (c: Buffer) => chunks.push(c));
    req.on("end", () => {
      const s = { method: req.method!, url: req.url!, headers: req.headers, body: Buffer.concat(chunks) };
      seen.push(s);
      const a = answer(s);
      res.writeHead(a.status, { "content-type": "application/json", ...a.headers }).end(a.body);
    });
  });
  await new Promise<void>((r) => server.listen(0, "127.0.0.1", () => r()));
  closers.push(() => new Promise<void>((r) => server.close(() => r())));
  return { url: `http://127.0.0.1:${(server.address() as AddressInfo).port}`, seen };
}

async function recorderFor(url: string): Promise<Recorder> {
  const rec = await startRecorder(url);
  closers.push(() => rec.close());
  return rec;
}

/** A raw request, so the test controls every header and sees a destroyed socket as an error. */
function send(base: string, method: string, path: string, opts: { headers?: Record<string, string>; body?: string | Buffer } = {}) {
  return new Promise<{ status: number; body: string; headers: IncomingMessage["headers"] }>((resolve, reject) => {
    const req = request(new URL(path, base), { method, headers: opts.headers }, (res) => {
      const chunks: Buffer[] = [];
      res.on("data", (c: Buffer) => chunks.push(c));
      res.on("end", () => resolve({ status: res.statusCode!, body: Buffer.concat(chunks).toString("utf8"), headers: res.headers }));
    });
    req.on("error", reject);
    req.end(opts.body);
  });
}

const snapshot = { agentVersion: "mod/0.1.0", observedAt: "2026-10-20T00:00:00.000Z", reachable: true, paused: false, status: { sessionName: "Owner's Session", tickRate: 30 } };

describe("recorder (ruling 11)", () => {
  it("forwards method, path, headers and body, and returns the upstream's status and body unchanged", async () => {
    const up = await upstream(() => ({ status: 418, body: '{"teapot":true}', headers: { "x-up": "1" } }));
    const rec = await recorderFor(up.url);
    expect(rec.url).toMatch(/^http:\/\/127\.0\.0\.1:\d+$/);
    const r = await send(rec.url, "PUT", "/some/where?q=1", { headers: { "x-custom": "abc", "content-type": "text/plain" }, body: "hello" });
    expect(r).toMatchObject({ status: 418, body: '{"teapot":true}' });
    expect(r.headers["x-up"]).toBe("1");
    expect(up.seen[0]).toMatchObject({ method: "PUT", url: "/some/where?q=1" });
    expect(up.seen[0]!.headers["x-custom"]).toBe("abc");
    expect(up.seen[0]!.body.toString()).toBe("hello");
    expect(rec.entries).toEqual([{ at: expect.any(String), doneAt: expect.any(String), agent: "other", method: "PUT", path: "/some/where", status: 418, authorization: false }]);
  });

  it("stamps doneAt when the answer has finished, not when the request arrived (review of 9b23fd1)", async () => {
    // Head at once, body 300 ms later: the agent only has the whole answer then.
    const server: Server = createServer((_req, res) => {
      res.writeHead(401, { "content-type": "application/json" });
      res.write("{");
      setTimeout(() => res.end("}"), 300);
    });
    await new Promise<void>((r) => server.listen(0, "127.0.0.1", () => r()));
    closers.push(() => new Promise<void>((r) => server.close(() => r())));
    const rec = await recorderFor(`http://127.0.0.1:${(server.address() as AddressInfo).port}`);
    await send(rec.url, "GET", "/agent/v1/commands", { headers: { "user-agent": MOD_UA } });
    await expect.poll(() => rec.entries.length).toBe(1);
    const e = rec.entries[0]!;
    expect(Date.parse(e.doneAt) - Date.parse(e.at)).toBeGreaterThanOrEqual(250);
  });

  it("records a gzipped snapshot decompressed, the mod's User-Agent as agent mod, and the session as its hash", async () => {
    const up = await upstream(() => ({ status: 200, body: '{"commandsPending":false}' }));
    const rec = await recorderFor(up.url);
    const gz = gzipSync(JSON.stringify(snapshot));
    const r = await send(rec.url, "POST", "/agent/v1/snapshots", {
      headers: { "user-agent": MOD_UA, authorization: `Bearer ${SECRET}`, "content-encoding": "gzip", "content-type": "application/json" },
      body: gz,
    });
    expect(r.status).toBe(200);
    expect(up.seen[0]!.body.equals(gz)).toBe(true); // forwarded as sent
    expect(up.seen[0]!.headers.authorization).toBe(`Bearer ${SECRET}`);
    const e = rec.entries[0]!;
    expect(e).toMatchObject({ agent: "mod", method: "POST", path: "/agent/v1/snapshots", status: 200, authorization: true });
    expect(e.snapshot).toEqual({ ...snapshot, status: { sessionName: sessionHash("Owner's Session"), tickRate: 30 } });
  });

  it("records the Node agent's User-Agent (backendClient.ts: satisfactory-dash-agent/<version>) as agent node, its session hashed too", async () => {
    const up = await upstream(() => ({ status: 200, body: '{"commandsPending":false}' }));
    const rec = await recorderFor(up.url);
    await send(rec.url, "POST", "/agent/v1/snapshots", {
      headers: { "user-agent": "satisfactory-dash-agent/0.1.0", "content-type": "application/json" },
      body: JSON.stringify(snapshot),
    });
    expect(rec.entries[0]).toMatchObject({ agent: "node", path: "/agent/v1/snapshots", status: 200 });
    expect(rec.entries[0]!.snapshot).toEqual({ ...snapshot, status: { sessionName: sessionHash("Owner's Session"), tickRate: 30 } });
  });

  it("hashes every player's name in a recorded snapshot, as it hashes the session (no names in evidence)", async () => {
    const up = await upstream(() => ({ status: 200, body: '{"commandsPending":false}' }));
    const rec = await recorderFor(up.url);
    const body = { ...snapshot, players: { available: true, players: [{ name: "Player One", online: true }] } };
    await send(rec.url, "POST", "/agent/v1/snapshots", { headers: { "user-agent": MOD_UA, "content-type": "application/json" }, body: JSON.stringify(body) });
    const recorded = rec.entries[0]!.snapshot as { players: { players: { name: string }[] } };
    expect(recorded.players.players[0]!.name).toBe(sessionHash("Player One"));
    expect(JSON.stringify(rec.entries)).not.toContain("Player One");
  });

  it("records train and station names as hashes, never the names (they are labels a player typed)", async () => {
    const up = await upstream(() => ({ status: 200, body: '{"commandsPending":false}' }));
    const rec = await recorderFor(up.url);
    const live = {
      trains: [{ id: "T1", name: "Test Train A", x: 1, y: 2, status: "Self-Driving" }],
      stations: [{ id: "S1", name: "Test Station A", x: 3, y: 4 }],
    };
    await send(rec.url, "POST", "/agent/v1/snapshots", {
      headers: { "user-agent": MOD_UA, "content-type": "application/json" },
      body: JSON.stringify({ ...snapshot, mapLive: live }),
    });
    const recorded = rec.entries[0]!.snapshot as { mapLive: typeof live };
    expect(recorded.mapLive.trains[0]).toEqual({ id: "T1", name: sessionHash("Test Train A"), x: 1, y: 2, status: "Self-Driving" });
    expect(recorded.mapLive.stations[0]).toEqual({ id: "S1", name: sessionHash("Test Station A"), x: 3, y: 4 });
    expect(JSON.stringify(rec.entries)).not.toContain("Test Train A");
    expect(JSON.stringify(rec.entries)).not.toContain("Test Station A");
  });

  it("records a world-layer post as its layer, item count and unzipped size, never its data", async () => {
    const up = await upstream(() => ({ status: 200, body: '{"accepted":true,"unchanged":false}' }));
    const rec = await recorderFor(up.url);
    const body = JSON.stringify({ observedAt: "2026-10-24T12:00:00.000Z", data: [{ id: "R1", points: [[0, 0], [3, 0]] }, { id: "R2", points: [[1, 1], [2, 2]] }] });
    await send(rec.url, "POST", "/agent/v1/world/rails", {
      headers: { "user-agent": MOD_UA, authorization: `Bearer ${SECRET}`, "content-type": "application/json", "content-encoding": "gzip" },
      body: gzipSync(body),
    });
    expect(rec.entries[0]).toMatchObject({ agent: "mod", method: "POST", path: "/agent/v1/world/rails", status: 200, world: { layer: "rails", items: 2, bytes: Buffer.byteLength(body) } });
    expect(JSON.stringify(rec.entries)).not.toContain("R1");
    expect(JSON.stringify(rec.entries)).not.toContain(SECRET);
  });

  const parts = { settings: { autoPause: false }, power: { circuits: [{ circuitGroupId: 1, productionMW: 5 }] } };
  const factory = { buildings: Array.from({ length: 30_000 }, (_, i) => ({ id: `b${i}`, name: "Constructor", className: "Build_ConstructorMk1_C", pad: "p".repeat(60) })) };
  const big = JSON.stringify({ ...snapshot, ...parts, factory, players: { available: true, players: [{ name: "Someone", online: true }] } });

  it("with trimming opted in for an agent, keeps that agent's snapshot over 2 MB without its bulky parts, naming what it dropped", async () => {
    expect(Buffer.byteLength(big)).toBeGreaterThan(2 * 1024 * 1024);
    const up = await upstream(() => ({ status: 200, body: "{}" }));
    const rec = await startRecorder(up.url, { trimOversizedFor: ["node"] });
    closers.push(() => rec.close());
    for (const gzip of [false, true]) {
      await send(rec.url, "POST", "/agent/v1/snapshots", {
        headers: { "user-agent": "satisfactory-dash-agent/0.1.0", "content-type": "application/json", ...(gzip ? { "content-encoding": "gzip" } : {}) },
        body: gzip ? gzipSync(big) : big,
      });
    }
    // The mod's oversized post is never trimmed, even on that recorder.
    await send(rec.url, "POST", "/agent/v1/snapshots", { headers: { "user-agent": MOD_UA, "content-type": "application/json" }, body: big });
    expect(up.seen[0]!.body.length).toBe(Buffer.byteLength(big)); // forwarded whole
    const [plain, zipped, mod] = rec.entries;
    for (const e of [plain!, zipped!]) {
      expect(e).toMatchObject({ agent: "node", status: 200, oversized: true, droppedKeys: ["factory", "players"] });
      expect(e.snapshot).toEqual({ ...snapshot, ...parts, status: { sessionName: sessionHash("Owner's Session"), tickRate: 30 } });
    }
    expect(mod).toMatchObject({ agent: "mod", status: 200 });
    expect(mod!.snapshot).toBeUndefined();
    expect(mod!.oversized).toBeUndefined();
  });

  it("by default records no snapshot over 2 MB, so enrol's judge fails a mod snapshot carrying factory (review of 9b60268)", async () => {
    const up = await upstream(() => ({ status: 200, body: "{}" }));
    const rec = await recorderFor(up.url);
    await send(rec.url, "POST", "/agent/v1/snapshots", { headers: { "user-agent": MOD_UA, "content-type": "application/json" }, body: big });
    const e = rec.entries[0]!;
    expect(e.snapshot).toBeUndefined();
    expect(e.oversized).toBeUndefined();
    const round = { at: e.at, world: true, snapshot: e.snapshot as Record<string, unknown>, api: null, autoPause: null, frmBefore: null, frmAfter: null, frmBeforeEndedAt: null, frmAfterStartedAt: null };
    expect(compareRound(round)).toEqual([`${e.at}: the snapshot body was not recorded`]);
  });

  it("forwards a body over 8 MB (more than the backend's 5 MB limit) but doesn't record it", async () => {
    const up = await upstream(() => ({ status: 413, body: "{}" }));
    const rec = await recorderFor(up.url);
    const big = JSON.stringify({ ...snapshot, pad: "p".repeat(8 * 1024 * 1024 + 10) });
    await send(rec.url, "POST", "/agent/v1/snapshots", { headers: { "user-agent": MOD_UA, "content-type": "application/json" }, body: big });
    expect(up.seen[0]!.body.length).toBe(Buffer.byteLength(big));
    expect(rec.entries[0]).toMatchObject({ status: 413 });
    expect(rec.entries[0]!.snapshot).toBeUndefined();
  });

  it("keeps a small snapshot whole and doesn't mark it oversized", async () => {
    const up = await upstream(() => ({ status: 200, body: "{}" }));
    const rec = await recorderFor(up.url);
    await send(rec.url, "POST", "/agent/v1/snapshots", { headers: { "user-agent": MOD_UA, "content-type": "application/json" }, body: JSON.stringify({ ...snapshot, factory: { buildings: [] } }) });
    expect(rec.entries[0]!.oversized).toBeUndefined();
    expect(rec.entries[0]!.snapshot).toMatchObject({ factory: { buildings: [] } });
  });

  it("never records the Authorization value, the code or the secret", async () => {
    const up = await upstream((s) => (s.url === "/agent/v1/enroll" ? { status: 201, body: JSON.stringify({ agentSecret: SECRET, serverId: "core-mod" }) } : { status: 200, body: "{}" }));
    const rec = await recorderFor(up.url);
    const enrolled = await send(rec.url, "POST", "/agent/v1/enroll", { headers: { "user-agent": MOD_UA, "content-type": "application/json" }, body: JSON.stringify({ code: CODE, agentVersion: "mod/0.1.0" }) });
    expect(enrolled.body).toContain(SECRET); // the mod gets it
    await send(rec.url, "POST", "/agent/v1/snapshots", { headers: { "user-agent": MOD_UA, authorization: `Bearer ${SECRET}`, "content-type": "application/json" }, body: JSON.stringify(snapshot) });
    await send(rec.url, "GET", "/agent/v1/whatever", { headers: { authorization: `Bearer ${SECRET}` } });
    const text = JSON.stringify(rec.entries);
    expect(text).not.toContain(SECRET);
    expect(text).not.toContain(CODE);
    expect(text).not.toContain("Bearer");
    expect(rec.entries.map((e) => e.authorization)).toEqual([false, true, true]);
    expect(rec.entries[0]).toEqual({ at: expect.any(String), doneAt: expect.any(String), agent: "mod", method: "POST", path: "/agent/v1/enroll", status: 201, authorization: false });
  });

  it("records the command IDs a poll returned, and a result's body", async () => {
    const id = "6f1c2b9e-1d2a-4c3b-9e8f-0a1b2c3d4e5f";
    const up = await upstream((s) =>
      s.method === "GET"
        ? { status: 200, body: gzipSync(JSON.stringify({ commands: [{ id, type: "set_auto_pause", params: { enabled: true }, expiresAt: "x" }] })), headers: { "content-encoding": "gzip" } }
        : { status: 204, body: "" });
    const rec = await recorderFor(up.url);
    await send(rec.url, "GET", "/agent/v1/commands?waitSeconds=25", { headers: { "user-agent": MOD_UA } });
    await send(rec.url, "POST", `/agent/v1/commands/${id}/result`, { headers: { "user-agent": MOD_UA, "content-type": "application/json" }, body: JSON.stringify({ status: "succeeded" }) });
    expect(rec.entries[0]).toMatchObject({ path: "/agent/v1/commands", status: 200, commandIds: [id] });
    expect(rec.entries[1]).toMatchObject({ path: `/agent/v1/commands/${id}/result`, status: 204, result: { status: "succeeded" } });
  });

  it("destroys the client's socket when the upstream refuses, and records status 0", async () => {
    const up = await upstream(() => ({ status: 200, body: "{}" }));
    const deadUrl = up.url;
    await closers.pop()!(); // the upstream is gone: its port refuses
    const rec = await recorderFor(deadUrl);
    await expect(send(rec.url, "POST", "/agent/v1/snapshots", { headers: { "user-agent": MOD_UA }, body: "{}" })).rejects.toThrow();
    await expect.poll(() => rec.entries.length).toBe(1);
    expect(rec.entries[0]).toMatchObject({ agent: "mod", path: "/agent/v1/snapshots", status: 0 });
  });
});
