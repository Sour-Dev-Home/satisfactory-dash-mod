import * as Pass from "../../../runs/soak-native/pass";
import { type CoreRunSpec } from "../core-run";
import type { Recorder } from "../recorder";
import { accepted } from "./enrol";
import { driveTickCost, tickCost } from "./tick-cost";

/**
 * The measured window after the warmup: 16 hours. ADR-0058 line 153 asks for one 24-hour soak with every reader on before the R7
 * removal release; the owner set it to 16 hours on 2026-11-03.
 */
export const SOAK_WINDOW_MS = 16 * 60 * 60_000;
/**
 * The fail-fast watchdog (owner confirmed 2026-11-03). The owner's PC runs this for about 17 hours, so if the backend host or the game server
 * dies the run must end loudly, not sit out the rest of the day. No accepted mod snapshot for this long (the snapshot cadence is 5 s;
 * the mod's retry back-off is at most 300 s, Pusher.h MaxRetryAfterSeconds) ends the measurement with an error that fails the run.
 * A dead game server already ends it through `proc.exited` (tick-cost's `stopped` callback, unchanged).
 */
export const SOAK_STALL_MS = 15 * 60_000;

/** The watchdog: a reason once no snapshot has been accepted for SOAK_STALL_MS (counted from the first call when none has been). */
export function snapshotWatchdog(recorder: Recorder, stallMs = SOAK_STALL_MS): (nowMs: number) => string | null {
  let firstCall: number | null = null;
  return (nowMs) => {
    firstCall ??= nowMs;
    const last = accepted(recorder).at(-1);
    const since = last ? Date.parse(last.at) : firstCall;
    const quiet = nowMs - since;
    if (!(quiet > stallMs)) return null;
    return `soak watchdog: no snapshot was accepted for ${Math.round(quiet / 60_000)} minutes (limit ${stallMs / 60_000}): the backend host or the game server is down, so the run is ending now instead of waiting out the window`;
  };
}

/**
 * The 16-hour all-readers soak (ADR-0058, the R7 precondition; owner 2026-11-03: "yes as recommended", then "16hr soak instead"):
 * tick-cost-native with the measured window set to 16 hours. The baseline boot (no mod) stays tick-cost's own 10 minutes; the with-mod boot has the same mod
 * config as tick-cost-native (every reader native, stepDiagnostics on), the same world-first order, the same sampling every 30 s, and
 * the same judge plus the soak conditions in runs/soak-native/pass.ts (owner confirmed 2026-11-03). Two additions only the soak
 * needs: a snapshot watchdog (SOAK_STALL_MS) and a recorder that keeps one snapshot body (see RecorderOptions.thinOlderSnapshots).
 */
export const soakNative: CoreRunSpec = {
  ...tickCost,
  requirements: Pass.requirements,
  worldKey: "sprint3",
  modConfig: { stepDiagnostics: true },
  evaluate: Pass.evaluate,
  drive: (ctx) => driveTickCost(ctx, "world-first", "sprint3", { windowMs: SOAK_WINDOW_MS, stall: (recorder) => snapshotWatchdog(recorder) }),
};
