// The parity run (spec "Testing" 2, Plan 2B Task 9): the mod and satisfactory-dash's Node agent read the same test server
// side by side for 30 minutes, each enrolled as its own server in the throwaway backend, and what each one got accepted
// must agree.
//
// The dash session's answers (2026-10-21, checked against satisfactory-dash main 81f22aef; Step 1 of the task):
//  1. The snapshots the recording proxy saw answered 200 are the right "stored snapshots": the backend has no route that
//     reads raw snapshots back, and keeps only the latest snapshot per server, in memory.
//  2. The Node agent needs a REAL game API token: a made-up one gets 401 even with AllowInsecureLocalAccess. The run makes
//     one with `server.GenerateAPIToken` through the HTTPS API's RunCommand on loopback (node-agent.ts says where that is
//     from), pipes "<token>\n<FRM token or empty>\n" into `agent set-tokens --host 127.0.0.1 --api-port <api>
//     --frm-port <frm>`, and prunes the server's application tokens at the end (`server.InvalidateAPITokens`). The agent
//     stores secrets with DPAPI for the Windows user that runs it, so set-tokens, check, enroll and run all run as
//     satis-runner, inside the run. The token is never logged or captured, and is searched for in the server log like the
//     agent secret (S8).
//  3. Build: in the satisfactory-dash checkout, `npm ci`, then `npm run build -w agent` makes `agent/dist/agent.cjs`.
//     Order: set-tokens, check, enroll <CODE> --url <backend>, run. SD_AGENT_HOME points at a throwaway folder in the
//     run's temp space.
//  4. The agent checks the 12-character code itself, so it is built from the same commit as the test backend: 81f22aef
//     (the contract is vendored from there too, PR #39). The plan's ab4626f is superseded.
//
// Status and auto-pause, nearest pair: each accepted mod snapshot goes with the Node agent's accepted snapshot nearest in
// observedAt, kept only when they are at most PAIR_GAP_MS apart (half the 5 s status cycle). Judged, never widened
// (CLAUDE.md rule 3):
//  - at least NEED_PAIRS pairs (30 * 60 / 5 * 0.8);
//  - in every pair: status exact for running, paused, players, limit, session hash and tick health; S6's tick-rate rule;
//    duration within 10 s; settings.autoPause exact;
//  - a part only one agent sent in a pair isn't compared there, but status and autoPause must each be compared in at
//    least NEED_PAIRS pairs, so missing data never passes.
//
// Power, the neighbours' bracket. Owner decision (2026-10-21, in the coordinator session, on the review of 0dee72a, which
// expected the nearest-pair 1% rule to fail on timing alone: consumption moved ~1.2% within a few hundred ms in enrol run
// 39517898558, and pairs may be 2.5 s apart). As the enrol run brackets the mod with FRM:
//  - for each accepted mod snapshot with power (a round), the Node agent's accepted snapshots with power that are the
//    last one at or before the mod's observedAt and the first one at or after it, each within PARITY_NEIGHBOUR_GAP_MS
//    of it. A missing neighbour on either side fails the round;
//  - each power field, per circuit group, must lie in [lo - tol, hi + tol], lo and hi the two Node values,
//    tol = 0.01 * max(|lo|, |hi|, 1): S3's 1% stays. Group ids and the fuse must match at least one neighbour, and only
//    the neighbour(s) whose group ids match bracket the values. A group's several rows (a joined group) are one row,
//    which they must all agree on;
//  - power must be compared in at least NEED_PAIRS rounds; a round where both sides have no circuit group doesn't count.
//
// Also: every snapshot either agent posted in the window was accepted, and every accepted one was readable (a post over
// the recorder's 2 MB cap is kept without its bulky parts and counted, never dropped); both servers enrolled (code
// consumed, seen), the mod as mod/0.1.0; a window of at least 30 minutes; the run's game API token invalidated; the
// Node agent stopped and its SD_AGENT_HOME deleted.
import type { CoreEvidence } from "../../harness/src/core-run";
import type { Verdict } from "../../harness/src/verdict";

