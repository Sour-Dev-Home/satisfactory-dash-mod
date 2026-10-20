import { describe, expect, it } from "vitest";
import { evaluate } from "../../spikes/S6/pass";
import type { Evidence, StatusPair, StatusReading } from "../src/verdict";

const entry: StatusReading = { running: false, paused: false, players: 0, limit: 4, duration: 0, tickRate: 30, health: "healthy", session: "da39a3ee5e6b" };
const world: StatusReading = { ...entry, running: true, paused: true, duration: 5000, tickRate: 29.9, session: "0123456789ab" };
const pairs = (r: StatusReading, n: number, api: (r: StatusReading) => StatusReading | null = (x) => x): StatusPair[] =>
  Array.from({ length: n }, (_, i) => ({ at: `t${i}`, mod: r, api: api(r) }));
const ev = (p: StatusPair[]): Evidence => ({ modLines: [], echo: [], status: { pairs: p } });

describe("S6 evaluate", () => {
  it("passes with 10 matching pairs without a world and 10 with one", () => {
    const v = evaluate(ev([...pairs(entry, 10), ...pairs(world, 10)]));
    expect(v).toEqual({ pass: true, reasons: ["10 readings without a world and 10 with one: every field matches QueryServerState and HealthCheck"] });
  });
  it("fails with the count when too few pairs were judged", () => {
    const v = evaluate(ev([...pairs(entry, 10), ...pairs(world, 9), ...pairs(world, 3, () => null)]));
    expect(v.pass).toBe(false);
    expect(v.reasons).toContain("9 readings with a world, need 10");
  });
  it("fails without any status evidence", () => {
    expect(evaluate({ modLines: [], echo: [] })).toEqual({ pass: false, reasons: ["the harness collected no status readings"] });
  });
  it("names each exact field that differs", () => {
    const bad = pairs(world, 10, (r) => ({ ...r, players: 1, health: "slow", session: "ffffffffffff" }));
    const v = evaluate(ev([...pairs(entry, 10), ...bad]));
    expect(v.reasons).toContain("t0: players 0 vs API 1");
    expect(v.reasons).toContain("t0: health healthy vs API slow");
    expect(v.reasons).toContain("t0: session 0123456789ab vs API ffffffffffff");
  });
  it("allows game duration to drift by one 10 s round, and no more", () => {
    const ok = pairs(world, 10, (r) => ({ ...r, duration: r.duration + 10 }));
    expect(evaluate(ev([...pairs(entry, 10), ...ok])).pass).toBe(true);
    const late = pairs(world, 10, (r) => ({ ...r, duration: r.duration + 11 }));
    expect(evaluate(ev([...pairs(entry, 10), ...late])).reasons).toContain("t0: duration 5000 vs API 5011");
  });
  it("doesn't judge the tick rate of the first reading after the world loads, which is still recovering (owner, 2026-10-20)", () => {
    const settling = pairs(world, 10, (r) => r);
    settling[0] = { ...settling[0]!, mod: { ...world, tickRate: 24.18 }, api: { ...world, tickRate: 28.4 } };
    expect(evaluate(ev([...pairs(entry, 10), ...settling])).pass).toBe(true);
  });
  it("still judges the first world reading's other fields, and the tick rate of every later one", () => {
    const bad = pairs(world, 10, (r) => r);
    bad[0] = { ...bad[0]!, api: { ...world, tickRate: 28.4, players: 1 } };
    bad[1] = { ...bad[1]!, api: { ...world, tickRate: 25 } };
    const v = evaluate(ev([...pairs(entry, 10), ...bad]));
    expect(v.reasons).toContain("t0: players 0 vs API 1");
    expect(v.reasons).not.toContain("t0: tick rate 29.9 vs API 28.4");
    expect(v.reasons).toContain("t1: tick rate 29.9 vs API 25");
  });
  it("allows the tick rate to differ by 2 or 10%, whichever is larger", () => {
    const ok = pairs(world, 10, (r) => ({ ...r, tickRate: r.tickRate - 2.9 }));
    expect(evaluate(ev([...pairs(entry, 10), ...ok])).pass).toBe(true);
    const off = pairs(world, 10, (r) => ({ ...r, tickRate: 25 }));
    // t0, the first world reading, isn't judged on tick rate (owner, 2026-10-20); t1 is.
    expect(evaluate(ev([...pairs(entry, 10), ...off])).reasons).toContain("t1: tick rate 29.9 vs API 25");
  });
});
