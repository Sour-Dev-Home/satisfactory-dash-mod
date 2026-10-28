// The combined native-reader parity run for sprint S1 (ADR-0058, Decision 3 and the owner's answers of 2026-10-25): the mod,
// with its players and resource-node readers set to "native" (power is native already), runs against the test server for
// 30 minutes while the harness reads FRM's own endpoints as a black box in the same minutes. Each reader is judged on its
// own, against Decision 3's tolerances, which the owner approved as written; a reader that fails stays on the bridge and
// does not change another reader's verdict (readers.json carries the three verdicts; result.json is red if any is).
//
//  - R0 power: every field per circuit group within PARITY_POWER_TOLERANCE (3%, runs/parity/pass.ts) of FRM's getPower reads
//    just before and just after the mod's sample (the enrol run's bracket, FRM_BRACKET_GAP_MS); group ids and the fuse exact;
//    at least NEED_ROUNDS compared rounds. Fields the world never exercises are reported, not failed (open question 1 of
//    docs/plans/2026-10-25-native-readers-s1.md; NOT_EXERCISED_FAILS below flips that).
//  - R1 players: the list of (hash of name, online) equal to FRM's getPlayer on every round where FRM's samples either side
//    agree; at least NEED_ROUNDS compared rounds. Names never enter evidence (ADR-0029).
//  - R4 resource nodes: the layer the mod posted is valid against the pinned contract and equals, as a sorted list, FRM's
//    getResourceNode (mapped to the contract's form by frm-native.ts) at the sample before or after the post, within
//    NODES_GAP_MS; every difference is listed; at least one accepted post.
// Both new readers must also be shown native by the mod's own log (reader-lines.ts): its `Readers:` line, and for the nodes a
// completed `Resource node scan` line. With a reader on the bridge the mod's output equals FRM's and the values pass trivially.
// Nothing here changes a pass condition of another run: the 3% bar and the round count are the parity run's own exports.
//
// Sprint S2 (ADR-0058, owner decisions of 2026-10-26) adds five judges BESIDE the three above, in pass-s2.ts, never inside them: the
// factory (R2a), the rates (R2b), the trains and stations (R3) and the native-only trainRoutes layer. A capture without `s2` (S1's)
// is judged exactly as before; a capture with it, or a mod config that writes factorySource/trainsSource, is judged on all eight, and
// a config that asks for S2 but a capture without it fails closed. Both new readers must also be shown native by the mod's log
// (reader-lines.ts) and to have completed a `Factory read` / `Trains read` / `Train routes read` line.
import type { CoreEvidence } from "../../harness/src/core-run";
import { ResourceNodesLayerDataSchema } from "../../harness/contract/src/map";
import type { FrmCircuitGroup } from "../../harness/src/frm";
import { canonNodes, type CanonNode } from "../../harness/src/frm-native";
import {
  conveyorsLogProblems, droneRoutesLogProblems, extractorsLogProblems, factoryLogProblems, generatorsLogProblems, stationRatesLogProblems, storageLogProblems, kindCheck, logisticsLineProblems, mapMarkersLogProblems, nodesLogProblems,
  playersLogProblems, powerLinesLogProblems, railsLogProblems, stepCosts, trainsLogProblems, withProblems,
} from "../../harness/src/reader-lines";
import type { Verdict } from "../../harness/src/verdict";
import { judgeFactory, judgeRates, judgeRoutes, judgeStations, judgeTrains, productivityRange, type S2Evidence, type S2Result } from "./pass-s2";
import { judgeConveyors, judgeRails, type S3Evidence } from "./pass-s3";
import { judgeDrone, judgeMarkers, judgeWires, type S4Evidence, type S4Result } from "./pass-s4";
import {
  judgeExtractors, judgeGenerators, judgeStations as judgeStationRates, judgeStorage, type ExtractorsEvidence, type GeneratorsEvidence, type S4bResult,
  type StationsEvidence, type StorageEvidence,
} from "./pass-s4b";
import type { Circuit, Posted, ServerState } from "../parity/pass";
import { inBracket, NEED_PAIRS, PARITY_POWER_TOLERANCE, WINDOW_MINUTES as PARITY_WINDOW_MINUTES } from "../parity/pass";