export const requirements = ["parity"];
export const PAIR_GAP_MS = 2500;
export const WINDOW_MINUTES = 30;
const STATUS_SECONDS = 5; // FCadence::StatusSeconds (Responses.h:35); the Node agent's INITIAL_CADENCE (agentRunner.ts:14)
export const NEED_PAIRS = Math.ceil(((WINDOW_MINUTES * 60) / STATUS_SECONDS) * 0.8);
/** How far from the mod's sample each Node power neighbour may be (owner, 2026-10-21; see the header). */
export const PARITY_NEIGHBOUR_GAP_MS = 5000;
const DURATION_DRIFT = 10; // S6
const tickClose = (a: number, b: number) => Math.abs(a - b) <= Math.max(2, 0.1 * Math.max(a, b)); // S6
/** S3's 1% around the range the two neighbours span, as enrol's bracket. */
const inBracket = (v: number, lo: number, hi: number) => {
  const tol = 0.01 * Math.max(Math.abs(lo), Math.abs(hi), 1);
  return lo - tol <= v && v <= hi + tol;
};

export interface Status {
  isGameRunning: boolean; gamePaused: boolean; connectedPlayers: number; playerLimit: number;
  totalGameDurationSeconds: number; tickRate: number; tickHealth: string; sessionName: string;
}
export interface Circuit {
  circuitGroupId: number; productionMW: number; consumptionMW: number; capacityMW: number; maxConsumptionMW: number;
  fuseTriggered: boolean; batteryCapacityMWh: number; batteryPercent: number; batteryDifferentialMW: number;
}
/** An accepted snapshot as captured: when the recorder got it, and only the parts the judge reads (session hashed). */
export interface Snap { at: string; observedAt: string; status?: Status; settings?: { autoPause: boolean }; power?: { circuits: Circuit[] } }
export interface Posted { at: string; status: number }
export interface ServerState { codeConsumed: boolean; agentVersion: string | null; lastSeen: boolean; revoked: boolean }
export interface Parity {
  windowStartedAt: string | null;
  windowEndedAt: string | null;
  /** The mod's accepted snapshots in the window. */
  mod: Snap[];
  /** The Node agent's accepted snapshots in the window and a little either side of it (the power neighbours). */
  node: Snap[];
  /** How many snapshot posts each agent had accepted in those spans, readable or not. */
  accepted: { mod: number; node: number };
  /** Accepted posts over the recorder's 2 MB cap: kept with status, settings and power only (recorder.ts). */
  oversizedAccepted: { mod: number; node: number };
  /** Accepted posts whose body the recorder couldn't read at all. */
  unrecordedAccepted: { mod: number; node: number };
  /** Snapshot posts in those spans that weren't answered 200. */
  refused: { mod: Posted[]; node: Posted[] };
  state: { mod: ServerState | null; node: ServerState | null };
  /** Whether server.InvalidateAPITokens ran at the end; null when no token was made or it never ran. */
  tokensInvalidated: boolean | null;
  /** Evidence: what ran as the Node agent (agent.cjs's sha256) and the contract pin it should be built from. */
  nodeAgent: { agentCjsSha256: string | null; pin: string } | null;
  /** How the Node agent's stop went; null when it never started. */
  cleanup: { stopped: boolean; homeLeft: boolean } | null;
  /** Evidence only (the owner's memory goal): the Node agent's own process tree at the end of the window. */
  nodeAgentMemory?: unknown;
}
export interface Pair { mod: Snap; node: Snap; gapMs: number }

const STATUS_KEYS = ["isGameRunning", "gamePaused", "connectedPlayers", "playerLimit", "totalGameDurationSeconds", "tickRate", "tickHealth", "sessionName"] as const;
const CIRCUIT_KEYS = ["circuitGroupId", "productionMW", "consumptionMW", "capacityMW", "maxConsumptionMW", "fuseTriggered", "batteryCapacityMWh", "batteryPercent", "batteryDifferentialMW"] as const;
const pick = <K extends string>(o: Record<string, unknown>, keys: readonly K[]) => Object.fromEntries(keys.filter((k) => k in o).map((k) => [k, o[k]]));
const isObject = (v: unknown): v is Record<string, unknown> => typeof v === "object" && v !== null && !Array.isArray(v);

