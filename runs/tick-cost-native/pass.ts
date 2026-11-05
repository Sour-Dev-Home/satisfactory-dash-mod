// The tick-cost run with sprint S1's native readers on (ADR-0058 Decision 3 item 3). The limits are the existing tick-cost
// run's, imported and unchanged (runs/tick-cost/pass.ts: C4, S12, same save, memory). Added, because "the reader was on" is
// part of the claim: the mod's log says every reader is native (all are, unconditionally, since R7); a snapshot in the measured window
// carried an available players part; and a resourceNodes world post was accepted inside the measured window after the warmup
// (so the actor scan ran while the ticks were measured). The scan's own cost is the mod's log line in result.json's modLines;
// since the owner's decision of 2026-10-26 the longest step of every sliced native read is judged against 2 ms.
import type { CoreEvidence } from "../../harness/src/core-run";
import { TICK_WARMUP_MS } from "../../harness/src/ticks";
import { MAX_NATIVE_STEP_MS, describeScanCost, describeWaitedSteps,factoryLogProblems, nodesLogProblems, trainsLogProblems, railsLogProblems, conveyorsLogProblems, mapMarkersLogProblems, droneRoutesLogProblems, powerLinesLogProblems, extractorsLogProblems, generatorsLogProblems, stationRatesLogProblems, stationRatesLineProblems, storageLogProblems, logisticsLineProblems, longestStepDiagnostics, playersLogProblems, scanCosts, stepCosts, stepProblems } from "../../harness/src/reader-lines";
import type { Verdict } from "../../harness/src/verdict";
import * as Base from "../tick-cost/pass";

export const requirements = [...Base.requirements, "R0-R1-R4-tick-cost"];

interface Parts { measured?: { from: string; to: string } | null; snapshots?: Array<{ at: string; playersAvailable?: boolean }> }
interface Recorded { at: string; status: number; agent: string; world?: { layer: string; items: number; bytes: number } }

/**
 * The sliced readers that must have logged a step line. Every reader is native and unconditional since the bridge was removed
 * (R7), so all of them are required, explicitly: the node scan, the factory read, trains and train routes, the shared logistics
 * pass ("Logistics read", one line for rails, conveyors and power lines), map markers, drone routes and the Buildables pass
 * (extractors, generators and storage; the station walk rides "Trains read").
 */
export const REQUIRED_STEP_READERS: readonly string[] = [
  "Resource node scan", "Factory read", "Trains read", "Train routes read", "Logistics read", "Map markers read", "Drone routes read", "Buildables read",
];
export function requiredStepReaders(): string[] {
  return [...REQUIRED_STEP_READERS];
}

/** A UE log line's UTC time, `[YYYY.MM.DD-HH.MM.SS:mmm]` (the harness's own tests also write the time with colons), or NaN. */
const LOG_TIME = /^\[(\d{4})\.(\d{2})\.(\d{2})-(\d{2})[.:](\d{2})[.:](\d{2})[:.](\d{3})\]/;
export function logTimeMs(line: string): number {
  const m = LOG_TIME.exec(line);
  return m ? Date.UTC(+m[1]!, +m[2]! - 1, +m[3]!, +m[4]!, +m[5]!, +m[6]!, +m[7]!) : NaN;
}

/** The S2 readers (from the config) whose step line is not inside the measured window. A line with no readable time does not count. */
function windowProblems(lines: readonly string[], required: readonly string[], from: number, to: number): string[] {
  return required
    .filter((r) => r !== "Resource node scan") // the scan's window is judged from the recorder's accepted post
    .filter((r) => !lines.some((l) => l.includes(`${r} (game thread only): `) && logTimeMs(l) >= from && logTimeMs(l) <= to))
    .map((r) => `the mod's log has no "${r} (game thread only):" line inside the measured window: the reader did not run while the ticks were measured`);
}