export * from "./pass-s2";
export * from "./pass-s3";
export * from "./pass-s4";
// pass-s4b's judgeStations clashes with S2's, so it is re-exported as judgeStationRates; everything else keeps its name.
export {
  S4B_GAP_MS, S4B_SHOW, EXTRACTOR_MAX_ABS, EXTRACTOR_MAX_REL, EXTRACTOR_POINTS, GENERATOR_CAP_ABS_MW, GENERATOR_CAP_REL, GENERATOR_CUR_ABS_MW,
  GENERATOR_CUR_REL, STATION_ABS, STATION_REL, STATION_FACTOR_MIN_F, STATION_FACTOR_LOW, STATION_FACTOR_HIGH, STORAGE_FLUID_REL, STORAGE_FLUID_ABS_M3,
  judgeExtractors, judgeGenerators, judgeStorage,
} from "./pass-s4b";
export { judgeStationRates };
export type { ExtractorsEvidence, GeneratorsEvidence, StationsEvidence, StorageEvidence, S4bEvidence, S4bResult } from "./pass-s4b";

export const requirements = [
  "native-parity", "R0-power", "R1-players", "R4-resource-nodes", "R2a-factory", "R2b-rates", "R3-trains", "R3-stations", "R3-train-routes", "R5-rails", "R6-conveyors",
  "R8-map-markers", "R9-drone-routes", "R10-power-lines",
  "R-extractors", "R-generators", "R-station-rates", "R-storage",
];
export const WINDOW_MINUTES = PARITY_WINDOW_MINUTES;
/** The parity run's bar: ceil(1800 / 5 x 0.8) rounds. */
export const NEED_ROUNDS = NEED_PAIRS;
/** Decision 3: power within 3%, the parity run's bar. */
export const POWER_TOLERANCE = PARITY_POWER_TOLERANCE;
export const PLAYERS_GAP_MS = 5_000;
export const NODES_GAP_MS = 120_000;
/**
 * Open question (owner, 2026-10-25): does a field the world never exercises (no battery, a fuse that never trips, no merged
 * group, nobody online, no exploited node) fail the reader? Ruling for now: no, it is reported as NOT EXERCISED. Set true to
 * make each such report a failure.
 */
export const NOT_EXERCISED_FAILS = false;
const SHOW = 10;

/** FRM reads as the harness's counters have them: startFrmPoller's field names (reads, failed), counted from the window's start. */
export interface FrmCounts { reads: number; failed: number }
export interface FrmBracket<T> { startedAt: string; endedAt: string; value: T }
/** One accepted mod snapshot's power, with FRM's last read before it and first after it (null: none within the gap). */
export interface PowerRound { observedAt: string; circuits: Circuit[]; before: FrmCircuitGroup[] | null; after: FrmCircuitGroup[] | null }
/** One accepted mod snapshot's players part (names already hashed by the recorder); keys null when the part was absent. */
export interface PlayerRound { observedAt: string; available: boolean | null; keys: string[] | null }
export interface NodePost { at: string; status: number; data: unknown }
export interface NativeParity {
  windowStartedAt: string | null;
  windowEndedAt: string | null;
  accepted: number;
  unrecorded: number;
  refused: Posted[];
  state: ServerState | null;
  power: { rounds: PowerRound[]; frm: FrmCounts; mergedGroupsMax: number | null };
  players: { rounds: PlayerRound[]; frmSamples: FrmBracket<string[]>[]; frm: FrmCounts };
  nodes: { posts: NodePost[]; frmSamples: FrmBracket<string>[]; distinct: Record<string, CanonNode[]>; frm: FrmCounts };
  /** Sprint S2's evidence (pass-s2.ts); absent in an S1 capture. */
  s2?: S2Evidence;
  /** Sprint S3's evidence (pass-s3.ts); absent in an S1 or S2 capture. */
  s3?: S3Evidence;
  /** Sprint S4-A's evidence (pass-s4.ts); absent in an S1 to S3 capture. */
  s4?: S4Evidence;
  /** Sprint S4-B's evidence (pass-s4b.ts); absent in an S1 to S4-A capture. */
  s4b?: S4bCapture;
}
/** What the S4-B run captures: one evidence block per reader, and the raw station pairs (the mod's posted rate beside FRM's, ids and numbers only; R4 evidence). */
export interface S4bCapture {
  extractors: ExtractorsEvidence; generators: GeneratorsEvidence; stations: StationsEvidence; storage: StorageEvidence;
  stationPairs: StationPair[];
}
export interface StationPair { at: string; id: string; mod: number; frm: Array<{ side: "before" | "after"; transfer: number; platformSum: number }> }
export interface ReaderVerdicts {
  power: Verdict; players: Verdict; resourceNodes: Verdict;
  /** S2: present exactly when the capture has `s2`. */
  factory?: Verdict; rates?: Verdict; trains?: Verdict; stations?: Verdict; trainRoutes?: Verdict;
  /** S3: present exactly when the capture has `s3`. */
  rails?: Verdict; conveyors?: Verdict;
  /** S4-A: present exactly when the capture has `s4`. */
  mapMarkers?: Verdict; droneRoutes?: Verdict; powerLines?: Verdict;
  /** S4-B: present exactly when the capture has `s4b`. */
  extractors?: Verdict; generators?: Verdict; stationRates?: Verdict; storage?: Verdict;
}

