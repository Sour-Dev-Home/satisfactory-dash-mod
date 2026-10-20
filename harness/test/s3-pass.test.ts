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
});
