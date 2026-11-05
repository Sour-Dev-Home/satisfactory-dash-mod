// What the mod's own log says about its native readers (ADR-0058 Decision 3): with a reader on the bridge the mod's output equals
// FRM's, so a parity or tick-cost run cannot tell native from bridge by values alone. The lines are written by the mod
// (Driver.cpp when it enters Running; GameReader.cpp when a node scan ends) and carry counts, enum names and times only.
// Three families: the Readers: line, the S1 node-scan cost line, and the S2 step-cost lines of every sliced native read.
import type { Verdict } from "./verdict";

export const READERS_LINE = "Readers:";
export const SCAN_LINE = "Resource node scan (game thread only):";

/** factory and trains are absent on the S1 two-field line. */
export interface ReaderSources { players: string; resourceNodes: string; factory?: string; trains?: string; rails?: string; conveyors?: string; mapMarkers?: string; droneRoutes?: string; powerLines?: string; extractors?: string; generators?: string; stationRates?: string; storage?: string }
export interface ScanCost { actors: number; steps: number; nodes: number; longestStepMs: number; totalMs: number }

// The S1 two-field form, the S2 four-field form, the S3 five-field form, the S3 six-field form (conveyors after rails), or the S4-A
// nine-field form (mapMarkers, droneRoutes and powerLines after conveyors, all three or none), or the S4-B thirteen-field form
// (extractors, generators, stationRates and storage after powerLines, all four or none).
const READERS = /\bReaders: players=(\S+) resourceNodes=(\S+)(?: factory=(\S+) trains=(\S+)(?: rails=(\S+)(?: conveyors=(\S+)(?: mapMarkers=(\S+) droneRoutes=(\S+) powerLines=(\S+)(?: extractors=(\S+) generators=(\S+) stationRates=(\S+) storage=(\S+))?)?)?)?)?\s*$/;
const SCAN = /Resource node scan \(game thread only\): (\d+) actors in (\d+) steps, (\d+) nodes, longest step (\d+(?:\.\d+)?) ms, total (\d+(?:\.\d+)?) ms/;

/** The last `Readers:` line (a later Running entry replaces an earlier one), or null when there is none. */
export function readerSources(lines: readonly string[]): ReaderSources | null {
  let found: ReaderSources | null = null;
  for (const line of lines) {
    const m = READERS.exec(line);
    if (!m) continue;
    const base = { players: m[1]!, resourceNodes: m[2]! };
    const withTrains = m[3] === undefined ? base : { ...base, factory: m[3], trains: m[4]! };
    const withRails = m[5] === undefined ? withTrains : { ...withTrains, rails: m[5] };
    const withConveyors = m[6] === undefined ? withRails : { ...withRails, conveyors: m[6] };
    const withS4A = m[7] === undefined ? withConveyors : { ...withConveyors, mapMarkers: m[7], droneRoutes: m[8]!, powerLines: m[9]! };
    found = m[10] === undefined ? withS4A : { ...withS4A, extractors: m[10], generators: m[11]!, stationRates: m[12]!, storage: m[13]! };
  }
  return found;
}

/** Every completed node scan's line, parsed. */
export function scanCosts(lines: readonly string[]): ScanCost[] {
  const out: ScanCost[] = [];
  for (const line of lines) {
    const m = SCAN.exec(line);
    if (m) out.push({ actors: Number(m[1]), steps: Number(m[2]), nodes: Number(m[3]), longestStepMs: Number(m[4]), totalMs: Number(m[5]) });
  }
  return out;
}

// The 2 ms step ceiling (owner, 2026-10-26): every sliced native read logs `<Name> (game thread only): ... longest step X ms, total Y ms`.
export const STEP_LINE_NAMES = ["Resource node scan", "Factory read", "Trains read", "Train routes read", "Logistics read", "Map markers read", "Drone routes read", "Buildables read"] as const;
export const MAX_NATIVE_STEP_MS = 2.0;
export interface StepCost { reader: string; longestStepMs: number; totalMs: number }

