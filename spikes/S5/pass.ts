// S5 (claim C7): the mod enrols with a one-time code and pushes one valid snapshot to a throwaway local backend.
import type { Evidence, Verdict } from "../../harness/src/verdict";
import { S5_AGENT_VERSION } from "./body";
export { S5_AGENT_VERSION, s5SnapshotBody } from "./body";

export const claim = "C7";
const LINE = /satis-mod s5 enroll=(-?\d+) snapshot=(-?\d+)/;

export function evaluate(e: Evidence): Verdict {
  const m = e.modLines.map((l) => LINE.exec(l)).find((x) => x !== null);
  if (!m) return { pass: false, reasons: ['no "satis-mod s5" line in this run\'s server log'] };
  if (!e.backend) return { pass: false, reasons: ["the harness read no backend state"] };
  const b = e.backend;
  const reasons: string[] = [];
  if (m[1] !== "201") reasons.push(`enrolment answered ${m[1]}, not 201`);
  if (m[2] !== "200") reasons.push(`the snapshot answered ${m[2]}, not 200`);
  if (!b.codeConsumed) reasons.push("the database still shows the code unspent");
  if (b.agentVersion !== S5_AGENT_VERSION) reasons.push(`the credential's agent_version is ${b.agentVersion}, not ${S5_AGENT_VERSION}`);
  if (!b.lastSeen) reasons.push("last_seen_at was never set: no snapshot reached the agent route");
  if (b.codeInLog) reasons.push("the enrolment code appears in the server log");
  if (b.bearerInLog) reasons.push('"Bearer" appears in the server log');
  return reasons.length
    ? { pass: false, reasons }
    : { pass: true, reasons: ["the mod enrolled (201), its snapshot was accepted (200), and the database agrees"] };
}
