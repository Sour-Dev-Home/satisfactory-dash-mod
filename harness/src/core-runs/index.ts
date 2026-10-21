import type { CoreRunId, CoreRunSpec } from "../core-run";
import { command } from "./command";
import { enrol } from "./enrol";
import { httpProbe } from "./http-probe";
import { revoke } from "./revoke";

/** The core runs this harness can drive; Plan 2B's later tasks add theirs. */
export const CORE_RUNS: Partial<Record<CoreRunId, CoreRunSpec>> = { "http-probe": httpProbe, enrol, revoke, command };
