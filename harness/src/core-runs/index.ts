import type { CoreRunId, CoreRunSpec } from "../core-run";
import { command } from "./command";
import { enrol } from "./enrol";
import { httpProbe } from "./http-probe";
import { manual } from "./manual";
import { outage } from "./outage";
import { parity } from "./parity";
import { revoke } from "./revoke";
import { tickCost } from "./tick-cost";

/** The core runs this harness can drive; Plan 2B's later tasks add theirs. */
export const CORE_RUNS: Partial<Record<CoreRunId, CoreRunSpec>> = { "http-probe": httpProbe, enrol, revoke, command, outage, "tick-cost": tickCost, parity, manual };
