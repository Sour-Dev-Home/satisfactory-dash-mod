// The manual session (not a claim of the mod): the test server is held up, with the staged world loaded and auto-pause
// off, for a person to play on (the dash's G-B1 and G-B2 checks). This file judges availability only.
// - QueryServerState is sampled every SAMPLE_INTERVAL_MS through the window. Every sample must have an answer saying
//   the game is running (within 10 s), and the window must have at least its expected number of samples
//   (floor(window / interval)); a missing or unanswered sample fails with a reason.
// - R7 step 4 retired the run's evidence-only reads of an external monitor's belt and pipe lists (never judged); the
//   verdict is unchanged. Earlier captures carry an extra field that is ignored.
import type { CoreEvidence } from "../../harness/src/core-run";
import type { Verdict } from "../../harness/src/verdict";

export const requirements = ["manual"];
export const SAMPLE_INTERVAL_MS = 30_000;

export interface Manual {
  windowMs: number;
  samples: Array<{ at: string; answered: boolean }>;
}

export const expectedSamples = (windowMs: number) => Math.floor(windowMs / SAMPLE_INTERVAL_MS);

export function evaluate(e: CoreEvidence): Verdict {
  const c = e.captures["manual.json"] as Manual | undefined;
  if (!c) return { pass: false, reasons: ["no manual capture"] };
  const reasons: string[] = [];
  const want = expectedSamples(c.windowMs);
  const unanswered = c.samples.filter((s) => !s.answered).length;
  let pass = true;
  if (c.samples.length < want) {
    pass = false;
    reasons.push(`${c.samples.length} samples, expected ${want} (one every ${SAMPLE_INTERVAL_MS / 1000} s for ${c.windowMs / 60_000} min)`);
  }
  if (unanswered) {
    pass = false;
    reasons.push(`${unanswered} of ${c.samples.length} samples got no answer from QueryServerState saying the game is running`);
  }
  if (pass) reasons.unshift(`the server answered QueryServerState in all ${c.samples.length} samples over ${c.windowMs / 60_000} min; availability only, G-B1/G-B2 are the owner's checks in the game`);
  return { pass, reasons };
}
