import { describe, expect, it } from "vitest";
import { evaluate, expectedSamples, SAMPLE_INTERVAL_MS, type Manual } from "../../runs/manual/pass";
import type { CoreEvidence } from "../src/core-run";

const START = Date.parse("2026-10-21T10:00:00.000Z");
const WINDOW = 5 * 60_000;
const samples = (n: number, answered = true) =>
  Array.from({ length: n }, (_, i) => ({ at: new Date(START + i * SAMPLE_INTERVAL_MS).toISOString(), answered }));
const capture = (over: Partial<Manual> = {}): Manual => ({
  windowMs: WINDOW,
  samples: samples(10),
  frm: { start: { belts: [], pipes: [] }, end: { belts: [], pipes: [] } },
  ...over,
});
const ev = (c: Manual | undefined): CoreEvidence => ({
  lines: [], errorLines: [], leaks: { code: false, secret: null, bearer: false }, captures: c ? { "manual.json": c } : {},
});

describe("manual pass", () => {
  it("samples every 30 s, so a window of N minutes wants 2N samples", () => {
    expect(SAMPLE_INTERVAL_MS).toBe(30_000);
    expect(expectedSamples(WINDOW)).toBe(10);
    expect(expectedSamples(45 * 60_000)).toBe(90);
  });

  it("passes when every sample answered, and says it judges availability only", () => {
    const v = evaluate(ev(capture()));
    expect(v.pass).toBe(true);
    expect(v.reasons.join(" ")).toMatch(/availability only/);
  });

  it("fails without a capture", () => {
    const v = evaluate(ev(undefined));
    expect(v.pass).toBe(false);
    expect(v.reasons[0]).toMatch(/no manual capture/);
  });

  it("fails when a sample got no answer, naming it", () => {
    const s = samples(10);
    s[4]!.answered = false;
    const v = evaluate(ev(capture({ samples: s })));
    expect(v.pass).toBe(false);
    expect(v.reasons.join(" ")).toMatch(/1 of 10 samples got no answer/);
  });

  it("fails when samples are missing, with how many were expected", () => {
    const v = evaluate(ev(capture({ samples: samples(7) })));
    expect(v.pass).toBe(false);
    expect(v.reasons.join(" ")).toMatch(/7 samples, expected 10/);
  });

  it("fails on no samples at all", () => {
    expect(evaluate(ev(capture({ samples: [] }))).pass).toBe(false);
  });

  it("does not fail on a failed FRM read, but records it in the reasons", () => {
    const v = evaluate(ev(capture({ frm: { start: { error: "boom" }, end: { belts: [], pipes: [] } } })));
    expect(v.pass).toBe(true);
    expect(v.reasons.join(" ")).toMatch(/FRM read at the start failed/);
  });
});
