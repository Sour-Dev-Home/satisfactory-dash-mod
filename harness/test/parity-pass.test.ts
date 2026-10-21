import { describe, expect, it } from "vitest";
import type { CoreEvidence } from "../src/core-run";
import {
  type Circuit, comparePair, evaluate, NEED_PAIRS, PAIR_GAP_MS, type Parity, pairSnapshots, type Snap, trimSnapshot,
} from "../../runs/parity/pass";

const T0 = Date.parse("2026-10-21T12:00:00.000Z");
const iso = (ms: number) => new Date(ms).toISOString();
const circuit = (over: Partial<Circuit> = {}): Circuit => ({
  circuitGroupId: 1, productionMW: 1000, consumptionMW: 800, capacityMW: 1200, maxConsumptionMW: 900, fuseTriggered: false,
  batteryCapacityMWh: 100, batteryPercent: 50, batteryDifferentialMW: 0, ...over,
});
const snap = (ms: number, over: Partial<Snap> = {}): Snap => ({
  at: iso(ms + 100),
  observedAt: iso(ms),
  status: {
    isGameRunning: true, gamePaused: false, connectedPlayers: 0, playerLimit: 4, totalGameDurationSeconds: 1000 + Math.round((ms - T0) / 1000),
    tickRate: 22, tickHealth: "healthy", sessionName: "a1b2c3",
  },
  settings: { autoPause: false },
  power: { circuits: [circuit()] },
  ...over,
});

const N = NEED_PAIRS + 12;
/** A good run: the mod every 5 s, the Node agent 1 s after it, every snapshot accepted. */
function good(over: Partial<Parity> = {}): Parity {
  return {
    windowStartedAt: iso(T0 - 1000),
    windowEndedAt: iso(T0 + 30 * 60_000),
    mod: Array.from({ length: N }, (_, i) => snap(T0 + i * 5000)),
    node: Array.from({ length: N }, (_, i) => snap(T0 + i * 5000 + 1000)),
    refused: { mod: [], node: [] },
    state: {
      mod: { codeConsumed: true, agentVersion: "mod/0.1.0", lastSeen: true, revoked: false },
      node: { codeConsumed: true, agentVersion: "0.1.0", lastSeen: true, revoked: false },
    },
    tokensInvalidated: true,
    ...over,
  };
}
const ev = (p: Parity | undefined): CoreEvidence => ({ lines: [], errorLines: [], leaks: { code: false, secret: null, bearer: false }, captures: p ? { "parity.json": p } : {} });

