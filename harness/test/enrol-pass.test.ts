import { describe, expect, it } from "vitest";
import { type Enrol, evaluate, type Round } from "../../runs/enrol/pass";
import type { CoreEvidence } from "../src/core-run";
import { parseGetPowerFull } from "../src/frm";

const SESSION = "0123456789ab";
const ENROLLED = "[t][1]LogSatisDash: Display: Enrolled; reporting to the backend";

const circuit = { circuitGroupId: 1, productionMW: 1200, consumptionMW: 800, capacityMW: 1500, maxConsumptionMW: 1400, fuseTriggered: false, batteryCapacityMWh: 100, batteryPercent: 50, batteryDifferentialMW: -12.5 };
const frmRow = { CircuitGroupID: 1, PowerProduction: 1200, PowerConsumed: 800, PowerCapacity: 1500, PowerMaxConsumed: 1400, BatteryCapacity: 100, BatteryPercent: 50, BatteryDifferential: -12.5, FuseTriggered: false };

function round(i: number, world: boolean, over: { snapshot?: Record<string, unknown>; status?: Record<string, unknown>; api?: Record<string, unknown>; frm?: Round["frm"] } = {}): Round {
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
    frm: world ? (over.frm !== undefined ? over.frm : parseGetPowerFull([frmRow])) : null,
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

  it("says so in the pass reason when the autoPause check was skipped, and when no circuit group was compared", () => {
    const d = enrol();
    for (const r of d.rounds) {
      r.autoPause = null;
      if (r.world) {
        r.frm = [];
        r.snapshot.power = { circuits: [] };
      }
    }
    expect(evaluate(evidence(d))).toEqual({
      pass: true,
      reasons: ["enrolled; 4 rounds with no game and 12 with the world match the game's APIs; the autoPause check was skipped (GetServerOptions never answered); power compared for 0 circuit group(s)"],
    });
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
});
