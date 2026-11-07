import { describe, expect, it } from "vitest";
import { type Circuit, type Enrol, evaluate, powerProblems, type Round } from "../../runs/enrol/pass";
import type { CoreEvidence } from "../src/core-run";

const SESSION = "0123456789ab";
const ENROLLED = "[t][1]LogSatisDash: Display: Enrolled; reporting to the backend";

const circuit: Circuit = { circuitGroupId: 1, productionMW: 1200, consumptionMW: 800, capacityMW: 1500, maxConsumptionMW: 1400, fuseTriggered: false, batteryCapacityMWh: 100, batteryPercent: 50, batteryDifferentialMW: -12.5 };

function round(i: number, world: boolean, over: { snapshot?: Record<string, unknown>; status?: Record<string, unknown>; api?: Record<string, unknown> } = {}): Round {
  const status = { isGameRunning: world, gamePaused: false, connectedPlayers: 0, playerLimit: 4, totalGameDurationSeconds: world ? 5000 + i * 5 : 0, tickRate: 30, tickHealth: "healthy", sessionName: world ? SESSION : "da39a3ee5e6b", ...over.status };
  return {
    at: `2026-10-20T16:00:${String(i).padStart(2, "0")}.000Z`,
    world,
    snapshot: {
      agentVersion: "mod/0.2.0", observedAt: "2026-10-20T16:00:00.000Z", reachable: true, paused: false, status, settings: { autoPause: false },
      ...(world ? { power: { circuits: [circuit] } } : {}),
      ...over.snapshot,
    },
    api: { running: world, paused: false, players: 0, limit: 4, duration: world ? 5000 + i * 5 : 0, tickRate: 30, health: "healthy", session: world ? SESSION : "da39a3ee5e6b", ...over.api } as Round["api"],
    autoPause: false,
  };
}

function enrol(over: Partial<Enrol> = {}, rounds?: Round[]): Enrol {
  return {
    state: { codeConsumed: true, agentVersion: "mod/0.2.0", lastSeen: true },
    codeBlanked: true,
    credentialWritten: true,
    refused: 0,
    rounds: rounds ?? [...Array.from({ length: 4 }, (_, i) => round(i, false)), ...Array.from({ length: 12 }, (_, i) => round(10 + i, true))],
    ...over,
  };
}

const evidence = (d: Enrol, lines = [ENROLLED]): CoreEvidence => ({ lines, errorLines: [], leaks: { code: false, secret: false, bearer: false }, captures: { "enrol.json": d } });

const withRound = (r: Round) => {
  const d = enrol();
  d.rounds[d.rounds.length - 1] = r;
  return d;
};
const withCircuit = (over: Partial<Circuit>) => round(30, true, { snapshot: { power: { circuits: [{ ...circuit, ...over }] } } });