const STEP = new RegExp(`(${STEP_LINE_NAMES.join("|")}) \\(game thread only\\): .*longest step (\\d+(?:\\.\\d+)?) ms, total (\\d+(?:\\.\\d+)?) ms`);
// A line that names a sliced reader but does not parse as a StepCost is malformed, and fails the ceiling (it never passes as "present").
const STEP_HEAD = new RegExp(`(${STEP_LINE_NAMES.join("|")}) \\(game thread only\\): `);

/**
 * The diagnostic `<Name> read longest step: X ms over N elements, phase A to B` (owner, 2026-10-27), with the optional tail
 * `, longest element Y ms at I (phase T)` added 2026-10-28. Older evidence has no tail, so its element fields are undefined.
 */
export interface LongestStepDiagnostic { reader: string; longestStepMs: number; elements: number; startPhase: number; endPhase: number; longestElementMs?: number; longestElementIndex?: number; longestElementPhase?: number; cyclesPerMsInStep?: number; cyclesPerMsInOtherSteps?: number; otherSteps?: number; conveyorCalls?: ConveyorCallMaxima }
/** The conveyor-call tail's five maxima: milliseconds and the conveyor slot each occurred at. */
export interface ConveyorCallMaxima { getMs: number; getSlot: number; nameMs: number; nameSlot: number; textMs: number; textSlot: number; speedMs: number; speedSlot: number; endsMs: number; endsSlot: number }
// The cycles tail (owner 2026-11-01, tick-cost run 41334503941): `, thread cycles per ms A in this step, B in N other steps`. Report-only.
// The conveyor-calls tail (owner 2026-11-01, run 41349617818), Logistics only, after it: `, conveyor calls max: get A ms at I, name B ms at J, text C ms at K, speed D ms at L, ends E ms at M`.
const LONGEST_STEP = /LogSatisDash: (?:Display: )?(.+?) read longest step: (\d+(?:\.\d+)?) ms over (\d+) elements, phase (\d+) to (\d+)(?:, longest element (\d+(?:\.\d+)?) ms at (\d+) \(phase (\d+)\))?(?:, thread cycles per ms (\d+) in this step, (\d+) in (\d+) other steps)?(?:, conveyor calls max: get (\d+(?:\.\d+)?) ms at (\d+), name (\d+(?:\.\d+)?) ms at (\d+), text (\d+(?:\.\d+)?) ms at (\d+), speed (\d+(?:\.\d+)?) ms at (\d+), ends (\d+(?:\.\d+)?) ms at (\d+))?\s*$/;

/** One entry per longest-step diagnostic line, old and new shapes. Not a pass input. */
export function longestStepDiagnostics(lines: readonly string[]): LongestStepDiagnostic[] {
  const out: LongestStepDiagnostic[] = [];
  for (const line of lines) {
    const m = LONGEST_STEP.exec(line);
    if (!m) continue;
    const d: LongestStepDiagnostic = { reader: m[1]!, longestStepMs: Number(m[2]), elements: Number(m[3]), startPhase: Number(m[4]), endPhase: Number(m[5]) };
    if (m[6] !== undefined) {
      d.longestElementMs = Number(m[6]);
      d.longestElementIndex = Number(m[7]);
      d.longestElementPhase = Number(m[8]);
    }
    if (m[9] !== undefined) {
      d.cyclesPerMsInStep = Number(m[9]);
      d.cyclesPerMsInOtherSteps = Number(m[10]);
      d.otherSteps = Number(m[11]);
    }
    if (m[12] !== undefined) {
      d.conveyorCalls = {
        getMs: Number(m[12]), getSlot: Number(m[13]), nameMs: Number(m[14]), nameSlot: Number(m[15]), textMs: Number(m[16]),
        textSlot: Number(m[17]), speedMs: Number(m[18]), speedSlot: Number(m[19]), endsMs: Number(m[20]), endsSlot: Number(m[21]),
      };
    }
    out.push(d);
  }
  return out;
}

/** One StepCost per sliced-read line in the log. */
export function stepCosts(lines: readonly string[]): StepCost[] {
  const out: StepCost[] = [];
  for (const line of lines) {
    const m = STEP.exec(line);
    if (m) out.push({ reader: m[1]!, longestStepMs: Number(m[2]), totalMs: Number(m[3]) });
  }
  return out;
}