/** The capture's form of a recorded snapshot body: no players, factory or map, nothing but ids, MW and the status. */
export function trimSnapshot(at: string, body: unknown): Snap | null {
  if (!isObject(body) || typeof body.observedAt !== "string") return null;
  const s: Snap = { at, observedAt: body.observedAt };
  if (isObject(body.status)) s.status = pick(body.status, STATUS_KEYS) as unknown as Status;
  if (isObject(body.settings) && typeof body.settings.autoPause === "boolean") s.settings = { autoPause: body.settings.autoPause };
  if (isObject(body.power) && Array.isArray(body.power.circuits)) {
    s.power = { circuits: body.power.circuits.filter(isObject).map((c) => pick(c, CIRCUIT_KEYS) as unknown as Circuit) };
  }
  return s;
}

const ms = (iso: string) => Date.parse(iso);
const unique = (xs: Snap[]) => {
  const seen = new Set<string>();
  return xs.filter((x) => Number.isFinite(ms(x.observedAt)) && !seen.has(x.observedAt) && (seen.add(x.observedAt), true))
    .sort((a, b) => ms(a.observedAt) - ms(b.observedAt));
};

/** Each mod snapshot with the Node agent's nearest in observedAt, when at most PAIR_GAP_MS away; a repeat counts once. */
export function pairSnapshots(mod: Snap[], node: Snap[]): Pair[] {
  const nodes = unique(node);
  const pairs: Pair[] = [];
  for (const m of unique(mod)) {
    let best: Snap | undefined;
    let gap = Infinity;
    for (const n of nodes) {
      const d = Math.abs(ms(n.observedAt) - ms(m.observedAt));
      if (d < gap) [best, gap] = [n, d];
    }
    if (best && gap <= PAIR_GAP_MS) pairs.push({ mod: m, node: best, gapMs: gap });
  }
  return pairs;
}

/** One row per group; a group whose rows disagree (exactly, as frm.ts parseGetPowerFull) is reported. */
function byGroup(circuits: Circuit[], disagree: (id: number) => string): { groups: Map<number, Circuit>; reasons: string[] } {
  const groups = new Map<number, Circuit>();
  const reasons: string[] = [];
  for (const c of circuits) {
    const seen = groups.get(c.circuitGroupId);
    if (!seen) groups.set(c.circuitGroupId, c);
    else if (JSON.stringify(seen) !== JSON.stringify(c) && !reasons.includes(disagree(c.circuitGroupId))) reasons.push(disagree(c.circuitGroupId));
  }
  return { groups, reasons };
}
const groupIds = (g: Map<number, Circuit>) => [...g.keys()].sort((x, y) => x - y).join(",");
const POWER_FIELDS = ["productionMW", "consumptionMW", "capacityMW", "maxConsumptionMW", "batteryCapacityMWh", "batteryPercent", "batteryDifferentialMW"] as const;

/**
 * One power round (header, owner 2026-10-21): the mod's sample against the Node agent's power readings at or before and
 * at or after it, each within PARITY_NEIGHBOUR_GAP_MS. compared: both neighbours were there and their bracket was
 * applied; counted: compared, with at least one circuit group (what the NEED_PAIRS minimum counts).
 */
