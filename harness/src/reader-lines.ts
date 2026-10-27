// What the mod's own log says about its native readers (ADR-0058 Decision 3): with a reader on the bridge the mod's output equals
// FRM's, so a parity or tick-cost run cannot tell native from bridge by values alone. The lines are written by the mod
// (Driver.cpp when it enters Running; GameReader.cpp when a node scan ends) and carry counts, enum names and times only.
// Three families: the Readers: line, the S1 node-scan cost line, and the S2 step-cost lines of every sliced native read.
import type { Verdict } from "./verdict";

export const READERS_LINE = "Readers:";
export const SCAN_LINE = "Resource node scan (game thread only):";

/** factory and trains are absent on the S1 two-field line. */
export interface ReaderSources { players: string; resourceNodes: string; factory?: string; trains?: string; rails?: string; conveyors?: string; mapMarkers?: string; droneRoutes?: string; powerLines?: string }
export interface ScanCost { actors: number; steps: number; nodes: number; longestStepMs: number; totalMs: number }

// The S1 two-field form, the S2 four-field form, the S3 five-field form, the S3 six-field form (conveyors after rails), or the S4-A
// nine-field form (mapMarkers, droneRoutes and powerLines after conveyors, all three or none).
const READERS = /\bReaders: players=(\S+) resourceNodes=(\S+)(?: factory=(\S+) trains=(\S+)(?: rails=(\S+)(?: conveyors=(\S+)(?: mapMarkers=(\S+) droneRoutes=(\S+) powerLines=(\S+))?)?)?)?\s*$/;
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
    found = m[7] === undefined ? withConveyors : { ...withConveyors, mapMarkers: m[7], droneRoutes: m[8]!, powerLines: m[9]! };
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
export const STEP_LINE_NAMES = ["Resource node scan", "Factory read", "Trains read", "Train routes read", "Logistics read", "Map markers read", "Drone routes read"] as const;
export const MAX_NATIVE_STEP_MS = 2.0;
export interface StepCost { reader: string; longestStepMs: number; totalMs: number }

const STEP = new RegExp(`(${STEP_LINE_NAMES.join("|")}) \\(game thread only\\): .*longest step (\\d+(?:\\.\\d+)?) ms, total (\\d+(?:\\.\\d+)?) ms`);
// A line that names a sliced reader but does not parse as a StepCost is malformed, and fails the ceiling (it never passes as "present").
const STEP_HEAD = new RegExp(`(${STEP_LINE_NAMES.join("|")}) \\(game thread only\\): `);

/** One StepCost per sliced-read line in the log. */
export function stepCosts(lines: readonly string[]): StepCost[] {
  const out: StepCost[] = [];
  for (const line of lines) {
    const m = STEP.exec(line);
    if (m) out.push({ reader: m[1]!, longestStepMs: Number(m[2]), totalMs: Number(m[3]) });
  }
  return out;
}

/** Reasons the step ceiling fails: a required reader with no line, or a logged longest step over 2 ms (2.00 passes). */
export function stepProblems(lines: readonly string[], required: readonly string[]): string[] {
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
    else if (c.longestStepMs > MAX_NATIVE_STEP_MS) out.push(`${c.reader}: longest step ${c.longestStepMs} ms exceeds the ${MAX_NATIVE_STEP_MS} ms ceiling (owner, 2026-10-26)`);
  }
  return out;
}

/** Why the log does not show the players reader native (empty: it does). */
export function playersLogProblems(lines: readonly string[]): string[] {
  const s = readerSources(lines);
  if (!s) return [`the mod's log has no "${READERS_LINE}" line: the run cannot show the players reader was native, not the bridge`];
  return s.players === "native" ? [] : [`the mod's "${READERS_LINE}" line says players=${s.players}, not native`];
}

function partLogProblems(lines: readonly string[], part: "factory" | "trains" | "rails" | "conveyors" | "mapMarkers" | "droneRoutes" | "powerLines"): string[] {
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

/** The counts on the shared pass's `Logistics read (game thread only): layers rails=%d conveyors=%d, ...` line. */
export interface LogisticsRead { rails: number; conveyors: number; buildables: number; tracks: number; belts: number; lifts: number; pipes: number; openEnds: number; ownerlessEnds: number }
const LOGISTICS = /Logistics read \(game thread only\): layers rails=(\d+) conveyors=(\d+), (\d+) buildables, (\d+) tracks, (\d+) belts, (\d+) lifts, (\d+) pipes, (\d+) open ends, (\d+) ownerless ends/;

const LOGISTICS_LAYERS = /Logistics read \(game thread only\): layers rails=(\d+) conveyors=(\d+)/;

/** The layers the shared logistics pass served, from its `Logistics read` line (the last one wins), or null when there is none. */
export function logisticsLayers(lines: readonly string[]): { rails: number; conveyors: number } | null {
  let found: { rails: number; conveyors: number } | null = null;
  for (const line of lines) {
    const m = LOGISTICS_LAYERS.exec(line);
    if (m) found = { rails: Number(m[1]), conveyors: Number(m[2]) };
  }
  return found;
}

/** Why the log does not show the shared pass serving this layer (empty: a `Logistics read` line with `<layer>=1`). */
export function logisticsLineProblems(lines: readonly string[], layer: "rails" | "conveyors"): string[] {
  const l = logisticsLayers(lines);
  if (!l) return [`the mod's log has no "Logistics read (game thread only):" line: the shared logistics pass never completed`];
  return l[layer] === 1 ? [] : [`the mod's "Logistics read" line says ${layer}=${l[layer]}, not 1: the pass did not serve the ${layer} layer`];
}

/** The `Conveyor kinds:` census line (the last one), or null: how many belt/lift/pipe ends the class table classified differently. */
export const kindCheck = (lines: readonly string[]): Census["kinds"] => censusLines(lines).kinds;

/** The first `Logistics read` line's counts (the first pass), or null when there is none. */
export function logisticsRead(lines: readonly string[]): LogisticsRead | null {
  for (const line of lines) {
    const m = LOGISTICS.exec(line);
    if (m) {
      const n = m.slice(1).map(Number);
      return { rails: n[0]!, conveyors: n[1]!, buildables: n[2]!, tracks: n[3]!, belts: n[4]!, lifts: n[5]!, pipes: n[6]!, openEnds: n[7]!, ownerlessEnds: n[8]! };
    }
  }
  return null;
}