describe("parity pass conditions (Task 9)", () => {
  it("needs 30 * 60 / 5 * 0.8 pairs, at most 2.5 s apart (half a status cycle)", () => {
    expect(NEED_PAIRS).toBe(288);
    expect(PAIR_GAP_MS).toBe(2500);
  });

  it("passes a run where every pair matches, and states the counts", () => {
    const v = evaluate(ev(good()));
    expect(v.pass).toBe(true);
    expect(v.reasons[0]).toContain(`${N} pairs within ${PAIR_GAP_MS} ms`);
    expect(v.reasons[0]).toContain(`${N} mod and ${N} Node agent snapshots accepted`);
    expect(v.reasons[0]).toContain(`status compared in ${N}, autoPause in ${N}, power in ${N} pairs (1 circuit group)`);
  });

  it("fails on no capture", () => {
    expect(evaluate(ev(undefined))).toEqual({ pass: false, reasons: ["no parity capture"] });
  });

  it("pairs each mod snapshot with the Node agent's nearest, dropping those more than 2.5 s away", () => {
    const mod = [snap(T0), snap(T0 + 5000), snap(T0 + 10_000)];
    const node = [snap(T0 + 2400), snap(T0 + 7600), snap(T0 + 9000)];
    const pairs = pairSnapshots(mod, node);
    expect(pairs.map((p) => [p.mod.observedAt, p.node.observedAt, p.gapMs])).toEqual([
      [iso(T0), iso(T0 + 2400), 2400],
      [iso(T0 + 10_000), iso(T0 + 9000), 1000],
    ]);
  });

  it("counts a snapshot posted twice (the same observedAt) once", () => {
    const mod = [snap(T0), snap(T0)];
    expect(pairSnapshots(mod, [snap(T0 + 500)])).toHaveLength(1);
  });

  it("fails with too few pairs, naming the counts (never passes on missing data)", () => {
    const g = good();
    const v = evaluate(ev({ ...g, node: g.node.slice(0, 100) }));
    expect(v.pass).toBe(false);
    expect(v.reasons).toContain(`only 100 pairs within ${PAIR_GAP_MS} ms (${N} mod and 100 Node agent snapshots accepted), need ${NEED_PAIRS}`);
    expect(evaluate(ev({ ...g, mod: [], node: [] })).pass).toBe(false);
  });

  it("fails a pair on each exact status field, naming the round and the field", () => {
    const fields: Array<[string, Record<string, unknown>]> = [
      ["isGameRunning", { isGameRunning: false }], ["gamePaused", { gamePaused: true }], ["connectedPlayers", { connectedPlayers: 1 }],
      ["playerLimit", { playerLimit: 8 }], ["sessionName", { sessionName: "ffff" }], ["tickHealth", { tickHealth: "slow" }],
    ];
    for (const [name, change] of fields) {
      const n = snap(T0 + 1000);
      const reasons = comparePair({ mod: snap(T0), node: { ...n, status: { ...n.status!, ...change } }, gapMs: 1000 }).reasons;
      expect(reasons).toHaveLength(1);
      expect(reasons[0]).toMatch(new RegExp(`^${iso(T0)}: ${name} `));
    }
  });

  it("uses S6's tick-rate rule and allows 10 s of duration", () => {
    const pair = (mod: Partial<Snap["status"] & object>, node: Partial<Snap["status"] & object>) => {
      const m = snap(T0);
      const n = snap(T0 + 1000);
      return comparePair({ mod: { ...m, status: { ...m.status!, ...mod } }, node: { ...n, status: { ...n.status!, ...node } }, gapMs: 1000 }).reasons;
    };
    expect(pair({ tickRate: 22 }, { tickRate: 24 })).toEqual([]); // within max(2, 10%)
    expect(pair({ tickRate: 22 }, { tickRate: 24.5 })).toEqual([`${iso(T0)}: tickRate mod 22 vs node 24.5`]);
    expect(pair({ totalGameDurationSeconds: 1000 }, { totalGameDurationSeconds: 1010 })).toEqual([]);
    expect(pair({ totalGameDurationSeconds: 1000 }, { totalGameDurationSeconds: 1011 })).toEqual([`${iso(T0)}: totalGameDurationSeconds mod 1000 vs node 1011`]);
  });

  it("compares settings.autoPause exactly", () => {
    const reasons = comparePair({ mod: snap(T0), node: snap(T0 + 1000, { settings: { autoPause: true } }), gapMs: 1000 }).reasons;
    expect(reasons).toEqual([`${iso(T0)}: settings.autoPause mod false vs node true`]);
  });

  it("compares power by group within 1% (S3's rule), every field, and the fuse exactly", () => {
    const withNode = (c: Partial<Circuit>) => comparePair({ mod: snap(T0), node: snap(T0 + 1000, { power: { circuits: [circuit(c)] } }), gapMs: 1000 }).reasons;
    expect(withNode({ consumptionMW: 807.9 })).toEqual([]); // 0.99%
    expect(withNode({ consumptionMW: 809 })).toEqual([`${iso(T0)}: group 1 consumptionMW mod 800 vs node 809`]);
    for (const k of ["productionMW", "capacityMW", "maxConsumptionMW", "batteryCapacityMWh", "batteryPercent"] as const) {
      expect(withNode({ [k]: circuit()[k] * 1.05 })[0]).toMatch(new RegExp(`group 1 ${k} `));
    }
    expect(withNode({ batteryDifferentialMW: 5 })[0]).toMatch(/group 1 batteryDifferentialMW mod 0 vs node 5/);
    expect(withNode({ fuseTriggered: true })).toEqual([`${iso(T0)}: group 1 fuseTriggered mod false vs node true`]);
  });

  it("compares a joined group as one row, and fails when that group's rows disagree", () => {
    const mod = snap(T0);
    const joined = snap(T0 + 1000, { power: { circuits: [circuit(), circuit()] } });
    expect(comparePair({ mod, node: joined, gapMs: 1000 }).reasons).toEqual([]);
    const split = snap(T0 + 1000, { power: { circuits: [circuit(), circuit({ productionMW: 1 })] } });
    expect(comparePair({ mod, node: split, gapMs: 1000 }).reasons).toEqual([`${iso(T0)}: the node's rows for group 1 disagree`]);
  });

  it("fails when the two agents see different circuit groups", () => {
    const node = snap(T0 + 1000, { power: { circuits: [circuit(), circuit({ circuitGroupId: 2 })] } });
    expect(comparePair({ mod: snap(T0), node, gapMs: 1000 }).reasons).toEqual([`${iso(T0)}: circuit groups mod 1 vs node 1,2`]);
  });

  it("a part only one side sent isn't compared in that pair, but each part must still be compared in enough pairs", () => {
    const one = comparePair({ mod: snap(T0), node: snap(T0 + 1000, { power: undefined, settings: undefined }), gapMs: 1000 });
    expect(one).toEqual({ reasons: [], compared: { status: true, autoPause: false, power: false } });
    const g = good();
    const node = g.node.map((s, i) => (i < 30 ? { ...s, power: undefined } : s));
    const v = evaluate(ev({ ...g, node }));
    expect(v.pass).toBe(false);
    expect(v.reasons).toContain(`power compared in only ${N - 30} pairs, need ${NEED_PAIRS}`);
    const noStatus = evaluate(ev({ ...g, mod: g.mod.map((s) => ({ ...s, status: undefined })) }));
    expect(noStatus.reasons).toContain(`status compared in only 0 pairs, need ${NEED_PAIRS}`);
  });

  it("fails when either agent had a snapshot refused", () => {
    const v = evaluate(ev(good({ refused: { mod: [], node: [{ at: iso(T0), status: 400 }, { at: iso(T0 + 5000), status: 0 }] } })));
    expect(v.pass).toBe(false);
    expect(v.reasons).toContain("the backend didn't accept 2 of the Node agent's snapshots (answers 400, 0)");
    expect(evaluate(ev(good({ refused: { mod: [{ at: iso(T0), status: 422 }], node: [] } }))).reasons).toContain("the backend didn't accept 1 of the mod's snapshots (answers 422)");
  });

  it("fails when either server's state wasn't read or shows no enrolment, or the mod's version is wrong", () => {
    const g = good();
    expect(evaluate(ev({ ...g, state: { ...g.state, node: null } })).reasons).toContain("the Node agent's server state was not read");
    expect(evaluate(ev({ ...g, state: { ...g.state, node: { ...g.state.node!, codeConsumed: false } } })).reasons).toContain("the Node agent's code was not consumed");
    expect(evaluate(ev({ ...g, state: { ...g.state, mod: { ...g.state.mod!, lastSeen: false } } })).reasons).toContain("the backend never saw the mod's authenticated call");
    expect(evaluate(ev({ ...g, state: { ...g.state, mod: { ...g.state.mod!, agentVersion: "mod/0.0.9" } } })).reasons).toContain("the mod's agentVersion is mod/0.0.9, expected mod/0.1.0");
  });

  it("fails when the window wasn't recorded or ran short of 30 minutes", () => {
    expect(evaluate(ev(good({ windowEndedAt: null }))).reasons).toContain("the 30-minute window was not recorded");
    expect(evaluate(ev(good({ windowEndedAt: iso(T0 + 20 * 60_000) }))).reasons).toContain("the window ran 20.0 minutes, need 30");
  });

  it("fails when the run's game API token was not invalidated, or that wasn't recorded", () => {
    expect(evaluate(ev(good({ tokensInvalidated: false }))).reasons).toContain("the run's game API token was not invalidated (server.InvalidateAPITokens)");
    expect(evaluate(ev(good({ tokensInvalidated: null }))).reasons).toContain("the run's game API token was not invalidated (server.InvalidateAPITokens)");
  });

  it("keeps only what the judge reads from a recorded snapshot: hashed session, ids and MW, never players or factory", () => {
    const body = {
      agentVersion: "0.1.0", observedAt: iso(T0), reachable: true, paused: false, settings: { autoPause: false },
      status: { ...snap(T0).status!, serverName: "x" },
      power: { circuits: [{ ...circuit(), extra: 1 }] },
      players: { available: true, players: [{ name: "Someone", online: true }] },
      factory: { buildings: [] },
    };
    const t = trimSnapshot(iso(T0 + 100), body)!;
    expect(t).toEqual(snap(T0));
    expect(JSON.stringify(t)).not.toContain("Someone");
    expect(trimSnapshot(iso(T0), undefined)).toBeNull();
    expect(trimSnapshot(iso(T0), { observedAt: 5 })).toBeNull();
  });
});
