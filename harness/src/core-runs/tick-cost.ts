import * as Pass from "../../../runs/tick-cost/pass";
import type { CoreDeps, CoreRunSpec } from "../core-run";
import { startRecorder } from "../recorder";
import { TICK_WARMUP_MS, type TickSeries } from "../ticks";
import { accepted, enrolPhase } from "./enrol";

const MIN = 60_000;
/** The spec's window: 10 minutes with a world loaded, after TICK_WARMUP_MS. */
export const TICK_COST_WINDOW_MS = 10 * MIN;
/** Outbound connections and memory are sampled this often (owner, 2026-10-20: at least every 30 s). */
export const TICK_COST_SAMPLE_MS = 30_000;

/** One boot's measurement: the tick window, and the outbound and memory samples taken alongside it. */
export interface Measured {
  ticks: TickSeries | null;
  outbound: string[];
  outboundSamples: Pass.OutboundCount;
  /** From the end of the warmup to the end of the window. */
  memory: Pass.MemoryPoint[];
}

/**
 * Ticks for TICK_WARMUP_MS + TICK_COST_WINDOW_MS, and every TICK_COST_SAMPLE_MS (on a fixed schedule from the start)
 * the outbound endpoints and the working set of the process tree under pid: the server's launcher, never the harness.
 * One more sample is taken when the window ends, so the last memory point is the end of the window. No FRM poller.
 */
export async function measureWindow(deps: CoreDeps, pid: number): Promise<Measured> {
  if (!deps.sampleTickWindow) throw new Error("no tick sampler (sampleTickWindow)");
  const startedMs = Date.now();
  const settledMs = startedMs + TICK_WARMUP_MS;
  const endpoints = new Set<string>();
  const outboundSamples = { ok: 0, failed: 0 };
  const memory: Pass.MemoryPoint[] = [];
  const sampleOnce = async () => {
    const now = Date.now();
    const inWindow = now >= settledMs;
    const [out, mem] = await Promise.all([
      deps.sampleOutbound ? deps.sampleOutbound(pid).catch(() => null) : Promise.resolve(null),
      inWindow && deps.processMemory ? deps.processMemory(pid).catch(() => null) : Promise.resolve(null),
    ]);
    if (out) {
      outboundSamples.ok++;
      for (const x of out) endpoints.add(x);
    } else {
      outboundSamples.failed++;
    }
    // A missing reader or a failed read is a null point: the judge fails closed on it.
    if (inWindow) memory.push({ at: new Date(now).toISOString(), workingSetBytes: mem?.totalWorkingSetBytes ?? null });
  };
  let done = false;
  const loop = (async () => {
    for (let k = 1; !done; k++) {
      await sampleOnce();
      const next = startedMs + k * TICK_COST_SAMPLE_MS;
      while (!done && Date.now() < next) await deps.sleep(Math.min(1000, next - Date.now()));
    }
  })();
  let ticks: TickSeries;
  try {
    ticks = await deps.sampleTickWindow({ warmupMs: TICK_WARMUP_MS, durationMs: TICK_COST_WINDOW_MS });
  } finally {
    done = true;
    await loop;
  }
  await sampleOnce();
  return { ticks, outbound: [...endpoints].sort(), outboundSamples, memory };
}

/** Circuit groups in the mod's last accepted snapshot (evidence on the reader's circuit scan). */
function circuitGroups(snapshot: unknown): number | null {
  const circuits = (snapshot as { power?: { circuits?: unknown } } | undefined)?.power?.circuits;
  return Array.isArray(circuits) ? circuits.length : null;
}

/**
 * C4 on the finished mod, S12 and the owner's memory limits. runCore boots once without the mod and measures it
 * (baseline); then the mod's boot enrols, loads the same world, turns auto-pause off and is measured the same way.
 */
export const tickCost: CoreRunSpec = {
  requirements: Pass.requirements,
  backend: true,
  world: true,
  baseline: true,
  measureBaseline: ({ deps, proc }) => measureWindow(deps, proc.pid),
  evaluate: Pass.evaluate,
  async drive(ctx) {
    const base = ctx.baseline as Measured | undefined;
    const host = await ctx.deps.backendHost!();
    const recorder = await startRecorder(host.relayUrl);
    const capture: Pass.TickCost = {
      baseline: base?.ticks ?? null,
      withMod: null,
      outboundBaseline: base?.outbound ?? [],
      outboundWithMod: [],
      outboundSamples: { baseline: base?.outboundSamples ?? { ok: 0, failed: 0 }, withMod: { ok: 0, failed: 0 } },
      memoryBaseline: base?.memory ?? [],
      memoryWithMod: [],
      recorder: new URL(recorder.url).host,
      circuitGroups: null,
    };
    try {
      await enrolPhase(ctx, host, recorder);
      await ctx.deps.loadWorld!();
      await ctx.deps.waitHealthy();
      ctx.rememberAutoPause(await ctx.deps.readAutoPause!());
      await ctx.deps.setAutoPause!(false);
      const w = await measureWindow(ctx.deps, ctx.proc.pid);
      capture.withMod = w.ticks;
      capture.outboundWithMod = w.outbound;
      capture.outboundSamples.withMod = w.outboundSamples;
      capture.memoryWithMod = w.memory;
    } finally {
      const last = accepted(recorder).at(-1);
      capture.circuitGroups = last ? circuitGroups(last.snapshot) : null;
      ctx.captures["tick-cost.json"] = capture;
      ctx.captures["recorder.json"] = recorder.entries.map(({ snapshot: _, ...rest }) => rest);
      await recorder.close();
    }
  },
};