/**
 * Owner ruling 2026-11-03 (tick-cost-native 41706085144): a longest step over the ceiling still passes when the thread was mostly
 * WAITING, not working — its thread cycles per ms are below WAITED_STEP_MAX_CYCLE_SHARE of the run's median (see referenceCyclesPerMs) — and its CPU estimate
 * (wall time x that share) is within the ceiling. It needs the cycles tail (the mod's "stepDiagnostics"); without it the plain rule holds.
 */
export const WAITED_STEP_MAX_CYCLE_SHARE = 0.25;
export interface WaitedStep { reader: string; longestStepMs: number; cycleShare: number; cpuEstimateMs: number; diagnostic: LongestStepDiagnostic; referenceCyclesPerMs: number; referenceReads: number }

/**
 * Owner 2026-11-05 (soak run 41878531754): the reference is the run-wide median of `cyclesPerMsInOtherSteps` over every diagnostic line
 * that has the cycles tail, not the slow read's own other steps (a read slowed as a whole depresses its own reference). Undefined when
 * no line has the tail. The median of one line is its own value.
 */
export function referenceCyclesPerMs(diagnostics: readonly LongestStepDiagnostic[]): { median: number; reads: number } | undefined {
  const values = diagnostics.map((d) => d.cyclesPerMsInOtherSteps).filter((v): v is number => v !== undefined && Number.isFinite(v)).sort((a, b) => a - b);
  if (values.length === 0) return undefined;
  const mid = values.length >> 1;
  return { median: values.length % 2 === 1 ? values[mid]! : (values[mid - 1]! + values[mid]!) / 2, reads: values.length };
}

/** The over-ceiling steps the waiting rule lets pass, each matched to its own diagnostic line (same reader, same longest step). */
export function waitedSteps(lines: readonly string[]): WaitedStep[] {
  const diagnostics = longestStepDiagnostics(lines);
  const reference = referenceCyclesPerMs(diagnostics);
  const out: WaitedStep[] = [];
  for (const c of stepCosts(lines)) {
    if (!(c.longestStepMs > MAX_NATIVE_STEP_MS)) continue;
    const d = diagnostics.find((x) => `${x.reader} read` === c.reader && Math.abs(x.longestStepMs - c.longestStepMs) < 0.005);
    if (!d || d.cyclesPerMsInStep === undefined || reference === undefined || !(reference.median > 0)) continue;
    const cycleShare = d.cyclesPerMsInStep / reference.median;
    const cpuEstimateMs = c.longestStepMs * cycleShare;
    if (cycleShare < WAITED_STEP_MAX_CYCLE_SHARE && cpuEstimateMs <= MAX_NATIVE_STEP_MS) out.push({ reader: c.reader, longestStepMs: c.longestStepMs, cycleShare, cpuEstimateMs, diagnostic: d, referenceCyclesPerMs: reference.median, referenceReads: reference.reads });
  }
  return out;
}

/** One report line per step the waiting rule let pass (owner ruling 2026-11-03); the verdict always carries them. */
export function describeWaitedSteps(lines: readonly string[]): string[] {
  return waitedSteps(lines).map((w) => {
    const d = w.diagnostic;
    const element = d.longestElementMs === undefined ? "" : `, longest element ${d.longestElementMs} ms at ${d.longestElementIndex} (phase ${d.longestElementPhase})`;
    const k = d.conveyorCalls;
    const calls = k === undefined ? "" : `; conveyor calls max: get ${k.getMs} ms at ${k.getSlot}, name ${k.nameMs} ms at ${k.nameSlot}, text ${k.textMs} ms at ${k.textSlot}, speed ${k.speedMs} ms at ${k.speedSlot}, ends ${k.endsMs} ms at ${k.endsSlot}`;
    return `${w.reader}: longest step ${w.longestStepMs} ms passed as a wait (owner ruling 2026-11-03): the thread ran at ${(w.cycleShare * 100).toFixed(1)}% of the run's median ${Math.round(w.referenceCyclesPerMs).toLocaleString("en-US")} cycles per ms over ${w.referenceReads} reads (< ${WAITED_STEP_MAX_CYCLE_SHARE * 100}%), CPU estimate ${w.cpuEstimateMs.toFixed(2)} ms <= ${MAX_NATIVE_STEP_MS} ms${element}${calls}`;
  });
}

