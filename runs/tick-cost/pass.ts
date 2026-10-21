// The tick-cost run (spec "Testing" 2, C4 and S12): the server boots once without the mod and once with it, each with
// the staged world loaded and auto-pause off, and is measured for 10 minutes after TICK_WARMUP_MS. No FRM poller runs
// in either window (it would load the server being measured). The mod's reader probes circuit ids 0-9999 on every power
// read (GameValues.h:27), so the with-mod median covers that scan; the pass reason says how many circuit groups the
// save has, so a reader can judge it.
// - C4 (S2's rule, spikes/S2/pass.ts): the with-mod tick median is at least 95% of the baseline's; both windows have samples.
// - S12: every endpoint the with-mod boot's process tree connected out to that the baseline boot's didn't is the
//   recorder's 127.0.0.1:<port>. Outbound connections are sampled every 30 s through both boots (harness/src/outbound.ts);
//   a failed sample, or fewer than 2 per window, fails the run. A positive control (review I1 of 1ca0065): the recorder's
//   endpoint must appear in outboundWithMod, since the mod's command long poll holds a connection to it open (25 s +
//   10 s, Requests.h:23), so a sampler that sees nothing can't pass S12.
//   Controller ruling (review I2 of 1ca0065), an accepted risk: endpoints are compared as raw ip:port against one
//   baseline boot, as the plan says. If the game itself reaches rotating online-service or CDN addresses, a with-mod boot
//   can fail on an address the mod never opened. The DNS cache names per address (outboundNames) are recorded so such a
//   failure can be triaged; they never change the verdict. A failure on such an address goes to the owner, never "fixed"
//   by loosening this rule.
// - Same save (controller ruling, review of 1ca0065): both boots load the same staged world, and the session hash each
//   boot runs is recorded; differing or missing hashes fail the run, since the comparison is valid only on the same save.
// - Memory (owner decision, 2026-10-20): the server process tree's working set (the reader memory.json uses, by pid),
//   sampled every 30 s through each 10-minute window.
//   - Size: at the end of the window, the with-mod working set is at most MEMORY_SIZE_LIMIT_MB above the baseline's at
//     the same point (same save, world loaded, auto-pause off).
//   - Leak: over the window, the with-mod working set grows at most MEMORY_GROWTH_LIMIT_MB more than the baseline's
//     growth (growth = last sample minus first sample after warmup).
//   - Fail closed: a sample that could not be read, or fewer than 2 per window, fails with a reason. MB is 2^20 bytes.
import { S2_TICK_TOLERANCE } from "../../spikes/S2/pass";
import type { CoreEvidence } from "../../harness/src/core-run";
import type { TickSeries } from "../../harness/src/ticks";
import type { Verdict } from "../../harness/src/verdict";

export const requirements = ["tick-cost", "C4", "S12", "memory"];
export const MEMORY_SIZE_LIMIT_MB = 100;
export const MEMORY_GROWTH_LIMIT_MB = 25;
const MB = 1024 * 1024;
const MIN_SAMPLES = 2;

/** The tree's working set (the judged figure) and, as evidence, each process's (review M2); null fields: unreadable. */
export interface MemoryPoint {
  at: string;
  workingSetBytes: number | null;
  processes?: Array<{ name: string; workingSetBytes: number; peakWorkingSetBytes: number }> | null;
}
export interface OutboundCount { ok: number; failed: number }

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
  /** The recorder's host:port, the one endpoint the mod may add. */
  recorder: string;
  /** Circuit groups in the mod's last accepted snapshot; null when unknown (evidence only). */
  circuitGroups: number | null;
  /** The session hash QueryServerState gave at the start of each boot's measurement; null when unread. */
  sessionBaseline: string | null;
  sessionWithMod: string | null;
  /** DNS cache names per endpoint address, both boots merged (evidence only; never judged). */
  outboundNames: Record<string, string[]>;
}

const fmt = (bytes: number) => (bytes / MB).toFixed(1);

/** The window's first and last readings, or null after pushing its reasons. */
function memoryOf(points: MemoryPoint[] | undefined, name: string, reasons: string[]): { first: number; last: number } | null {
  const all = points ?? [];
  const read = all.map((p) => p.workingSetBytes).filter((b): b is number => typeof b === "number" && Number.isFinite(b));
  const missing = all.length - read.length;
  if (missing) reasons.push(`memory: ${missing} sample(s) of the ${name} window could not be read`);
  if (all.length < MIN_SAMPLES) reasons.push(`memory: the ${name} window has ${all.length} sample(s), fewer than ${MIN_SAMPLES}`);
  if (missing || all.length < MIN_SAMPLES) return null;
  return { first: read[0]!, last: read[read.length - 1]! };
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
  }
  if (!c.outboundWithMod.includes(c.recorder)) reasons.push(`S12: the sampler never saw the mod's connection to the recorder (${c.recorder})`);
  const known = new Set(c.outboundBaseline);
  const added = c.outboundWithMod.filter((x) => !known.has(x));
  for (const x of added) {
    if (!recorderOk || x !== c.recorder) reasons.push(`S12: the mod's boot contacted ${x}, which the baseline didn't and isn't the recorder (${c.recorder})`);
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
    const growB = mb.last - mb.first;
    const growW = mw.last - mw.first;
    const extra = growW - growB;
    if (above > MEMORY_SIZE_LIMIT_MB * MB) reasons.push(`memory: the with-mod working set ended ${fmt(above)} MB above the baseline's, more than ${MEMORY_SIZE_LIMIT_MB} MB`);
    if (extra > MEMORY_GROWTH_LIMIT_MB * MB) {
      reasons.push(`memory: the with-mod working set grew ${fmt(growW)} MB vs the baseline's ${fmt(growB)} MB, ${fmt(extra)} MB more, more than ${MEMORY_GROWTH_LIMIT_MB} MB`);
    }
    memoryText = `working set at the end ${fmt(above)} MB above the baseline's (limit ${MEMORY_SIZE_LIMIT_MB}); over the window it grew ${fmt(growW)} MB vs ${fmt(growB)} MB, ${fmt(extra)} MB more (limit ${MEMORY_GROWTH_LIMIT_MB})`;
  }

  if (reasons.length) return { pass: false, reasons };
  const groups = c.circuitGroups === null ? "circuit groups unknown" : `${c.circuitGroups} circuit group(s) in the save (the reader scans ids 0-9999 per power read)`;
  return {
    pass: true,
    reasons: [
      `tick median ${base} without the mod, ${withMod} with it (${c.baseline!.samples.length} and ${c.withMod!.samples.length} samples); ` +
        `S12: ${added.length} new endpoint(s), all the recorder's (${c.outboundSamples.baseline.ok} and ${c.outboundSamples.withMod.ok} outbound samples); ` +
        `${memoryText} (${c.memoryBaseline.length} and ${c.memoryWithMod.length} memory samples); ` +
        `same save in both boots (session ${c.sessionWithMod}); ${groups}`,
    ],
  };
}