const ms = (iso: string) => Date.parse(iso);
const fmt = (n: number) => String(Math.round(n * 1000) / 1000);

/** The sample that ended by t and the one that started at or after t, each within gapMs. */
function around<T>(samples: FrmBracket<T>[], t: number, gapMs: number): { before: FrmBracket<T> | null; after: FrmBracket<T> | null } {
  const before = samples.filter((s) => ms(s.endedAt) <= t && t - ms(s.endedAt) <= gapMs).at(-1) ?? null;
  const after = samples.find((s) => ms(s.startedAt) >= t && ms(s.startedAt) - t <= gapMs) ?? null;
  return { before, after };
}

/** A passing verdict, or a failing one when the world left something unexercised and NOT_EXERCISED_FAILS says that fails. */
function passing(lines: string[], never: string[]): Verdict {
  if (never.length === 0) return { pass: true, reasons: lines };
  const note = `NOT EXERCISED by this world: ${never.join("; ")}`;
  return { pass: !NOT_EXERCISED_FAILS, reasons: [...lines, note] };
}

// ---------------------------------------------------------------------------------------------------------------- R0 power

const FIELD_PAIRS = [
  ["productionMW", "production"], ["consumptionMW", "consumption"], ["capacityMW", "capacity"], ["maxConsumptionMW", "maxConsumption"],
  ["batteryCapacityMWh", "batteryCapacity"], ["batteryPercent", "batteryPercent"], ["batteryDifferentialMW", "batteryDifferential"],
] as const satisfies ReadonlyArray<readonly [keyof Circuit, keyof FrmCircuitGroup]>;

const idsOf = (ids: number[]) => [...ids].sort((a, b) => a - b).join(",");

