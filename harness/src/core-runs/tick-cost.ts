import * as Pass from "../../../runs/tick-cost/pass";
import type { CoreContext, CoreDeps, CoreRunSpec } from "../core-run";
import type { WorldKey } from "../run";
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
  /** This boot's DNS cache names per address it contacted (S12 matches by them; owner, 2026-10-21). */
  outboundNames: Record<string, string[]>;
  outboundSamples: Pass.OutboundCount;
  /** From the end of the warmup to the end of the window. */
  memory: Pass.MemoryPoint[];
  /** The session hash QueryServerState gave when the measurement started; null when unread. */
  session: string | null;
}

/**
 * Ticks for TICK_WARMUP_MS + TICK_COST_WINDOW_MS, and every TICK_COST_SAMPLE_MS (on a fixed schedule from the start)
 * the outbound endpoints and the working set of the process tree under pid: the server's launcher, never the harness.
 * One more sample is taken when the window ends, so the last memory point is the end of the window. No FRM poller.
 * When `exited` settles (the server died), the samplers stop and the measurement rejects, so nothing keeps measuring
 * into the next boot (review M1).
 */
export async function measureWindow(deps: CoreDeps, pid: number, exited?: Promise<unknown>): Promise<Measured> {
  if (!deps.sampleTickWindow) throw new Error("no tick sampler (sampleTickWindow)");
  let gone = false;
  void exited?.then(() => void (gone = true), () => void (gone = true));
  const now = () => deps.now().getTime();
  const startedMs = now();
  const settledMs = startedMs + TICK_WARMUP_MS;
  const state = deps.gameState ? await deps.gameState().catch(() => null) : null;
  const endpoints = new Set<string>();
  const names = new Map<string, Set<string>>();
  const outboundSamples: Pass.OutboundCount = { ok: 0, failed: 0, dnsFailed: 0 };
  const memory: Pass.MemoryPoint[] = [];
  const sampleOnce = async () => {
    const at = now();
    const inWindow = at >= settledMs;
    const [out, mem] = await Promise.all([
      deps.sampleOutbound ? deps.sampleOutbound(pid).catch(() => null) : Promise.resolve(null),
      inWindow && deps.processMemory ? deps.processMemory(pid).catch(() => null) : Promise.resolve(null),
    ]);
    if (out) {
      outboundSamples.ok++;
      if (out.dnsFailed) outboundSamples.dnsFailed++;
      for (const x of out.endpoints) endpoints.add(x);
      for (const [address, ns] of Object.entries(out.names)) {
        if (!names.has(address)) names.set(address, new Set());
        for (const n of ns) names.get(address)!.add(n);
      }
    } else {
      outboundSamples.failed++;
    }
    // A missing reader or a failed read is a null point: the judge fails closed on it.
    if (inWindow) memory.push({ at: new Date(at).toISOString(), workingSetBytes: mem?.totalWorkingSetBytes ?? null, processes: mem?.processes ?? null });
  };
  let done = false;
  const loop = (async () => {
    for (let k = 1; !done && !gone; k++) {
      await sampleOnce();
      const next = startedMs + k * TICK_COST_SAMPLE_MS;
      while (!done && !gone && now() < next) await deps.sleep(Math.min(1000, next - now()));
    }
  })();
  let ticks: TickSeries;
  try {
    ticks = await deps.sampleTickWindow({ warmupMs: TICK_WARMUP_MS, durationMs: TICK_COST_WINDOW_MS, stopped: () => gone });
  } finally {
    done = true;
    await loop;
  }
  if (gone) throw new Error("the server exited during the measurement");
  await sampleOnce();
  const outboundNames: Record<string, string[]> = {};
  for (const a of [...names.keys()].sort()) outboundNames[a] = [...names.get(a)!].sort();
  // No game running is no save: null, so the judge's same-save guard can't pass on two empty sessions.
  return { ticks, outbound: [...endpoints].sort(), outboundNames, outboundSamples, memory, session: state?.running ? state.session : null };
}

