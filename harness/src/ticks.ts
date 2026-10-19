import { z } from "zod";
import type { ServerApi } from "./server-api";

/** 20 minutes per window: the owner's decision of 2026-10-19 (the claims table said 30). */
export const TICK_WINDOW_MS = 20 * 60_000;
export const TICK_INTERVAL_MS = 10_000;
/**
 * Readings before this point are kept but left out of the median. The test server runs at ~30 after boot, then
 * settles at ~22 with or without the mod: within a minute on later boots, 10 minutes on the first (world-creating)
 * boot (S2 runs of 2026-10-19). So a fresh test server must boot once before S2.
 */
export const TICK_WARMUP_MS = 3 * 60_000;

// The field the satisfactory-dash tick-rate card reads (game-adapter rawSchemas.ts, captured QueryServerState).
const QueryServerStateSchema = z.object({
  data: z.object({ serverGameState: z.object({ averageTickRate: z.number().nonnegative() }) }),
});

export async function readTickRate(api: ServerApi): Promise<number> {
  const { status, body } = await api.call("QueryServerState");
  if (status !== 200) throw new Error(`QueryServerState returned ${status}`);
  return QueryServerStateSchema.parse(body).data.serverGameState.averageTickRate;
}

export interface TickSample { at: string; value: number }
export interface TickSeries { warmup: TickSample[]; samples: TickSample[]; errors: number; median: number | null }

export function median(values: number[]): number | null {
  if (values.length === 0) return null;
  const s = [...values].sort((a, b) => a - b);
  const mid = Math.floor(s.length / 2);
  return s.length % 2 ? s[mid]! : (s[mid - 1]! + s[mid]!) / 2;
}

/** Medians of the window's first and second halves: a boot phase inside the window shows as a gap between them. */
export function halfMedians(samples: TickSample[]): { first: number; second: number } | null {
  if (samples.length === 0) return null;
  const values = samples.map((s) => s.value);
  const mid = Math.ceil(values.length / 2);
  return { first: median(values.slice(0, mid))!, second: median(values.slice(values.length - mid))! };
}

export async function sampleTicks(
  read: () => Promise<number>,
  opts: { warmupMs?: number; durationMs: number; intervalMs: number; sleep: (ms: number) => Promise<void>; now: () => number },
): Promise<TickSeries> {
  const warmup: TickSample[] = [];
  const samples: TickSample[] = [];
  let errors = 0;
  const settled = opts.now() + (opts.warmupMs ?? 0);
  const end = settled + opts.durationMs;
  while (opts.now() < end) {
    const now = opts.now();
    const at = new Date(now).toISOString();
    try {
      (now < settled ? warmup : samples).push({ at, value: await read() });
    } catch {
      if (now >= settled) errors++;
    }
    await opts.sleep(opts.intervalMs);
  }
  return { warmup, samples, errors, median: median(samples.map((s) => s.value)) };
}
