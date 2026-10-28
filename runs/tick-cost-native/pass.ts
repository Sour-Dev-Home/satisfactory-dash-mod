// The tick-cost run with sprint S1's native readers on (ADR-0058 Decision 3 item 3). The limits are the existing tick-cost
// run's, imported and unchanged (runs/tick-cost/pass.ts: C4, S12, same save, memory). Added, because "the reader was on" is
// part of the claim: the mod's config had playersSource and resourceNodesSource native; a snapshot in the measured window
// carried an available players part; and a resourceNodes world post was accepted inside the measured window after the warmup
// (so the actor scan ran while the ticks were measured). The scan's own cost is the mod's log line in result.json's modLines;
// since the owner's decision of 2026-10-26 the longest step of every sliced native read is judged against 2 ms.
import type { CoreEvidence } from "../../harness/src/core-run";
import { TICK_WARMUP_MS } from "../../harness/src/ticks";
import { MAX_NATIVE_STEP_MS, describeScanCost, factoryLogProblems, nodesLogProblems, trainsLogProblems, railsLogProblems, conveyorsLogProblems, mapMarkersLogProblems, droneRoutesLogProblems, powerLinesLogProblems, extractorsLogProblems, generatorsLogProblems, stationRatesLogProblems, stationRatesLineProblems, storageLogProblems, logisticsLineProblems, playersLogProblems, scanCosts, stepCosts, stepProblems } from "../../harness/src/reader-lines";
import type { Verdict } from "../../harness/src/verdict";
import * as Base from "../tick-cost/pass";

export const requirements = [...Base.requirements, "R0-R1-R4-tick-cost"];

interface Parts { measured?: { from: string; to: string } | null; snapshots?: Array<{ at: string; playersAvailable?: boolean }> }
interface Recorded { at: string; status: number; agent: string; world?: { layer: string; items: number; bytes: number } }

interface S2Sources { playersSource?: string; resourceNodesSource?: string; factorySource?: string; trainsSource?: string; railsSource?: string; conveyorsSource?: string; mapMarkersSource?: string; droneRoutesSource?: string; powerLinesSource?: string; extractorsSource?: string; generatorsSource?: string; stationRatesSource?: string; storageSource?: string }

/**
 * The sliced readers that must have logged a step line: the node scan always, the S2 readers when the config made them native,
 * and the shared logistics pass ("Logistics read", one line for all layers) when rails, conveyors or powerLines is native (S3, Task 14;
 * powerLines rides that line in S4), plus the map-markers and drone-routes reads when those keys are native (S4-A, Task 16).
 */
export function requiredStepReaders(sources: S2Sources | undefined): string[] {
  const out = ["Resource node scan"];
  if (sources?.factorySource === "native") out.push("Factory read");
  if (sources?.trainsSource === "native") out.push("Trains read", "Train routes read");
  if (sources?.railsSource === "native" || sources?.conveyorsSource === "native" || sources?.powerLinesSource === "native") out.push("Logistics read");
  if (sources?.mapMarkersSource === "native") out.push("Map markers read");
  if (sources?.droneRoutesSource === "native") out.push("Drone routes read");
  // S4-B (Task 20): extractors, generators and storage share the one Buildables pass; the station walk rides "Trains read".
  if (sources?.extractorsSource === "native" || sources?.generatorsSource === "native" || sources?.storageSource === "native") out.push("Buildables read");
  return out;
}

