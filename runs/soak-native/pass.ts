// The 24-hour all-readers soak (ADR-0058 line 153: a precondition of the R7 removal release; owner 2026-11-03). It is judged by every
// condition of the tick-cost-native run, imported and unchanged (runs/tick-cost-native/pass.ts, which imports runs/tick-cost/pass.ts:
// C4, S12, same save, the memory size and leak rules, native readers live, the 2 ms step ceiling with the waited-step rule, the
// Readers line), applied to the 24-hour window. The leak rule is the existing one, on the last 5 minutes of the day.
//
// PROPOSED soak conditions: the four below are NEW, written by the builder and not yet approved. Each failure and each report line
// says "(proposed, owner to confirm)": the run is not judged final until the owner has confirmed them. None loosens an existing one.
//   P1. Every reader keeps posting: no required reader and no status snapshot goes silent for more than 3x its own cadence at any
//       point in the window (SILENCE_FACTOR; the cadences are READ_CADENCE_S and STATUS_CADENCE_S below).
//   P2. Memory over the day: the whole-window slope and growth are reported; nothing new fails (the existing last-5-minutes rule does).
//   P3. No Error line from the mod inside the window.
//   P4. The verdict always reports the per-reader counts and longest gaps, the waited steps and the memory figures.
import type { CoreEvidence } from "../../harness/src/core-run";
import { TICK_WARMUP_MS } from "../../harness/src/ticks";
import { MAX_NATIVE_STEP_MS, stepCosts } from "../../harness/src/reader-lines";
import type { Verdict } from "../../harness/src/verdict";
import * as Native from "../tick-cost-native/pass";
import type { MemoryPoint } from "../tick-cost/pass";

export const requirements = [...Native.requirements, "soak-24h"];

/** PROPOSED (owner to confirm): a reader is silent when its gap is over this many of its own cadences. */
export const SILENCE_FACTOR = 3;
/** The status snapshot cadence, FCadence::StatusSeconds (Responses.h:37; the outage run's STATUS_CADENCE_MS is the same 5 s). */
export const STATUS_CADENCE_S = 5;
/**
 * Seconds between one read of a sliced reader and the next, from the mod: the factory-cadence readers run every FactorySeconds
 * (30, Responses.h:39; Buildables and the station walk ride that tick), the world layers at WorldLayerTable's IntervalSeconds
 * (WorldRead.cpp: resourceNodes 1800, the others 600). Every read logs its "<Name> (game thread only):" step line, which is what is
 * counted: a world layer whose data did not change posts nothing (WorldSender.cpp SameData), so posts cannot show a live layer.
 */
export const READ_CADENCE_S: Record<string, number> = {
  "Factory read": 30,
  "Trains read": 30,
  "Buildables read": 30,
  "Resource node scan": 1800,
  "Train routes read": 600,
  "Logistics read": 600,
  "Map markers read": 600,
  "Drone routes read": 600,
};

interface Recorded { at: string; status: number; agent: string; path?: string; world?: { layer: string } }

export interface Silence {
  /** The reader or "status snapshots". */
  name: string;
  cadenceS: number;
  /** Reads (step lines) or accepted snapshots inside the window. */
  count: number;
  /** The longest gap between consecutive ones, or from the last one to the window's end; for status snapshots also from the window's start. */
  longestGapMs: number;
  limitMs: number;
}

const gapsOf = (times: number[], from: number | null, to: number): number => {
  const ts = [...times].sort((a, b) => a - b);
  let longest = 0;
  const edge = (a: number, b: number) => void (longest = Math.max(longest, b - a));
  if (from !== null && ts.length) edge(from, ts[0]!);
  for (let i = 1; i < ts.length; i++) edge(ts[i - 1]!, ts[i]!);
  if (ts.length) edge(ts[ts.length - 1]!, to);
  return longest;
};

/**
 * P1. For each required sliced reader, the gaps between its step lines inside [from, to] (a line with no readable time is not counted),
 * measured from its first line in the window (a slow layer's first read comes 8 minutes after Running, inside the window; the first
 * line is already required by the existing window rule) to the window's end; and the status snapshots the recorder accepted, measured
 * from the window's start as well, since they run from the first second.
 */
