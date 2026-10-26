// The tick-cost run (spec "Testing" 2, C4 and S12): the server boots once without the mod and once with it, each with
// the staged world loaded and auto-pause off, and is measured for 10 minutes after TICK_WARMUP_MS. No FRM poller runs
// in either window (it would load the server being measured). The mod's reader probes circuit ids 0-9999 on every power
// read (GameValues.h:27), so the with-mod median covers that scan; the pass reason says how many circuit groups the
// save has, so a reader can judge it.
// - C4 (S2's rule, spikes/S2/pass.ts): the with-mod tick median is at least 95% of the baseline's; both windows have samples.
// - S12: the with-mod boot's process tree connects out only where the baseline boot's did, plus the recorder.
//   Outbound connections and the DNS client cache are sampled every 30 s through both boots (harness/src/outbound.ts).
//   A failed sample, or fewer than 2 per window, fails the run. A positive control (review I1 of 1ca0065): the recorder's
//   endpoint must appear in outboundWithMod, since the mod's command long poll holds a connection to it open (25 s +
//   10 s, Requests.h:23), so a sampler that sees nothing can't pass S12.
//   Owner decision (2026-10-21, after run 39538061390, which failed only on the game's own public-IP lookup:
//   api.ipify.org at 104.26.12.205 in the baseline and 172.67.74.152 with the mod, rotating Cloudflare addresses; the
//   mod has no such code): endpoints are matched by DNS name. This replaces the controller's earlier "raw ip:port"
//   ruling (review I2 of 1ca0065). An endpoint seen only in the with-mod boot passes if, and only if:
//   1. it is the recorder's 127.0.0.1:<port>; or
//   2. the with-mod boot's DNS cache names its address, and at least one of those names belongs to an endpoint the
//      baseline boot contacted on the same port, named by the baseline boot's own DNS cache (outboundNamesBaseline holds
//      names only for addresses the baseline contacted).
//   A new endpoint with no name, or whose names the baseline didn't contact, fails with its IP and names. Any failed
//   DNS read in either boot fails S12 (a missing name could hide a mismatch, and baseline names could be incomplete).
//   The pass reason lists each endpoint matched by name with the name and both IPs.
//   Owner decision (2026-10-26, after tick-cost-native runs 40247280125 and 40256047941, which failed S12 only on
//   127.0.0.1:18081, FRM's HTTP server, read by the mod's FRM bridge since Plan 3A): FRM's endpoint, taken from the
//   runner config's frmBaseUrl and recorded as `frm`, is allowed like the recorder's, only as 127.0.0.1:<port> and only
//   that exact port. It isn't required to appear. A capture without `frm` allows nothing extra.
// - Same save (controller ruling, review of 1ca0065): both boots load the same staged world, and the session hash each
//   boot runs is recorded; differing or missing hashes fail the run, since the comparison is valid only on the same save.
// - Memory (owner decision, 2026-10-20): the server process tree's working set (the reader memory.json uses, by pid),
//   sampled every 30 s through each 10-minute window.
//   - Size: at the end of the window, the with-mod working set is at most MEMORY_SIZE_LIMIT_MB above the baseline's at
//     the same point (same save, world loaded, auto-pause off).
//   - Leak: over the last MEMORY_TREND_MS of each window, the with-mod working set grows at most MEMORY_GROWTH_LIMIT_MB
//     more than the baseline's growth, with the baseline's growth floored at 0 (growth = the window's last sample minus
//     its first sample at or after last - MEMORY_TREND_MS). Owner decision (2026-10-26, after the same two runs: both
//     boots took the same one-time +36-40 MB step, the with-mod set then stayed flat, and the baseline freed ~62 MB
//     inside its window, so the whole-window rule counted the baseline's release as ~60 MB of mod growth): a leak is
//     sustained growth, judged on the trend, and a baseline release can't count against the mod. This replaces the
//     earlier whole-window rule, under which a shrinking baseline counted against the mod.
//   - Fail closed: a sample that could not be read, or fewer than 2 per window, fails with a reason. MB is 2^20 bytes.
import { S2_TICK_TOLERANCE } from "../../spikes/S2/pass";
import type { CoreEvidence } from "../../harness/src/core-run";
import type { TickSeries } from "../../harness/src/ticks";
import type { Verdict } from "../../harness/src/verdict";

export const requirements = ["tick-cost", "C4", "S12", "memory"];
export const MEMORY_SIZE_LIMIT_MB = 100;
export const MEMORY_GROWTH_LIMIT_MB = 25;
export const MEMORY_TREND_MS = 5 * 60_000;
const MB = 1024 * 1024;
const MIN_SAMPLES = 2;

/** The tree's working set (the judged figure) and, as evidence, each process's (review M2); null fields: unreadable. */
export interface MemoryPoint {
  at: string;
  workingSetBytes: number | null;
  processes?: Array<{ name: string; workingSetBytes: number; peakWorkingSetBytes: number }> | null;
}
/** Outbound samples read, failed, and (of those read) whose DNS cache read failed. */
export interface OutboundCount { ok: number; failed: number; dnsFailed: number }

