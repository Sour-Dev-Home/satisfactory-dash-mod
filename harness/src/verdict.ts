import type { CapturedRequest } from "./echo";
import type { TickSeries } from "./ticks";

/** The spikes this harness version runs. S3, S4 and S5 come with their probes in the spikes plan. */
export const SPIKE_IDS = ["S0", "S1", "S1b", "S2"] as const;
export type SpikeId = (typeof SPIKE_IDS)[number];

export interface Evidence { modLines: string[]; echo: CapturedRequest[]; ticks?: { baseline: TickSeries; withMod: TickSeries } }
export interface Verdict { pass: boolean; reasons: string[] }