/**
 * Owner, 2026-10-27 (S12): forward-resolves, once the with-mod window is over, every name the baseline boot's DNS cache gave
 * to an address the baseline contacted. name -> its sorted addresses; [] for a lookup that failed (the judge reads that as no match).
 */
export async function forwardResolve(resolve: (name: string) => Promise<string[]>, base: Measured | undefined): Promise<Record<string, string[]>> {
  const contacted = new Set((base?.outbound ?? []).map((x) => x.slice(0, x.lastIndexOf(":")).replace(/^\[(.*)\]$/, "$1")));
  const wanted = new Set<string>();
  for (const [address, ns] of Object.entries(base?.outboundNames ?? {})) {
    if (contacted.has(address)) for (const n of ns) wanted.add(n);
  }
  const out: Record<string, string[]> = {};
  for (const name of [...wanted].sort()) {
    out[name] = await resolve(name).then((a) => [...new Set(a)].sort(), () => []);
  }
  return out;
}

/**
 * Owner ruling 2026-10-28 (S12, after run 40706193392): forward-resolves, once, every name the with-mod boot's own DNS cache
 * gave to an address it contacted that the baseline did not. name -> sorted addresses; [] for a failed lookup (no match).
 */
export async function forwardResolveWithModNames(
  resolve: (name: string) => Promise<string[]>,
  base: Measured | undefined,
  withMod: Measured,
): Promise<Record<string, string[]>> {
  const addressOf = (x: string) => x.slice(0, x.lastIndexOf(":")).replace(/^\[(.*)\]$/, "$1");
  const known = new Set((base?.outbound ?? []).map(addressOf));
  const wanted = new Set<string>();
  for (const x of withMod.outbound) {
    const address = addressOf(x);
    if (!known.has(address)) for (const n of withMod.outboundNames[address] ?? []) wanted.add(n);
  }
  const out: Record<string, string[]> = {};
  for (const name of [...wanted].sort()) {
    out[name] = await resolve(name).then((a) => [...new Set(a)].sort(), () => []);
  }
  return out;
}

/** Circuit groups in the mod's last accepted snapshot (evidence on the reader's circuit scan). */
function circuitGroups(snapshot: unknown): number | null {
  const circuits = (snapshot as { power?: { circuits?: unknown } } | undefined)?.power?.circuits;
  return Array.isArray(circuits) ? circuits.length : null;
}

/**
 * The with-mod boot's order. "enrol-first" is tick-cost's own (enrol, load the world, auto-pause off, measure).
 * "world-first" (tick-cost-native, as parity does) loads the world and turns auto-pause off, then enrols, then measures
 * straight away: the mod's first resource-node post comes 8 minutes (+/-10%) after the agent starts Running, which lands
 * inside the measured span [warmup, warmup + 10 minutes] = [3, 13] minutes after the measurement starts.
 */
/** Which parts the mod's accepted snapshots carried (no body, no names): the evidence tick-cost-native's judge reads. */
export function snapshotParts(
  measured: { from: string; to: string } | null,
  entries: ReadonlyArray<{ at: string; snapshot?: unknown }>,
): { measured: { from: string; to: string } | null; snapshots: Array<{ at: string; playersAvailable: boolean }> } {
  return {
    measured,
    snapshots: entries.map((e) => ({
      at: e.at,
      playersAvailable: (e.snapshot as { players?: { available?: unknown } } | undefined)?.players?.available === true,
    })),
  };
}

export type TickCostOrder = "enrol-first" | "world-first";

/**
 * C4 on the finished mod, S12 and the owner's memory limits. runCore boots once without the mod and measures it
 * (baseline); then the mod's boot enrols, loads the same world, turns auto-pause off and is measured the same way.
 */
export const tickCost: CoreRunSpec = {
  requirements: Pass.requirements,
  backend: true,
  world: true,
  baseline: true,
  measureBaseline: ({ deps, proc }) => measureWindow(deps, proc.pid, proc.exited),
  evaluate: Pass.evaluate,
  drive: (ctx) => driveTickCost(ctx, "enrol-first"),
};