export function judgePower(p: NativeParity["power"]): Verdict {
  const reasons: string[] = [];
  let compared = 0;
  let batteryRounds = 0;
  let fuseRounds = 0;
  let missing = 0;
  for (const r of p.rounds) {
    if (!r.before || !r.after) {
      missing++;
      if (missing <= SHOW) reasons.push(`${r.observedAt}: no FRM power read ${r.before ? "after" : "before"} the mod's sample within the gap`);
      continue;
    }
    const seen = new Map<number, Circuit>();
    for (const c of r.circuits) {
      if (seen.has(c.circuitGroupId)) reasons.push(`${r.observedAt}: the mod sent group ${c.circuitGroupId} twice`);
      seen.set(c.circuitGroupId, c);
    }
    if (seen.size === 0) continue;
    const modIds = idsOf([...seen.keys()]);
    // Only a neighbour whose group ids match brackets the values (the parity run's rule).
    const neighbours = [r.before, r.after].filter((g) => idsOf(g.map((x) => x.groupId)) === modIds);
    compared++;
    if (neighbours.length === 0) {
      reasons.push(`${r.observedAt}: circuit groups mod ${modIds} vs FRM ${idsOf(r.before.map((x) => x.groupId))} before, ${idsOf(r.after.map((x) => x.groupId))} after`);
      continue;
    }
    for (const [id, mod] of seen) {
      const frm = neighbours.map((g) => g.find((x) => x.groupId === id)!);
      for (const [modKey, frmKey] of FIELD_PAIRS) {
        const values = frm.map((g) => g[frmKey]);
        const lo = Math.min(...values);
        const hi = Math.max(...values);
        if (!inBracket(mod[modKey], lo, hi)) reasons.push(`${r.observedAt}: group ${id} ${modKey} mod ${fmt(mod[modKey])} vs FRM ${lo === hi ? fmt(lo) : `${fmt(lo)}..${fmt(hi)}`}`);
      }
      if (!frm.some((g) => g.fuseTriggered === mod.fuseTriggered)) reasons.push(`${r.observedAt}: group ${id} fuseTriggered mod ${mod.fuseTriggered} vs FRM ${frm[0]!.fuseTriggered}`);
      if (mod.batteryCapacityMWh > 0) batteryRounds++;
      if (mod.fuseTriggered) fuseRounds++;
    }
  }
  if (missing > SHOW) reasons.push(`... ${missing - SHOW} more rounds had no FRM neighbour`);
  if (compared < NEED_ROUNDS) reasons.push(`power compared in only ${compared} rounds, need ${NEED_ROUNDS}`);
  if (p.frm.failed > 0) reasons.push(`${p.frm.failed} of the harness's FRM getPower reads in the window failed (${p.frm.reads} succeeded)`);
  if (reasons.length) return { pass: false, reasons: reasons.slice(0, 30) };
  const never = [
    batteryRounds === 0 ? "batteryCapacityMWh/batteryPercent/batteryDifferentialMW (no battery in any compared round)" : null,
    fuseRounds === 0 ? "fuseTriggered (never true)" : null,
    (p.mergedGroupsMax ?? 0) === 0 ? "the circuit-group merge (FRM never reported a group with more than one circuit)" : null,
  ].filter((x): x is string => x !== null);
  return passing([`${compared} rounds within ${POWER_TOLERANCE * 100}% of FRM's neighbouring reads; every compared field matches`], never);
}

// -------------------------------------------------------------------------------------------------------------- R1 players

export function judgePlayers(p: NativeParity["players"]): Verdict {
  const reasons: string[] = [];
  let compared = 0;
  let skipped = 0;
  let unpaired = 0;
  let onlineSeen = 0;
  let someoneListed = 0;
  for (const r of p.rounds) {
    if (r.keys === null || r.available !== true) {
      reasons.push(`${r.observedAt}: the mod's players part was ${r.keys === null ? "absent" : "not available"}`);
      continue;
    }
    const { before, after } = around(p.frmSamples, ms(r.observedAt), PLAYERS_GAP_MS);
    if (!before || !after) {
      unpaired++;
      continue;
    }
    if (before.value.join() !== after.value.join()) {
      skipped++; // somebody joined or left between FRM's two reads: no fair comparison
      continue;
    }
    compared++;
    if (before.value.length > 0) someoneListed++;
    if (before.value.some((k) => k.endsWith(":true"))) onlineSeen++;
    if (r.keys.join() !== before.value.join()) reasons.push(`${r.observedAt}: players mod [${r.keys.join(",")}] vs FRM [${before.value.join(",")}] (hash:online)`);
  }
  if (compared < NEED_ROUNDS) reasons.push(`players compared in only ${compared} rounds (${unpaired} had no FRM read within ${PLAYERS_GAP_MS} ms, ${skipped} fell across a join or leave), need ${NEED_ROUNDS}`);
  if (p.frm.failed > 0) reasons.push(`${p.frm.failed} of the harness's FRM getPlayer reads in the window failed (${p.frm.reads} succeeded)`);
  if (reasons.length) return { pass: false, reasons: reasons.slice(0, 30) };
  const never = [someoneListed === 0 ? "any listed player (both sides empty)" : null, onlineSeen === 0 ? "any online player" : null].filter((x): x is string => x !== null);
  return passing([`${compared} rounds, the (hash, online) lists equal FRM's`], never);
}