export function comparePower(mod: Snap, node: Snap[]): { reasons: string[]; compared: boolean; counted: boolean } {
  const at = mod.observedAt;
  const t = ms(at);
  const none = { reasons: [] as string[], compared: false, counted: false };
  if (!mod.power || !Number.isFinite(t)) return none;
  const readings = unique(node.filter((n) => n.power));
  const before = readings.filter((n) => ms(n.observedAt) <= t && t - ms(n.observedAt) <= PARITY_NEIGHBOUR_GAP_MS).at(-1);
  const after = readings.find((n) => ms(n.observedAt) >= t && ms(n.observedAt) - t <= PARITY_NEIGHBOUR_GAP_MS);
  const missing: string[] = [];
  if (!before) missing.push(`${at}: no Node agent power reading at or before the mod's sample, within ${PARITY_NEIGHBOUR_GAP_MS} ms`);
  if (!after) missing.push(`${at}: no Node agent power reading at or after the mod's sample, within ${PARITY_NEIGHBOUR_GAP_MS} ms`);
  if (!before || !after) return { ...none, reasons: missing };

  const m = byGroup(mod.power.circuits, (id) => `${at}: the mod's rows for group ${id} disagree`);
  const b = byGroup(before.power!.circuits, (id) => `${at}: the Node agent's rows for group ${id} disagree (before)`);
  const a = byGroup(after.power!.circuits, (id) => `${at}: the Node agent's rows for group ${id} disagree (after)`);
  const rows = [...m.reasons, ...b.reasons, ...a.reasons];
  if (rows.length) return { ...none, reasons: rows };

  const modIds = groupIds(m.groups);
  // Only the neighbour(s) whose group ids match bracket the values.
  const reads = [b.groups, a.groups].filter((g) => groupIds(g) === modIds);
  if (!reads.length) {
    const nodeIds = groupIds(b.groups) === groupIds(a.groups) ? groupIds(b.groups) : `${groupIds(b.groups)} before, ${groupIds(a.groups)} after`;
    return { reasons: [`${at}: circuit groups mod ${modIds} vs node ${nodeIds}`], compared: true, counted: m.groups.size > 0 };
  }
  const out: string[] = [];
  for (const [id, mc] of m.groups) {
    const gs = reads.map((g) => g.get(id)!);
    for (const k of POWER_FIELDS) {
      const values = gs.map((g) => g[k]);
      const lo = Math.min(...values);
      const hi = Math.max(...values);
      if (!inBracket(mc[k], lo, hi)) out.push(`${at}: group ${id} ${k} mod ${mc[k]} vs node ${lo === hi ? lo : `${lo}..${hi}`}`);
    }
    if (!gs.some((g) => g.fuseTriggered === mc.fuseTriggered)) out.push(`${at}: group ${id} fuseTriggered mod ${mc.fuseTriggered} vs node ${gs[0]!.fuseTriggered}`);
  }
  return { reasons: out, compared: true, counted: m.groups.size > 0 };
}

/** Status and auto-pause of one nearest pair (power is judged by comparePower). */
export function comparePair(p: Pair): { reasons: string[]; compared: { status: boolean; autoPause: boolean } } {
  const at = p.mod.observedAt;
  const out: string[] = [];
  const differ = (name: string, a: unknown, b: unknown) => out.push(`${at}: ${name} mod ${a} vs node ${b}`);
  const compared = { status: false, autoPause: false };

  const a = p.mod.status;
  const b = p.node.status;
  if (a && b) {
    compared.status = true;
    for (const k of ["isGameRunning", "gamePaused", "connectedPlayers", "playerLimit", "sessionName", "tickHealth"] as const) if (a[k] !== b[k]) differ(k, a[k], b[k]);
    if (!tickClose(a.tickRate, b.tickRate)) differ("tickRate", a.tickRate, b.tickRate);
    if (!(Math.abs(a.totalGameDurationSeconds - b.totalGameDurationSeconds) <= DURATION_DRIFT)) differ("totalGameDurationSeconds", a.totalGameDurationSeconds, b.totalGameDurationSeconds);
  }
  if (p.mod.settings && p.node.settings) {
    compared.autoPause = true;
    if (p.mod.settings.autoPause !== p.node.settings.autoPause) differ("settings.autoPause", p.mod.settings.autoPause, p.node.settings.autoPause);
  }
  return { reasons: out, compared };
}

