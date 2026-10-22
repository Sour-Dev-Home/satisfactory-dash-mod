import { describe, expect, it } from "vitest";
import { type Enrol, evaluate, FRM_BRACKET_GAP_MS, type Round } from "../../runs/enrol/pass";
import type { CoreEvidence } from "../src/core-run";
import { parseGetPowerFull } from "../src/frm";

const SESSION = "0123456789ab";
const ENROLLED = "[t][1]LogSatisDash: Display: Enrolled; reporting to the backend";

const circuit = { circuitGroupId: 1, productionMW: 1200, consumptionMW: 800, capacityMW: 1500, maxConsumptionMW: 1400, fuseTriggered: false, batteryCapacityMWh: 100, batteryPercent: 50, batteryDifferentialMW: -12.5 };
const frmRow = { CircuitGroupID: 1, PowerProduction: 1200, PowerConsumed: 800, PowerCapacity: 1500, PowerMaxConsumed: 1400, BatteryCapacity: 100, BatteryPercent: 50, BatteryDifferential: -12.5, FuseTriggered: false };

type Frm = Round["frmBefore"];
/** frm sets both reads; frmBefore / frmAfter override one of them. */
function round(i: number, world: boolean, over: { snapshot?: Record<string, unknown>; status?: Record<string, unknown>; api?: Record<string, unknown>; frm?: Frm; frmBefore?: Frm; frmAfter?: Frm } = {}): Round {
  const frm = world ? (over.frm !== undefined ? over.frm : parseGetPowerFull([frmRow])) : null;
  const status = { isGameRunning: world, gamePaused: false, connectedPlayers: 0, playerLimit: 4, totalGameDurationSeconds: world ? 5000 + i * 5 : 0, tickRate: 30, tickHealth: "healthy", sessionName: world ? SESSION : "da39a3ee5e6b", ...over.status };
  return {
    at: `2026-10-20T16:00:${String(i).padStart(2, "0")}.000Z`,
    world,
    snapshot: {
      agentVersion: "mod/0.1.0", observedAt: "2026-10-20T16:00:00.000Z", reachable: true, paused: false, status, settings: { autoPause: false },
      ...(world ? { power: { circuits: [circuit] } } : {}),
      ...over.snapshot,
    },
    api: { running: world, paused: false, players: 0, limit: 4, duration: world ? 5000 + i * 5 : 0, tickRate: 30, health: "healthy", session: world ? SESSION : "da39a3ee5e6b", ...over.api } as Round["api"],
    autoPause: false,
    frmBefore: over.frmBefore !== undefined ? over.frmBefore : frm,
    frmAfter: over.frmAfter !== undefined ? over.frmAfter : frm,
    // The sample is at OBSERVED; the reads 0.3 s either side of it.
    frmBeforeEndedAt: world ? "2026-10-20T15:59:59.700Z" : null,
    frmAfterStartedAt: world ? "2026-10-20T16:00:00.300Z" : null,
  };
}