// ------------------------------------------------------------------------------------------------------ R4 resource nodes

const diff = (mod: string[], frm: string[]) => {
  const count = (xs: string[]) => xs.reduce((m, x) => m.set(x, (m.get(x) ?? 0) + 1), new Map<string, number>());
  const a = count(mod);
  const b = count(frm);
  const missing: string[] = [];
  const extra: string[] = [];
  for (const [k, n] of b) for (let i = a.get(k) ?? 0; i < n; i++) missing.push(k);
  for (const [k, n] of a) for (let i = b.get(k) ?? 0; i < n; i++) extra.push(k);
  return { missing, extra };
};

export function judgeNodes(n: NativeParity["nodes"]): Verdict {
  const reasons: string[] = [];
  let compared = 0;
  let exploited = 0;
  let items = 0;
  if (n.posts.length === 0) reasons.push("the mod posted no resourceNodes layer in the window");
  for (const post of n.posts) {
    const parsed = ResourceNodesLayerDataSchema.safeParse(post.data);
    if (!parsed.success) {
      reasons.push(`${post.at}: the posted layer is not valid against the pinned contract (${parsed.error.issues[0]?.message ?? "?"})`);
      continue;
    }
    if (parsed.data.length === 0) {
      reasons.push(`${post.at}: the posted layer is empty`);
      continue;
    }
    const { before, after } = around(n.frmSamples, ms(post.at), NODES_GAP_MS);
    if (!before && !after) {
      reasons.push(`${post.at}: no FRM getResourceNode sample within ${NODES_GAP_MS / 1000} s`);
      continue;
    }
    compared++;
    items = parsed.data.length;
    exploited = parsed.data.filter((x) => x.exploited).length;
    const mod = canonNodes(parsed.data as CanonNode[]);
    const sides = [before, after].filter((s): s is FrmBracket<string> => s !== null);
    const lists = sides.map((s) => canonNodes(n.distinct[s.value] ?? []));
    if (lists.some((l) => l.join("\n") === mod.join("\n"))) continue;
    const d = diff(mod, lists[0]!);
    reasons.push(`${post.at}: the layer differs from FRM's: ${d.missing.length} in FRM and not in the mod, ${d.extra.length} in the mod and not in FRM`);
    if (d.missing.length) reasons.push(`  in FRM only (first ${SHOW}): ${d.missing.slice(0, SHOW).join(" ")}`);
    if (d.extra.length) reasons.push(`  in the mod only (first ${SHOW}): ${d.extra.slice(0, SHOW).join(" ")}`);
  }
  if (n.frm.failed > 0 && n.frmSamples.length === 0) reasons.push("no FRM getResourceNode sample succeeded");
  if (reasons.length) return { pass: false, reasons: reasons.slice(0, 30) };
  return passing(
    [`${compared} posted layer(s) valid against the contract and equal to FRM's (${items} items, ${exploited} exploited)`],
    exploited === 0 ? ["exploited true (no occupied node)"] : [],
  );
}

// ------------------------------------------------------------------------------------------------------------- the run

const finish = (r: S2Result | S4Result | S4bResult): Verdict => (r.pass ? passing(r.reasons, r.never) : { pass: false, reasons: r.reasons });

/**
 * The verdicts, each computed from its own evidence only: S1's three unchanged, and S2's five when the capture has them. `lines`
 * (the mod's log) gives the rates judge the raw productivity range for its scale check; without them that check is skipped.
 */
