// The tick-cost run with sprint S1's native readers on (ADR-0058 Decision 3 item 3). The limits are the existing tick-cost
// run's, imported and unchanged (runs/tick-cost/pass.ts: C4, S12, same save, memory). Added, because "the reader was on" is
// part of the claim: the mod's config had playersSource and resourceNodesSource native; a snapshot in the measured window
// carried an available players part; and a resourceNodes world post was accepted inside the measured window after the warmup
// (so the actor scan ran while the ticks were measured). The scan's own cost is the mod's log line in result.json's modLines.
import type { CoreEvidence } from "../../harness/src/core-run";
import { TICK_WARMUP_MS } from "../../harness/src/ticks";
import { MAX_NATIVE_STEP_MS, describeScanCost, nodesLogProblems, playersLogProblems, scanCosts, stepCosts, stepProblems } from "../../harness/src/reader-lines";
import type { Verdict } from "../../harness/src/verdict";
import * as Base from "../tick-cost/pass";

export const requirements = [...Base.requirements, "R0-R1-R4-tick-cost"];

interface Parts { measured?: { from: string; to: string } | null; snapshots?: Array<{ at: string; playersAvailable?: boolean }> }
interface Recorded { at: string; status: number; agent: string; world?: { layer: string; items: number; bytes: number } }

interface S2Sources { playersSource?: string; resourceNodesSource?: string; factorySource?: string; trainsSource?: string }

/** The sliced readers that must have logged a step line: the node scan always, the S2 readers when the config made them native. */
function requiredStepReaders(sources: S2Sources | undefined): string[] {
  const out = ["Resource node scan"];
  if (sources?.factorySource === "native") out.push("Factory read");
  if (sources?.trainsSource === "native") out.push("Trains read", "Train routes read");
  return out;
}

export function evaluate(e: CoreEvidence): Verdict {
  const base = Base.evaluate(e);
  const extra: string[] = [];
  const sources = e.captures["mod-config.json"] as { playersSource?: string; resourceNodesSource?: string } | undefined;
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
  // native scan must have completed. Its cost goes into the reasons, recorded and not judged (no ceiling, Decision 3 item 3).
  extra.push(...playersLogProblems(e.lines), ...nodesLogProblems(e.lines));
  const scans = scanCosts(e.lines);
  // The 2 ms step ceiling (owner, 2026-10-26): a new condition, not a change to the limits above. It covers the sliced native
  // reads only (node scan, factory, trains, train routes); one-shot reads (status, auto-pause, players, power's circuit-id probe)
  // are S0/S1 code outside this rule.
  extra.push(...stepProblems(e.lines, requiredStepReaders(sources)));
  if (extra.length === 0 && base.pass) {
    const steps = stepCosts(e.lines).map((c) => `${c.reader} longest step ${c.longestStepMs} ms`).join("; ");
    return { pass: true, reasons: [...base.reasons, `native readers live in the window: ${withPlayers} snapshots with players, ${nodePosts.length} resourceNodes post(s)`, describeScanCost(scans), `step ceiling ${MAX_NATIVE_STEP_MS} ms held (sliced native reads only): ${steps}`] };
  }
  // When the base verdict passes, its reasons describe what passed, so they are listed only on success.
  return { pass: false, reasons: [...(base.pass ? [] : base.reasons), ...extra, ...(scans.length ? [describeScanCost(scans)] : [])] };
}