function stateReasons(who: string, s: ServerState | null, version?: string): string[] {
  if (!s) return [`${who}'s server state was not read`];
  const out: string[] = [];
  if (!s.codeConsumed) out.push(`${who}'s code was not consumed`);
  if (!s.lastSeen) out.push(`the backend never saw ${who}'s authenticated call`);
  if (version !== undefined && s.agentVersion !== version) out.push(`${who}'s agentVersion is ${s.agentVersion}, expected ${version}`);
  return out;
}

export function evaluate(e: CoreEvidence): Verdict {
  const d = e.captures["parity.json"] as Parity | undefined;
  if (!d) return { pass: false, reasons: ["no parity capture"] };
  const reasons: string[] = [];
  if (!d.windowStartedAt || !d.windowEndedAt) reasons.push(`the ${WINDOW_MINUTES}-minute window was not recorded`);
  else {
    const minutes = (ms(d.windowEndedAt) - ms(d.windowStartedAt)) / 60_000;
    if (!(minutes >= WINDOW_MINUTES)) reasons.push(`the window ran ${minutes.toFixed(1)} minutes, need ${WINDOW_MINUTES}`);
  }
  if (d.tokensInvalidated !== true) reasons.push("the run's game API token was not invalidated (server.InvalidateAPITokens)");
  reasons.push(...stateReasons("the mod", d.state.mod, "mod/0.1.0"), ...stateReasons("the Node agent", d.state.node));
  const agents = [["mod", "the mod"], ["node", "the Node agent"]] as const;
  for (const [key, who] of agents) {
    const list = d.refused[key];
    if (list.length) reasons.push(`the backend didn't accept ${list.length} of ${who}'s snapshots (answers ${list.map((p) => p.status).join(", ")})`);
    const unread = d.unrecordedAccepted[key];
    if (unread) reasons.push(`${unread} of ${who}'s accepted snapshots couldn't be read by the recorder, so they weren't judged`);
  }
  if (d.cleanup && !d.cleanup.stopped) reasons.push("stopping the Node agent failed");
  if (d.cleanup?.homeLeft) reasons.push("the Node agent's SD_AGENT_HOME was left on disk");

  const pairs = pairSnapshots(d.mod, d.node);
  const counts = `${d.accepted.mod} mod and ${d.accepted.node} Node agent snapshots accepted`;
  if (pairs.length < NEED_PAIRS) reasons.push(`only ${pairs.length} pairs within ${PAIR_GAP_MS} ms (${counts}), need ${NEED_PAIRS}`);
  const compared = { status: 0, autoPause: 0 };
  for (const p of pairs) {
    const r = comparePair(p);
    reasons.push(...r.reasons);
    for (const k of ["status", "autoPause"] as const) if (r.compared[k]) compared[k]++;
  }
  for (const k of ["status", "autoPause"] as const) {
    if (compared[k] < NEED_PAIRS) reasons.push(`${k} compared in only ${compared[k]} pairs, need ${NEED_PAIRS}`);
  }
  let powerRounds = 0;
  for (const m of unique(d.mod)) {
    const r = comparePower(m, d.node);
    reasons.push(...r.reasons);
    if (r.counted) powerRounds++;
  }
  if (powerRounds < NEED_PAIRS) reasons.push(`power compared in only ${powerRounds} rounds with a circuit group, need ${NEED_PAIRS}`);
  if (reasons.length) return { pass: false, reasons };

  const groups = new Set(d.mod.flatMap((s) => (s.power?.circuits ?? []).map((c) => c.circuitGroupId))).size;
  const largest = Math.max(...pairs.map((p) => p.gapMs));
  const o = d.oversizedAccepted;
  return {
    pass: true,
    reasons: [
      `${pairs.length} pairs within ${PAIR_GAP_MS} ms, largest gap ${largest} ms; ${counts} (${o.mod} and ${o.node} over the recorder's 2 MB cap, ` +
        `judged without their bulky parts): status compared in ${compared.status} pairs, autoPause in ${compared.autoPause}; power in ${powerRounds} ` +
        `rounds against the Node agent's neighbours (${groups} circuit group${groups === 1 ? "" : "s"}); every compared field matches`,
    ],
  };
}