/** Reasons the step ceiling fails: a required reader with no line, or a logged longest step over 2 ms (2.00 passes) that is not a wait. */
export function stepProblems(lines: readonly string[], required: readonly string[]): string[] {
  const waited = waitedSteps(lines);
  const costs = stepCosts(lines);
  const out: string[] = [];
  for (const reader of required) {
    if (!costs.some((c) => c.reader === reader)) out.push(`the mod's log has no "${reader} (game thread only):" line: the reader did not run, so its step cost is unknown`);
  }
  for (const line of lines) {
    const head = STEP_HEAD.exec(line);
    if (head && !STEP.test(line)) out.push(`${head[1]}: the mod's log line has a malformed step cost, so the ceiling cannot be judged`);
  }
  for (const c of costs) {
    if (!Number.isFinite(c.longestStepMs) || !Number.isFinite(c.totalMs)) out.push(`${c.reader}: step cost is not a finite number`);
    else if (c.longestStepMs > MAX_NATIVE_STEP_MS && !waited.some((w) => w.reader === c.reader && w.longestStepMs === c.longestStepMs)) out.push(`${c.reader}: longest step ${c.longestStepMs} ms exceeds the ${MAX_NATIVE_STEP_MS} ms ceiling (owner, 2026-10-26)`);
  }
  return out;
}

/** Why the log does not show the players reader native (empty: it does). */
export function playersLogProblems(lines: readonly string[]): string[] {
  const s = readerSources(lines);
  if (!s) return [`the mod's log has no "${READERS_LINE}" line: the run cannot show the players reader was native, not the bridge`];
  return s.players === "native" ? [] : [`the mod's "${READERS_LINE}" line says players=${s.players}, not native`];
}

function partLogProblems(lines: readonly string[], part: "factory" | "trains" | "rails" | "conveyors" | "mapMarkers" | "droneRoutes" | "powerLines" | "extractors" | "generators" | "stationRates" | "storage"): string[] {
  const s = readerSources(lines);
  if (!s) return [`the mod's log has no "${READERS_LINE}" line: the run cannot show the ${part} reader was native, not the bridge`];
  const source = s[part];
  if (source === undefined) return [`the mod's "${READERS_LINE}" line has no ${part}= field (an older line shape): the run cannot show the ${part} reader was native`];
  return source === "native" ? [] : [`the mod's "${READERS_LINE}" line says ${part}=${source}, not native`];
}

/** Why the log does not show the factory reader native (empty: it does). */
export const factoryLogProblems = (lines: readonly string[]): string[] => partLogProblems(lines, "factory");

/** Why the log does not show the trains reader native (empty: it does). */
export const trainsLogProblems = (lines: readonly string[]): string[] => partLogProblems(lines, "trains");

/** Why the log does not show the rails reader native (empty: it does). */
export const railsLogProblems = (lines: readonly string[]): string[] => partLogProblems(lines, "rails");

/** Why the log does not show the conveyors reader native (empty: it does). */
export const conveyorsLogProblems = (lines: readonly string[]): string[] => partLogProblems(lines, "conveyors");

/** Why the log does not show the map markers reader native (empty: it does). */
export const mapMarkersLogProblems = (lines: readonly string[]): string[] => partLogProblems(lines, "mapMarkers");

/** Why the log does not show the drone routes reader native (empty: it does). */
export const droneRoutesLogProblems = (lines: readonly string[]): string[] => partLogProblems(lines, "droneRoutes");

/** Why the log does not show the power lines reader native (empty: it does). */
export const powerLinesLogProblems = (lines: readonly string[]): string[] => partLogProblems(lines, "powerLines");

/** Why the log does not show the extractors reader native (empty: it does). */
export const extractorsLogProblems = (lines: readonly string[]): string[] => partLogProblems(lines, "extractors");

/** Why the log does not show the generators reader native (empty: it does). */
export const generatorsLogProblems = (lines: readonly string[]): string[] => partLogProblems(lines, "generators");

