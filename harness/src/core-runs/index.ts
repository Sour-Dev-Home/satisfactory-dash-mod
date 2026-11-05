import type { CoreRunId, CoreRunSpec } from "../core-run";
import { command } from "./command";
import { enrol } from "./enrol";
import { httpProbe } from "./http-probe";
import { manual } from "./manual";
import { outage } from "./outage";
import { revoke } from "./revoke";
import { tickCost } from "./tick-cost";
import { tickCostNative } from "./tick-cost-native";
import { s3Census } from "./s3-census";
import { soakNative } from "./soak-native";

/** The core runs this harness can drive. */
export const CORE_RUNS: Partial<Record<CoreRunId, CoreRunSpec>> = { "http-probe": httpProbe, enrol, revoke, command, outage, "tick-cost": tickCost, manual, "tick-cost-native": tickCostNative, "s3-census": s3Census, "soak-native": soakNative };