export function silences(lines: readonly string[], required: readonly string[], recorded: readonly Recorded[], from: number, to: number): Silence[] {
  const out: Silence[] = [];
  for (const reader of required) {
    const cadenceS = READ_CADENCE_S[reader];
    if (cadenceS === undefined) continue;
    const times = lines
      .filter((l) => l.includes(`${reader} (game thread only): `))
      .map(Native.logTimeMs)
      .filter((t) => t >= from && t <= to);
    out.push({ name: reader, cadenceS, count: times.length, longestGapMs: gapsOf(times, null, to), limitMs: SILENCE_FACTOR * cadenceS * 1000 });
  }
  const snaps = recorded
    .filter((r) => r.agent === "mod" && r.status === 200 && r.path === "/agent/v1/snapshots")
    .map((r) => Date.parse(r.at))
    .filter((t) => t >= from && t <= to);
  out.push({ name: "status snapshots", cadenceS: STATUS_CADENCE_S, count: snaps.length, longestGapMs: snaps.length ? gapsOf(snaps, from, to) : to - from, limitMs: SILENCE_FACTOR * STATUS_CADENCE_S * 1000 });
  return out;
}

const PROPOSED = "soak (proposed, owner to confirm)";
const fmtS = (ms: number) => `${(ms / 1000).toFixed(ms < 60_000 ? 1 : 0)} s`;

/** P1's failures: a reader with no read in the window, or a gap over the limit. */
export function silenceProblems(all: readonly Silence[]): string[] {
  const out: string[] = [];
  for (const s of all) {
    if (s.count === 0) out.push(`${PROPOSED}: ${s.name} has no entry in the window, so it was silent for all of it`);
    else if (s.longestGapMs > s.limitMs) out.push(`${PROPOSED}: ${s.name} went silent for ${fmtS(s.longestGapMs)}, more than ${SILENCE_FACTOR} x its ${s.cadenceS} s cadence (${fmtS(s.limitMs)})`);
  }
  return out;
}

/** P2's figures, report only: the with-mod working set over the whole window (least-squares slope, first to last, peak). */
export function memoryDay(points: readonly MemoryPoint[] | undefined): string {
  const read = (points ?? []).map((p) => ({ t: Date.parse(p.at), b: p.workingSetBytes })).filter((p): p is { t: number; b: number } => Number.isFinite(p.t) && typeof p.b === "number" && Number.isFinite(p.b));
  if (read.length < 2) return `${PROPOSED}: memory over the day: fewer than 2 readable samples, so no slope`;
  const MB = 1024 * 1024;
  const n = read.length;
  const hours = (p: { t: number }) => (p.t - read[0]!.t) / 3_600_000;
  const mx = read.reduce((a, p) => a + hours(p), 0) / n;
  const my = read.reduce((a, p) => a + p.b / MB, 0) / n;
  const sxx = read.reduce((a, p) => a + (hours(p) - mx) ** 2, 0);
  const slope = sxx > 0 ? read.reduce((a, p) => a + (hours(p) - mx) * (p.b / MB - my), 0) / sxx : 0;
  const first = read[0]!.b / MB;
  const last = read[n - 1]!.b / MB;
  const peak = Math.max(...read.map((p) => p.b)) / MB;
  return `${PROPOSED}: memory over the day (report only; the existing last-5-minutes leak rule decides): ${n} samples over ${hours(read[n - 1]!).toFixed(2)} h, ${first.toFixed(1)} MB at the end of the warmup, ${last.toFixed(1)} MB at the end (${(last - first >= 0 ? "+" : "")}${(last - first).toFixed(1)} MB), peak ${peak.toFixed(1)} MB, least-squares slope ${slope >= 0 ? "+" : ""}${slope.toFixed(2)} MB/h`;
}

const isWarning = (l: string) => /LogSatisDash: Warning:/.test(l);

/** At most this many report-only "diagnostic" lines are kept in a failing verdict (a day of reads could log thousands). */
export const MAX_DIAGNOSTIC_LINES = 20;

/**
 * Presentation only, no condition changes: over 24 hours the tick-cost-native verdict would list one "longest step" per logged read
 * (thousands). The step-ceiling line becomes one entry per reader (its worst step and its read count; the ceiling itself was judged
 * on every line by Native.evaluate), and the report-only diagnostic lines are capped.
 */