export interface TickCost {
  baseline: TickSeries | null;
  withMod: TickSeries | null;
  /** The union of every outbound sample's endpoints in each boot. */
  outboundBaseline: string[];
  outboundWithMod: string[];
  outboundSamples: { baseline: OutboundCount; withMod: OutboundCount };
  /** The process tree's working set, every 30 s from the end of the warmup to the end of the window. */
  memoryBaseline: MemoryPoint[];
  memoryWithMod: MemoryPoint[];
  /** The recorder's host:port, an endpoint the mod may add. */
  recorder: string;
  /** FRM's host:port from the runner config (owner, 2026-10-26: the other endpoint the mod may add); null when unset. */
  frm?: string | null;
  /** Circuit groups in the mod's last accepted snapshot; null when unknown (evidence only). */
  circuitGroups: number | null;
  /** The session hash QueryServerState gave at the start of each boot's measurement; null when unread. */
  sessionBaseline: string | null;
  sessionWithMod: string | null;
  /** Each boot's DNS cache names per address it contacted (owner, 2026-10-21: judged for S12). */
  outboundNamesBaseline: Record<string, string[]>;
  outboundNamesWithMod: Record<string, string[]>;
}

/** "a.b.c.d:443" or "[v6]:443" into its address and port. */
function splitEndpoint(x: string): { address: string; port: string } {
  const i = x.lastIndexOf(":");
  return { address: x.slice(0, i).replace(/^\[(.*)\]$/, "$1"), port: x.slice(i + 1) };
}

const fmt = (bytes: number) => (bytes / MB).toFixed(1);

/** The window's last reading and its growth over the trend span, or null after pushing its reasons. */
function memoryOf(points: MemoryPoint[] | undefined, name: string, reasons: string[]): { last: number; trend: number } | null {
  const all = points ?? [];
  const read = all.map((p) => p.workingSetBytes).filter((b): b is number => typeof b === "number" && Number.isFinite(b));
  const missing = all.length - read.length;
  if (missing) reasons.push(`memory: ${missing} sample(s) of the ${name} window could not be read`);
  if (all.length < MIN_SAMPLES) reasons.push(`memory: the ${name} window has ${all.length} sample(s), fewer than ${MIN_SAMPLES}`);
  if (missing || all.length < MIN_SAMPLES) return null;
  const end = Date.parse(all[all.length - 1]!.at);
  const span = all.filter((p) => Date.parse(p.at) >= end - MEMORY_TREND_MS);
  if (!Number.isFinite(end) || span.length < MIN_SAMPLES) {
    reasons.push(`memory: the ${name} window has ${span.length} sample(s) in its last ${MEMORY_TREND_MS / 60_000} minutes, fewer than ${MIN_SAMPLES}`);
    return null;
  }
  return { last: read[read.length - 1]!, trend: read[read.length - 1]! - span[0]!.workingSetBytes! };
}

