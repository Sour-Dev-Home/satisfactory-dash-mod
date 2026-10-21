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
// Controller ruling (2026-10-20, review of 9b23fd1), ratified by the owner (2026-10-21, final review of Plan 2B):
// "after the first 401" is judged from when that 401 was delivered
// (the recorder's doneAt), not from when it was sent. The mod learns of a 401 only when the transport's answer is
// dispatched on the core ticker (CurlTransport.cpp), and the push and the poll/report run independently, so a second
// request with the revoked credential can already be on its way. HALT_DISPATCH_GRACE_MS (one ticker dispatch, with
// margin for a hitch) bounds that: after the first 401's doneAt, a request other than an enrolment fails the run unless
// it started inside the grace AND was itself answered 401. Nothing may reach the backend successfully. The halting 401
// is the earliest delivered 401 after the revoke, including one whose request started just before revokedAt.
import type { CoreEvidence } from "../../harness/src/core-run";
import type { RecordedCall } from "../../harness/src/recorder";
import type { Verdict } from "../../harness/src/verdict";

export const requirements = ["revoke", "S9"];
export const HALT_LINE = "The backend rejected the credential (401)";
export const HALT_WITHIN_MS = 15_000;
/** How long after the first 401 was delivered a request already scheduled may still start (see the header). */
export const HALT_DISPATCH_GRACE_MS = 1000;
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

  // After the first 401 was delivered, nothing but a new enrolment may reach the recorder (snapshots, polls, results,
  // anything else), except a request already scheduled: inside the grace, and itself refused with 401.
  const until = c.reseededAt === null ? Infinity : Date.parse(c.reseededAt);
  // Answered after the revoke (a request that started just before it can carry the halting 401), and started before
  // the new code.
  const mod = c.afterRevoke.filter((r) => r.agent === "mod" && Date.parse(r.doneAt) >= revokedAt && Date.parse(r.at) < until);
  const first401 = mod.filter((r) => r.status === 401).sort((a, b) => Date.parse(a.doneAt) - Date.parse(b.doneAt))[0];
  const show = (r: RecordedCall) => `${r.method} ${r.path} answered ${r.status} at ${r.at}`;
  let inGrace: RecordedCall[] = [];
  if (first401 === undefined) reasons.push("no request was refused with 401 after the revoke");
  else {
    const bound = Date.parse(first401.doneAt) + HALT_DISPATCH_GRACE_MS;
    const later = mod.filter((r) => r !== first401 && r.path !== ENROLL && Date.parse(r.at) >= Date.parse(first401.at));
    const beyond = later.filter((r) => Date.parse(r.at) > bound);
    inGrace = later.filter((r) => Date.parse(r.at) <= bound);
    const notRefused = inGrace.filter((r) => r.status !== 401);
    if (beyond.length) reasons.push(`${beyond.length} request(s) started more than ${HALT_DISPATCH_GRACE_MS} ms after the first 401 was delivered (${first401.doneAt}): ${beyond.slice(0, 3).map(show).join(", ")}`);
    if (notRefused.length) reasons.push(`${notRefused.length} request(s) inside the grace were not refused with 401: ${notRefused.slice(0, 3).map(show).join(", ")}`);
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
          `halted once, ${haltMs} ms after the revoke; ${mod.length} mod request(s) answered after the revoke, first 401 delivered at ${first401!.doneAt}; ` +
            `${inGrace.length} request(s) in flight inside the ${HALT_DISPATCH_GRACE_MS} ms grace, all answered 401${inGrace.length ? ` (${inGrace.map(show).join(", ")})` : ""}, none after it; ` +
            `credential.json removed; re-enrolled (201) on the same server with ${r.acceptedAfter} snapshot(s) accepted after it`,
        ],
      };
}
