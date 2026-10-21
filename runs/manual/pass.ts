// The manual session (not a claim of the mod): the test server is held up, with the staged world loaded and auto-pause
// off, for a person to play on (the dash's G-B1 and G-B2 checks). This file judges availability only.
// - QueryServerState is sampled every SAMPLE_INTERVAL_MS through the window. Every sample must have an answer, and the
//   window must have at least its expected number of samples (floor(window / interval)); a missing or unanswered
//   sample fails with a reason.
// - FRM's getBelts and getPipes are read at the start and at the end of the window (trimmed, evidence for G-B1/G-B2).
//   A failed FRM read is recorded in the reasons but does not fail the verdict.
import type { CoreEvidence } from "../../harness/src/core-run";
import type { Verdict } from "../../harness/src/verdict";

export const requirements = ["manual"];
export const SAMPLE_INTERVAL_MS = 30_000;

export interface TrimmedBelt { ID?: unknown; ClassName?: unknown; ConnectedTo0?: unknown; ConnectedTo1?: unknown; ItemsPerMinute?: unknown }
export interface TrimmedPipe { ID?: unknown; ClassName?: unknown; ConnectedTo0?: unknown; ConnectedTo1?: unknown; Speed?: unknown }
/** One FRM read: the trimmed lists, or why the read failed. */
export type FrmRead = { belts: TrimmedBelt[]; pipes: TrimmedPipe[] } | { error: string };

export interface Manual {
  windowMs: number;
  samples: Array<{ at: string; answered: boolean }>;
  frm: { start: FrmRead; end: FrmRead };
}

export const expectedSamples = (windowMs: number) => Math.floor(windowMs / SAMPLE_INTERVAL_MS);

export function evaluate(e: CoreEvidence): Verdict {
  const c = e.captures["manual.json"] as Manual | undefined;
  if (!c) return { pass: false, reasons: ["no manual capture"] };
  const reasons: string[] = [];
  const want = expectedSamples(c.windowMs);
  const unanswered = c.samples.filter((s) => !s.answered).length;
  let pass = true;
  if (c.samples.length < want || c.samples.length === 0) {
    pass = false;
    reasons.push(`${c.samples.length} samples, expected ${want} (one every ${SAMPLE_INTERVAL_MS / 1000} s for ${c.windowMs / 60_000} min)`);
  }
  if (unanswered) {
    pass = false;
    reasons.push(`${unanswered} of ${c.samples.length} samples got no answer from QueryServerState`);
  }
  for (const [when, read] of [["start", c.frm.start], ["end", c.frm.end]] as const) {
    if ("error" in read) reasons.push(`the FRM read at the ${when} failed (${read.error}); recorded, not judged`);
  }
  if (pass) reasons.unshift(`the server answered QueryServerState in all ${c.samples.length} samples over ${c.windowMs / 60_000} min; availability only, G-B1/G-B2 are the owner's checks in the game`);
  return { pass, reasons };
}