export function judgeReaders(c: NativeParity, lines: readonly string[] = []): ReaderVerdicts {
  const s1 = { power: judgePower(c.power), players: judgePlayers(c.players), resourceNodes: judgeNodes(c.nodes) };
  let out: ReaderVerdicts = s1;
  if (c.s2) {
    const { factory, mapLive, routes } = c.s2;
    out = {
      ...out,
      factory: finish(judgeFactory(factory)),
      rates: finish(judgeRates(factory, productivityRange(lines))),
      trains: finish(judgeTrains(mapLive)),
      stations: finish(judgeStations(mapLive)),
      trainRoutes: finish(judgeRoutes(routes)),
    };
  }
  // S3's two judges read only their own evidence (the conveyors judge also takes the mod's `Conveyor kinds:` census line, when logged).
  if (c.s3) out = { ...out, rails: finish(judgeRails(c.s3.rails)), conveyors: finish(judgeConveyors(c.s3.conveyors, kindCheck(lines))) };
  // S4-A's three judges read only their own evidence; one failing leaves the others' verdicts alone.
  // judgeWires' optional { dropped } is not passed: there is no wires-only dropped parser, so the dropped-wire count check is skipped.
  if (c.s4) out = { ...out, mapMarkers: finish(judgeMarkers(c.s4.markers)), droneRoutes: finish(judgeDrone(c.s4.drone)), powerLines: finish(judgeWires(c.s4.wires)) };
  // S4-B's four judges likewise read only their own evidence.
  if (c.s4b) {
    out = {
      ...out,
      extractors: finish(judgeExtractors(c.s4b.extractors)), generators: finish(judgeGenerators(c.s4b.generators)),
      stationRates: finish(judgeStationRates(c.s4b.stations)), storage: finish(judgeStorage(c.s4b.storage)),
    };
  }
  return out;
}

/** Why the log does not show a sliced S2 read completed (its `<name> (game thread only):` line): the values would pass trivially otherwise. */
const readLineProblems = (lines: readonly string[], reader: string): string[] =>
  stepCosts(lines).some((x) => x.reader === reader) ? [] : [`the mod's log has no "${reader} (game thread only):" line: the native read never completed`];

/** S2 is expected when the capture has its evidence or the config asked for either S2 source (so a drive that lost the evidence fails closed). */
const expectsS2 = (c: NativeParity, sources: unknown): boolean => {
  const s = sources as { factorySource?: unknown; trainsSource?: unknown } | undefined | null;
  return c.s2 !== undefined || s?.factorySource !== undefined || s?.trainsSource !== undefined;
};

/** S3 is expected when the capture has its evidence or the config asked for either S3 source (so a drive that lost the evidence fails closed). */
const expectsS3 = (c: NativeParity, sources: unknown): boolean => {
  const s = sources as { railsSource?: unknown; conveyorsSource?: unknown } | undefined | null;
  return c.s3 !== undefined || s?.railsSource !== undefined || s?.conveyorsSource !== undefined;
};

/** S4-A is expected when the capture has its evidence or the config asked for any of the three S4 sources (so a drive that lost the evidence fails closed). */
const expectsS4 = (c: NativeParity, sources: unknown): boolean => {
  const s = sources as { mapMarkersSource?: unknown; droneRoutesSource?: unknown; powerLinesSource?: unknown } | undefined | null;
  return c.s4 !== undefined || s?.mapMarkersSource !== undefined || s?.droneRoutesSource !== undefined || s?.powerLinesSource !== undefined;
};

/** S4-B is expected when the capture has its evidence or the config asked for any of the four S4-B sources (so a drive that lost the evidence fails closed). */
const expectsS4b = (c: NativeParity, sources: unknown): boolean => {
  const s = sources as { extractorsSource?: unknown; generatorsSource?: unknown; stationRatesSource?: unknown; storageSource?: unknown } | undefined | null;
  return c.s4b !== undefined || s?.extractorsSource !== undefined || s?.generatorsSource !== undefined || s?.stationRatesSource !== undefined || s?.storageSource !== undefined;
};

