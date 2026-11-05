import * as Pass from "../../../runs/tick-cost-native/pass";
import { type CoreRunSpec } from "../core-run";
import { driveTickCost, tickCost } from "./tick-cost";

/**
 * The tick-cost run (baseline boot without the mod, then the mod's boot, each measured for the warmup plus 10 minutes) with
 * every reader native (all are, unconditionally, since R7; the Test3 save, worldKey "sprint3"). Same measurement and same limits; a different
 * judge (runs/tick-cost-native/pass.ts), which also requires the native readers to have been live in the window. The mod
 * enrols after the world is up (as parity does), so its first resource-node post and its first shared logistics pass and the markers and drone reads (the
 * native first read is 480 s +/-10% after the agent starts Running) fall inside the measured span, 3 to 13 minutes after
 * the measurement starts.
 */
export const tickCostNative: CoreRunSpec = {
  ...tickCost,
  requirements: Pass.requirements,
  worldKey: "sprint3",
  // stepDiagnostics (owner 2026-11-01): the mod's thread-cycle and conveyor call-timing diagnostics are off in a normal install; this run turns them on.
  modConfig: { stepDiagnostics: true },
  evaluate: Pass.evaluate,
  drive: (ctx) => driveTickCost(ctx, "world-first", "sprint3"),
};
