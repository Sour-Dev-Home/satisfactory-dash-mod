// The enrol run (spec "Testing" 2): enrol with a seeded code, write the credential, blank the code, and have the
// snapshots accepted. Every native reading is compared with the game's APIs (C8, C9, power; Task 3, ruling 7).
// Owner decision (2026-10-20), after enrol run 39517898558 failed one of 12 world rounds on consumption (1233.10 vs FRM
// 1218.20, 1.2%; consumption swings ~60 MW between rounds, and the other 11 matched to ~0.05%): power is judged against
// FRM read just before and just after the mod's sample. Each compared field must lie in [lo - tol, hi + tol], lo and hi
// the min and max of the two reads, tol = 0.01 * max(|lo|, |hi|, 1): the 1% stays. Group ids and the fuse must match at
// least one of the two reads. A round missing either read fails.
// "Just before and just after" means within 1 s (controller ruling on the owner's decision, re-review 2 of 4cca001):
// the "before" read ended at most FRM_BRACKET_GAP_MS before the snapshot's observedAt, the "after" read started at most
// that long after it, checked here from the recorded times, so a rejudge sees the same rule. The harness collects reads
// with the same limit (it imports this constant) and polls about every 250 ms, so one failed read still leaves one.
// Owner ruling (2026-10-23; PR #44, and satisfactory-dash #620 option A: "production" is the total produced), after enrol run 40038539365 failed every world round on
// "group 1 production 4 vs FRM 0" (two biomass burners powering a 4 MW Constructor): FRM 1.5.4's PowerProduction is
// the group's mBaseProduction (FRM Power.cpp:47), only the base part of the game's split (FGPowerCircuit.h:426-433),
// while the mod sends PowerProduced, the total (FGPowerCircuit.h:104). Demand-following generators make up whatever
// base leaves of the load, so for a group without batteries the reference is max(FRM production, FRM consumption).
// Where base covers the load (every run before this one), that is FRM's own figure, so the same bracket and 1% apply
// unchanged. A group with batteries (whose charging is demand too) keeps the raw FRM figure; the pass reason says how
// many groups the derived total judged.
import type { CoreEvidence } from "../../harness/src/core-run";
import type { Verdict } from "../../harness/src/verdict";

export const requirements = ["enrol", "C8", "C9", "power", "S10", "S13"];
/** How far from the mod's sample each FRM read may be (see the header). */
export const FRM_BRACKET_GAP_MS = 1000;
const NEED_WORLD = 10;
const NEED_EMPTY = 3;
const tickClose = (a: number, b: number) => Math.abs(a - b) <= Math.max(2, 0.1 * Math.max(a, b)); // S6
/** The owner's bracket (2026-10-20): mod within 1% of the range the two FRM reads span. */
const inBracket = (mod: number, lo: number, hi: number) => {
  const tol = 0.01 * Math.max(Math.abs(lo), Math.abs(hi), 1);
  return lo - tol <= mod && mod <= hi + tol;
};
/** FRM's production as the game's total (see the header): base plus what demand-following generators add. */
const derivesProduction = (g: FrmGroup) => g.batteryCapacity === 0;
const frmProduction = (g: FrmGroup) => (derivesProduction(g) ? Math.max(g.production, g.consumption) : g.production);
const SNAPSHOT_KEYS = new Set(["agentVersion", "observedAt", "reachable", "paused", "status", "power", "settings"]);

export interface ApiStatus { running: boolean; paused: boolean; players: number; limit: number; duration: number; tickRate: number; health: string; session: string }
export interface ModStatus { isGameRunning: boolean; gamePaused: boolean; connectedPlayers: number; playerLimit: number; totalGameDurationSeconds: number; tickRate: number; tickHealth: string; sessionName: string }
export interface Circuit { circuitGroupId: number; productionMW: number; consumptionMW: number; capacityMW: number; maxConsumptionMW: number; fuseTriggered: boolean; batteryCapacityMWh: number; batteryPercent: number; batteryDifferentialMW: number }
export interface FrmGroup { groupId: number; production: number; consumption: number; capacity: number; maxConsumption: number; batteryCapacity: number; batteryPercent: number; batteryDifferential: number; fuseTriggered: boolean }
/** frmBefore and frmAfter: FRM read just before and just after the mod's sample (null: no such read); when they ran. */
export interface Round {
  at: string; world: boolean; snapshot: Record<string, unknown>; api: ApiStatus | null; autoPause: boolean | null;
  frmBefore: FrmGroup[] | null; frmAfter: FrmGroup[] | null; frmBeforeEndedAt: string | null; frmAfterStartedAt: string | null;
}
/** How FRM was polled during the world rounds (evidence: the tick and memory figures were taken under this load). */
export interface FrmPolling { intervalMs: number; reads: number; failed: number }
export interface Enrol { state: { codeConsumed: boolean; agentVersion: string | null; lastSeen: boolean }; codeBlanked: boolean; credentialWritten: boolean; refused: number; rounds: Round[]; frmPolling?: FrmPolling }