function enrol(over: Partial<Enrol> = {}, rounds?: Round[]): Enrol {
  return {
    state: { codeConsumed: true, agentVersion: "mod/0.1.0", lastSeen: true },
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

describe("enrol pass conditions", () => {
  it("passes a clean capture", () => {
    expect(evaluate(evidence(enrol()))).toEqual({
      pass: true,
      reasons: ["enrolled; 4 rounds with no game and 12 with the world match the game's APIs; autoPause compared in 16 of 16 rounds; power compared for 1 circuit group(s)"],
    });
  });

  it("says so in the pass reason when no circuit group was compared", () => {
    const d = enrol();
    for (const r of d.rounds) {
      if (r.world) {
        r.frmBefore = [];
        r.frmAfter = [];
        r.snapshot.power = { circuits: [] };
      }
    }
    expect(evaluate(evidence(d))).toEqual({
      pass: true,
      reasons: ["enrolled; 4 rounds with no game and 12 with the world match the game's APIs; autoPause compared in 16 of 16 rounds; power compared for 0 circuit group(s)"],
    });
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
    const v = evaluate(evidence(enrol({ state: { codeConsumed: false, agentVersion: "mod/0.1.0", lastSeen: true } })));
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

  it("fails a group set that differs from FRM's", () => {
    const r = round(30, true, { frm: parseGetPowerFull([frmRow, { ...frmRow, CircuitGroupID: 2 }]) });
    const v = evaluate(evidence(withRound(r)));
    expect(v.pass).toBe(false);
    expect(v.reasons).toContain(`${r.at}: circuit groups 1 vs FRM 1,2`);
  });

  it("fails a battery percent off by 2%", () => {
    const r = round(30, true, { frm: parseGetPowerFull([{ ...frmRow, BatteryPercent: 51 }]) });
    const v = evaluate(evidence(withRound(r)));
    expect(v.pass).toBe(false);
    expect(v.reasons).toContain(`${r.at}: group 1 batteryPercent 50 vs FRM 51`);
  });

  it("fails an extra top-level snapshot field (S13)", () => {
    const r = round(30, true, { snapshot: { extra: 1 } });
    expect(evaluate(evidence(withRound(r))).reasons).toContain(`${r.at}: S13: unexpected snapshot field extra`);
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

  it("compares a joined group (two FRM circuits, one group id) as one row", () => {
    const r = round(30, true, { frm: parseGetPowerFull([{ ...frmRow, AssociatedCircuits: [0] }, { ...frmRow, AssociatedCircuits: [3] }]) });
    expect(evaluate(evidence(withRound(r))).pass).toBe(true);
  });

  describe("power bracketed by FRM before and after the sample (owner, 2026-10-20; run 39517898558)", () => {
    const at = (consumed: number) => parseGetPowerFull([{ ...frmRow, CircuitGroupID: 0, PowerConsumed: consumed }]);
    const modRound = (consumption: number, frmBefore: Frm, frmAfter: Frm) =>
      round(30, true, { snapshot: { power: { circuits: [{ ...circuit, circuitGroupId: 0, consumptionMW: consumption }] } }, frmBefore, frmAfter });

    it("passes run 39517898558's failing round when the reads bracket it (before 1233.2, after 1218.2, mod 1233.1)", () => {
      const r = modRound(1233.1043701171875, at(1233.2), at(1218.204345703125));
      expect(evaluate(evidence(withRound(r))).pass).toBe(true);
    });

    it("passes a value within 1% outside the bracket, either side", () => {
      // tol = 0.01 * max(|1218.2|, |1233.2|, 1) = 12.332
      expect(evaluate(evidence(withRound(modRound(1245.5, at(1233.2), at(1218.2))))).pass).toBe(true);
      expect(evaluate(evidence(withRound(modRound(1205.9, at(1218.2), at(1233.2))))).pass).toBe(true);
    });

    it("fails a value more than 1% outside the bracket, with the exact reason", () => {
      const r = modRound(1245.6, at(1233.2), at(1218.2));
      const v = evaluate(evidence(withRound(r)));
      expect(v.pass).toBe(false);
      expect(v.reasons).toEqual([`${r.at}: group 0 consumption 1245.6 vs FRM 1218.2..1233.2`]);
    });

    it("fails a round missing the read before or after the sample", () => {
      const before = modRound(800, null, at(800));
      expect(evaluate(evidence(withRound(before))).reasons).toEqual([`${before.at}: no FRM read before the mod's sample`]);
      const after = modRound(800, at(800), null);
      expect(evaluate(evidence(withRound(after))).reasons).toEqual([`${after.at}: no FRM read after the mod's sample`]);
    });

    it("says in the pass reason that FRM was polled during the world rounds, how often and how many reads", () => {
      const v = evaluate(evidence(enrol({ frmPolling: { intervalMs: 250, reads: 412, failed: 1 } })));
      expect(v.pass).toBe(true);
      expect(v.reasons[0]).toMatch(/; FRM was polled every 250 ms during the world rounds \(412 reads, 1 failed\)$/);
    });

    it("the gap constant is 1 s (controller ruling on the owner's decision)", () => {
      expect(FRM_BRACKET_GAP_MS).toBe(1000);
    });

    it("passes reads 0.4 s either side of the sample", () => {
      const r = { ...round(30, true), frmBeforeEndedAt: "2026-10-20T15:59:59.600Z", frmAfterStartedAt: "2026-10-20T16:00:00.400Z" };
      expect(evaluate(evidence(withRound(r))).pass).toBe(true);
    });

    it("fails a read 1.2 s away, either side, with the exact reason", () => {
      const b = { ...round(30, true), frmBeforeEndedAt: "2026-10-20T15:59:58.800Z" };
      expect(evaluate(evidence(withRound(b))).reasons).toEqual([`${b.at}: the FRM read before the mod's sample ended 1200 ms before it, more than 1000 ms`]);
      const a = { ...round(30, true), frmAfterStartedAt: "2026-10-20T16:00:01.200Z" };
      expect(evaluate(evidence(withRound(a))).reasons).toEqual([`${a.at}: the FRM read after the mod's sample started 1200 ms after it, more than 1000 ms`]);
    });

    it("fails reversed reads: a 'before' that ended after the sample, an 'after' that started before it", () => {
      const b = { ...round(30, true), frmBeforeEndedAt: "2026-10-20T16:00:00.100Z" };
      expect(evaluate(evidence(withRound(b))).reasons).toEqual([`${b.at}: the FRM read before the mod's sample ended 100 ms after it`]);
      const a = { ...round(30, true), frmAfterStartedAt: "2026-10-20T15:59:59.900Z" };
      expect(evaluate(evidence(withRound(a))).reasons).toEqual([`${a.at}: the FRM read after the mod's sample started 100 ms before it`]);
    });

    it("fails a missing read time, or a snapshot with no observedAt", () => {
      const b = { ...round(30, true), frmBeforeEndedAt: null };
      expect(evaluate(evidence(withRound(b))).reasons).toEqual([`${b.at}: no time for the FRM read before the mod's sample`]);
      const a = { ...round(30, true), frmAfterStartedAt: "not a time" };
      expect(evaluate(evidence(withRound(a))).reasons).toEqual([`${a.at}: no time for the FRM read after the mod's sample`]);
      const o = round(30, true, { snapshot: { observedAt: undefined } });
      expect(evaluate(evidence(withRound(o))).reasons).toEqual([`${o.at}: the snapshot has no observedAt to bracket`]);
    });

    it("takes group ids and the fuse from either read, and fails when neither matches", () => {
      const two = parseGetPowerFull([frmRow, { ...frmRow, CircuitGroupID: 2 }]);
      expect(evaluate(evidence(withRound(round(30, true, { frmBefore: parseGetPowerFull([frmRow]), frmAfter: two })))).pass).toBe(true);
      const tripped = parseGetPowerFull([{ ...frmRow, FuseTriggered: true }]);
      expect(evaluate(evidence(withRound(round(30, true, { frmBefore: tripped, frmAfter: parseGetPowerFull([frmRow]) })))).pass).toBe(true);
      const r = round(30, true, { frmBefore: two, frmAfter: parseGetPowerFull([{ ...frmRow, CircuitGroupID: 3 }]) });
      expect(evaluate(evidence(withRound(r))).reasons).toEqual([`${r.at}: circuit groups 1 vs FRM 1,2 before, 3 after`]);
      const f = round(30, true, { frm: tripped });
      expect(evaluate(evidence(withRound(f))).reasons).toEqual([`${f.at}: group 1 fuse false vs FRM true`]);
    });
  });
});