export function compact(reasons: readonly string[], lines: readonly string[]): string[] {
  const worst = new Map<string, { max: number; reads: number }>();
  for (const c of stepCosts(lines)) {
    const w = worst.get(c.reader) ?? { max: 0, reads: 0 };
    worst.set(c.reader, { max: Math.max(w.max, c.longestStepMs), reads: w.reads + 1 });
  }
  const out: string[] = [];
  let diagnostics = 0;
  let dropped = 0;
  for (const r of reasons) {
    if (r.startsWith("step ceiling ")) {
      out.push(`step ceiling ${MAX_NATIVE_STEP_MS} ms held (sliced native reads only, every logged read judged): ${[...worst].map(([k, w]) => `${k} worst step ${w.max} ms over ${w.reads} read(s)`).join("; ")}`);
    } else if (r.startsWith("diagnostic (report-only):") && ++diagnostics > MAX_DIAGNOSTIC_LINES) {
      dropped++;
    } else out.push(r);
  }
  if (dropped) out.push(`${dropped} more report-only diagnostic line(s) left out (the first ${MAX_DIAGNOSTIC_LINES} are shown; the mod's log has them all)`);
  return out;
}

export function evaluate(e: CoreEvidence): Verdict {
  const base = Native.evaluate(e);
  const parts = e.captures["snapshot-parts.json"] as { measured?: { from: string; to: string } | null } | undefined;
  const from = parts?.measured ? Date.parse(parts.measured.from) + TICK_WARMUP_MS : NaN;
  const to = parts?.measured ? Date.parse(parts.measured.to) : NaN;
  const sources = e.captures["mod-config.json"] as Parameters<typeof Native.requiredStepReaders>[0];
  const problems: string[] = [];
  const report: string[] = [];

  if (!Number.isFinite(from) || !Number.isFinite(to)) {
    problems.push(`${PROPOSED}: the measured window was not recorded, so the soak conditions cannot be judged`);
  } else {
    const recorded = (e.captures["recorder.json"] as Recorded[] | undefined) ?? [];
    // P1
    const all = silences(e.lines, Native.requiredStepReaders(sources), recorded, from, to);
    problems.push(...silenceProblems(all));
    report.push(
      `${PROPOSED}: readers (reads in the window, longest gap, limit ${SILENCE_FACTOR} x cadence): ` +
        all.map((s) => `${s.name} ${s.count} (${fmtS(s.longestGapMs)} vs ${fmtS(s.limitMs)}; cadence ${s.cadenceS} s)`).join("; "),
    );
    const layers = new Map<string, number>();
    for (const r of recorded) if (r.agent === "mod" && r.status === 200 && r.world && Date.parse(r.at) >= from && Date.parse(r.at) <= to) layers.set(r.world.layer, (layers.get(r.world.layer) ?? 0) + 1);
    report.push(`${PROPOSED}: world-layer posts accepted in the window (report only: a layer whose data did not change posts nothing): ${[...layers].sort().map(([k, n]) => `${k} ${n}`).join(", ") || "none"}`);
    // P3
    const inWindow = (l: string) => {
      const t = Native.logTimeMs(l);
      return !Number.isFinite(t) || (t >= from && t <= to); // an Error line with no readable time is counted
    };
    const errors = e.errorLines.filter(inWindow);
    const warnings = e.lines.filter((l) => isWarning(l) && inWindow(l)).length;
    if (errors.length) problems.push(`${PROPOSED}: ${errors.length} Error line(s) from the mod in the window; the first: ${errors[0]}`);
    report.push(`${PROPOSED}: mod log in the window: ${errors.length} Error line(s) (${e.errorLines.length} in the whole boot),${warnings} Warning line(s) (report only)`);
  }
  // P2
  const capture = e.captures["tick-cost.json"] as { memoryWithMod?: MemoryPoint[] } | undefined;
  report.push(memoryDay(capture?.memoryWithMod));

  const baseReasons = compact(base.reasons, e.lines);
  if (base.pass && problems.length === 0) return { pass: true, reasons: [...baseReasons, ...report] };
  // The base verdict lists the waited steps itself, on a pass and on a failure.
  return { pass: false, reasons: [...baseReasons, ...problems, ...report] };
}
