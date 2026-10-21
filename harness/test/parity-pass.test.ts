import { describe, expect, it } from "vitest";
import type { CoreEvidence } from "../src/core-run";
import {
  type Circuit, comparePair, comparePower, evaluate, NEED_PAIRS, PAIR_GAP_MS, PARITY_NEIGHBOUR_GAP_MS, type Parity, pairSnapshots,
  type Snap, trimSnapshot,
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
/** A good run: the mod every 5 s; the Node agent every 5 s, 1 s after each mod sample, plus one 4 s before the first. */
function good(over: Partial<Parity> = {}): Parity {
  return {
    windowStartedAt: iso(T0 - 1000),
    windowEndedAt: iso(T0 + 30 * 60_000),
    mod: Array.from({ length: N }, (_, i) => snap(T0 + i * 5000)),
    node: Array.from({ length: N + 1 }, (_, i) => snap(T0 + i * 5000 - 4000)),
    accepted: { mod: N, node: N + 1 },
    oversizedAccepted: { mod: 0, node: 0 },
    unrecordedAccepted: { mod: 0, node: 0 },
    refused: { mod: [], node: [] },
    state: {
      mod: { codeConsumed: true, agentVersion: "mod/0.1.0", lastSeen: true, revoked: false },
      node: { codeConsumed: true, agentVersion: "0.1.0", lastSeen: true, revoked: false },
    },
    tokensInvalidated: true,
    nodeAgent: { agentCjsSha256: "a".repeat(64), pin: "81f22aef6d679b5a2b8465673b2abe51dbaf98ef" },
    cleanup: { stopped: true, homeLeft: false },
    ...over,
  };
}
const ev = (p: Parity | undefined): CoreEvidence => ({ lines: [], errorLines: [], leaks: { code: false, secret: null, bearer: false }, captures: p ? { "parity.json": p } : {} });

/** The mod's power at T0 judged against Node readings at the given offsets (null: none). */
const power = (mod: Circuit[], before: { dt: number; circuits: Circuit[] } | null, after: { dt: number; circuits: Circuit[] } | null) =>
  comparePower(snap(T0, { power: { circuits: mod } }), [before, after].filter((x) => x !== null).map((x) => snap(T0 + x.dt, { power: { circuits: x.circuits } })));

describe("parity pass conditions (Task 9)", () => {
  it("needs 30 * 60 / 5 * 0.8 pairs, pairs at most 2.5 s apart, and power neighbours at most 6 s away", () => {
    expect(NEED_PAIRS).toBe(288);
    expect(PAIR_GAP_MS).toBe(2500);
    expect(PARITY_NEIGHBOUR_GAP_MS).toBe(6000);
  });

  it("passes a run where everything matches, and states the real counts", () => {
    const v = evaluate(ev(good()));
    expect(v.pass).toBe(true);
    expect(v.reasons[0]).toContain(`${N} pairs within ${PAIR_GAP_MS} ms`);
    expect(v.reasons[0]).toContain(`${N} mod and ${N + 1} Node agent snapshots accepted (0 and 0 over the recorder's 2 MB cap, judged without their bulky parts)`);
    expect(v.reasons[0]).toContain(`status compared in ${N} pairs, autoPause in ${N}; power in ${N} rounds against the Node agent's neighbours (1 circuit group)`);
  });

  it("states the oversized posts it kept in the pass reason", () => {
    const v = evaluate(ev(good({ oversizedAccepted: { mod: 3, node: 50 } })));
    expect(v.pass).toBe(true);
    expect(v.reasons[0]).toContain("(3 and 50 over the recorder's 2 MB cap, judged without their bulky parts)");
  });

  it("fails when an accepted post's body couldn't be read at all (the counts are real, nothing is dropped silently)", () => {
    const v = evaluate(ev(good({ unrecordedAccepted: { mod: 0, node: 2 } })));
    expect(v.pass).toBe(false);
    expect(v.reasons).toContain("2 of the Node agent's accepted snapshots couldn't be read by the recorder, so they weren't judged");
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
    const v = evaluate(ev({ ...g, node: g.node.slice(0, 100), accepted: { mod: N, node: 100 } }));
    expect(v.pass).toBe(false);
    expect(v.reasons).toContain(`only 99 pairs within ${PAIR_GAP_MS} ms (${N} mod and 100 Node agent snapshots accepted), need ${NEED_PAIRS}`);
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

  it("compares settings.autoPause exactly, and leaves power to the neighbours' bracket", () => {
    const r = comparePair({ mod: snap(T0), node: snap(T0 + 1000, { settings: { autoPause: true }, power: { circuits: [circuit({ productionMW: 1 })] } }), gapMs: 1000 });
    expect(r.reasons).toEqual([`${iso(T0)}: settings.autoPause mod false vs node true`]);
  });

  describe("power against the Node agent's neighbours (owner, 2026-10-21)", () => {
    it("passes a value inside the two readings' range, or within 3% of its ends (owner, after run 39625857599)", () => {
      const b = { dt: -4000, circuits: [circuit({ consumptionMW: 800 })] };
      const a = { dt: 1000, circuits: [circuit({ consumptionMW: 820 })] };
      expect(power([circuit({ consumptionMW: 815 })], b, a)).toEqual({ reasons: [], compared: true, counted: true });
      expect(power([circuit({ consumptionMW: 844 })], b, a).reasons).toEqual([]); // 820 + 24.6
      expect(power([circuit({ consumptionMW: 776 })], b, a).reasons).toEqual([]); // 800 - 24.6
      expect(power([circuit({ consumptionMW: 845 })], b, a).reasons).toEqual([`${iso(T0)}: group 1 consumptionMW mod 845 vs node 800..820`]);
      expect(power([circuit({ consumptionMW: 775 })], b, a).reasons).toEqual([`${iso(T0)}: group 1 consumptionMW mod 775 vs node 800..820`]);
    });

    it("judges every power field the same way", () => {
      for (const k of ["productionMW", "capacityMW", "maxConsumptionMW", "batteryCapacityMWh", "batteryPercent"] as const) {
        const r = power([circuit({ [k]: circuit()[k] * 1.05 })], { dt: -2000, circuits: [circuit()] }, { dt: 3000, circuits: [circuit()] });
        expect(r.reasons[0]).toMatch(new RegExp(`group 1 ${k} `));
      }
      const diff = power([circuit({ batteryDifferentialMW: 5 })], { dt: -2000, circuits: [circuit()] }, { dt: 3000, circuits: [circuit()] });
      expect(diff.reasons).toEqual([`${iso(T0)}: group 1 batteryDifferentialMW mod 5 vs node 0`]);
    });

    it("takes the last Node reading at or before the sample and the first at or after it, each within 6 s", () => {
      const nodes = [-9000, -4000, -1000, 0, 2000, 6000].map((dt) => snap(T0 + dt, { power: { circuits: [circuit({ consumptionMW: 800 + dt / 100 })] } }));
      // The neighbours are the one at the sample's own time on both sides: 800..800.
      expect(comparePower(snap(T0, { power: { circuits: [circuit({ consumptionMW: 800 })] } }), nodes).reasons).toEqual([]);
      const off = snap(T0 + 1000, { power: { circuits: [circuit({ consumptionMW: 850 })] } });
      // Past 820 + 3%; within reach of the 6 s reading (860), which is not the first one after the sample.
      expect(comparePower(off, nodes).reasons).toEqual([`${iso(T0 + 1000)}: group 1 consumptionMW mod 850 vs node 800..820`]);
    });

    it("fails a round missing a neighbour on either side, or with one more than 6 s away", () => {
      const c = [circuit()];
      expect(power(c, null, { dt: 1000, circuits: c }).reasons).toEqual([`${iso(T0)}: no Node agent power reading at or before the mod's sample, within ${PARITY_NEIGHBOUR_GAP_MS} ms`]);
      expect(power(c, { dt: -1000, circuits: c }, null).reasons).toEqual([`${iso(T0)}: no Node agent power reading at or after the mod's sample, within ${PARITY_NEIGHBOUR_GAP_MS} ms`]);
      expect(power(c, { dt: -6001, circuits: c }, { dt: 1000, circuits: c }).reasons).toHaveLength(1);
      expect(power(c, { dt: -1000, circuits: c }, { dt: 6001, circuits: c }).reasons).toHaveLength(1);
      expect(power(c, { dt: -6000, circuits: c }, { dt: 6000, circuits: c }).reasons).toEqual([]);
      // Run 39625857599's case: a Node sample 3 ms before the mod's, the next one 5051 ms after that one.
      expect(power(c, { dt: -3, circuits: c }, { dt: 5048, circuits: c }).reasons).toEqual([]);
      expect(power(c, null, { dt: 1000, circuits: c }).compared).toBe(false);
    });

    it("never takes a Node snapshot without a power part as a neighbour", () => {
      const c = [circuit()];
      const r = comparePower(snap(T0), [snap(T0 - 1000, { power: undefined }), snap(T0 + 1000, { power: { circuits: c } })]);
      expect(r.reasons).toEqual([`${iso(T0)}: no Node agent power reading at or before the mod's sample, within ${PARITY_NEIGHBOUR_GAP_MS} ms`]);
    });

    it("needs the group ids of at least one neighbour, and brackets with only the one(s) that match", () => {
      const two = [circuit(), circuit({ circuitGroupId: 2 })];
      const r = power([circuit({ consumptionMW: 900 })], { dt: -1000, circuits: two }, { dt: 1000, circuits: [circuit({ consumptionMW: 900 })] });
      expect(r.reasons).toEqual([]);
      expect(power([circuit()], { dt: -1000, circuits: two }, { dt: 1000, circuits: two }).reasons).toEqual([`${iso(T0)}: circuit groups mod 1 vs node 1,2`]);
      expect(power([circuit()], { dt: -1000, circuits: two }, { dt: 1000, circuits: [circuit({ circuitGroupId: 3 })] }).reasons).toEqual([`${iso(T0)}: circuit groups mod 1 vs node 1,2 before, 3 after`]);
    });

    it("needs the fuse of at least one neighbour", () => {
      const c = [circuit({ fuseTriggered: true })];
      expect(power(c, { dt: -1000, circuits: [circuit()] }, { dt: 1000, circuits: c }).reasons).toEqual([]);
      expect(power(c, { dt: -1000, circuits: [circuit()] }, { dt: 1000, circuits: [circuit()] }).reasons).toEqual([`${iso(T0)}: group 1 fuseTriggered mod true vs node false`]);
    });

    it("compares a joined group as one row, and fails when that group's rows disagree", () => {
      const joined = { dt: 1000, circuits: [circuit(), circuit()] };
      expect(power([circuit()], { dt: -1000, circuits: [circuit()] }, joined).reasons).toEqual([]);
      const split = { dt: 1000, circuits: [circuit(), circuit({ productionMW: 1 })] };
      expect(power([circuit()], { dt: -1000, circuits: [circuit()] }, split).reasons).toEqual([`${iso(T0)}: the Node agent's rows for group 1 disagree (after)`]);
    });

    it("doesn't count a round where both sides have no circuit group toward the 288", () => {
      expect(power([], { dt: -1000, circuits: [] }, { dt: 1000, circuits: [] })).toEqual({ reasons: [], compared: true, counted: false });
      const g = good();
      const empty = (s: Snap) => ({ ...s, power: { circuits: [] } });
      const v = evaluate(ev({ ...g, mod: g.mod.map(empty), node: g.node.map(empty) }));
      expect(v.pass).toBe(false);
      expect(v.reasons).toContain(`power compared in only 0 rounds with a circuit group, need ${NEED_PAIRS}`);
    });

    it("a mod snapshot without power isn't a round, and power must still reach 288 rounds", () => {
      const g = good();
      const v = evaluate(ev({ ...g, mod: g.mod.map((s, i) => (i < 30 ? { ...s, power: undefined } : s)) }));
      expect(v.pass).toBe(false);
      expect(v.reasons).toContain(`power compared in only ${N - 30} rounds with a circuit group, need ${NEED_PAIRS}`);
    });
  });

  it("a status or autoPause part only one side sent isn't compared in that pair, but each must be compared in enough pairs", () => {
    const one = comparePair({ mod: snap(T0), node: snap(T0 + 1000, { settings: undefined }), gapMs: 1000 });
    expect(one).toEqual({ reasons: [], compared: { status: true, autoPause: false } });
    const g = good();
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

  it("fails when the Node agent didn't stop or its SD_AGENT_HOME was left behind", () => {
    expect(evaluate(ev(good({ cleanup: { stopped: false, homeLeft: false } }))).reasons).toContain("stopping the Node agent failed");
    expect(evaluate(ev(good({ cleanup: { stopped: true, homeLeft: true } }))).reasons).toContain("the Node agent's SD_AGENT_HOME was left on disk");
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