/** Why the log does not show the station rates reader native (empty: it does). */
export const stationRatesLogProblems = (lines: readonly string[]): string[] => partLogProblems(lines, "stationRates");

const STATION_RATES = /Station rates \(reflection read\): (\d+) platforms read, (\d+) fluid platforms skipped, (\d+) lookups failed/;
const STATION_RATES_HEAD = /Station rates \(reflection read\):/;

/**
 * Why the mod's `Station rates (reflection read)` counts do not show the reflection read working (empty: they do). OQ2: a rate lookup that
 * fails everywhere must fail the verdict even when no FRM station transfers. Needs at least one such line, `lookups failed` = 0 on every
 * line, and `platforms read` > 0 on at least one line. A line that names the read but does not parse is malformed and fails.
 */
export function stationRatesLineProblems(lines: readonly string[]): string[] {
  const out: string[] = [];
  let seen = 0;
  let read = 0;
  for (const line of lines) {
    if (!STATION_RATES_HEAD.test(line)) continue;
    const m = STATION_RATES.exec(line);
    if (!m) {
      out.push(`the mod's "Station rates (reflection read)" line is malformed, so the lookups cannot be judged`);
      continue;
    }
    seen += 1;
    read += Number(m[1]);
    if (Number(m[3]) > 0) out.push(`the mod's "Station rates (reflection read)" line says ${m[3]} lookups failed (must be 0): the transfer-rate reflection read did not resolve`);
  }
  if (seen === 0) out.push(`the mod's log has no "Station rates (reflection read):" line: the native station rates read never ran`);
  else if (read === 0) out.push(`the mod's "Station rates (reflection read)" lines read 0 platforms: no station rate was read, so the verdict cannot be a pass`);
  return out;
}

/** Why the log does not show the storage reader native (empty: it does). */
export const storageLogProblems = (lines: readonly string[]): string[] => partLogProblems(lines, "storage");

/** Why the log does not show the resource-node reader native and at work (empty: it does). */
export function nodesLogProblems(lines: readonly string[]): string[] {
  const out: string[] = [];
  const s = readerSources(lines);
  if (!s) out.push(`the mod's log has no "${READERS_LINE}" line: the run cannot show the resource-node reader was native, not the bridge`);
  else if (s.resourceNodes !== "native") out.push(`the mod's "${READERS_LINE}" line says resourceNodes=${s.resourceNodes}, not native`);
  if (scanCosts(lines).length === 0) out.push(`the mod's log has no "${SCAN_LINE}" line: the native scan never completed`);
  return out;
}

/** A verdict with log problems added: a pass with problems becomes a fail, a fail keeps its reasons first. */
export function withProblems(v: Verdict, problems: string[]): Verdict {
  return problems.length === 0 ? v : { pass: false, reasons: [...v.reasons, ...problems] };
}

/** The scan cost as recorded: the longest single step is judged against the 2 ms ceiling (owner, 2026-10-26, by stepProblems); the total is recorded only. */
export function describeScanCost(costs: readonly ScanCost[]): string {
  const longest = Math.max(...costs.map((c) => c.longestStepMs));
  const total = Math.max(...costs.map((c) => c.totalMs));
  return `native node scan cost (longest step judged against the ${MAX_NATIVE_STEP_MS} ms ceiling, total recorded only): ${costs.length} scan(s); longest single step ${longest} ms; longest total ${total} ms (game thread only)`;
}

// The one-off conveyor census (Task 8's lines, read by the s3-census run). Game class names and tier display names are game data;
// the lines carry no id. A line that does not parse is ignored.
export interface CensusTier { kind: string; name: string; speed: number; count: number }
export interface Census {
  tiers: CensusTier[];
  kinds: { checked: number; differ: number; classes: string[] } | null;
  ports: { into: number; outOf: number } | null;
}
const CENSUS_TIER = /Conveyor tier census: (belt|lift|pipe) \| (.*) \| speed (-?\d+(?:\.\d+)?) \| count (\d+)\s*$/;
const CENSUS_KINDS = /Conveyor kinds: (\d+) ends checked, (\d+) differ from the class table \(first classes: (.*)\)\s*$/;
const CENSUS_PORTS = /Conveyor drone ports: (\d+) edges end at a port, (\d+) start at one\s*$/;

