// The backend-outage run (spec "Testing" 2): the relay stops for 3 minutes; the mod logs the failure once and the
// recovery once, delivers what it observed during the outage afterwards (its queue holds 120 snapshots, the outage
// makes about 36), and the game's tick rate does not drop while the backend is gone.
// - Logs: exactly one "Pushing snapshots failed" and one "Pushing snapshots recovered" line (Pusher.cpp); at most one
//   "Polling for commands failed" and one "Polling for commands recovered" line (DriverCommands.cpp).
// - Catch-up: the status cadence is 5 s (FCadence::StatusSeconds, Responses.h), so 180 s make 36 snapshots; at least
//   80% of them (29) must be accepted after the restart, counted by distinct observedAt inside [stoppedAt, startedAt].
//   stoppedAt is taken after the stop call returned and startedAt before the start call, so the window is one in which
//   the relay was certainly down.
// - Ticks: the median during the outage is at least 95% of the median before it (S2's tolerance, spikes/S2/pass.ts).
import { S2_TICK_TOLERANCE } from "../../spikes/S2/pass";
import type { CoreEvidence } from "../../harness/src/core-run";
import type { TickSeries } from "../../harness/src/ticks";
import type { Verdict } from "../../harness/src/verdict";

export const requirements = ["outage", "C4"];
export const OUTAGE_MS = 180_000;
/**
 * The status cadence the expectation assumes. The mod takes it from the backend's answer (Responses.cpp), and both the
 * backend's value and the mod's default are 5 s today (FCadence::StatusSeconds, Responses.h:35). Kept hard-coded by the
 * controller's ruling (review M3 of 1ca0065); if the backend's cadence changes, this changes with it.
 */
export const STATUS_CADENCE_MS = 5000;
export const OUTAGE_EXPECTED_SNAPSHOTS = OUTAGE_MS / STATUS_CADENCE_MS;
export const OUTAGE_DELIVERED_SHARE = 0.8;

export interface Outage {
  /** Ticks for 3 minutes before the stop, during the outage, and for the 3 minutes after the restart (evidence). */
  before: TickSeries | null;
  during: TickSeries | null;
  after: TickSeries | null;
  /** After the relay's stop call returned; null when the run never got there. */
  stoppedAt: string | null;
  /** Before the relay's start call; null when the run never got there. */
  startedAt: string | null;
  /** observedAt of each snapshot the backend accepted after the restart. */
  acceptedAfter: string[];
}

const count = (lines: string[], text: string) => lines.filter((l) => l.includes(text)).length;

export function evaluate(e: CoreEvidence): Verdict {
  const c = e.captures["outage.json"] as Outage | undefined;
  if (!c) return { pass: false, reasons: ["no outage capture"] };
  const reasons: string[] = [];

  for (const text of ["Pushing snapshots failed", "Pushing snapshots recovered"]) {
    const n = count(e.lines, text);
    if (n !== 1) reasons.push(`'${text}' appears ${n} times in the log, not once`);
  }
  const polls: string[] = [];
  for (const text of ["Polling for commands failed", "Polling for commands recovered"]) {
    const n = count(e.lines, text);
    polls.push(`${n}`);
    if (n > 1) reasons.push(`'${text}' appears ${n} times in the log, more than once`);
  }

  const need = Math.ceil(OUTAGE_EXPECTED_SNAPSHOTS * OUTAGE_DELIVERED_SHARE);
  const stopped = c.stoppedAt === null ? NaN : Date.parse(c.stoppedAt);
  const started = c.startedAt === null ? NaN : Date.parse(c.startedAt);
  let delivered = 0;
  if (!Number.isFinite(stopped) || !Number.isFinite(started)) {
    reasons.push("the relay's stop or restart time was not recorded");
  } else {
    const down = started - stopped;
    if (down < OUTAGE_MS) reasons.push(`the relay was down ${Math.round(down / 1000)} s, less than ${OUTAGE_MS / 1000} s`);
    const inside = new Set(c.acceptedAfter.filter((o) => {
      const t = Date.parse(o);
      return Number.isFinite(t) && t >= stopped && t <= started;
    }));
    delivered = inside.size;
    if (delivered < need) {
      reasons.push(`${delivered} snapshots observed during the outage were delivered afterwards; at least ${need} (${OUTAGE_DELIVERED_SHARE * 100}% of ${OUTAGE_EXPECTED_SNAPSHOTS}) must be`);
    }
  }

  const before = c.before?.samples.length ? c.before.median : null;
  const during = c.during?.samples.length ? c.during.median : null;
  if (before === null) reasons.push("the tick window before the outage has no samples");
  if (during === null) reasons.push("the tick window during the outage has no samples");
  if (before !== null && during !== null && during < before * (1 - S2_TICK_TOLERANCE)) {
    reasons.push(`tick median dropped from ${before} before the outage to ${during} during it, more than ${S2_TICK_TOLERANCE * 100}%`);
  }

  if (reasons.length) return { pass: false, reasons };
  const after = c.after?.samples.length ? `${c.after.median}` : "not measured";
  return {
    pass: true,
    reasons: [
      `one push failure and one recovery logged (command poll: ${polls[0]} failed, ${polls[1]} recovered); ${delivered} of ${OUTAGE_EXPECTED_SNAPSHOTS} ` +
        `snapshots observed during the outage delivered afterwards (${need} needed); tick median ${before} before, ${during} during, ${after} after`,
    ],
  };
}
