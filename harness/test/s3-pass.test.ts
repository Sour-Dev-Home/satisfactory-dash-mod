import { describe, expect, it } from "vitest";
import { evaluate } from "../../spikes/S3/pass";
import type { PowerPair } from "../src/verdict";

const pair = (i: number, frmProd = 1000): PowerPair => ({
  at: `t${i}`, circuitId: 0,
  mod: { production: 1000, consumption: 700, capacity: 1200 },
  frm: { production: frmProd, consumption: 650, capacity: 1200 },
});
const ten = Array.from({ length: 10 }, (_, i) => pair(i));
const ev = (pairs: PowerPair[], frmCircuits = [0], modCircuits = [0]) => ({ modLines: [], echo: [], power: { pairs, frmCircuits, modCircuits } });

describe("S3 verdict", () => {
  it("passes on 10 pairs within 1% on production and capacity (consumption isn't judged)", () => {
    expect(evaluate(ev(ten)).pass).toBe(true);
  });
  it("fails below 10 judged pairs, counting FRM misses as unjudged (Review Focus 4)", () => {
    const nine = [...ten.slice(0, 9), { ...pair(9), frm: null }];
    expect(evaluate(ev(nine)).reasons.join(" ")).toMatch(/9 paired readings, need 10/);
  });
  it("fails when a pair is more than 1% apart", () => {
    expect(evaluate(ev([...ten.slice(0, 9), pair(9, 1020)])).reasons.join(" ")).toMatch(/production 1000 vs FRM 1020/);
  });
  it("fails when the circuit sets differ, naming the circuit (Review Focus 5)", () => {
    expect(evaluate(ev(ten, [0, 7], [0])).reasons.join(" ")).toMatch(/FRM has circuit 7/);
    expect(evaluate(ev(ten, [0], [0, 5])).reasons.join(" ")).toMatch(/the mod logged circuit 5/);
  });

  // The owner's world, run #11: one FRM group [0, 3]; the mod read the group's figures for both circuits.
  const joined = (i: number, circuitId: number, production = 3633.33): PowerPair => ({
    at: `t${i}`, circuitId, group: [0, 3],
    mod: { production, consumption: 1300, capacity: 4083.33 },
    frm: { production: 3633.3335, consumption: 1299.38, capacity: 4083.3335 },
  });
  const rounds = Array.from({ length: 6 }, (_, i) => [joined(i, 0), joined(i, 3)]).flat();

  it("judges a joined group when its circuits read the same values in each round (ruling 6, amended 2026-10-20)", () => {
    expect(evaluate(ev(rounds, [0, 3], [0, 3])).pass).toBe(true);
  });
  it("fails when circuits of one FRM group read different values in the same round, naming them", () => {
    const split = [...rounds.slice(0, 11), joined(5, 3, 1000)];
    expect(evaluate(ev(split, [0, 3], [0, 3])).reasons.join(" ")).toMatch(/t5: circuits 0, 3 share an FRM group but the mod read different values/);
  });
});