/** The census lines in the log; the last kinds and ports lines win. */
export function censusLines(lines: readonly string[]): Census {
  const out: Census = { tiers: [], kinds: null, ports: null };
  for (const line of lines) {
    const t = CENSUS_TIER.exec(line);
    if (t) {
      out.tiers.push({ kind: t[1]!, name: t[2]!, speed: Number(t[3]), count: Number(t[4]) });
      continue;
    }
    const k = CENSUS_KINDS.exec(line);
    if (k) {
      out.kinds = { checked: Number(k[1]), differ: Number(k[2]), classes: k[3]!.trim() === "" ? [] : k[3]!.split(", ") };
      continue;
    }
    const p = CENSUS_PORTS.exec(line);
    if (p) out.ports = { into: Number(p[1]), outOf: Number(p[2]) };
  }
  return out;
}

/**
 * The counts on the shared pass's `Logistics read (game thread only): layers rails=%d conveyors=%d[ powerLines=%d], ...` line.
 * The S4-A fields (`powerLines`, `wires`) are absent on an S3 line, which has neither group.
 */
export interface LogisticsRead { rails: number; conveyors: number; powerLines?: number; buildables: number; tracks: number; belts: number; lifts: number; pipes: number; openEnds: number; ownerlessEnds: number; wires?: number }
const LOGISTICS = /Logistics read \(game thread only\): layers rails=(\d+) conveyors=(\d+)(?: powerLines=(\d+))?, (\d+) buildables, (\d+) tracks, (\d+) belts, (\d+) lifts, (\d+) pipes, (\d+) open ends, (\d+) ownerless ends(?:, wires (\d+), expected wires -?\d+)?/;

const LOGISTICS_LAYERS = /Logistics read \(game thread only\): layers rails=(\d+) conveyors=(\d+)(?: powerLines=(\d+))?/;

/** The layers the shared logistics pass served, from its `Logistics read` line (the last one wins), or null when there is none. `powerLines` is absent on an S3 line. */
export function logisticsLayers(lines: readonly string[]): { rails: number; conveyors: number; powerLines?: number } | null {
  let found: { rails: number; conveyors: number; powerLines?: number } | null = null;
  for (const line of lines) {
    const m = LOGISTICS_LAYERS.exec(line);
    if (m) found = { rails: Number(m[1]), conveyors: Number(m[2]), ...(m[3] !== undefined ? { powerLines: Number(m[3]) } : {}) };
  }
  return found;
}

/** Why the log does not show the shared pass serving this layer (empty: a `Logistics read` line with `<layer>=1`). A line without the layer's group did not serve it. */
export function logisticsLineProblems(lines: readonly string[], layer: "rails" | "conveyors" | "powerLines"): string[] {
  const l = logisticsLayers(lines);
  if (!l) return [`the mod's log has no "Logistics read (game thread only):" line: the shared logistics pass never completed`];
  const served = l[layer] ?? 0;
  return served === 1 ? [] : [`the mod's "Logistics read" line says ${layer}=${served}, not 1: the pass did not serve the ${layer} layer`];
}

/** The `Conveyor kinds:` census line (the last one), or null: how many belt/lift/pipe ends the class table classified differently. */
export const kindCheck = (lines: readonly string[]): Census["kinds"] => censusLines(lines).kinds;

/** The first `Logistics read` line's counts (the first pass), or null when there is none. */
export function logisticsRead(lines: readonly string[]): LogisticsRead | null {
  for (const line of lines) {
    const m = LOGISTICS.exec(line);
    if (m) {
      const n = (i: number) => Number(m[i]);
      return {
        rails: n(1), conveyors: n(2), ...(m[3] !== undefined ? { powerLines: n(3) } : {}),
        buildables: n(4), tracks: n(5), belts: n(6), lifts: n(7), pipes: n(8), openEnds: n(9), ownerlessEnds: n(10),
        ...(m[11] !== undefined ? { wires: n(11) } : {}),
      };
    }
  }
  return null;
}
