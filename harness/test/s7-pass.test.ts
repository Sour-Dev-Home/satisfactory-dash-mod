import { describe, expect, it } from "vitest";
import { evaluate } from "../../spikes/S7/pass";
import type { AutoPauseStep, Evidence } from "../src/verdict";

const run = (orig: boolean, over: Partial<Record<AutoPauseStep["step"], Partial<AutoPauseStep>>> = {}): Evidence => {
  const s = (step: AutoPauseStep["step"], mod: boolean, pending: boolean | null): AutoPauseStep => ({ at: step, step, mod, pending, api: mod, ...over[step] });
  return { modLines: [], echo: [], autoPause: { steps: [s("read", orig, false), s("set", !orig, null), s("check", !orig, false), s("restore", orig, null), s("final", orig, false)] } };
};

describe("S7 evaluate", () => {
  it("passes when all five steps agree with GetServerOptions", () => {
    expect(evaluate(run(true))).toEqual({ pass: true, reasons: ["auto-pause read True, changed to False and back natively; GetServerOptions agreed at all five steps"] });
  });
  it("fails without any auto-pause evidence", () => {
    expect(evaluate({ modLines: [], echo: [] })).toEqual({ pass: false, reasons: ["the harness collected no auto-pause steps"] });
  });
  it("fails when the API didn't see the change", () => {
    expect(evaluate(run(true, { check: { api: true } })).reasons).toContain("check: mod False vs GetServerOptions True");
  });
  it("fails when the change stayed pending", () => {
    expect(evaluate(run(false, { check: { pending: true } })).reasons).toContain("check: the option is still pending");
  });
  it("fails when the value wasn't put back", () => {
    expect(evaluate(run(true, { final: { mod: false, api: false } })).reasons).toContain("final: False, but the mod read True first");
  });
  it("names a missing step", () => {
    const e = run(true);
    e.autoPause!.steps = e.autoPause!.steps.filter((s) => s.step !== "restore");
    expect(evaluate(e).reasons).toContain("the mod never logged step=restore");
  });
  it("names a step whose API read failed", () => {
    expect(evaluate(run(true, { set: { api: null } })).reasons).toContain("set: GetServerOptions failed");
  });
});