export function evaluate(e: CoreEvidence): Verdict {
  const base = Base.evaluate(e);
  const extra: string[] = [];
  // Every reader is native and unconditional (R7): the mod's own log must say so for all of them, and the powerLines layer rides
  // the shared Logistics read line, which must have served it. The run no longer writes per-reader sources into the mod's config.
  extra.push(
    ...factoryLogProblems(e.lines), ...trainsLogProblems(e.lines), ...railsLogProblems(e.lines), ...conveyorsLogProblems(e.lines),
    ...mapMarkersLogProblems(e.lines), ...droneRoutesLogProblems(e.lines), ...powerLinesLogProblems(e.lines), ...logisticsLineProblems(e.lines, "powerLines"),
    ...extractorsLogProblems(e.lines), ...generatorsLogProblems(e.lines), ...stationRatesLogProblems(e.lines), ...stationRatesLineProblems(e.lines), ...storageLogProblems(e.lines),
  );
  const parts = e.captures["snapshot-parts.json"] as Parts | undefined;
  const from = parts?.measured ? Date.parse(parts.measured.from) + TICK_WARMUP_MS : NaN;
  const to = parts?.measured ? Date.parse(parts.measured.to) : NaN;
  if (!Number.isFinite(from) || !Number.isFinite(to)) extra.push("the measured window was not recorded");
  const inWindow = (at: string) => Date.parse(at) >= from && Date.parse(at) <= to;
  const withPlayers = (parts?.snapshots ?? []).filter((s) => s.playersAvailable === true && inWindow(s.at)).length;
  if (withPlayers === 0) extra.push("no accepted snapshot in the measured window carried an available players part: the native players reader did not run");
  const recorded = (e.captures["recorder.json"] as Recorded[] | undefined) ?? [];
  const nodePosts = recorded.filter((r) => r.agent === "mod" && r.status === 200 && r.world?.layer === "resourceNodes" && inWindow(r.at));
  if (nodePosts.length === 0) extra.push("no resourceNodes world post was accepted inside the measured window: the native scan was not measured");
  // A bridge reader would also give a players part and an accepted resourceNodes post: the mod's own log must say native, and a
  // native scan must have completed. The scan cost goes into the reasons as a record; its longest step, like every sliced
  // native read's, is judged against the 2 ms ceiling by stepProblems below (owner, 2026-10-26).
  extra.push(...playersLogProblems(e.lines), ...nodesLogProblems(e.lines));
  const scans = scanCosts(e.lines);
  // The 2 ms step ceiling (owner, 2026-10-26): a new condition, not a change to the limits above. It covers the sliced native
  // reads only (node scan, factory, trains, train routes); one-shot reads (status, auto-pause, players, power's circuit-id probe)
  // are S0/S1 code outside this rule.
  const required = requiredStepReaders();
  extra.push(...stepProblems(e.lines, required));
  extra.push(...windowProblems(e.lines, required, from, to));
  if (extra.length === 0 && base.pass) {
    const steps = stepCosts(e.lines).map((c) => `${c.reader} longest step ${c.longestStepMs} ms`).join("; ");
    return { pass: true, reasons: [...base.reasons, `native readers live in the window: ${withPlayers} snapshots with players, ${nodePosts.length} resourceNodes post(s)`, describeScanCost(scans), `step ceiling ${MAX_NATIVE_STEP_MS} ms held (sliced native reads only): ${steps}`, ...describeWaitedSteps(e.lines)] };
  }
  // When the base verdict passes, its reasons describe what passed, so they are listed only on success.
  return { pass: false, reasons: [...(base.pass ? [] : base.reasons), ...extra, ...(scans.length ? [describeScanCost(scans)] : []), ...describeWaitedSteps(e.lines), ...diagnosticLines(e.lines)] };
}

/**
 * Report-only (owner 2026-11-01, tick-cost run 41334503941): one line per longest-step diagnostic the mod logged, with the thread
 * cycles when it had them, so a reader of a failed verdict can tell a slow step from a stalled one. It never decides `pass`, and it
 * is added to failing verdicts only.
 */
function diagnosticLines(lines: readonly string[]): string[] {
  return longestStepDiagnostics(lines).map((d) => {
    const element = d.longestElementMs === undefined ? "" : `, longest element ${d.longestElementMs.toFixed(2)} ms at ${d.longestElementIndex} (phase ${d.longestElementPhase})`;
    const cycles = d.cyclesPerMsInStep === undefined || d.cyclesPerMsInOtherSteps === undefined
      ? ""
      : `; thread cycles per ms ${d.cyclesPerMsInStep} in this step vs ${d.cyclesPerMsInOtherSteps} in ${d.otherSteps} other steps${d.cyclesPerMsInOtherSteps > 0 ? ` (${((100 * d.cyclesPerMsInStep) / d.cyclesPerMsInOtherSteps).toFixed(1)}%)` : ""}`;
    const c = d.conveyorCalls;
    const calls = c === undefined
      ? ""
      : `; conveyor calls max: get ${c.getMs.toFixed(2)} ms at ${c.getSlot}, name ${c.nameMs.toFixed(2)} ms at ${c.nameSlot}, text ${c.textMs.toFixed(2)} ms at ${c.textSlot}, speed ${c.speedMs.toFixed(2)} ms at ${c.speedSlot}, ends ${c.endsMs.toFixed(2)} ms at ${c.endsSlot}`;
    return `diagnostic (report-only): ${d.reader} longest step ${d.longestStepMs.toFixed(2)} ms${element}${cycles}${calls}`;
  });
}
