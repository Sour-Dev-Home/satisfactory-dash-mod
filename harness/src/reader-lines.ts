// What the mod's own log says about its native readers (ADR-0058 Decision 3): with a reader on the bridge the mod's output equals
// FRM's, so a parity or tick-cost run cannot tell native from bridge by values alone. The lines are written by the mod
// (Driver.cpp when it enters Running; GameReader.cpp when a node scan ends) and carry counts, enum names and times only.
// Three families: the Readers: line, the S1 node-scan cost line, and the S2 step-cost lines of every sliced native read.
import type { Verdict } from "./verdict";

export const READERS_LINE = "Readers:";
export const SCAN_LINE = "Resource node scan (game thread only):";

/** factory and trains are absent on the S1 two-field line. */
export interface ReaderSources { players: string; resourceNodes: string; factory?: string; trains?: string }
export interface ScanCost { actors: number; steps: number; nodes: number; longestStepMs: number; totalMs: number }

// The S1 two-field form, or the S2 four-field form.
const READERS = /\bReaders: players=(\S+) resourceNodes=(\S+)(?: factory=(\S+) trains=(\S+))?\s*$/;
const SCAN = /Resource node scan \(game thread only\): (\d+) actors in (\d+) steps, (\d+) nodes, longest step (\d+(?:\.\d+)?) ms, total (\d+(?:\.\d+)?) ms/;

/** The last `Readers:` line (a later Running entry replaces an earlier one), or null when there is none. */
export function readerSources(lines: readonly string[]): ReaderSources | null {
  let found: ReaderSources | null = null;
  for (const line of lines) {
    const m = READERS.exec(line);
    if (m) found = m[3] === undefined ? { players: m[1]!, resourceNodes: m[2]! } : { players: m[1]!, resourceNodes: m[2]!, factory: m[3], trains: m[4]! };
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
export const STEP_LINE_NAMES = ["Resource node scan", "Factory read", "Trains read", "Train routes read"] as const;
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

function partLogProblems(lines: readonly string[], part: "factory" | "trains"): string[] {
  const s = readerSources(lines);
  if (!s) return [`the mod's log has no "${READERS_LINE}" line: the run cannot show the ${part} reader was native, not the bridge`];
  const source = s[part];
  if (source === undefined) return [`the mod's "${READERS_LINE}" line has no ${part}= field (an S1 line): the run cannot show the ${part} reader was native`];
  return source === "native" ? [] : [`the mod's "${READERS_LINE}" line says ${part}=${source}, not native`];
}

/** Why the log does not show the factory reader native (empty: it does). */
export const factoryLogProblems = (lines: readonly string[]): string[] => partLogProblems(lines, "factory");

/** Why the log does not show the trains reader native (empty: it does). */
export const trainsLogProblems = (lines: readonly string[]): string[] => partLogProblems(lines, "trains");

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
