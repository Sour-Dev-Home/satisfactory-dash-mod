// What the mod's own log says about its native readers (ADR-0058 Decision 3): with a reader on the bridge the mod's output equals
// FRM's, so a parity or tick-cost run cannot tell native from bridge by values alone. The two lines are written by the mod
// (Driver.cpp when it enters Running; GameReader.cpp when a node scan ends) and carry counts, enum names and times only.
import type { Verdict } from "./verdict";

export const READERS_LINE = "Readers:";
export const SCAN_LINE = "Resource node scan (game thread only):";

export interface ReaderSources { players: string; resourceNodes: string }
export interface ScanCost { actors: number; steps: number; nodes: number; longestStepMs: number; totalMs: number }

const READERS = /\bReaders: players=(\S+) resourceNodes=(\S+)\s*$/;
const SCAN = /Resource node scan \(game thread only\): (\d+) actors in (\d+) steps, (\d+) nodes, longest step ([\d.]+) ms, total ([\d.]+) ms/;

/** The last `Readers:` line (a later Running entry replaces an earlier one), or null when there is none. */
export function readerSources(lines: readonly string[]): ReaderSources | null {
  let found: ReaderSources | null = null;
  for (const line of lines) {
    const m = READERS.exec(line);
    if (m) found = { players: m[1]!, resourceNodes: m[2]! };
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

/** Why the log does not show the players reader native (empty: it does). */
export function playersLogProblems(lines: readonly string[]): string[] {
  const s = readerSources(lines);
  if (!s) return [`the mod's log has no "${READERS_LINE}" line: the run cannot show the players reader was native, not the bridge`];
  return s.players === "native" ? [] : [`the mod's "${READERS_LINE}" line says players=${s.players}, not native`];
}

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

/** The scan cost as recorded (not judged: no ceiling, ADR-0058 Decision 3 item 3). */
export function describeScanCost(costs: readonly ScanCost[]): string {
  const longest = Math.max(...costs.map((c) => c.longestStepMs));
  const total = Math.max(...costs.map((c) => c.totalMs));
  return `native node scan cost, recorded not judged: ${costs.length} scan(s); longest single step ${longest} ms; longest total ${total} ms (game thread only)`;
}