/** What must hold for any verdict to mean anything: the window, the mod enrolled, nothing refused or unreadable. */
function runReasons(c: NativeParity, sources: unknown): string[] {
  const out: string[] = [];
  if (!c.windowStartedAt || !c.windowEndedAt) out.push(`the ${WINDOW_MINUTES}-minute window was not recorded`);
  else {
    const minutes = (ms(c.windowEndedAt) - ms(c.windowStartedAt)) / 60_000;
    if (!(minutes >= WINDOW_MINUTES)) out.push(`the window ran ${minutes.toFixed(1)} minutes, need ${WINDOW_MINUTES}`);
  }
  const s = sources as { playersSource?: string; resourceNodesSource?: string; factorySource?: string; trainsSource?: string } | undefined | null;
  if (s?.playersSource !== "native" || s?.resourceNodesSource !== "native") out.push("the run did not write playersSource and resourceNodesSource as native into the mod's config");
  if (expectsS2(c, sources)) {
    if (s?.factorySource !== "native" || s?.trainsSource !== "native") out.push("the run did not write factorySource and trainsSource as native into the mod's config");
    if (!c.s2) out.push("the S2 evidence (factory, mapLive, trainRoutes) was not captured");
  }
  if (expectsS3(c, sources)) {
    const t = sources as { railsSource?: string; conveyorsSource?: string } | undefined | null;
    if (t?.railsSource !== "native" || t?.conveyorsSource !== "native") out.push("the run did not write railsSource and conveyorsSource as native into the mod's config");
    if (!c.s3) out.push("the S3 evidence (rails, conveyors) was not captured");
  }
  if (expectsS4(c, sources)) {
    const t = sources as { mapMarkersSource?: string; droneRoutesSource?: string; powerLinesSource?: string } | undefined | null;
    if (t?.mapMarkersSource !== "native" || t?.droneRoutesSource !== "native" || t?.powerLinesSource !== "native") {
      out.push("the run did not write mapMarkersSource, droneRoutesSource and powerLinesSource as native into the mod's config");
    }
    if (!c.s4) out.push("the S4 evidence (mapMarkers, droneRoutes, powerLines) was not captured");
  }
  if (expectsS4b(c, sources)) {
    const t = sources as { extractorsSource?: string; generatorsSource?: string; stationRatesSource?: string; storageSource?: string } | undefined | null;
    if (t?.extractorsSource !== "native" || t?.generatorsSource !== "native" || t?.stationRatesSource !== "native" || t?.storageSource !== "native") {
      out.push("the run did not write extractorsSource, generatorsSource, stationRatesSource and storageSource as native into the mod's config");
    }
    if (!c.s4b) out.push("the S4-B evidence (extractors, generators, station rates, storage) was not captured");
  }
  if (!c.state) out.push("the mod's server state was not read");
  else {
    if (!c.state.codeConsumed) out.push("the mod's code was not consumed");
    if (!c.state.lastSeen) out.push("the backend never saw the mod's authenticated call");
  }
  if (c.refused.length) out.push(`the backend didn't accept ${c.refused.length} of the mod's posts (answers ${c.refused.map((p) => p.status).join(", ")})`);
  if (c.unrecorded) out.push(`${c.unrecorded} of the mod's accepted snapshots couldn't be read by the recorder`);
  return out;
}