export function evaluate(e: CoreEvidence): Verdict {
  const c = e.captures["tick-cost.json"] as TickCost | undefined;
  if (!c) return { pass: false, reasons: ["no tick-cost capture"] };
  const reasons: string[] = [];

  // C4
  const base = c.baseline?.samples.length ? c.baseline.median : null;
  const withMod = c.withMod?.samples.length ? c.withMod.median : null;
  if (base === null) reasons.push("the baseline tick window has no samples");
  if (withMod === null) reasons.push("the with-mod tick window has no samples");
  if (base !== null && withMod !== null && withMod < base * (1 - S2_TICK_TOLERANCE)) {
    reasons.push(`tick median dropped from ${base} without the mod to ${withMod} with it, more than ${S2_TICK_TOLERANCE * 100}%`);
  }

  // S12
  const recorderOk = /^127\.0\.0\.1:\d{1,5}$/.test(c.recorder);
  if (!recorderOk) reasons.push(`S12: the recorder's endpoint is not 127.0.0.1:<port> (${c.recorder})`);
  for (const [name, n] of [["baseline", c.outboundSamples?.baseline], ["with-mod", c.outboundSamples?.withMod]] as const) {
    const ok = n?.ok ?? 0;
    if (n?.failed) reasons.push(`S12: ${n.failed} outbound sample(s) of the ${name} window failed`);
    if (ok < MIN_SAMPLES) reasons.push(`S12: the ${name} window has ${ok} outbound sample(s), fewer than ${MIN_SAMPLES}`);
    if (typeof n?.dnsFailed !== "number") reasons.push(`S12: the ${name} window's DNS reads were not counted`);
    else if (n.dnsFailed) reasons.push(`S12: ${n.dnsFailed} DNS read(s) of the ${name} window failed`);
  }
  if (!c.outboundWithMod.includes(c.recorder)) reasons.push(`S12: the sampler never saw the mod's connection to the recorder (${c.recorder})`);
  const frmOk = typeof c.frm === "string" && /^127\.0\.0\.1:\d{1,5}$/.test(c.frm);
  if (c.frm != null && !frmOk) reasons.push(`S12: FRM's endpoint is not 127.0.0.1:<port> (${c.frm})`);
  const known = new Set(c.outboundBaseline);
  const added = c.outboundWithMod.filter((x) => !known.has(x));
  // name:port -> the baseline endpoints of that name, from the baseline boot's own DNS cache only.
  const baselineByName = new Map<string, string[]>();
  for (const x of c.outboundBaseline) {
    const { address, port } = splitEndpoint(x);
    for (const name of c.outboundNamesBaseline?.[address] ?? []) {
      const key = `${name}:${port}`;
      baselineByName.set(key, [...(baselineByName.get(key) ?? []), x]);
    }
  }
  const byName: string[] = [];
  for (const x of added) {
    if (recorderOk && x === c.recorder) continue;
    if (frmOk && x === c.frm) continue;
    const { address, port } = splitEndpoint(x);
    const names = c.outboundNamesWithMod?.[address] ?? [];
    const which = `S12: the mod's boot contacted ${x}${names.length ? ` (${names.join(", ")})` : ""}, which the baseline didn't, isn't the recorder (${c.recorder})`;
    if (!names.length) {
      reasons.push(`${which}, and has no DNS name`);
      continue;
    }
    const shared = names.filter((n) => baselineByName.has(`${n}:${port}`));
    if (!shared.length) {
      reasons.push(`${which}, and none of its names was contacted in the baseline`);
      continue;
    }
    for (const n of shared) byName.push(`${x} as ${n} (baseline ${baselineByName.get(`${n}:${port}`)!.join(", ")})`);
  }

  // Same save
  if (c.sessionBaseline == null) reasons.push("the session of the baseline boot was not recorded");
  if (c.sessionWithMod == null) reasons.push("the session of the with-mod boot was not recorded");
  if (c.sessionBaseline != null && c.sessionWithMod != null && c.sessionBaseline !== c.sessionWithMod) {
    reasons.push(`the two boots ran different saves (session ${c.sessionBaseline} without the mod, ${c.sessionWithMod} with it): the comparison is valid only on the same save`);
  }

  // Memory
  const mb = memoryOf(c.memoryBaseline, "baseline", reasons);
  const mw = memoryOf(c.memoryWithMod, "with-mod", reasons);
  let memoryText = "";
  if (mb && mw) {
    const above = mw.last - mb.last;
    const growB = mb.trend;
    const growW = mw.trend;
    const extra = growW - Math.max(growB, 0);
    const span = `the last ${MEMORY_TREND_MS / 60_000} minutes`;
    if (above > MEMORY_SIZE_LIMIT_MB * MB) reasons.push(`memory: the with-mod working set ended ${fmt(above)} MB above the baseline's, more than ${MEMORY_SIZE_LIMIT_MB} MB`);
    if (extra > MEMORY_GROWTH_LIMIT_MB * MB) {
      reasons.push(`memory: over ${span} the with-mod working set grew ${fmt(growW)} MB vs the baseline's ${fmt(growB)} MB (counted from 0 when negative), ${fmt(extra)} MB more, more than ${MEMORY_GROWTH_LIMIT_MB} MB`);
    }
    memoryText = `working set at the end ${fmt(above)} MB above the baseline's (limit ${MEMORY_SIZE_LIMIT_MB}); over ${span} it grew ${fmt(growW)} MB vs ${fmt(growB)} MB (counted from 0 when negative), ${fmt(extra)} MB more (limit ${MEMORY_GROWTH_LIMIT_MB})`;
  }

  if (reasons.length) return { pass: false, reasons };
  const groups = c.circuitGroups === null ? "circuit groups unknown" : `${c.circuitGroups} circuit group(s) in the save (the reader scans ids 0-9999 per power read)`;
  return {
    pass: true,
    reasons: [
      `tick median ${base} without the mod, ${withMod} with it (${c.baseline!.samples.length} and ${c.withMod!.samples.length} samples); ` +
        `S12: ${added.length} new endpoint(s), ` +
        (byName.length ? `the recorder's and, matched by DNS name: ${byName.join("; ")}` : "all the recorder's") +
        (frmOk && added.includes(c.frm!) ? `, and FRM's ${c.frm} (owner, 2026-10-26)` : "") +
        ` (${c.outboundSamples.baseline.ok} and ${c.outboundSamples.withMod.ok} outbound samples); ` +
        `${memoryText} (${c.memoryBaseline.length} and ${c.memoryWithMod.length} memory samples); ` +
        `same save in both boots (session ${c.sessionWithMod}); ${groups}`,
    ],
  };
}
