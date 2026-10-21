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
// Pairing: each accepted mod snapshot goes with the Node agent's accepted snapshot nearest in observedAt, kept only when
// they are at most PAIR_GAP_MS apart (half the 5 s status cycle). Judged, never widened (CLAUDE.md rule 3; if the game
// legitimately moves faster than the pairing allows, that goes to the owner):
//  - at least NEED_PAIRS pairs (30 * 60 / 5 * 0.8);
//  - in every pair: status exact for running, paused, players, limit, session hash and tick health; S6's tick-rate rule;
//    duration within 10 s; settings.autoPause exact; power by group with S3's rule (within 1%) for every field and the
//    fuse exact, a group's several rows (a joined group) compared as one row, which they must all agree on;
//  - a part only one agent sent in a pair isn't compared there, but status, autoPause and power must each be compared in
//    at least NEED_PAIRS pairs, so missing data never passes;
//  - every snapshot either agent posted in the window was accepted; both servers enrolled (code consumed, seen), the
//    mod as mod/0.1.0; a window of at least 30 minutes; the run's game API token invalidated.
import type { CoreEvidence } from "../../harness/src/core-run";
import type { Verdict } from "../../harness/src/verdict";

export const requirements = ["parity"];
export const PAIR_GAP_MS = 2500;
export const WINDOW_MINUTES = 30;
const STATUS_SECONDS = 5; // FCadence::StatusSeconds (Responses.h:35); the Node agent's INITIAL_CADENCE (agentRunner.ts:14)
export const NEED_PAIRS = Math.ceil(((WINDOW_MINUTES * 60) / STATUS_SECONDS) * 0.8);
const DURATION_DRIFT = 10; // S6
const tickClose = (a: number, b: number) => Math.abs(a - b) <= Math.max(2, 0.1 * Math.max(a, b)); // S6
const within = (a: number, b: number) => Math.abs(a - b) <= 0.01 * Math.max(Math.abs(a), Math.abs(b), 1); // S3

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
  mod: Snap[];
  node: Snap[];
  /** Snapshot posts in the window that weren't answered 200. */
  refused: { mod: Posted[]; node: Posted[] };
  state: { mod: ServerState | null; node: ServerState | null };
  /** Whether server.InvalidateAPITokens ran at the end; null when no token was made or it never ran. */
  tokensInvalidated: boolean | null;
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
function byGroup(side: string, circuits: Circuit[], at: string): { groups: Map<number, Circuit>; reasons: string[] } {
  const groups = new Map<number, Circuit>();
  const reasons: string[] = [];
  for (const c of circuits) {
    const seen = groups.get(c.circuitGroupId);
    if (!seen) groups.set(c.circuitGroupId, c);
    else if (JSON.stringify(seen) !== JSON.stringify(c) && !reasons.some((r) => r.includes(`group ${c.circuitGroupId} `))) {
      reasons.push(`${at}: the ${side}'s rows for group ${c.circuitGroupId} disagree`);
    }
  }
  return { groups, reasons };
}

export function comparePair(p: Pair): { reasons: string[]; compared: { status: boolean; autoPause: boolean; power: boolean } } {
  const at = p.mod.observedAt;
  const out: string[] = [];
  const differ = (name: string, a: unknown, b: unknown) => out.push(`${at}: ${name} mod ${a} vs node ${b}`);
  const compared = { status: false, autoPause: false, power: false };

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
  if (p.mod.power && p.node.power) {
    compared.power = true;
    const m = byGroup("mod", p.mod.power.circuits, at);
    const n = byGroup("node", p.node.power.circuits, at);
    out.push(...m.reasons, ...n.reasons);
    const ids = (g: Map<number, Circuit>) => [...g.keys()].sort((x, y) => x - y).join(",");
    if (ids(m.groups) !== ids(n.groups)) {
      out.push(`${at}: circuit groups mod ${ids(m.groups)} vs node ${ids(n.groups)}`);
    } else if (!m.reasons.length && !n.reasons.length) {
      for (const [id, mc] of m.groups) {
        const nc = n.groups.get(id)!;
        for (const k of ["productionMW", "consumptionMW", "capacityMW", "maxConsumptionMW", "batteryCapacityMWh", "batteryPercent", "batteryDifferentialMW"] as const) {
          if (!within(mc[k], nc[k])) differ(`group ${id} ${k}`, mc[k], nc[k]);
        }
        if (mc.fuseTriggered !== nc.fuseTriggered) differ(`group ${id} fuseTriggered`, mc.fuseTriggered, nc.fuseTriggered);
      }
    }
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
  for (const [who, list] of [["the mod", d.refused.mod], ["the Node agent", d.refused.node]] as const) {
    if (list.length) reasons.push(`the backend didn't accept ${list.length} of ${who}'s snapshots (answers ${list.map((p) => p.status).join(", ")})`);
  }

  const pairs = pairSnapshots(d.mod, d.node);
  const counts = `${unique(d.mod).length} mod and ${unique(d.node).length} Node agent snapshots accepted`;
  if (pairs.length < NEED_PAIRS) reasons.push(`only ${pairs.length} pairs within ${PAIR_GAP_MS} ms (${counts}), need ${NEED_PAIRS}`);
  const compared = { status: 0, autoPause: 0, power: 0 };
  for (const p of pairs) {
    const r = comparePair(p);
    reasons.push(...r.reasons);
    for (const k of ["status", "autoPause", "power"] as const) if (r.compared[k]) compared[k]++;
  }
  for (const k of ["status", "autoPause", "power"] as const) {
    if (compared[k] < NEED_PAIRS) reasons.push(`${k} compared in only ${compared[k]} pairs, need ${NEED_PAIRS}`);
  }
  if (reasons.length) return { pass: false, reasons };

  const groups = new Set(pairs.flatMap((p) => (p.mod.power?.circuits ?? []).map((c) => c.circuitGroupId))).size;
  const largest = Math.max(...pairs.map((p) => p.gapMs));
  return {
    pass: true,
    reasons: [
      `${pairs.length} pairs within ${PAIR_GAP_MS} ms (${counts}; largest gap ${largest} ms): status compared in ${compared.status}, ` +
        `autoPause in ${compared.autoPause}, power in ${compared.power} pairs (${groups} circuit group${groups === 1 ? "" : "s"}); every compared field matches`,
    ],
  };
}
