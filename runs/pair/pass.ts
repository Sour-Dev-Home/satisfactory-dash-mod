// The pair run (spec 2026-11-09 §4, plan Task 8): the mod boots with a loopback cfg and no code, opens a pairing against the
// harness's recorder, prints the code, and the harness approves it as the dashboard's operator. The mod then saves a credential
// and its snapshots are accepted. Judged from the recorder's calls (no bodies), the code line and the Paired line.
// Ruling (plan Task 8): the capture carries `codeCount`, the number of distinct codes in the log, so "one start per code" is
// starts <= codeCount and starts >= 1. The pairing secret is never in a capture: `secretSeen` holds booleans only.
import type { CoreEvidence } from "../../harness/src/core-run";
import type { Verdict } from "../../harness/src/verdict";

export const requirements = ["backend", "pair"];
export const CODE_WITHIN_MS = 30_000;
export const SNAPSHOT_WITHIN_MS = 60_000;
/** The 5 s poll interval less jitter. */
export const MIN_POLL_GAP_MS = 4_500;
export const PAIRED_LINE = /^.*Paired with the dashboard; reporting to the backend( \(approved by ".{1,64}"\))?$/;
const START = "/agent/v1/pairings";
const POLL = "/agent/v1/pairings/poll";
const SNAPSHOTS = "/agent/v1/snapshots";

export interface PairCall { path: string; status: number; at: string; doneAt: string }
export interface Pair {
  /** When the run wrote the recorder's URL into the cfg (the boot cfg is loopback), ISO. */
  bootAt: string;
  /** The first "Pairing code " log line, as the harness first saw it. */
  codeLineAt: string | null;
  userCode: string | null;
  /** How many distinct codes the log shows. */
  codeCount: number;
  approvedAt: string | null;
  approveStatus: number | null;
  /** The "Paired with the dashboard" log line, as logged. */
  pairedLine: string | null;
  credentialWritten: boolean;
  /** The mod's calls to the backend, from the recorder, no bodies. */
  calls: PairCall[];
  /** inHeader is real: the recorder keeps header values in memory (keepPairingSecrets) and the drive searches them. */
  secretSeen: { inLog: boolean; inUrl: boolean; inHeader: boolean };
}

const ms = (iso: string) => Date.parse(iso);

export function evaluate(e: CoreEvidence): Verdict {
  const c = e.captures["pair.json"] as Pair | undefined;
  if (!c) return { pass: false, reasons: ["no pair capture"] };
  const reasons: string[] = [];
  const boot = ms(c.bootAt);
  if (c.codeLineAt === null) reasons.push("no code line (\"Pairing code ...\") in the log");
  else if (ms(c.codeLineAt) - boot > CODE_WITHIN_MS) reasons.push(`the code line came ${Math.round((ms(c.codeLineAt) - boot) / 1000)} s after boot, need within ${CODE_WITHIN_MS / 1000} s`);

  const starts = c.calls.filter((x) => x.path === START);
  if (starts.length < 1) reasons.push("the mod never started a pairing");
  else if (starts.length > c.codeCount) reasons.push(`${starts.length} pairing starts for ${c.codeCount} code(s): more than one start per code`);

  const polls = c.calls.filter((x) => x.path === POLL).map((x) => ms(x.at)).sort((a, b) => a - b);
  for (let i = 1; i < polls.length; i++) {
    if (polls[i]! - polls[i - 1]! < MIN_POLL_GAP_MS) {
      reasons.push(`polls ${((polls[i]! - polls[i - 1]!) / 1000).toFixed(1)} s apart, need at least ${MIN_POLL_GAP_MS / 1000} s`);
      break;
    }
  }

  if (c.approveStatus === null || c.approveStatus < 200 || c.approveStatus > 299) reasons.push(`the approval answered ${c.approveStatus ?? "nothing"}, need 2xx`);

  const firstSnapshot = c.calls.filter((x) => x.path === SNAPSHOTS && x.status === 200).sort((a, b) => ms(a.at) - ms(b.at))[0];
  if (!firstSnapshot) reasons.push("no accepted snapshot");
  else {
    if (c.approvedAt === null) reasons.push("no approval time recorded");
    else if (ms(firstSnapshot.doneAt) - ms(c.approvedAt) > SNAPSHOT_WITHIN_MS) {
      reasons.push(`the first accepted snapshot came ${Math.round((ms(firstSnapshot.doneAt) - ms(c.approvedAt)) / 1000)} s after the approval, need within ${SNAPSHOT_WITHIN_MS / 1000} s`);
    }
    const late = c.calls.filter((x) => x.path.startsWith(START) && ms(x.at) > ms(firstSnapshot.at)).length;
    if (late) reasons.push(`${late} /agent/v1/pairings* call(s) started after the first accepted snapshot`);
  }

  if (!c.credentialWritten) reasons.push("no credential.json was written");
  if (c.pairedLine === null || !PAIRED_LINE.test(c.pairedLine)) reasons.push("no well-formed \"Paired with the dashboard\" line in the log");
  const s = c.secretSeen;
  if (s.inLog || s.inUrl || s.inHeader) {
    reasons.push(`the pairing secret was seen in ${[s.inLog && "the log", s.inUrl && "a URL", s.inHeader && "a header"].filter(Boolean).join(", ")}`);
  }
  if (reasons.length) return { pass: false, reasons };
  return { pass: true, reasons: [`paired: code line ${Math.round((ms(c.codeLineAt!) - boot) / 1000)} s after boot, ${starts.length} start(s), ${polls.length} poll(s), first snapshot accepted ${Math.round((ms(firstSnapshot!.doneAt) - ms(c.approvedAt!)) / 1000)} s after the approval; the pairing secret never in the log, a URL or a header`] };
}
