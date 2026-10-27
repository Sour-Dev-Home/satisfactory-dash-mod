// The one-off S3 census (ADR-0058 Sprint S3, Task 10): the staged Test3 world is booted with the logistics layers native, and the
// mod's first Logistics pass is read for what only the running game says: the belt/lift GetSpeed() unit, tier display names, pipe
// flow limits and the drone-port ends. It judges no FRM data. Pass = a `Logistics read` line with `layers rails=1 conveyors=1`
// and at least one census line; every value is printed in the reasons and none is judged, except that the belt/lift speeds are
// shown next to the expected 60/120/270/480 items a minute (speed / 2) so the owner reads the unit at a glance.
import type { CoreEvidence } from "../../harness/src/core-run";
import { censusLines, logisticsRead, type CensusTier } from "../../harness/src/reader-lines";
import type { Verdict } from "../../harness/src/verdict";

export const requirements = ["R6-census"];

/** Items a minute by tier, Mk.1 to Mk.4, for belts and lifts (owner's survey). */
export const EXPECTED_BELT_RATE: Record<string, number> = { "Mk.1": 60, "Mk.2": 120, "Mk.3": 270, "Mk.4": 480 };
/** m3 a minute by pipe tier. */
export const EXPECTED_PIPE_FLOW: Record<string, number> = { "Mk.1": 300, "Mk.2": 600 };

/** Mirrors `WorldSenderCensusMaxLines` (mod/SatisDashAgent/Source/SatisDashCore/Private/WorldSender.cpp:15): the mod logs at most this many tier lines. */
export const CENSUS_MAX_LINES = 16;

const TIER = /\b(Mk\.[1-4])\s*$/;

function tierReason(t: CensusTier): string {
  const head = `${t.kind} "${t.name}": speed ${t.speed}, count ${t.count}`;
  const mk = TIER.exec(t.name)?.[1];
  if (!mk) return head;
  if (t.kind === "pipe") {
    const want = EXPECTED_PIPE_FLOW[mk];
    if (want === undefined) return head;
    const flow = t.speed * 60;
    return `${head}; flow x 60 = ${flow} ${flow === want ? "match" : "NO MATCH"} (expected ${want})`;
  }
  const want = EXPECTED_BELT_RATE[mk]!;
  const rate = t.speed / 2;
  return `${head}; speed / 2 = ${rate} ${rate === want ? "match" : "NO MATCH"} (expected ${want})`;
}

export function evaluate(e: CoreEvidence): Verdict {
  const logistics = logisticsRead(e.lines);
  const census = censusLines(e.lines);
  const failures: string[] = [];
  if (!logistics) failures.push('the mod\'s log has no "Logistics read (game thread only):" line: the first logistics pass did not complete');
  else if (logistics.rails !== 1 || logistics.conveyors !== 1) failures.push(`the Logistics read line says layers rails=${logistics.rails} conveyors=${logistics.conveyors}, not rails=1 conveyors=1: the layers were not both native`);
  if (census.tiers.length === 0 && census.kinds === null && census.ports === null) failures.push("the mod's log has no conveyor census line");
  if (failures.length) return { pass: false, reasons: failures };
  const reasons = [
    `Logistics read: ${JSON.stringify(logistics)}`,
    ...(census.tiers.length >= CENSUS_MAX_LINES ? [`${census.tiers.length} tier census lines is the mod's cap (WorldSenderCensusMaxLines = ${CENSUS_MAX_LINES}): the census may be truncated (report only)`] : []),
    ...census.tiers.map(tierReason),
    census.kinds ? `kinds: ${census.kinds.checked} ends checked, ${census.kinds.differ} differ from the class table (first classes: ${census.kinds.classes.join(", ") || "none"})` : "kinds: no line",
    census.ports ? `drone ports: ${census.ports.into} edges end at a port, ${census.ports.outOf} start at one` : "drone ports: no line",
  ];
  return { pass: true, reasons };
}
