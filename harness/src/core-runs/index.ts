import type { CoreRunId, CoreRunSpec } from "../core-run";
import { command } from "./command";
import { enrol } from "./enrol";
import { httpProbe } from "./http-probe";
import { manual } from "./manual";
import { outage } from "./outage";
import { parity } from "./parity";
import { nativeParity } from "./native-parity";
import { revoke } from "./revoke";
import { tickCost } from "./tick-cost";
import { tickCostNative } from "./tick-cost-native";
import { s3Census } from "./s3-census";
import { s4Capture } from "./s4-capture";

/** The core runs this harness can drive; Plan 2B's later tasks add theirs. */
export const CORE_RUNS: Partial<Record<CoreRunId, CoreRunSpec>> = { "http-probe": httpProbe, enrol, revoke, command, outage, "tick-cost": tickCost, parity, manual, "native-parity": nativeParity, "tick-cost-native": tickCostNative, "s3-census": s3Census, "s4-capture": s4Capture };