describe("enrol pass conditions", () => {
  it("passes a clean capture", () => {
    expect(evaluate(evidence(enrol()))).toEqual({
      pass: true,
      reasons: ["enrolled; 4 rounds with no game and 12 with the world match the game's APIs; autoPause compared in 16 of 16 rounds; power checked for internal consistency for 1 circuit group(s) (the FRM equality is retired, owner 2026-11-05)"],
    });
  });

  it("says so in the pass reason when no circuit group was present", () => {
    const d = enrol();
    for (const r of d.rounds) if (r.world) r.snapshot.power = { circuits: [] };
    expect(evaluate(evidence(d)).reasons[0]).toContain("internal consistency for 0 circuit group(s)");
    expect(evaluate(evidence(d)).pass).toBe(true);
  });

  it("ignores the FRM fields an older capture's rounds still carry", () => {
    const d = enrol();
    const old = d.rounds[5] as unknown as Record<string, unknown>;
    old.frmBefore = [{ groupId: 9, production: 1 }];
    old.frmAfter = null;
    old.frmBeforeEndedAt = "not a time";
    (d as unknown as Record<string, unknown>).frmPolling = { intervalMs: 250, reads: 1, failed: 1 };
    expect(evaluate(evidence(d)).pass).toBe(true);
  });

  it("fails when autoPause was compared in fewer than 10 rounds (final review of Plan 2B)", () => {
    const d = enrol();
    d.rounds.forEach((r, i) => void (i >= 9 && (r.autoPause = null)));
    expect(evaluate(evidence(d)).reasons).toContain("autoPause compared in only 9 rounds, need 10 (GetServerOptions didn't answer)");
    d.rounds[9]!.autoPause = d.rounds[0]!.autoPause;
    expect(evaluate(evidence(d)).pass).toBe(true);
  });

  it("fails, without throwing, a round whose 200 snapshot wasn't recorded", () => {
    const d = enrol();
    const r = d.rounds[3]!;
    (r as { snapshot: unknown }).snapshot = undefined;
    let v: ReturnType<typeof evaluate> | undefined;
    expect(() => (v = evaluate(evidence(d)))).not.toThrow();
    expect(v!.pass).toBe(false);
    expect(v!.reasons).toEqual([`${r.at}: the snapshot body was not recorded`]);
  });

  it("fails with no capture or no enrolment line", () => {
    expect(evaluate({ ...evidence(enrol()), captures: {} })).toEqual({ pass: false, reasons: ["no enrol capture"] });
    expect(evaluate(evidence(enrol(), [])).reasons).toContain("S9: no enrolment line");
  });

  it("fails when the code wasn't consumed", () => {
    const v = evaluate(evidence(enrol({ state: { codeConsumed: false, agentVersion: "mod/0.2.0", lastSeen: true } })));
    expect(v.pass).toBe(false);
    expect(v.reasons).toContain("the code was not consumed");
  });

  it("fails when the code is still in the config", () => {
    expect(evaluate(evidence(enrol({ codeBlanked: false }))).reasons).toContain("the code is still in SatisDashAgent.cfg");
  });

  it("fails a power part in a round with no game", () => {
    const d = enrol();
    d.rounds[0] = round(0, false, { snapshot: { power: { circuits: [circuit] } } });
    expect(evaluate(evidence(d)).reasons).toContain(`${d.rounds[0]!.at}: power sent with no game running`);
  });

  it("fails a world round with no power part", () => {
    const r = round(30, true, { snapshot: { power: undefined } });
    expect(evaluate(evidence(withRound(r))).reasons).toContain(`${r.at}: no power to compare`);
  });

  it("fails an extra top-level snapshot field (S13)", () => {
    const r = round(30, true, { snapshot: { extra: 1 } });
    expect(evaluate(evidence(withRound(r))).reasons).toContain(`${r.at}: S13: unexpected snapshot field extra`);
  });

  it("accepts players, factory and mapLive in a snapshot (S13, owner rulings 2026-10-24 and 2026-10-25) but no other field", () => {
    const ok = round(30, true, { snapshot: { players: { available: true, players: [] }, factory: { available: true, buildings: [] }, mapLive: { trains: [], stations: [] } } });
    expect(evaluate(evidence(withRound(ok))).reasons.filter((x) => x.includes("S13"))).toEqual([]);
    const bad = round(30, true, { snapshot: { players: { available: true, players: [] }, conveyors: {} } });
    expect(evaluate(evidence(withRound(bad))).reasons).toContain(`${bad.at}: S13: unexpected snapshot field conveyors`);
  });

  it("accepts S4's gameReachable, lastGameReadAt, modVersion and storage (S13, owner ruling 2026-11-07) but no other field", () => {
    const s4 = { gameReachable: true, lastGameReadAt: "2026-11-07T04:09:11.555Z", modVersion: "mod/0.2.0", storage: { available: true, containers: [] } };
    const ok = round(30, true, { snapshot: s4 });
    expect(evaluate(evidence(withRound(ok))).reasons.filter((x) => x.includes("S13"))).toEqual([]);
    const bad = round(30, true, { snapshot: { ...s4, drones: {} } });
    expect(evaluate(evidence(withRound(bad))).reasons).toContain(`${bad.at}: S13: unexpected snapshot field drones`);
  });

  it("fails a tick rate off by 20%", () => {
    const r = round(30, true, { status: { tickRate: 24 } });
    expect(evaluate(evidence(withRound(r))).reasons).toContain(`${r.at}: tick rate 24 vs API 30`);
  });

  it("fails too few rounds of either kind", () => {
    const v = evaluate(evidence(enrol({}, [round(0, false), round(1, true)])));
    expect(v.reasons).toContain("only 1 rounds with no game, need 3");
    expect(v.reasons).toContain("only 1 rounds with the world, need 10");
  });

  // Owner ruling 2026-11-05 (R7, D5): the FRM equality is retired; each accepted power circuit is checked for internal consistency.
  describe("power circuits are internally consistent (owner ruling 2026-11-05, replacing the FRM equality)", () => {
    it("passes the default circuit, a circuit with no battery, and one at the limits", () => {
      expect(powerProblems("t", [circuit])).toEqual([]);
      expect(powerProblems("t", [{ ...circuit, batteryCapacityMWh: 0, batteryPercent: 0 }])).toEqual([]);
      expect(powerProblems("t", [{ ...circuit, productionMW: 0, consumptionMW: 1400, capacityMW: 0, batteryPercent: 100 }])).toEqual([]);
    });

    it.each([
      ["productionMW"], ["consumptionMW"], ["capacityMW"], ["maxConsumptionMW"],
    ] as const)("fails a negative %s, naming the circuit", (field) => {
      const r = withCircuit({ circuitGroupId: 7, [field]: -1 });
      const v = evaluate(evidence(withRound(r)));
      expect(v.pass).toBe(false);
      expect(v.reasons).toContain(`${r.at}: circuit 7 ${field} is negative (-1)`);
    });

    it("passes zero in each of the four fields", () => {
      expect(powerProblems("t", [{ ...circuit, productionMW: 0, consumptionMW: 0, capacityMW: 0, maxConsumptionMW: 0 }])).toEqual([]);
    });

    it("fails consumption above maxConsumption, and passes it equal", () => {
      const r = withCircuit({ circuitGroupId: 3, consumptionMW: 1400.5, maxConsumptionMW: 1400 });
      expect(evaluate(evidence(withRound(r))).reasons).toEqual([`${r.at}: circuit 3 consumptionMW 1400.5 is above maxConsumptionMW 1400`]);
      expect(powerProblems("t", [{ ...circuit, consumptionMW: 1400, maxConsumptionMW: 1400 }])).toEqual([]);
    });

    it("skips the consumption check when either figure is absent", () => {
      const noMax = { ...circuit, maxConsumptionMW: undefined } as unknown as Circuit;
      expect(powerProblems("t", [noMax])).toEqual([]);
      const noConsumption = { ...circuit, consumptionMW: undefined } as unknown as Circuit;
      expect(powerProblems("t", [noConsumption])).toEqual([]);
    });

    it("fails a battery percent below 0 or above 100, and passes it absent", () => {
      const low = withCircuit({ circuitGroupId: 2, batteryPercent: -0.5 });
      expect(evaluate(evidence(withRound(low))).reasons).toEqual([`${low.at}: circuit 2 batteryPercent -0.5 is outside 0-100`]);
      const high = withCircuit({ circuitGroupId: 2, batteryPercent: 100.1 });
      expect(evaluate(evidence(withRound(high))).reasons).toEqual([`${high.at}: circuit 2 batteryPercent 100.1 is outside 0-100`]);
      const absent = { ...circuit, batteryPercent: undefined } as unknown as Circuit;
      expect(powerProblems("t", [absent])).toEqual([]);
    });

    it("names every failing circuit and every failing field", () => {
      const out = powerProblems("t", [{ ...circuit, circuitGroupId: 1, productionMW: -2 }, { ...circuit, circuitGroupId: 2, capacityMW: -3, batteryPercent: 101 }]);
      expect(out).toEqual(["t: circuit 1 productionMW is negative (-2)", "t: circuit 2 capacityMW is negative (-3)", "t: circuit 2 batteryPercent 101 is outside 0-100"]);
    });

    it("no longer compares the figures with any outside reading: a mod figure of any size passes", () => {
      expect(evaluate(evidence(withRound(withCircuit({ productionMW: 99999, consumptionMW: 1, capacityMW: 5 })))).pass).toBe(true);
    });
  });

  // Owner ruling, 2026-10-24 (satisfactory-dash #621): pending is checked every round against GetServerOptions.
  describe("settings.pending against GetServerOptions' pending options", () => {
    const pendingRound = (settings: Record<string, unknown>, autoPausePending: boolean | null | undefined) =>
      ({ ...round(30, true, { snapshot: { settings } }), autoPausePending });

    it("passes a pending change: autoPause shows the pending value, and pending is true", () => {
      expect(evaluate(evidence(withRound(pendingRound({ autoPause: true, pending: true }, true)))).pass).toBe(true);
    });

    it("fails a mod that sends the applied value without pending while a change waits", () => {
      const r = pendingRound({ autoPause: false, pending: false }, true);
      expect(evaluate(evidence(withRound(r))).reasons).toEqual([`${r.at}: autoPause false vs GetServerOptions true (pending)`, `${r.at}: pending false vs GetServerOptions true`]);
    });

    it("fails pending: true when nothing waits, and counts an absent pending as false", () => {
      const r = pendingRound({ autoPause: false, pending: true }, null);
      expect(evaluate(evidence(withRound(r))).reasons).toEqual([`${r.at}: pending true vs GetServerOptions false`]);
      expect(evaluate(evidence(withRound(pendingRound({ autoPause: false }, null)))).pass).toBe(true);
    });

    it("judges a round recorded before the change (no autoPausePending) as having nothing pending", () => {
      expect(evaluate(evidence(withRound(pendingRound({ autoPause: false }, undefined)))).pass).toBe(true);
    });
  });
});
