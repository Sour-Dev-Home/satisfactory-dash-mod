import { createHash } from "node:crypto";
import { describe, expect, it } from "vitest";
import { collectStatusPairs, parseS6Line, readApiStatus, sessionHash } from "../src/status";
import type { StatusReading } from "../src/verdict";

const reading: StatusReading = { running: true, paused: false, players: 0, limit: 4, duration: 1234, tickRate: 29.88, health: "healthy", session: sessionHash("My Factory") };
const line = (r: StatusReading) =>
  `[t]LogSatisMod: Display: satis-mod s6 running=${r.running ? 1 : 0} paused=${r.paused ? 1 : 0} players=${r.players} limit=${r.limit} duration=${r.duration} tick=${r.tickRate.toFixed(2)} health=${r.health} session=${r.session}`;

describe("sessionHash", () => {
  it("is the first 12 hex characters of SHA-1 over UTF-8", () => {
    expect(sessionHash("")).toBe("da39a3ee5e6b");
    expect(sessionHash("My Factory")).toBe(createHash("sha1").update("My Factory", "utf8").digest("hex").slice(0, 12));
  });
  it("hashes a non-ASCII session name over its UTF-8 bytes", () => {
    expect(sessionHash("Fábrica ☃")).toBe(createHash("sha1").update(Buffer.from("Fábrica ☃", "utf8")).digest("hex").slice(0, 12));
  });
});

describe("parseS6Line", () => {
  it("reads every field of an s6 line", () => {
    expect(parseS6Line(line(reading))).toEqual(reading);
  });
  it("keeps an empty health as an empty string rather than dropping the line", () => {
    expect(parseS6Line(line({ ...reading, health: "" }))?.health).toBe("");
  });
  it("ignores other lines, error lines included", () => {
    expect(parseS6Line("satis-mod s6 error=no-server-subsystem")).toBeNull();
    expect(parseS6Line("satis-mod loaded")).toBeNull();
  });
});

describe("readApiStatus", () => {
  const body = { data: { serverGameState: { activeSessionName: "My Factory", numConnectedPlayers: 0, playerLimit: 4, isGameRunning: true, totalGameDuration: 1234, isGamePaused: false, averageTickRate: 29.88 } } };
  it("maps QueryServerState and HealthCheck the way satisfactory-dash's adapter does", async () => {
    const calls: unknown[] = [];
    const api = { call: async (fn: string, data?: Record<string, unknown>) => (calls.push([fn, data]), fn === "HealthCheck" ? { status: 200, body: { data: { health: "healthy" } } } : { status: 200, body }) };
    expect(await readApiStatus(api)).toEqual(reading);
    expect(calls).toEqual([["QueryServerState", undefined], ["HealthCheck", { ClientCustomData: "" }]]);
  });
  it("throws on a non-2xx answer", async () => {
    await expect(readApiStatus({ call: async () => ({ status: 401, body: null }) })).rejects.toThrow(/QueryServerState returned 401/);
  });
});

describe("collectStatusPairs", () => {
  const clock = () => {
    let t = 0;
    return { now: () => t, sleep: async (ms: number) => void (t += ms) };
  };
  it("pairs each s6 line with an API read taken after it, until it has `want` pairs", async () => {
    const batches = [[line(reading)], [], [line(reading)], [line(reading)]];
    const c = clock();
    const pairs = await collectStatusPairs({ readNew: () => batches.shift() ?? [], api: async () => reading, ...c, durationMs: 60_000, intervalMs: 500, want: 3, skipBacklog: false });
    expect(pairs).toHaveLength(3);
    expect(pairs[0]).toEqual({ at: new Date(0).toISOString(), mod: reading, api: reading });
  });
  it("skips the backlog it finds on its first read", async () => {
    const batches = [[line(reading), line(reading)], [line(reading)]];
    const c = clock();
    const pairs = await collectStatusPairs({ readNew: () => batches.shift() ?? [], api: async () => reading, ...c, durationMs: 2_000, intervalMs: 500, want: 5, skipBacklog: true });
    expect(pairs).toHaveLength(1);
  });
  it("keeps a failed API read as null", async () => {
    const batches = [[line(reading)]];
    const c = clock();
    const pairs = await collectStatusPairs({ readNew: () => batches.shift() ?? [], api: async () => { throw new Error("loading"); }, ...c, durationMs: 1_000, intervalMs: 500, want: 1, skipBacklog: false });
    expect(pairs).toEqual([{ at: new Date(0).toISOString(), mod: reading, api: null }]);
  });
  it("stops at the deadline with what it has", async () => {
    const c = clock();
    const pairs = await collectStatusPairs({ readNew: () => [], api: async () => reading, ...c, durationMs: 2_000, intervalMs: 500, want: 3, skipBacklog: false });
    expect(pairs).toEqual([]);
  });
});
