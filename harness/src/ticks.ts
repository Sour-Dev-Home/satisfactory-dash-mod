import { z } from "zod";
import type { ServerApi } from "./server-api";

/** 20 minutes per window: the owner's decision of 2026-10-19 (the claims table said 30). */
export const TICK_WINDOW_MS = 20 * 60_000;
export const TICK_INTERVAL_MS = 10_000;

// The field the satisfactory-dash tick-rate card reads (game-adapter rawSchemas.ts, captured QueryServerState).
const QueryServerStateSchema = z.object({
  data: z.object({ serverGameState: z.object({ averageTickRate: z.number().nonnegative() }) }),
});

export async function readTickRate(api: ServerApi): Promise<number> {
  const { status, body } = await api.call("QueryServerState");
  if (status !== 200) throw new Error(`QueryServerState returned ${status}`);
  return QueryServerStateSchema.parse(body).data.serverGameState.averageTickRate;
}

export interface TickSeries { samples: { at: string; value: number }[]; errors: number; median: number | null }

export function median(values: number[]): number | null {
  if (values.length === 0) return null;
  const s = [...values].sort((a, b) => a - b);
  const mid = Math.floor(s.length / 2);
  return s.length % 2 ? s[mid]! : (s[mid - 1]! + s[mid]!) / 2;
}

export async function sampleTicks(
  read: () => Promise<number>,
  opts: { durationMs: number; intervalMs: number; sleep: (ms: number) => Promise<void>; now: () => number },
): Promise<TickSeries> {
  const samples: { at: string; value: number }[] = [];
  let errors = 0;
  const end = opts.now() + opts.durationMs;
  while (opts.now() < end) {
    const at = new Date(opts.now()).toISOString();
    try {
      samples.push({ at, value: await read() });
    } catch {
      errors++;
    }
    await opts.sleep(opts.intervalMs);
  }
  return { samples, errors, median: median(samples.map((s) => s.value)) };
}
