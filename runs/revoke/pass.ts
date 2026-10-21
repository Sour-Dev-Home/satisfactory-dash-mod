// The revoke run (spec "Testing" 3): the dashboard revokes the server, the mod halts within one status cycle, sends
// nothing more, deletes its credential, and enrols again with a new code.
// Re-enrolment, from the satisfactory-dash backend (controller and the dash session, 2026-10-20):
// - Re-enrolling replaces the credential on the same server: agents.agent_credentials is keyed by server
//   (migrations/1790899200000_agents.sql:32, `server_id uuid PRIMARY KEY`), and UPSERT_CREDENTIAL is
//   `INSERT ... ON CONFLICT (server_id) DO UPDATE SET secret_hash = ..., revoked_at = NULL`
//   (backend/src/modules/agents/repositories/agentRepository.ts:99-105). So the re-enrol half uses the same server
//   with a new code; no second server is seeded.
// - A new code may be issued after a revoke (createEnrollmentCode, services/agentsService.ts:64-81: it deletes any
//   open code, 10 per user per minute).
// - Revoking deletes any open code (agentsService.ts:107), so the drive issues the re-enrol code after the revoke.
// - The old secret fails on its very next request: findActiveAgent requires `revoked_at IS NULL`
//   (agentRepository.ts:111-116).
// The halt line is Agent.cpp's Halted entry ("The backend rejected the credential (401): ..."). "Within one cycle":
// one status cycle is 5 s, plus the one request in flight, so at most 15 s from the revoke. The halt's time is when the
// harness first saw the line (it reads the log every second), which is never earlier than the line itself.
import type { CoreEvidence } from "../../harness/src/core-run";
import type { RecordedCall } from "../../harness/src/recorder";
import type { Verdict } from "../../harness/src/verdict";

export const requirements = ["revoke", "S9"];
export const HALT_LINE = "The backend rejected the credential (401)";
export const HALT_WITHIN_MS = 15_000;
const NEED_AFTER = 3;
const ENROLL = "/agent/v1/enroll";

export interface Revoke {
  /** Taken just before the revoke was sent (so the 15 s count is never short). */
  revokedAt: string;
  /** When the harness saw each halt line, in order. */
  halts: string[];
  /** When the new code was seeded; null when the run never got there. */
  reseededAt: string | null;
  /** What reached the recorder from revokedAt until reseededAt (bodies dropped). */
  afterRevoke: RecordedCall[];
  /** Whether credential.json existed after the halt; null when the run never checked. */
  credentialAfterHalt: boolean | null;
  /** The first enrolment after the new code (null: none) and the snapshots accepted after it. */
  reenrolled: { enrolStatus: number | null; acceptedAfter: number };
}

export function evaluate(e: CoreEvidence): Verdict {
  const c = e.captures["revoke.json"] as Revoke | undefined;
  if (!c) return { pass: false, reasons: ["no revoke capture"] };
  const reasons: string[] = [];
  const revokedAt = Date.parse(c.revokedAt);

  const inLog = e.lines.filter((l) => l.includes(HALT_LINE)).length;
  if (inLog !== 1) reasons.push(`the halt line appears ${inLog} times in the log, not once`);
  let haltMs: number | null = null;
  if (!c.halts.length) reasons.push("the harness never saw the halt line");
  else {
    if (c.halts.length !== 1) reasons.push(`the harness saw the halt line ${c.halts.length} times, not once`);
    haltMs = Date.parse(c.halts[0]!) - revokedAt;
    if (!Number.isFinite(haltMs)) reasons.push("the halt or the revoke has no valid time");
    else if (haltMs < 0) reasons.push(`the halt line came ${-haltMs} ms before the revoke`);
    else if (haltMs > HALT_WITHIN_MS) reasons.push(`the halt line came ${haltMs} ms after the revoke, more than ${HALT_WITHIN_MS} ms`);
  }

  // After the first 401, nothing but a new enrolment may reach the recorder (snapshots, polls, results, anything else).
  const until = c.reseededAt === null ? Infinity : Date.parse(c.reseededAt);
  const mod = c.afterRevoke.filter((r) => r.agent === "mod" && Date.parse(r.at) >= revokedAt && Date.parse(r.at) < until);
  const first401 = mod.filter((r) => r.status === 401).map((r) => r.at).sort()[0];
  let afterCount = 0;
  if (first401 === undefined) reasons.push("no request was refused with 401 after the revoke");
  else {
    const after = mod.filter((r) => r.at > first401 && r.path !== ENROLL);
    afterCount = after.length;
    if (after.length) reasons.push(`${after.length} request(s) reached the recorder after the first 401: ${after.slice(0, 3).map((r) => `${r.method} ${r.path} at ${r.at}`).join(", ")}`);
  }

  if (c.credentialAfterHalt === null) reasons.push("credential.json was never checked after the halt");
  else if (c.credentialAfterHalt) reasons.push("credential.json is still there after the halt");

  if (c.reseededAt === null) reasons.push("the new code was never seeded");
  const r = c.reenrolled;
  if (r.enrolStatus === null) reasons.push("no enrolment request after the new code");
  else if (r.enrolStatus !== 201) reasons.push(`the re-enrolment answered ${r.enrolStatus}, not 201`);
  if (r.acceptedAfter < NEED_AFTER) reasons.push(`only ${r.acceptedAfter} snapshot(s) accepted after the re-enrolment, not ${NEED_AFTER}`);

  return reasons.length
    ? { pass: false, reasons }
    : {
        pass: true,
        reasons: [
          `halted once, ${haltMs} ms after the revoke; ${mod.length} mod request(s) after the revoke, ${afterCount} request(s) after the first 401 (${first401}); ` +
            `credential.json removed; re-enrolled (201) on the same server with ${r.acceptedAfter} snapshot(s) accepted after it`,
        ],
      };
}
