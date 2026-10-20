import type { CoreRunId, CoreRunSpec } from "../core-run";
import { httpProbe } from "./http-probe";

/** The core runs this harness can drive; Plan 2B's later tasks add theirs. */
export const CORE_RUNS: Partial<Record<CoreRunId, CoreRunSpec>> = { "http-probe": httpProbe };
