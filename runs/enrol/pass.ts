// The enrol run (spec "Testing" 2): enrol with a seeded code, write the credential, blank the code, and have the
// snapshots accepted. Every native reading is compared with the game's APIs (C8, C9, power; Task 3, ruling 7).
import type { CoreEvidence } from "../../harness/src/core-run";
import type { Verdict } from "../../harness/src/verdict";

export const requirements = ["enrol", "C8", "C9", "power", "S10", "S13"];
const NEED_WORLD = 10;
const NEED_EMPTY = 3;
const tickClose = (a: number, b: number) => Math.abs(a - b) <= Math.max(2, 0.1 * Math.max(a, b)); // S6
const within = (a: number, b: number) => Math.abs(a - b) <= 0.01 * Math.max(Math.abs(a), Math.abs(b), 1); // S3
const SNAPSHOT_KEYS = new Set(["agentVersion", "observedAt", "reachable", "paused", "status", "power", "settings"]);

export interface ApiStatus { running: boolean; paused: boolean; players: number; limit: number; duration: number; tickRate: number; health: string; session: string }
export interface ModStatus { isGameRunning: boolean; gamePaused: boolean; connectedPlayers: number; playerLimit: number; totalGameDurationSeconds: number; tickRate: number; tickHealth: string; sessionName: string }
export interface Circuit { circuitGroupId: number; productionMW: number; consumptionMW: number; capacityMW: number; maxConsumptionMW: number; fuseTriggered: boolean; batteryCapacityMWh: number; batteryPercent: number; batteryDifferentialMW: number }
export interface Round { at: string; world: boolean; snapshot: Record<string, unknown>; api: ApiStatus | null; autoPause: boolean | null; frm: Array<{ groupId: number; production: number; consumption: number; capacity: number; maxConsumption: number; batteryCapacity: number; batteryPercent: number; batteryDifferential: number; fuseTriggered: boolean }> | null }
export interface Enrol { state: { codeConsumed: boolean; agentVersion: string | null; lastSeen: boolean }; codeBlanked: boolean; credentialWritten: boolean; refused: number; rounds: Round[] }

export function compareRound(r: Round): string[] {
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
  if (!power || !r.frm) return [...out, `${r.at}: no power to compare`];
  const ids = (xs: number[]) => [...xs].sort((x, y) => x - y).join(",");
  if (ids(power.map((c) => c.circuitGroupId)) !== ids(r.frm.map((g) => g.groupId))) {
    return [...out, `${r.at}: circuit groups ${ids(power.map((c) => c.circuitGroupId))} vs FRM ${ids(r.frm.map((g) => g.groupId))}`];
  }
  for (const c of power) {
    const g = r.frm.find((x) => x.groupId === c.circuitGroupId)!;
    const pairs: Array<[string, number, number]> = [
      ["production", c.productionMW, g.production], ["consumption", c.consumptionMW, g.consumption], ["capacity", c.capacityMW, g.capacity],
      ["maxConsumption", c.maxConsumptionMW, g.maxConsumption], ["batteryCapacity", c.batteryCapacityMWh, g.batteryCapacity],
      ["batteryPercent", c.batteryPercent, g.batteryPercent], ["batteryDifferential", c.batteryDifferentialMW, g.batteryDifferential],
    ];
    for (const [name, mod, frm] of pairs) if (!within(mod, frm)) out.push(`${r.at}: group ${c.circuitGroupId} ${name} ${mod} vs FRM ${frm}`);
    if (c.fuseTriggered !== g.fuseTriggered) out.push(`${r.at}: group ${c.circuitGroupId} fuse ${c.fuseTriggered} vs FRM ${g.fuseTriggered}`);
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
  if (!e.lines.some((l) => /Enrolled; reporting to the backend/.test(l))) reasons.push("S9: no enrolment line");
  return reasons.length ? { pass: false, reasons } : { pass: true, reasons: [`enrolled; ${empty.length} rounds with no game and ${world.length} with the world match the game's APIs`] };
}