export function compareRound(r: Round): string[] {
  // A 200 snapshot whose body wasn't recorded (over 2 MB, not JSON) fails its round instead of crashing the judge, so
  // the bundle is still written (review of 66e1f0d, minor 3).
  if (r.snapshot === null || typeof r.snapshot !== "object" || Array.isArray(r.snapshot)) return [`${r.at}: the snapshot body was not recorded`];
  const out: string[] = [];
  const s = r.snapshot.status as ModStatus | undefined;
  for (const k of Object.keys(r.snapshot)) if (!SNAPSHOT_KEYS.has(k)) out.push(`${r.at}: S13: unexpected snapshot field ${k}`);
  if (r.snapshot.reachable !== true) out.push(`${r.at}: reachable is not true`);
  if (!s || !r.api) return [...out, `${r.at}: no status to compare`];
  const a = r.api;
  if (s.isGameRunning !== a.running) out.push(`${r.at}: isGameRunning ${s.isGameRunning} vs API ${a.running}`);
  if (s.gamePaused !== a.paused) out.push(`${r.at}: gamePaused ${s.gamePaused} vs API ${a.paused}`);
  if (r.snapshot.paused !== s.gamePaused) out.push(`${r.at}: paused doesn't match status.gamePaused`);
  if (s.connectedPlayers !== a.players || s.playerLimit !== a.limit) out.push(`${r.at}: players ${s.connectedPlayers}/${s.playerLimit} vs API ${a.players}/${a.limit}`);
  if (Math.abs(s.totalGameDurationSeconds - a.duration) > 10) out.push(`${r.at}: duration ${s.totalGameDurationSeconds} vs API ${a.duration}`);
  if (!tickClose(s.tickRate, a.tickRate)) out.push(`${r.at}: tick rate ${s.tickRate} vs API ${a.tickRate}`);
  if (s.tickHealth !== a.health) out.push(`${r.at}: tick health ${s.tickHealth} vs API ${a.health}`);
  if (s.sessionName !== a.session) out.push(`${r.at}: session hash differs`);
  const settings = r.snapshot.settings as { autoPause?: boolean } | undefined;
  if (r.autoPause !== null && settings?.autoPause !== r.autoPause) out.push(`${r.at}: autoPause ${settings?.autoPause} vs GetServerOptions ${r.autoPause}`);
  const power = (r.snapshot.power as { circuits?: Circuit[] } | undefined)?.circuits;
  if (!r.world) {
    if (power !== undefined) out.push(`${r.at}: power sent with no game running`);
    return out;
  }
  if (!power) return [...out, `${r.at}: no power to compare`];
  // Never passes on missing data: both reads must be there.
  if (!r.frmBefore) return [...out, `${r.at}: no FRM read before the mod's sample`];
  if (!r.frmAfter) return [...out, `${r.at}: no FRM read after the mod's sample`];
  // The time rule (header): before ended in [observedAt - gap, observedAt], after started in [observedAt, observedAt + gap].
  const ms = (v: unknown) => (typeof v === "string" ? Date.parse(v) : Number.NaN);
  const observed = ms(r.snapshot.observedAt);
  if (!Number.isFinite(observed)) return [...out, `${r.at}: the snapshot has no observedAt to bracket`];
  const timing: string[] = [];
  const beforeEnded = ms(r.frmBeforeEndedAt);
  if (!Number.isFinite(beforeEnded)) timing.push(`${r.at}: no time for the FRM read before the mod's sample`);
  else if (beforeEnded > observed) timing.push(`${r.at}: the FRM read before the mod's sample ended ${beforeEnded - observed} ms after it`);
  else if (observed - beforeEnded > FRM_BRACKET_GAP_MS) timing.push(`${r.at}: the FRM read before the mod's sample ended ${observed - beforeEnded} ms before it, more than ${FRM_BRACKET_GAP_MS} ms`);
  const afterStarted = ms(r.frmAfterStartedAt);
  if (!Number.isFinite(afterStarted)) timing.push(`${r.at}: no time for the FRM read after the mod's sample`);
  else if (afterStarted < observed) timing.push(`${r.at}: the FRM read after the mod's sample started ${observed - afterStarted} ms before it`);
  else if (afterStarted - observed > FRM_BRACKET_GAP_MS) timing.push(`${r.at}: the FRM read after the mod's sample started ${afterStarted - observed} ms after it, more than ${FRM_BRACKET_GAP_MS} ms`);
  if (timing.length) return [...out, ...timing];
  const ids = (xs: number[]) => [...xs].sort((x, y) => x - y).join(",");
  const modIds = ids(power.map((c) => c.circuitGroupId));
  const beforeIds = ids(r.frmBefore.map((g) => g.groupId));
  const afterIds = ids(r.frmAfter.map((g) => g.groupId));
  // The group set must match at least one read; only the read(s) that match bracket the values.
  const reads = [r.frmBefore, r.frmAfter].filter((f) => ids(f.map((g) => g.groupId)) === modIds);
  if (!reads.length) {
    const frmIds = beforeIds === afterIds ? beforeIds : `${beforeIds} before, ${afterIds} after`;
    return [...out, `${r.at}: circuit groups ${modIds} vs FRM ${frmIds}`];
  }
  const fields: Array<[string, (c: Circuit) => number, (g: FrmGroup) => number]> = [
    ["production", (c) => c.productionMW, frmProduction], ["consumption", (c) => c.consumptionMW, (g) => g.consumption],
    ["capacity", (c) => c.capacityMW, (g) => g.capacity], ["maxConsumption", (c) => c.maxConsumptionMW, (g) => g.maxConsumption],
    ["batteryCapacity", (c) => c.batteryCapacityMWh, (g) => g.batteryCapacity], ["batteryPercent", (c) => c.batteryPercent, (g) => g.batteryPercent],
    ["batteryDifferential", (c) => c.batteryDifferentialMW, (g) => g.batteryDifferential],
  ];
  for (const c of power) {
    const gs = reads.map((f) => f.find((x) => x.groupId === c.circuitGroupId)!);
    for (const [name, modOf, frmOf] of fields) {
      const values = gs.map(frmOf);
      const lo = Math.min(...values);
      const hi = Math.max(...values);
      const mod = modOf(c);
      if (!inBracket(mod, lo, hi)) out.push(`${r.at}: group ${c.circuitGroupId} ${name} ${mod} vs FRM ${lo === hi ? lo : `${lo}..${hi}`}`);
    }
    if (!gs.some((g) => g.fuseTriggered === c.fuseTriggered)) out.push(`${r.at}: group ${c.circuitGroupId} fuse ${c.fuseTriggered} vs FRM ${gs[0]!.fuseTriggered}`);
  }
  return out;
}

