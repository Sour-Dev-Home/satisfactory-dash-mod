import * as Pass from "../../../runs/tick-cost-native/pass";
import { NATIVE_SOURCES_S4B, type CoreRunSpec } from "../core-run";
import { driveTickCost, tickCost } from "./tick-cost";

/**
 * The tick-cost run (baseline boot without the mod, then the mod's boot, each measured for the warmup plus 10 minutes) with
 * the players, resource-node, factory, trains, rails, conveyors, map markers, drone routes and power lines readers set to native through the mod's config
 * (NATIVE_SOURCES_S4B, which adds extractors, generators, station rates and storage; the Test3 save, worldKey "sprint3"). Same measurement and same limits; a different
 * judge (runs/tick-cost-native/pass.ts), which also requires the native readers to have been live in the window. The mod
 * enrols after the world is up (as parity does), so its first resource-node post and its first shared logistics pass and the markers and drone reads (the
 * native first read is 480 s +/-10% after the agent starts Running) fall inside the measured span, 3 to 13 minutes after
 * the measurement starts.
 */
export const tickCostNative: CoreRunSpec = {
  ...tickCost,
  requirements: Pass.requirements,
  worldKey: "sprint3",
  modConfig: NATIVE_SOURCES_S4B,
  evaluate: Pass.evaluate,
  drive: (ctx) => driveTickCost(ctx, "world-first", "sprint3"),
};
