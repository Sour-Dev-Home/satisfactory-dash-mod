// The enrol run (spec "Testing" 2): enrol with a seeded code, write the credential, blank the code, and have the
// snapshots accepted. Every native reading is compared with the game's APIs (C8, C9; Task 3, ruling 7).
// Owner ruling 2026-11-05 (R7, D5): the equality of the mod's power circuits with FRM's getPower (a 1% bracket of two FRM reads
// around each sample, and a world round with no FRM read failing) is RETIRED, because FRM is no longer part of this repository.
// The record is native-parity: power R0 passed 356 rounds in run 41340686528, plus the earlier sprint runs, kept in the dash repo's
// docs-vault/raw-sources/mod-native-readers/. In its place every accepted power part must be internally consistent (see
// powerProblems): no negative production, consumption, capacity or maxConsumption; consumption not above maxConsumption; battery
// percent within 0-100. The contract validation of the power part (the backend accepts the snapshot) and every other check here stay.
// Owner ruling (2026-10-24, the settings.pending change after satisfactory-dash #621): every round also checks pending.
// GetServerOptions' pendingServerOptions gives the expected values: settings.autoPause is the pending value when one
// waits, else the applied one, and settings.pending (absent counts as false, as the contract says) is whether one
// waits. A round recorded before this change has no autoPausePending and is judged as having none.
// Owner ruling (2026-10-24, Plan 3A Task 10; S13 snapshot fields): players and factory join the snapshot (spec "What the Node
// agent sends"), so both join SNAPSHOT_KEYS. Any other field still fails.
// Owner ruling (2026-10-25, Plan 3B Task 4; S13 snapshot fields): mapLive (trains and stations,
// spec "What the Node agent sends") joins SNAPSHOT_KEYS with PR 3B-1. Any other field still fails.
import type { CoreEvidence } from "../../harness/src/core-run";
import type { Verdict } from "../../harness/src/verdict";

export const requirements = ["enrol", "C8", "C9", "power", "S10", "S13"];
const NEED_WORLD = 10;
const NEED_EMPTY = 3;
const tickClose = (a: number, b: number) => Math.abs(a - b) <= Math.max(2, 0.1 * Math.max(a, b)); // S6
const SNAPSHOT_KEYS = new Set(["agentVersion", "observedAt", "reachable", "paused", "status", "power", "settings", "players", "factory", "mapLive"]);

export interface ApiStatus { running: boolean; paused: boolean; players: number; limit: number; duration: number; tickRate: number; health: string; session: string }
export interface ModStatus { isGameRunning: boolean; gamePaused: boolean; connectedPlayers: number; playerLimit: number; totalGameDurationSeconds: number; tickRate: number; tickHealth: string; sessionName: string }
export interface Circuit { circuitGroupId: number; productionMW: number; consumptionMW: number; capacityMW: number; maxConsumptionMW: number; fuseTriggered: boolean; batteryCapacityMWh: number; batteryPercent: number; batteryDifferentialMW: number }
/** Rounds recorded before R7 step 4 also carry frmBefore, frmAfter and their times, and the capture a frmPolling field: ignored. */
export interface Round {
  at: string; world: boolean; snapshot: Record<string, unknown>; api: ApiStatus | null; autoPause: boolean | null;
  /** FG.DSAutoPause's pending value from GetServerOptions: null when none waits; absent in rounds recorded before 2026-10-24. */
  autoPausePending?: boolean | null;
}
export interface Enrol { state: { codeConsumed: boolean; agentVersion: string | null; lastSeen: boolean }; codeBlanked: boolean; credentialWritten: boolean; refused: number; rounds: Round[] }

/**
 * Internal consistency of each accepted power circuit (owner ruling 2026-11-05, replacing the FRM equality): no negative
 * production, consumption, capacity or maxConsumption; consumption not above maxConsumption (when both are numbers); battery
 * percent within 0-100 (when a number). Each reason names the circuit.
 */
export function powerProblems(at: string, circuits: readonly Circuit[]): string[] {
  const out: string[] = [];
  const num = (v: unknown): v is number => typeof v === "number" && Number.isFinite(v);
  for (const c of circuits) {
    const who = `${at}: circuit ${c.circuitGroupId}`;
    for (const [name, v] of [["productionMW", c.productionMW], ["consumptionMW", c.consumptionMW], ["capacityMW", c.capacityMW], ["maxConsumptionMW", c.maxConsumptionMW]] as const) {
      if (num(v) && v < 0) out.push(`${who} ${name} is negative (${v})`);
    }
    if (num(c.consumptionMW) && num(c.maxConsumptionMW) && c.consumptionMW > c.maxConsumptionMW) out.push(`${who} consumptionMW ${c.consumptionMW} is above maxConsumptionMW ${c.maxConsumptionMW}`);
    if (num(c.batteryPercent) && (c.batteryPercent < 0 || c.batteryPercent > 100)) out.push(`${who} batteryPercent ${c.batteryPercent} is outside 0-100`);
  }
  return out;
}

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
  const settings = r.snapshot.settings as { autoPause?: boolean; pending?: boolean } | undefined;
  if (r.autoPause !== null) {
    const waiting = r.autoPausePending ?? null;
    const expected = waiting ?? r.autoPause;
    if (settings?.autoPause !== expected) out.push(`${r.at}: autoPause ${settings?.autoPause} vs GetServerOptions ${expected}${waiting === null ? "" : " (pending)"}`);
    const pending = settings?.pending ?? false;
    if (pending !== (waiting !== null)) out.push(`${r.at}: pending ${pending} vs GetServerOptions ${waiting !== null}`);
  }
  const power = (r.snapshot.power as { circuits?: Circuit[] } | undefined)?.circuits;
  if (!r.world) {
    if (power !== undefined) out.push(`${r.at}: power sent with no game running`);
    return out;
  }
  if (!power) return [...out, `${r.at}: no power to compare`];
  out.push(...powerProblems(r.at, power));
  return out;
}

export function evaluate(e: CoreEvidence): Verdict {
  const d = e.captures["enrol.json"] as Enrol | undefined;
  if (!d) return { pass: false, reasons: ["no enrol capture"] };
  const reasons: string[] = [];
  if (!d.state.codeConsumed) reasons.push("the code was not consumed");
  if (d.state.agentVersion !== "mod/0.2.0") reasons.push(`agentVersion ${d.state.agentVersion}, expected mod/0.2.0`);
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
  const groups = new Set(world.flatMap((r) => ((r.snapshot.power as { circuits?: Circuit[] } | undefined)?.circuits ?? []).map((c) => c.circuitGroupId))).size;
  return { pass: true, reasons: [`enrolled; ${empty.length} rounds with no game and ${world.length} with the world match the game's APIs; ${autoPause}; power checked for internal consistency for ${groups} circuit group(s) (the FRM equality is retired, owner 2026-11-05)`] };
}
