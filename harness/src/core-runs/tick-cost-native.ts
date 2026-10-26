import * as Pass from "../../../runs/tick-cost-native/pass";
import { NATIVE_SOURCES_S2, type CoreRunSpec } from "../core-run";
import { driveTickCost, tickCost } from "./tick-cost";

/**
 * The tick-cost run (baseline boot without the mod, then the mod's boot, each measured for the warmup plus 10 minutes) with
 * the players, resource-node, factory and trains readers set to native through the mod's config (NATIVE_SOURCES_S2; the
 * Test2 save, worldKey "s2"). Same measurement and same limits; a different
 * judge (runs/tick-cost-native/pass.ts), which also requires the native readers to have been live in the window. The mod
 * enrols after the world is up (as parity does), so its first resource-node post (8 minutes +/-10% after the agent starts
 * Running) falls inside the measured span, 3 to 13 minutes after the measurement starts.
 */
export const tickCostNative: CoreRunSpec = {
  ...tickCost,
  requirements: Pass.requirements,
  worldKey: "s2",
  modConfig: NATIVE_SOURCES_S2,
  evaluate: Pass.evaluate,
  drive: (ctx) => driveTickCost(ctx, "world-first", "s2"),
};
