import { describe, expect, it } from "vitest";
import { evaluate, OUTAGE_DELIVERED_SHARE, OUTAGE_EXPECTED_SNAPSHOTS, OUTAGE_MS, type Outage } from "../../runs/outage/pass";
import type { CoreEvidence } from "../src/core-run";
import type { TickSeries } from "../src/ticks";

const STOPPED = "2026-10-20T16:00:00.000Z";
const at = (s: number) => new Date(Date.parse(STOPPED) + s * 1000).toISOString();
const series = (values: number[]): TickSeries => ({
  warmup: [], samples: values.map((value, i) => ({ at: at(i * 10), value })), errors: 0, median: values.length ? [...values].sort((a, b) => a - b)[Math.floor(values.length / 2)]! : null,
});
/** A snapshot every 5 s through the outage (36), all delivered afterwards, plus some taken after the restart. */
const observed = (n: number, from = 2.5) => Array.from({ length: n }, (_, i) => at(from + i * 5));

function capture(over: Partial<Outage> = {}): Outage {
  return {
    before: series([22, 22, 23, 22]),
    during: series([22, 21.5, 22, 22]),
    after: series([22, 22]),
    stoppedAt: STOPPED,
    startedAt: at(181),
    acceptedAfter: [...observed(36), at(185), at(190)],
    ...over,
  };
}
const LINES = [
  "[t][1]LogSatisDash: Display: Enrolled",
  "[t][2]LogSatisDash: Warning: Pushing snapshots failed (0); retrying with backoff (1 queued)",
  "[t][3]LogSatisDash: Warning: Polling for commands failed (0); retrying with backoff",
  "[t][4]LogSatisDash: Display: Polling for commands recovered after 5 attempts",
  "[t][5]LogSatisDash: Display: Pushing snapshots recovered after 9 attempts (36 queued, 0 dropped)",
];
const ev = (c: Outage | undefined, lines = LINES): CoreEvidence => ({
  lines, errorLines: [], leaks: { code: false, secret: false, bearer: false }, captures: c ? { "outage.json": c } : {},
});

describe("outage pass", () => {
  it("pins the plan's numbers: 3 minutes, a snapshot every 5 s, 80% delivered", () => {
    expect(OUTAGE_MS).toBe(180_000);
    expect(OUTAGE_EXPECTED_SNAPSHOTS).toBe(36);
    expect(OUTAGE_DELIVERED_SHARE).toBe(0.8);
  });

  it("passes a clean capture and states its counts", () => {
    const v = evaluate(ev(capture()));
    expect(v.pass).toBe(true);
    expect(v.reasons[0]).toContain("36 of 36");
    expect(v.reasons[0]).toContain("22");
  });

  it("passes with no command-poll failure at all", () => {
    expect(evaluate(ev(capture(), [LINES[0]!, LINES[1]!, LINES[4]!])).pass).toBe(true);
  });

  it("fails without a capture", () => {
    expect(evaluate(ev(undefined))).toEqual({ pass: false, reasons: ["no outage capture"] });
  });

  it("fails unless the push failure is logged exactly once", () => {
    expect(evaluate(ev(capture(), LINES.filter((_, i) => i !== 1))).reasons).toContain("'Pushing snapshots failed' appears 0 times in the log, not once");
    expect(evaluate(ev(capture(), [...LINES, LINES[1]!])).reasons).toContain("'Pushing snapshots failed' appears 2 times in the log, not once");
  });

  it("fails unless the push recovery is logged exactly once", () => {
    expect(evaluate(ev(capture(), LINES.slice(0, 4))).reasons).toContain("'Pushing snapshots recovered' appears 0 times in the log, not once");
    expect(evaluate(ev(capture(), [...LINES, LINES[4]!])).reasons).toContain("'Pushing snapshots recovered' appears 2 times in the log, not once");
  });

  it("fails when the command poll logs its failure or recovery more than once", () => {
    expect(evaluate(ev(capture(), [...LINES, LINES[2]!])).reasons).toContain("'Polling for commands failed' appears 2 times in the log, more than once");
    expect(evaluate(ev(capture(), [...LINES, LINES[3]!])).reasons).toContain("'Polling for commands recovered' appears 2 times in the log, more than once");
  });

  it("fails when fewer than 80% of the outage's snapshots are delivered afterwards (29 of 36 needed)", () => {
    expect(evaluate(ev(capture({ acceptedAfter: observed(29) }))).pass).toBe(true);
    const v = evaluate(ev(capture({ acceptedAfter: [...observed(28), at(185), at(190), at(-5)] })));
    expect(v.pass).toBe(false);
    expect(v.reasons).toContain("28 snapshots observed during the outage were delivered afterwards; at least 29 (80% of 36) must be");
  });

  it("counts a repeated observedAt once", () => {
    const v = evaluate(ev(capture({ acceptedAfter: [...observed(28), ...observed(28)] })));
    expect(v.reasons).toContain("28 snapshots observed during the outage were delivered afterwards; at least 29 (80% of 36) must be");
  });

  it("fails when the outage times are missing, reversed or shorter than 3 minutes", () => {
    expect(evaluate(ev(capture({ stoppedAt: null }))).reasons).toContain("the relay's stop or restart time was not recorded");
    expect(evaluate(ev(capture({ startedAt: null }))).reasons).toContain("the relay's stop or restart time was not recorded");
    expect(evaluate(ev(capture({ startedAt: at(170) }))).reasons).toContain("the relay was down 170 s, less than 180 s");
  });

  it("fails when the tick median during the outage is below 95% of the one before", () => {
    const v = evaluate(ev(capture({ before: series([22, 22, 22]), during: series([20.8, 20.8, 20.8]) })));
    expect(v.pass).toBe(false);
    expect(v.reasons).toContain("tick median dropped from 22 before the outage to 20.8 during it, more than 5%");
    expect(evaluate(ev(capture({ before: series([22, 22, 22]), during: series([21, 21, 21]) }))).pass).toBe(true);
  });

  it("fails when a tick window has no samples or is missing", () => {
    expect(evaluate(ev(capture({ before: series([]) }))).reasons).toContain("the tick window before the outage has no samples");
    expect(evaluate(ev(capture({ during: null }))).reasons).toContain("the tick window during the outage has no samples");
  });
});