/** A UE log line's UTC time, `[YYYY.MM.DD-HH.MM.SS:mmm]` (the harness's own tests also write the time with colons), or NaN. */
const LOG_TIME = /^\[(\d{4})\.(\d{2})\.(\d{2})-(\d{2})[.:](\d{2})[.:](\d{2})[:.](\d{3})\]/;
function logTimeMs(line: string): number {
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
  const sources = e.captures["mod-config.json"] as S2Sources | undefined;
  // S2 (Task 11): when the config carries the S2 keys (the run writes NATIVE_SOURCES_S2), both must be native, and the mod's own
  // log must say so too. S1-shaped evidence has neither key and is judged as before.
  const s2Shaped = sources?.factorySource !== undefined || sources?.trainsSource !== undefined;
  if (s2Shaped) {
    if (sources?.factorySource !== "native") extra.push(`the run did not write factorySource as native into the mod's config (it says ${String(sources?.factorySource)})`);
    if (sources?.trainsSource !== "native") extra.push(`the run did not write trainsSource as native into the mod's config (it says ${String(sources?.trainsSource)})`);
    extra.push(...factoryLogProblems(e.lines), ...trainsLogProblems(e.lines));
  }
  // S3 (Task 14): when the config carries a logistics key (the run writes NATIVE_SOURCES_S3), both must be native and the mod's
  // own log must say so. S1- and S2-shaped evidence has neither key and is judged as before. (A rails-only config still needs
  // the "Logistics read" line, see requiredStepReaders, but it is not a pass of this S3 run.)
  const s3Shaped = sources?.railsSource !== undefined || sources?.conveyorsSource !== undefined;
  if (s3Shaped) {
    if (sources?.railsSource !== "native") extra.push(`the run did not write railsSource as native into the mod's config (it says ${String(sources?.railsSource)})`);
    if (sources?.conveyorsSource !== "native") extra.push(`the run did not write conveyorsSource as native into the mod's config (it says ${String(sources?.conveyorsSource)})`);
    extra.push(...railsLogProblems(e.lines), ...conveyorsLogProblems(e.lines));
  }
  // S4-A (Task 16): when the config carries an S4 key (the run writes NATIVE_SOURCES_S4), all three must be native and the mod's own
  // log must say so; the powerLines layer rides the shared Logistics read line, which must have served it. Earlier shapes have none
  // of these keys and are judged as before.
  const s4Shaped = sources?.mapMarkersSource !== undefined || sources?.droneRoutesSource !== undefined || sources?.powerLinesSource !== undefined;
  if (s4Shaped) {
    if (sources?.mapMarkersSource !== "native") extra.push(`the run did not write mapMarkersSource as native into the mod's config (it says ${String(sources?.mapMarkersSource)})`);
    if (sources?.droneRoutesSource !== "native") extra.push(`the run did not write droneRoutesSource as native into the mod's config (it says ${String(sources?.droneRoutesSource)})`);
    if (sources?.powerLinesSource !== "native") extra.push(`the run did not write powerLinesSource as native into the mod's config (it says ${String(sources?.powerLinesSource)})`);
    extra.push(...mapMarkersLogProblems(e.lines), ...droneRoutesLogProblems(e.lines), ...powerLinesLogProblems(e.lines), ...logisticsLineProblems(e.lines, "powerLines"));
  }
  // S4-B (Task 20): an S4-B-shaped config (any of its four keys) must have all four native. Earlier shapes have none of them.
  const s4bShaped = sources?.extractorsSource !== undefined || sources?.generatorsSource !== undefined || sources?.stationRatesSource !== undefined || sources?.storageSource !== undefined;
  if (s4bShaped) {
    for (const k of ["extractorsSource", "generatorsSource", "stationRatesSource", "storageSource"] as const) {
      if (sources?.[k] !== "native") extra.push(`the run did not write ${k} as native into the mod's config (it says ${String(sources?.[k])})`);
    }
    extra.push(...extractorsLogProblems(e.lines), ...generatorsLogProblems(e.lines), ...stationRatesLogProblems(e.lines), ...stationRatesLineProblems(e.lines), ...storageLogProblems(e.lines));
  }
  if (sources?.playersSource !== "native" || sources?.resourceNodesSource !== "native") extra.push("the run did not write playersSource and resourceNodesSource as native into the mod's config");
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
  const required = requiredStepReaders(sources);
  extra.push(...stepProblems(e.lines, required));
  if (s2Shaped || s3Shaped || s4Shaped || s4bShaped) extra.push(...windowProblems(e.lines, required, from, to));
  if (extra.length === 0 && base.pass) {
    const steps = stepCosts(e.lines).map((c) => `${c.reader} longest step ${c.longestStepMs} ms`).join("; ");
    return { pass: true, reasons: [...base.reasons, `native readers live in the window: ${withPlayers} snapshots with players, ${nodePosts.length} resourceNodes post(s)`, describeScanCost(scans), `step ceiling ${MAX_NATIVE_STEP_MS} ms held (sliced native reads only): ${steps}`] };
  }
  // When the base verdict passes, its reasons describe what passed, so they are listed only on success.
  return { pass: false, reasons: [...(base.pass ? [] : base.reasons), ...extra, ...(scans.length ? [describeScanCost(scans)] : [])] };
}
