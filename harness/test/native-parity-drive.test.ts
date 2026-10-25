import { describe, expect, it } from "vitest";
import { NATIVE_SOURCES } from "../src/core-run";
import { createRoundCollector, nativeParity, sinceWindow } from "../src/core-runs/native-parity";
import type { FrmRead } from "../src/frm";
import type { RecordedCall } from "../src/recorder";
import { FRM_BRACKET_GAP_MS } from "../../runs/enrol/pass";

const T0 = Date.parse("2026-10-26T12:00:00.000Z");
const group = (production: number) => ({
  groupId: 0, production, consumption: 80, capacity: 120, maxConsumption: 110, batteryCapacity: 0, batteryPercent: 0, batteryDifferential: 0, fuseTriggered: false,
});
const read = (startedAt: number, production = 100): FrmRead => ({ startedAt, endedAt: startedAt + 50, groups: [group(production)] });
const circuit = { circuitGroupId: 0, productionMW: 100 };
const call = (observedMs: number | null, extra: Record<string, unknown> = {}): RecordedCall =>
  ({
    at: new Date(observedMs ?? T0).toISOString(), doneAt: "", agent: "mod", method: "POST", path: "/agent/v1/snapshots", status: 200, authorization: true,
    snapshot: observedMs === null ? { note: "no observedAt" } : { observedAt: new Date(observedMs).toISOString(), power: { circuits: [circuit] }, ...extra },
  }) as RecordedCall;

describe("native-parity drive: rounds", () => {
  it("starts at the window's first snapshot and takes each snapshot once, however often feed runs", () => {
    const all = [call(T0 - 5000), call(T0 + 1000), call(T0 + 6000)];
    const c = createRoundCollector({ reads: () => [], now: () => T0, startAt: 1 });
    c.feed(all);
    c.feed(all);
    expect(c.players.map((p) => p.observedAt)).toEqual([new Date(T0 + 1000).toISOString(), new Date(T0 + 6000).toISOString()]);
    all.push(call(T0 + 11_000));
    c.feed(all);
    expect(c.players).toHaveLength(3);
    expect(c.waiting).toBe(3);
  });

  it("holds a snapshot until an FRM read has started at or after it, then brackets it with the reads either side", () => {
    const reads: FrmRead[] = [read(T0 + 400, 99)];
    let now = T0 + 1000;
    const c = createRoundCollector({ reads: () => reads, now: () => now, startAt: 0 });
    c.feed([call(T0 + 1000)]);
    c.settle(false);
    expect(c.power).toHaveLength(0); // no read has started since the sample
    expect(c.waiting).toBe(1);
    reads.push(read(T0 + 1250, 101));
    c.settle(false);
    expect(c.waiting).toBe(0);
    expect(c.power).toHaveLength(1);
    expect(c.power[0]!.before![0]!.production).toBe(99);
    expect(c.power[0]!.after![0]!.production).toBe(101);
  });

  it("gives up waiting after FRM_BRACKET_GAP_MS and records the missing neighbour as null, so the judge fails the round", () => {
    const reads: FrmRead[] = [read(T0 + 400)];
    let now = T0 + 1000;
    const c = createRoundCollector({ reads: () => reads, now: () => now, startAt: 0 });
    c.feed([call(T0 + 1000)]);
    now = T0 + 1000 + FRM_BRACKET_GAP_MS;
    c.settle(false);
    expect(c.waiting).toBe(1); // exactly the gap: still waiting
    now += 1;
    c.settle(false);
    expect(c.power).toHaveLength(1);
    expect(c.power[0]!.before).not.toBeNull();
    expect(c.power[0]!.after).toBeNull();
  });

  it("force settles everything left at the end of the window", () => {
    const c = createRoundCollector({ reads: () => [], now: () => T0, startAt: 0 });
    c.feed([call(T0 + 1000), call(T0 + 6000)]);
    c.settle(true);
    expect(c.waiting).toBe(0);
    expect(c.power).toHaveLength(2);
  });

  it("drops a snapshot with no usable observedAt from the power rounds, and gives no round for one without power", () => {
    const c = createRoundCollector({ reads: () => [], now: () => T0, startAt: 0 });
    c.feed([call(null), { ...call(T0 + 1000), snapshot: { observedAt: new Date(T0 + 1000).toISOString() } } as RecordedCall]);
    c.settle(true);
    expect(c.power).toHaveLength(0);
    expect(c.players).toHaveLength(1);
    expect(c.players[0]).toEqual({ observedAt: new Date(T0 + 1000).toISOString(), available: null, keys: null });
  });

  it("builds the players round from the recorder's hashed names, sorted, never a name of its own", () => {
    const c = createRoundCollector({ reads: () => [], now: () => T0, startAt: 0 });
    c.feed([call(T0 + 1000, { players: { available: true, players: [{ name: "hash-b", online: false }, { name: "hash-a", online: true }] } })]);
    expect(c.players[0]).toMatchObject({ available: true, keys: ["hash-a:true", "hash-b:false"] });
  });
});

describe("native-parity drive: counts and spec", () => {
  it("leaves out the reads and failures that happened before the window", () => {
    expect(sinceWindow({ reads: 40, failed: 7 }, { reads: 10, failed: 7 })).toEqual({ reads: 30, failed: 0 });
  });

  it("writes the native sources into the mod's config, needs the backend, the world and FRM", () => {
    expect(nativeParity.modConfig).toEqual(NATIVE_SOURCES);
    expect(nativeParity.backend && nativeParity.world).toBe(true);
    expect(() => nativeParity.preflight!({ config: {} } as never)).toThrow(/frmBaseUrl/);
    expect(() => nativeParity.preflight!({ config: { frmBaseUrl: "http://127.0.0.1:18081" } } as never)).not.toThrow();
  });
});
