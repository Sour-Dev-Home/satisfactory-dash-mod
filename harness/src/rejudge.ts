// Re-applies a spike's current pass rule to a downloaded bundle, when the owner amends a rule after a run (S6, owner
// 2026-10-20). The bundle's result.json is left as the run wrote it; rejudge.json beside it records both verdicts and
// the commit of the rule, so the evidence stays harness-written.
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { SPIKES } from "./run";
import type { Evidence, SpikeId, Verdict } from "./verdict";

/** The spikes whose whole evidence is in their bundle, and the capture each needs. */
const CAPTURE: Partial<Record<SpikeId, string>> = { S3: "power.json", S6: "status.json" };

export interface Rejudged {
  spike: SpikeId;
  claim: string;
  rejudgedAt: string;
  ruleCommit: string;
  original: { pass: boolean; reasons: string[] };
  verdict: Verdict;
}

export function rejudge(dir: string, opts: { now: Date; commit: string }): Rejudged {
  const result = JSON.parse(readFileSync(join(dir, "result.json"), "utf8")) as { spike: SpikeId; claim: string; pass: boolean; reasons: string[]; modLines: string[] };
  const capture = CAPTURE[result.spike];
  const spec = SPIKES[result.spike];
  if (!capture || !spec) throw new Error(`${result.spike} can't be re-judged from its bundle`);
  const file = join(dir, capture);
  if (!existsSync(file)) throw new Error(`the bundle has no ${capture}`);
  const data = JSON.parse(readFileSync(file, "utf8"));
  const evidence: Evidence = {
    modLines: result.modLines,
    echo: [],
    ...(result.spike === "S3" ? { power: data } : { status: { pairs: data } }),
  };
  const out: Rejudged = {
    spike: result.spike,
    claim: result.claim,
    rejudgedAt: opts.now.toISOString(),
    ruleCommit: opts.commit,
    original: { pass: result.pass, reasons: result.reasons },
    verdict: spec.evaluate(evidence),
  };
  writeFileSync(join(dir, "rejudge.json"), JSON.stringify(out, null, 2));
  return out;
}