/**
 * Names the step a with-mod failure came from. A socket error such as "read ECONNRESET" carries no harness frame in its
 * stack (run 40562669608 failed with only that message), so the step is the only clue.
 */
export async function step<T>(what: string, p: Promise<T>): Promise<T> {
  try {
    return await p;
  } catch (e) {
    throw new Error(`${what}: ${e instanceof Error ? e.message : String(e)}`, { cause: e });
  }
}

export async function driveTickCost(ctx: CoreContext, order: TickCostOrder, key?: WorldKey): Promise<void> {
  {
    const base = ctx.baseline as Measured | undefined;
    const host = await ctx.deps.backendHost!();
    const recorder = await startRecorder(host.relayUrl);
    const capture: Pass.TickCost = {
      baseline: base?.ticks ?? null,
      withMod: null,
      outboundBaseline: base?.outbound ?? [],
      outboundWithMod: [],
      outboundSamples: { baseline: base?.outboundSamples ?? { ok: 0, failed: 0, dnsFailed: 0 }, withMod: { ok: 0, failed: 0, dnsFailed: 0 } },
      memoryBaseline: base?.memory ?? [],
      memoryWithMod: [],
      recorder: new URL(recorder.url).host,
      // The mod's FRM bridge reads FRM here (owner, 2026-10-26: S12 allows it).
      frm: ctx.deps.config.frmBaseUrl ? new URL(ctx.deps.config.frmBaseUrl).host : null,
      circuitGroups: null,
      sessionBaseline: base?.session ?? null,
      sessionWithMod: null,
      // Kept per boot: S12 matches a with-mod address by name only against names the baseline boot itself resolved.
      outboundNamesBaseline: base?.outboundNames ?? {},
      outboundNamesWithMod: {},
    };
    let measured: { from: string; to: string } | null = null;
    try {
      if (order === "enrol-first") await step("enrolling the mod", enrolPhase(ctx, host, recorder));
      await step("loading the world", ctx.deps.loadWorld!(key));
      await step("waiting for the server to answer", ctx.deps.waitHealthy());
      ctx.rememberAutoPause(await step("reading auto-pause", ctx.deps.readAutoPause!()));
      await step("turning auto-pause off", ctx.deps.setAutoPause!(false));
      if (order === "world-first") await step("enrolling the mod", enrolPhase(ctx, host, recorder));
      // The same staged world as the baseline boot (loadWorld uploads the keyed world each time); the session hash checks it.
      const measureFrom = ctx.deps.now().toISOString();
      const w = await step("measuring the with-mod window", measureWindow(ctx.deps, ctx.proc.pid, ctx.proc.exited));
      measured = { from: measureFrom, to: ctx.deps.now().toISOString() };
      capture.withMod = w.ticks;
      capture.outboundWithMod = w.outbound;
      capture.outboundSamples.withMod = w.outboundSamples;
      capture.memoryWithMod = w.memory;
      capture.sessionWithMod = w.session;
      capture.outboundNamesWithMod = w.outboundNames;
      if (ctx.deps.resolveHost) {
        capture.outboundForwardWithMod = await forwardResolve(ctx.deps.resolveHost, base);
        capture.outboundForwardWithModNames = await forwardResolveWithModNames(ctx.deps.resolveHost, base, w);
      }
    } finally {
      const last = accepted(recorder).at(-1);
      capture.circuitGroups = last ? circuitGroups(last.snapshot) : null;
      ctx.captures["tick-cost.json"] = capture;
      // Evidence on which parts the mod's snapshots carried (no body, no names): tick-cost-native's judge reads it.
      ctx.captures["snapshot-parts.json"] = snapshotParts(measured, accepted(recorder));
      ctx.captures["recorder.json"] = recorder.entries.map(({ snapshot: _, ...rest }) => rest);
      await recorder.close();
    }
  }
}