export function evaluate(e: CoreEvidence): Verdict {
  const d = e.captures["enrol.json"] as Enrol | undefined;
  if (!d) return { pass: false, reasons: ["no enrol capture"] };
  const reasons: string[] = [];
  if (!d.state.codeConsumed) reasons.push("the code was not consumed");
  if (d.state.agentVersion !== "mod/0.1.0") reasons.push(`agentVersion ${d.state.agentVersion}, expected mod/0.1.0`);
  if (!d.state.lastSeen) reasons.push("the backend never saw an authenticated call");
  if (!d.codeBlanked) reasons.push("the code is still in SatisDashAgent.cfg");
  if (!d.credentialWritten) reasons.push("no credential.json was written");
  if (d.refused) reasons.push(`the backend refused ${d.refused} snapshot(s)`);
  const empty = d.rounds.filter((r) => !r.world);
  const world = d.rounds.filter((r) => r.world);
  if (empty.length < NEED_EMPTY) reasons.push(`only ${empty.length} rounds with no game, need ${NEED_EMPTY}`);
  if (world.length < NEED_WORLD) reasons.push(`only ${world.length} rounds with the world, need ${NEED_WORLD}`);
  for (const r of d.rounds) reasons.push(...compareRound(r));
  // C9 must actually be compared, not skipped in every round (final review of Plan 2B): at least NEED_WORLD rounds.
  const comparedAutoPause = d.rounds.filter((r) => r.autoPause !== null).length;
  if (comparedAutoPause < NEED_WORLD) reasons.push(`autoPause compared in only ${comparedAutoPause} rounds, need ${NEED_WORLD} (GetServerOptions didn't answer)`);
  if (!e.lines.some((l) => /Enrolled; reporting to the backend/.test(l))) reasons.push("S9: no enrolment line");
  if (reasons.length) return { pass: false, reasons };
  // Said in the pass reason, never skipped silently (review of 66e1f0d): how many rounds compared auto-pause, and how
  // many circuit groups the power comparison covered (zero groups pass, as the plan has it, but the reason says so).
  const autoPauseRounds = d.rounds.filter((r) => r.autoPause !== null).length;
  const autoPause = autoPauseRounds
    ? `autoPause compared in ${autoPauseRounds} of ${d.rounds.length} rounds`
    : "the autoPause check was skipped (GetServerOptions never answered)";
  const p = d.frmPolling;
  const polling = p ? `; FRM was polled every ${p.intervalMs} ms during the world rounds (${p.reads} reads, ${p.failed} failed)` : "";
  const groups = new Set(world.flatMap((r) => (r.frmBefore ?? []).map((g) => g.groupId))).size;
  const derived = new Set(world.flatMap((r) => (r.frmBefore ?? []).filter(derivesProduction).map((g) => g.groupId))).size;
  const derivedNote = derived ? ` (production against max(FRM production, consumption) for ${derived}: no batteries)` : "";
  return { pass: true, reasons: [`enrolled; ${empty.length} rounds with no game and ${world.length} with the world match the game's APIs; ${autoPause}; power compared for ${groups} circuit group(s)${derivedNote}${polling}`] };
}