export function evaluate(e: CoreEvidence): Verdict {
  const c = e.captures["native-parity.json"] as NativeParity | undefined;
  if (!c) return { pass: false, reasons: ["no native-parity capture"] };
  // Values alone cannot tell a native reader from the bridge (the mod's output then equals FRM's): the mod's own log must say
  // the reader was native, and the nodes reader must have completed a scan.
  const judged = judgeReaders(c, e.lines);
  const v: ReaderVerdicts = {
    ...judged,
    players: withProblems(judged.players, playersLogProblems(e.lines)),
    resourceNodes: withProblems(judged.resourceNodes, nodesLogProblems(e.lines)),
  };
  const run = runReasons(c, e.captures["mod-config.json"]);
  const names: Array<[string, string, Verdict]> = [["R0 power", "power", v.power], ["R1 players", "players", v.players], ["R4 resource nodes", "resourceNodes", v.resourceNodes]];
  if (expectsS2(c, e.captures["mod-config.json"])) {
    // The S2 readers must be native and have completed a read; a capture that lost its S2 evidence fails each of them.
    const none: Verdict = { pass: false, reasons: ["no S2 evidence in the capture"] };
    const factoryProblems = [...factoryLogProblems(e.lines), ...readLineProblems(e.lines, "Factory read")];
    const trainsProblems = [...trainsLogProblems(e.lines), ...readLineProblems(e.lines, "Trains read")];
    const routesProblems = [...trainsLogProblems(e.lines), ...readLineProblems(e.lines, "Train routes read")];
    names.push(
      ["R2a factory", "factory", withProblems(v.factory ?? none, factoryProblems)],
      ["R2b rates", "rates", withProblems(v.rates ?? none, factoryProblems)],
      ["R3 trains", "trains", withProblems(v.trains ?? none, trainsProblems)],
      ["R3 stations", "stations", withProblems(v.stations ?? none, trainsProblems)],
      ["R3 trainRoutes", "trainRoutes", withProblems(v.trainRoutes ?? none, routesProblems)],
    );
  }
  if (expectsS3(c, e.captures["mod-config.json"])) {
    // S3's readers: native by the Readers line, and the shared pass logged serving their layer. One failing leaves the other's verdict alone.
    const none: Verdict = { pass: false, reasons: ["no S3 evidence in the capture"] };
    names.push(
      ["R5 rails", "rails", withProblems(v.rails ?? none, [...railsLogProblems(e.lines), ...logisticsLineProblems(e.lines, "rails")])],
      ["R6 conveyors", "conveyors", withProblems(v.conveyors ?? none, [...conveyorsLogProblems(e.lines), ...logisticsLineProblems(e.lines, "conveyors")])],
    );
  }
  if (expectsS4(c, e.captures["mod-config.json"])) {
    // S4-A's readers: native by the Readers line and having completed a read (the shared logistics pass serves powerLines).
    const none: Verdict = { pass: false, reasons: ["no S4 evidence in the capture"] };
    names.push(
      ["R8 mapMarkers", "mapMarkers", withProblems(v.mapMarkers ?? none, [...mapMarkersLogProblems(e.lines), ...readLineProblems(e.lines, "Map markers read")])],
      ["R9 droneRoutes", "droneRoutes", withProblems(v.droneRoutes ?? none, [...droneRoutesLogProblems(e.lines), ...readLineProblems(e.lines, "Drone routes read")])],
      ["R10 powerLines", "powerLines", withProblems(v.powerLines ?? none, [...powerLinesLogProblems(e.lines), ...logisticsLineProblems(e.lines, "powerLines")])],
    );
  }
  if (expectsS4b(c, e.captures["mod-config.json"])) {
    // S4-B's readers: native by the Readers line; the extractors, generators and storage also need the Buildables pass to have completed (the station rates ride the trains' mapLive part).
    const none: Verdict = { pass: false, reasons: ["no S4-B evidence in the capture"] };
    names.push(
      ["R-extractors", "extractors", withProblems(v.extractors ?? none, [...extractorsLogProblems(e.lines), ...readLineProblems(e.lines, "Buildables read")])],
      ["R-generators", "generators", withProblems(v.generators ?? none, [...generatorsLogProblems(e.lines), ...readLineProblems(e.lines, "Buildables read")])],
      ["R-station-rates", "stationRates", withProblems(v.stationRates ?? none, [...stationRatesLogProblems(e.lines)])],
      ["R-storage", "storage", withProblems(v.storage ?? none, [...storageLogProblems(e.lines), ...readLineProblems(e.lines, "Buildables read")])],
    );
  }
  // readers.json is these same final verdicts, the ones the PASS/FAIL lines come from: the drive wrote a first version without the
  // log, and this replaces it, so the file can only be as strict as result.json. The core run writes the captures after evaluate.
  e.captures["readers.json"] = Object.fromEntries(names.map(([, key, x]) => [key, x]));
  const lines = names.flatMap(([name, , x]) => x.reasons.map((r) => `${name} ${x.pass ? "PASS" : "FAIL"}: ${r}`));
  return { pass: run.length === 0 && names.every(([, , x]) => x.pass), reasons: [...run, ...lines] };
}
