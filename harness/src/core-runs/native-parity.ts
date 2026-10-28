import { createHash } from "node:crypto";
import * as Pass from "../../../runs/native-parity/pass";
import { FRM_BRACKET_GAP_MS } from "../../../runs/enrol/pass";
import { NATIVE_SOURCES_S4B, type CoreContext, type CoreRunSpec } from "../core-run";
import { canonDrone, canonMarkers, canonWires, mapFrmDroneFull, mapFrmMarkers, mapFrmWires, type FrmDronePort, type FrmMarker, type FrmWire } from "../frm-s4";
import { evidenceHash } from "../evidence-hash";
import { mapFrmExtractors, mapFrmGenerators, mapFrmStationRates, mapFrmStorage, type FrmContainer, type FrmExtractor, type FrmGenerator, type FrmStationRate } from "../frm-s4b";
import { FactoryBuildingSchema } from "../../contract/src/factory";
import { PowerGeneratorSchema } from "../../contract/src/power";
import { StorageContainerSchema } from "../../contract/src/storage";
import { AgentFactorySchema } from "../../contract/src/agent";
import { MapLiveSchema, MapTrainStationSchema, TrainRoutesLayerDataSchema } from "../../contract/src/map";
import { type FrmRead, parseGetPowerFull, pickBracket, startFrmPoller } from "../frm";
import { canonNodes, countMergedGroups, mapFrmResourceNodes, playerKeys, startSampler, type CanonNode } from "../frm-native";
import { canonEdges, canonRails, mapFrmConveyors, mapFrmDronePorts, mapFrmRails, type FrmConveyorSample, type FrmRail } from "../frm-s3";
import {
  countSomersloops, createFactoryEncoder, FactoryTable, hashRoutes, mapFrmFactory, readFrmRoutes, readFrmStations, readFrmTrains, type CanonMachine,
  type FactorySampleRows, type RouteRecord,
} from "../frm-s2";
import { publicEntries, startRecorder, type RecordedCall } from "../recorder";
import { accepted, enrolPhase } from "./enrol";

const MIN = 60_000;
const SECOND = 1000;
const POWER_POLL_MS = 250; // the enrol run's pause between getPower reads
const PLAYERS_POLL_MS = 2 * SECOND;
const NODES_POLL_MS = MIN;
// S2's FRM samplers (the owner's brief for Task 10): getFactory every 5 s, getTrains every 2 s, getTrainStation every 30 s, and the
// getTrainStation+getTrains pair for the routes layer every minute and at once after each mod routes post.
const FACTORY_POLL_MS = 5 * SECOND;
const TRAINS_POLL_MS = 2 * SECOND;
const STATIONS_POLL_MS = 30 * SECOND;
const ROUTES_POLL_MS = MIN;
const realSleep = (ms: number) => new Promise<void>((r) => setTimeout(r, ms));
const iso = (ms: number) => new Date(ms).toISOString();

type Snap = {
  observedAt?: string;
  power?: { circuits?: Pass.PowerRound["circuits"] };
  // The recorder has already replaced each name with its hash (hashPlayers, recorder.ts): this is `<hash>:<online>`.
  players?: { available?: boolean; players?: Array<{ name: string; online: boolean }> };
};

/**
 * Turns the mod's accepted snapshots into the judge's rounds as they arrive (the drive's cursor and settle logic, apart from
 * the clock and the recorder so it can be tested). feed() reads the accepted list from where it left off: each new snapshot
 * gives a players round at once and waits for its power neighbours; settle() gives a snapshot its power round once an FRM read
 * has started at or after its observedAt, or FRM_BRACKET_GAP_MS has passed (or force), whichever comes first.
 */
export function createRoundCollector(opts: { reads: () => FrmRead[]; now: () => number; startAt: number }) {
  let cursor = opts.startAt;
  const waiting: RecordedCall[] = [];
  const power: Pass.PowerRound[] = [];
  const players: Pass.PlayerRound[] = [];
  const observed = (e: RecordedCall) => Date.parse(String((e.snapshot as Snap | undefined)?.observedAt ?? ""));
  return {
    power,
    players,
    get waiting() {
      return waiting.length;
    },
    feed(all: RecordedCall[]) {
      while (cursor < all.length) {
        const e = all[cursor++]!;
        waiting.push(e);
        const snap = e.snapshot as Snap | undefined;
        if (snap?.observedAt) {
          players.push({
            observedAt: snap.observedAt,
            available: typeof snap.players?.available === "boolean" ? snap.players.available : null,
            keys: Array.isArray(snap.players?.players) ? snap.players.players.map((p) => `${p.name}:${p.online}`).sort() : null,
          });
        }
      }
    },
    settle(force: boolean) {
      for (const e of [...waiting]) {
        const o = observed(e);
        const ready = !Number.isFinite(o) || force || opts.reads().some((r) => r.startedAt >= o) || opts.now() - o > FRM_BRACKET_GAP_MS;
        if (!ready) continue;
        waiting.splice(waiting.indexOf(e), 1);
        if (!Number.isFinite(o)) continue;
        const snap = e.snapshot as Snap;
        if (snap.power?.circuits) {
          const b = pickBracket(opts.reads(), o, FRM_BRACKET_GAP_MS);
          power.push({ observedAt: snap.observedAt!, circuits: snap.power.circuits, before: b.before?.groups ?? null, after: b.after?.groups ?? null });
        }
      }
    },
  };
}

/**
 * The judge's S2 rounds from the mod's accepted snapshots (their `parts`, which the recorder kept with mapLive names hashed): one
 * factory round per snapshot that carried a factory part and one mapLive round per snapshot that carried a mapLive part. A part that
 * does not parse against the pinned contract is a round with null rows, which the judge reports. Names of trains and stations are
 * hashes already; the factory's machine names are building display names, not player text.
 */
export function s2RoundsFrom(entries: readonly RecordedCall[], table: FactoryTable): { factory: Pass.FactoryRound[]; mapLive: Pass.LiveRound[] } {
  const factory: Pass.FactoryRound[] = [];
  const mapLive: Pass.LiveRound[] = [];
  for (const e of entries) {
    const observedAt = (e.snapshot as Snap | undefined)?.observedAt;
    if (!observedAt || !e.parts) continue;
    if (e.parts.factory !== undefined) {
      const parsed = AgentFactorySchema.safeParse(e.parts.factory);
      factory.push({ observedAt, rows: parsed.success ? parsed.data.buildings.map((b) => table.row(b as CanonMachine)) : null });
    }
    if (e.parts.mapLive !== undefined) {
      const parsed = MapLiveSchema.safeParse(e.parts.mapLive);
      mapLive.push({ observedAt, trains: parsed.success ? parsed.data.trains : null, stations: parsed.success ? parsed.data.stations : null });
    }
  }
  return { factory, mapLive };
}

/** The trainRoutes layers the mod posted, with names hashed before they enter evidence (the recorder keeps the raw data in memory only). */
export function routePosts(entries: readonly RecordedCall[], from: number): Pass.NodePost[] {
  return entries
    .filter((e) => e.agent === "mod" && e.world?.layer === "trainRoutes" && e.status === 200 && Date.parse(e.at) >= from)
    .map((e) => {
      // A layer that is not valid is kept as a marker only (its names could not be hashed field by field); the judge fails it as invalid.
      const parsed = TrainRoutesLayerDataSchema.safeParse(e.worldData);
      return { at: e.at, status: e.status, data: parsed.success ? hashRoutes(parsed.data as RouteRecord[]) : "not a valid trainRoutes layer (not recorded)" };
    });
}

/** FRM reads made since the window began: a read that failed while the world was still coming up never reaches a judge. */
export const sinceWindow = (now: { reads: number; failed: number }, atStart: { reads: number; failed: number }): Pass.FrmCounts => ({
  reads: now.reads - atStart.reads,
  failed: now.failed - atStart.failed,
});

// S3's FRM samplers (the owner's brief for Task 13). The world layers post about every 10 minutes, so these are slow: rails and conveyors
// every 2 minutes (the judge's S3_GAP_MS) and at once after each accepted post, the drone ports every minute. S1's power and players
// judges fail an FRM read over 2 s, so none of these runs continuously.
export const RAILS_POLL_MS = 2 * MIN;
export const CONVEYORS_POLL_MS = 2 * MIN;
export const DRONE_POLL_MS = MIN;

type Get = (path: string, timeoutMs: number) => Promise<unknown>;
type Clock = { now: () => number; sleep: (ms: number) => Promise<void> };

/** The three S3 FRM samplers, started together (before the mod enrols, so an early post has neighbours on both sides). */
export function startS3Samplers(get: Get, clock: Clock = { now: Date.now, sleep: realSleep }) {
  const railsHash = (mapped: FrmRail[]) => createHash("sha256").update(canonRails(mapped).join("\n")).digest("hex").slice(0, 16);
  const edgesHash = (mapped: FrmConveyorSample) => createHash("sha256").update(canonEdges(mapped).join("\n")).digest("hex").slice(0, 16);
  const rails = startSampler(
    async () => {
      const mapped = mapFrmRails(await get("/getTrainRails", 15_000));
      return { hash: railsHash(mapped), mapped };
    },
    { intervalMs: RAILS_POLL_MS, ...clock },
  );
  const conveyors = startSampler(
    async () => {
      const belts = await get("/getBelts", 20_000);
      const lifts = await get("/getLifts", 20_000);
      const pipes = await get("/getPipes", 20_000);
      const mapped = mapFrmConveyors(belts, lifts, pipes);
      return { hash: edgesHash(mapped), mapped };
    },
    { intervalMs: CONVEYORS_POLL_MS, ...clock },
  );
  // Ids only: a port's label is player text and never leaves mapFrmDronePorts.
  const drone = startSampler(async () => mapFrmDronePorts(await get("/getDroneStation", 5000)), { intervalMs: DRONE_POLL_MS, ...clock });
  return {
    rails, conveyors, drone,
    counts: () => ({ rails: rails.counts(), conveyors: conveyors.counts(), drone: drone.counts() }),
    stop() {
      rails.stop();
      conveyors.stop();
      drone.stop();
    },
  };
}
export type S3Samplers = ReturnType<typeof startS3Samplers>;

/** The accepted posts of one S3 layer by the mod, since `from` (the start of enrolment: the first native read can land before the window). */
export function s3LayerPosts(entries: readonly RecordedCall[], layer: "rails" | "conveyors", from: number): Array<{ at: string; status: number; data: unknown }> {
  return entries
    .filter((e) => e.agent === "mod" && e.world?.layer === layer && e.status === 200 && Date.parse(e.at) >= from)
    .map((e) => ({ at: e.at, status: e.status, data: e.worldData }));
}

/**
 * Takes a fresh FRM sample for each S3 layer that has a new accepted mod post since enrolment (so the judge has a sample within its
 * gap). `seen` is the drive's per-layer count of posts already answered; it is updated here. Returns the layers sampled.
 */
export async function takeAfterNewS3Posts(
  s: Pick<S3Samplers, "rails" | "conveyors">,
  entries: readonly RecordedCall[],
  enrolledAt: number,
  seen: { rails: number; conveyors: number },
): Promise<Array<"rails" | "conveyors">> {
  const done: Array<"rails" | "conveyors"> = [];
  for (const layer of ["rails", "conveyors"] as const) {
    const count = s3LayerPosts(entries, layer, enrolledAt).length;
    if (count > seen[layer]) {
      seen[layer] = count;
      await s[layer].take();
      done.push(layer);
    }
  }
  return done;
}

/** Sprint S3's evidence: the posts since enrolment, the FRM samples as hashes with each distinct mapped body kept once, the ports' ids. */
export function s3EvidenceFrom(s: S3Samplers, entries: readonly RecordedCall[], enrolledAt: number, atStart: ReturnType<S3Samplers["counts"]>): Pass.S3Evidence {
  const iv = (x: { startedAt: number; endedAt: number }) => ({ startedAt: iso(x.startedAt), endedAt: iso(x.endedAt) });
  const railsDistinct: Record<string, FrmRail[]> = {};
  for (const x of s.rails.samples) railsDistinct[x.value.hash] ??= x.value.mapped;
  const convDistinct: Record<string, FrmConveyorSample> = {};
  for (const x of s.conveyors.samples) convDistinct[x.value.hash] ??= x.value.mapped;
  const ids = [...new Set(s.drone.samples.flatMap((x) => x.value))].sort();
  const now = s.counts();
  return {
    rails: {
      posts: s3LayerPosts(entries, "rails", enrolledAt),
      frmSamples: s.rails.samples.map((x) => ({ ...iv(x), value: x.value.hash })),
      distinct: railsDistinct,
      frm: sinceWindow(now.rails, atStart.rails),
    },
    conveyors: {
      posts: s3LayerPosts(entries, "conveyors", enrolledAt),
      frmSamples: s.conveyors.samples.map((x) => ({ ...iv(x), value: x.value.hash })),
      distinct: convDistinct,
      frm: sinceWindow(now.conveyors, atStart.conveyors),
      drone: { ids, frm: sinceWindow(now.drone, atStart.drone) },
    },
  };
}

// S4-A's FRM samplers (Task 15): markers, drone ports (the full mapping, beside S3's ids-only sampler, which stays as it was) and
// cables, at once after each accepted post and every S4_SAMPLER_MS. The timeouts are the brief's.
// The sampler sleeps the full interval after each read, so the real period is the interval plus the read time. At 120 s (the judge's
// S4_GAP_MS) a mod post could find no FRM sample inside the window; 90 s keeps every post within S4_GAP_MS of a sample
// (controller, 2026-10-28). S2 and S3 samplers are unchanged.
export const S4_SAMPLER_MS = 90_000;
export const MARKERS_POLL_MS = S4_SAMPLER_MS;
export const DRONE_FULL_POLL_MS = S4_SAMPLER_MS;
export const WIRES_POLL_MS = S4_SAMPLER_MS;

/** The three S4 FRM samplers, started with the others (before the mod enrols). Each sample is `{ hash, mapped }` (frm-s4.ts, names already hashed). */
export function startS4Samplers(get: Get, clock: Clock = { now: Date.now, sleep: realSleep }) {
  const hashOf = (lines: string[]) => createHash("sha256").update(lines.join("\n")).digest("hex").slice(0, 16);
  const markers = startSampler(
    async () => {
      const mapped = mapFrmMarkers(await get("/getMapMarkers", 10_000));
      return { hash: hashOf(canonMarkers(mapped)), mapped };
    },
    { intervalMs: MARKERS_POLL_MS, ...clock },
  );
  const drone = startSampler(
    async () => {
      const mapped = mapFrmDroneFull(await get("/getDroneStation", 10_000));
      return { hash: hashOf(canonDrone(mapped)), mapped };
    },
    { intervalMs: DRONE_FULL_POLL_MS, ...clock },
  );
  const wires = startSampler(
    async () => {
      const mapped = mapFrmWires(await get("/getCables", 20_000));
      return { hash: hashOf(canonWires(mapped)), mapped };
    },
    { intervalMs: WIRES_POLL_MS, ...clock },
  );
  return {
    markers, drone, wires,
    counts: () => ({ markers: markers.counts(), drone: drone.counts(), wires: wires.counts() }),
    stop() {
      markers.stop();
      drone.stop();
      wires.stop();
    },
  };
}
export type S4Samplers = ReturnType<typeof startS4Samplers>;

export type S4Layer = "mapMarkers" | "droneRoutes" | "powerLines";
const S4_SAMPLER: Record<S4Layer, "markers" | "drone" | "wires"> = { mapMarkers: "markers", droneRoutes: "drone", powerLines: "wires" };
const isRec = (v: unknown): v is Record<string, unknown> => typeof v === "object" && v !== null && !Array.isArray(v);
/** A posted name as evidence: its evidenceHash. Anything that is not a string becomes a number, which the contract rejects (so the judge fails it) and which carries no text. */
const hashedName = (v: unknown): unknown => (typeof v === "string" ? evidenceHash(v) : v === null ? null : 0);

/**
 * One posted item with every name replaced by its evidenceHash (plan M6). Only the contract's own keys are kept, so an unexpected field
 * could not carry text into a capture. Ids, numbers, class names and hashes are what is left.
 */
function hashItemNames(layer: S4Layer, item: unknown): unknown {
  if (!isRec(item)) return null; // defence in depth: raw data never passes through
  if (layer === "mapMarkers") return { id: item.id, name: hashedName(item.name), kind: item.kind, icon: item.icon, color: item.color, x: item.x, y: item.y };
  if (layer === "droneRoutes") {
    return {
      id: item.id, name: hashedName(item.name), paired: hashedName(item.paired), ...(item.pairedId !== undefined ? { pairedId: item.pairedId } : {}),
      input: item.input, output: item.output, fuel: item.fuel, x: item.x, y: item.y,
    };
  }
  return { id: item.id, points: item.points };
}

/** The accepted posts of one S4 layer by the mod, since `from`, with every name hashed before it can reach a capture. */
export function s4LayerPosts(entries: readonly RecordedCall[], layer: S4Layer, from: number): Array<{ at: string; status: number; data: unknown }> {
  return entries
    .filter((e) => e.agent === "mod" && e.world?.layer === layer && e.status === 200 && Date.parse(e.at) >= from)
    .map((e) => ({ at: e.at, status: e.status, data: Array.isArray(e.worldData) ? e.worldData.map((i) => hashItemNames(layer, i)) : [] }));
}

/** Like takeAfterNewS3Posts: a fresh FRM sample for each S4 layer with a new accepted mod post. `seen` is updated here. Returns the layers sampled. */
export async function takeAfterNewS4Posts(
  s: Pick<S4Samplers, "markers" | "drone" | "wires">,
  entries: readonly RecordedCall[],
  enrolledAt: number,
  seen: Record<S4Layer, number>,
): Promise<S4Layer[]> {
  const done: S4Layer[] = [];
  for (const layer of ["mapMarkers", "droneRoutes", "powerLines"] as const) {
    const count = s4LayerPosts(entries, layer, enrolledAt).length;
    if (count > seen[layer]) {
      seen[layer] = count;
      await s[S4_SAMPLER[layer]].take();
      done.push(layer);
    }
  }
  return done;
}

/** Sprint S4-A's evidence: the posts since enrolment (names hashed), the FRM samples as hashes with each distinct mapped body kept once. */
export function s4EvidenceFrom(s: S4Samplers, entries: readonly RecordedCall[], enrolledAt: number, atStart: ReturnType<S4Samplers["counts"]>): Pass.S4Evidence {
  const iv = (x: { startedAt: number; endedAt: number }) => ({ startedAt: iso(x.startedAt), endedAt: iso(x.endedAt) });
  const now = s.counts();
  const layer = <T>(sampler: { samples: Array<{ startedAt: number; endedAt: number; value: { hash: string; mapped: T } }> }, name: S4Layer, k: "markers" | "drone" | "wires") => {
    const distinct: Record<string, T> = {};
    for (const x of sampler.samples) distinct[x.value.hash] ??= x.value.mapped;
    return {
      posts: s4LayerPosts(entries, name, enrolledAt),
      frmSamples: sampler.samples.map((x) => ({ ...iv(x), value: x.value.hash })),
      distinct,
      frm: sinceWindow(now[k], atStart[k]),
    };
  };
  return { markers: layer<FrmMarker[]>(s.markers, "mapMarkers", "markers"), drone: layer<FrmDronePort[]>(s.drone, "droneRoutes", "drone"), wires: layer<FrmWire[]>(s.wires, "powerLines", "wires") };
}

// S4-B's FRM samplers (Task 19): extractors, generators, station rates and storage. Same period as S4-A's (see S4_SAMPLER_MS: the real
// period is the interval plus the read, and the judge brackets a post by S4B_GAP_MS = 120 s), plus a fresh sample after each accepted
// snapshot that carries the layer's fragment. The read timeout is 20 s: /getStorageInv and /getExtractor are large.
export const S4B_SAMPLER_MS = S4_SAMPLER_MS;
const S4B_READ_MS = 20_000;

/** The four S4-B FRM samplers, started with the others (before the mod enrols). Each sample is `{ hash, mapped }` (frm-s4b.ts, names already hashed). */
export function startS4bSamplers(get: Get, clock: Clock = { now: Date.now, sleep: realSleep }) {
  const sampler = <T>(path: string, map: (body: unknown) => T[]) =>
    startSampler(
      async () => {
        const mapped = map(await get(path, S4B_READ_MS));
        return { hash: createHash("sha256").update(JSON.stringify(mapped)).digest("hex").slice(0, 16), mapped };
      },
      { intervalMs: S4B_SAMPLER_MS, ...clock },
    );
  const extractors = sampler("/getExtractor", mapFrmExtractors);
  const generators = sampler("/getGenerators", mapFrmGenerators);
  const stationRates = sampler("/getTrainStation", mapFrmStationRates);
  const storage = sampler("/getStorageInv", mapFrmStorage);
  return {
    extractors, generators, stationRates, storage,
    counts: () => ({ extractors: extractors.counts(), generators: generators.counts(), stationRates: stationRates.counts(), storage: storage.counts() }),
    stop() {
      extractors.stop();
      generators.stop();
      stationRates.stop();
      storage.stop();
    },
  };
}
export type S4bSamplers = ReturnType<typeof startS4bSamplers>;

export type S4bLayer = "extractors" | "generators" | "stationRates" | "storage";
const S4B_LAYERS = ["extractors", "generators", "stationRates", "storage"] as const;
type PostArray = { safeParse: (d: unknown) => { success: boolean; data?: unknown } };
/** The judge's own record schemas (they strip keys the contract does not name), per layer. */
const S4B_RECORD: Record<S4bLayer, PostArray> = {
  extractors: FactoryBuildingSchema, generators: PowerGeneratorSchema, stationRates: MapTrainStationSchema, storage: StorageContainerSchema,
};
/** Where a layer's fragment sits in an accepted snapshot's kept parts. */
const s4bFragment = (parts: NonNullable<RecordedCall["parts"]>, layer: S4bLayer): unknown => {
  const part = (k: string) => (parts as Record<string, unknown>)[k] as Record<string, unknown> | null | undefined;
  switch (layer) {
    case "extractors": return part("factory")?.extractors;
    case "generators": return part("power")?.generators;
    case "stationRates": return part("mapLive")?.stations;
    case "storage": return part("storage")?.containers;
  }
};
/** Every `name` string, at any depth, replaced by its evidenceHash (the empty name stays empty): station names are player text, the rest are game text. A station name arrives from the recorder already as sessionHash(name), so it leaves as evidenceHash(sessionHash(name)). */
function hashAllNames(v: unknown): unknown {
  if (Array.isArray(v)) return v.map(hashAllNames);
  if (!isRec(v)) return v;
  return Object.fromEntries(Object.entries(v).map(([k, x]) => [k, k === "name" && typeof x === "string" ? (x === "" ? "" : evidenceHash(x)) : hashAllNames(x)]));
}
/** One posted record as evidence: names hashed, then cut to the contract's own keys; a record the contract rejects becomes null (it fails the judge's schema and carries no text). */
function s4bRecord(layer: S4bLayer, item: unknown): unknown {
  const parsed = S4B_RECORD[layer].safeParse(hashAllNames(item));
  return parsed.success ? parsed.data : null;
}

/** The accepted snapshots of the mod since `from` that carry the layer's fragment (an array), as posts with every name hashed before it can reach a capture. */
export function s4bLayerPosts(entries: readonly RecordedCall[], layer: S4bLayer, from: number): Array<{ at: string; status: number; data: unknown }> {
  const out: Array<{ at: string; status: number; data: unknown }> = [];
  for (const e of entries) {
    if (e.agent !== "mod" || e.path !== "/agent/v1/snapshots" || e.status !== 200 || !e.parts || Date.parse(e.at) < from) continue;
    const fragment = s4bFragment(e.parts, layer);
    if (Array.isArray(fragment)) out.push({ at: e.at, status: e.status, data: fragment.map((i) => s4bRecord(layer, i)) });
  }
  return out;
}

/** The least time between two extra samples of one endpoint (controller ruling, 2026-10-27): snapshots come every 5 s. */
export const S4B_TAKE_MIN_MS = 30_000;
export interface S4bSeen { hash: string | null; takenAt: number }
/** The incremental state of takeAfterNewS4bPosts: `cursor` is how many recorder entries were looked at already, `examined` counts the work done (tests). */
export interface S4bState { cursor: number; examined: number; layers: Record<S4bLayer, S4bSeen> }
export const newS4bSeen = (): S4bState => ({
  cursor: 0, examined: 0,
  layers: {
    extractors: { hash: null, takenAt: -Infinity }, generators: { hash: null, takenAt: -Infinity },
    stationRates: { hash: null, takenAt: -Infinity }, storage: { hash: null, takenAt: -Infinity },
  },
});

/**
 * An extra FRM sample for an S4-B layer only after a snapshot that carries a fragment from a NEW mod read, and at most once per
 * S4B_TAKE_MIN_MS per endpoint (the periodic samplers stay). Extractors, storage and station rates: a snapshot carrying the fragment
 * is new (the mod sends them once per read; mapLive rides every snapshot, so the 30 s floor bounds it). Generators ride every power
 * part, so a part is new only when a hash of its generators array's JSON text differs from the last one seen.
 * Incremental: only the entries added since the last call are looked at (a cursor, as createRoundCollector does) and no evidence
 * record is built here (s4bEvidenceFrom does that once), so the cost per call is O(new entries). `seen` is updated here.
 * The due takes run concurrently. Returns the layers sampled.
 */
export async function takeAfterNewS4bPosts(
  s: Pick<S4bSamplers, S4bLayer>,
  entries: readonly RecordedCall[],
  enrolledAt: number,
  seen: S4bState,
  now: () => number = Date.now,
): Promise<S4bLayer[]> {
  const fresh = new Set<S4bLayer>();
  for (; seen.cursor < entries.length; seen.cursor++) {
    seen.examined++;
    const e = entries[seen.cursor]!;
    if (e.agent !== "mod" || e.path !== "/agent/v1/snapshots" || e.status !== 200 || !e.parts || Date.parse(e.at) < enrolledAt) continue;
    for (const layer of S4B_LAYERS) {
      const fragment = s4bFragment(e.parts, layer);
      if (!Array.isArray(fragment)) continue;
      if (layer !== "generators") {
        fresh.add(layer);
        continue;
      }
      const h = createHash("sha256").update(JSON.stringify(fragment)).digest("hex");
      if (h !== seen.layers.generators.hash) fresh.add(layer);
      seen.layers.generators.hash = h;
    }
  }
  const t = now();
  // Owner ruling 2026-10-28 after run 40623235820: the extractors endpoint has no floor (a sample right after EVERY new extractors post,
  // so the brackets are tight); the other endpoints keep S4B_TAKE_MIN_MS.
  const floorOf = (l: S4bLayer) => (l === "extractors" ? 0 : S4B_TAKE_MIN_MS);
  const due = S4B_LAYERS.filter((l) => fresh.has(l) && t - seen.layers[l].takenAt >= floorOf(l));
  for (const l of due) seen.layers[l].takenAt = t;
  await Promise.all(due.map((l) => s[l].take()));
  return due;
}

/** Sprint S4-B's evidence: the snapshot fragments since enrolment (names hashed), the FRM samples as hashes with each distinct mapped body kept once, and the raw station pairs. */
export function s4bEvidenceFrom(s: S4bSamplers, entries: readonly RecordedCall[], enrolledAt: number, atStart: ReturnType<S4bSamplers["counts"]>): Pass.S4bCapture {
  const iv = (x: { startedAt: number; endedAt: number }) => ({ startedAt: iso(x.startedAt), endedAt: iso(x.endedAt) });
  const now = s.counts();
  const layer = <T>(name: S4bLayer) => {
    const sampler = s[name] as unknown as { samples: Array<{ startedAt: number; endedAt: number; value: { hash: string; mapped: T[] } }> };
    const distinct: Record<string, T[]> = {};
    for (const x of sampler.samples) distinct[x.value.hash] ??= x.value.mapped;
    return {
      posts: s4bLayerPosts(entries, name, enrolledAt),
      frmSamples: sampler.samples.map((x) => ({ ...iv(x), value: x.value.hash })),
      distinct,
      frm: sinceWindow(now[name], atStart[name]),
    };
  };
  const stations = layer<FrmStationRate>("stationRates");
  return {
    extractors: layer<FrmExtractor>("extractors"), generators: layer<FrmGenerator>("generators"), stations, storage: layer<FrmContainer>("storage"),
    stationPairs: stationPairsFrom(stations),
  };
}

/** The mod's posted transferPerMinute next to FRM's TransferRate for the same station, on the same two sides the judge brackets with (ids and numbers only). */
export function stationPairsFrom(e: Pass.StationsEvidence): Pass.StationPair[] {
  const ms = (t: string) => Date.parse(t);
  const out: Pass.StationPair[] = [];
  for (const post of e.posts) {
    const at = ms(post.at);
    const before = e.frmSamples.filter((x) => ms(x.endedAt) <= at && at - ms(x.endedAt) <= Pass.S4B_GAP_MS).at(-1);
    const after = e.frmSamples.find((x) => ms(x.startedAt) >= at && ms(x.startedAt) - at <= Pass.S4B_GAP_MS);
    const sides = [["before", before], ["after", after]] as const;
    for (const rec of Array.isArray(post.data) ? post.data : []) {
      const r = rec as { id?: unknown; transferPerMinute?: unknown } | null;
      if (!r || typeof r.id !== "string" || typeof r.transferPerMinute !== "number") continue;
      const frm: Pass.StationPair["frm"] = [];
      for (const [side, x] of sides) {
        const f = x ? e.distinct[x.value]?.find((y) => y.id === r.id) : undefined;
        if (f) frm.push({ side, transfer: f.transfer, platformSum: f.platformSum });
      }
      out.push({ at: post.at, id: r.id, mod: r.transferPerMinute, frm });
    }
  }
  return out;
}

/**
 * The combined native-reader parity run for sprints S1 to S4-B (runs/native-parity/pass.ts says what is judged). The mod enrols
 * with all thirteen sources native (spec.modConfig, NATIVE_SOURCES_S4B), on the Test3 world; S2, S3 and S4 add their own FRM samplers (below,
 * startS3Samplers and startS4Samplers). The S1 part: Order: the world loads first (FRM is down before it,
 * docs/runner.md), the harness's FRM readers start, then the mod enrols (so its one resource-node post, about 8 minutes after
 * enrolment, lands inside the window), a one-minute settle, and 30 minutes of the harness reading FRM's getPower (back to
 * back), getPlayer (every 2 s) and getResourceNode (every minute, and at once after each mod post) beside the mod's posts.
 * The harness drives no construction, so the save is idle. Names are hashed before they are stored.
 */
export const nativeParity: CoreRunSpec = {
  requirements: Pass.requirements,
  backend: true,
  world: true,
  worldKey: "sprint3",
  modConfig: NATIVE_SOURCES_S4B,
  evaluate: Pass.evaluate,
  preflight(deps) {
    if (!deps.config.frmBaseUrl) throw new Error("native-parity needs frmBaseUrl in the runner config: FRM is the oracle (docs/runner.md)");
  },
  async drive(ctx: CoreContext) {
    const frmBase = ctx.deps.config.frmBaseUrl!;
    const get = async (path: string, timeoutMs: number): Promise<unknown> => {
      const r = await fetch(new URL(path, frmBase), { signal: AbortSignal.timeout(timeoutMs) });
      if (!r.ok) throw new Error(`${path} answered ${r.status}`);
      return r.json();
    };
    const host = await ctx.deps.backendHost!();
    const recorder = await startRecorder(host.relayUrl, { keepWorldData: ["resourceNodes", "trainRoutes", "rails", "conveyors", "mapMarkers", "droneRoutes", "powerLines"], keepSnapshotParts: ["factory", "mapLive", "power", "storage"] });
    const capture: Pass.NativeParity = {
      windowStartedAt: null, windowEndedAt: null, accepted: 0, unrecorded: 0, refused: [], state: null,
      power: { rounds: [], frm: { reads: 0, failed: 0 }, mergedGroupsMax: null },
      players: { rounds: [], frmSamples: [], frm: { reads: 0, failed: 0 } },
      nodes: { posts: [], frmSamples: [], distinct: {}, frm: { reads: 0, failed: 0 } },
    };
    let mergedMax = 0;
    let power: ReturnType<typeof startFrmPoller> | null = null;
    let players: ReturnType<typeof startSampler<string[]>> | null = null;
    let nodes: ReturnType<typeof startSampler<{ hash: string; mapped: CanonNode[] }>> | null = null;
    let rounds: ReturnType<typeof createRoundCollector> | null = null;
    let atStart = { power: { reads: 0, failed: 0 }, players: { reads: 0, failed: 0 }, nodes: { reads: 0, failed: 0 } };
    // S2: the factory samples are kept as the rows that changed (frm-s2.ts), the routes samples as a hash of the canonical layer.
    const table = new FactoryTable();
    const encoder = createFactoryEncoder(table);
    let sloops = 0;
    let factory: ReturnType<typeof startSampler<FactorySampleRows>> | null = null;
    let trains: ReturnType<typeof startSampler<ReturnType<typeof readFrmTrains>>> | null = null;
    let stations: ReturnType<typeof startSampler<ReturnType<typeof readFrmStations>>> | null = null;
    let routes: ReturnType<typeof startSampler<ReturnType<typeof readFrmRoutes>>> | null = null;
    let s2Start = { factory: { reads: 0, failed: 0 }, trains: { reads: 0, failed: 0 }, stations: { reads: 0, failed: 0 }, routes: { reads: 0, failed: 0 } };
    let s2From = 0;
    let s3: S3Samplers | null = null;
    let s3Start: ReturnType<S3Samplers["counts"]> = { rails: { reads: 0, failed: 0 }, conveyors: { reads: 0, failed: 0 }, drone: { reads: 0, failed: 0 } };
    let s4: S4Samplers | null = null;
    let s4Start: ReturnType<S4Samplers["counts"]> = { markers: { reads: 0, failed: 0 }, drone: { reads: 0, failed: 0 }, wires: { reads: 0, failed: 0 } };
    let s4b: S4bSamplers | null = null;
    let s4bStart: ReturnType<S4bSamplers["counts"]> = { extractors: { reads: 0, failed: 0 }, generators: { reads: 0, failed: 0 }, stationRates: { reads: 0, failed: 0 }, storage: { reads: 0, failed: 0 } };
    let enrolledAt = 0;
    try {
      await ctx.deps.loadWorld!("sprint3");
      await ctx.deps.waitHealthy();
      await ctx.sampleMemory("withMod");
      ctx.rememberAutoPause(await ctx.deps.readAutoPause!());
      await ctx.deps.setAutoPause!(false); // the factory runs, so power moves (S3)
      // FRM answers only once the world is up, so the readers start here (a failed read is also left out of the judged counts).
      power = startFrmPoller(
        async () => {
          const body = await get("/getPower", 2000);
          mergedMax = Math.max(mergedMax, countMergedGroups(body));
          return parseGetPowerFull(body);
        },
        { now: Date.now, sleep: realSleep, intervalMs: POWER_POLL_MS },
      );
      players = startSampler(async () => playerKeys(await get("/getPlayer", 3000)), { intervalMs: PLAYERS_POLL_MS, now: Date.now, sleep: realSleep });
      nodes = startSampler(
        async () => {
          const mapped = mapFrmResourceNodes(await get("/getResourceNode", 15_000));
          const hash = createHash("sha256").update(canonNodes(mapped).join("\n")).digest("hex").slice(0, 16);
          return { hash, mapped };
        },
        { intervalMs: NODES_POLL_MS, now: Date.now, sleep: realSleep },
      );
      // S2's oracle: FRM's own getFactory, getTrains and getTrainStation (and the pair for the routes layer), read as a black box from
      // before the mod enrols, so a mod part from the first minute of the window has FRM reads before it too.
      factory = startSampler(
        async () => {
          const body = await get("/getFactory", 20_000);
          sloops = Math.max(sloops, countSomersloops(body));
          return encoder.push(mapFrmFactory(body));
        },
        { intervalMs: FACTORY_POLL_MS, now: Date.now, sleep: realSleep },
      );
      trains = startSampler(async () => readFrmTrains(await get("/getTrains", 5000)), { intervalMs: TRAINS_POLL_MS, now: Date.now, sleep: realSleep });
      stations = startSampler(async () => readFrmStations(await get("/getTrainStation", 5000)), { intervalMs: STATIONS_POLL_MS, now: Date.now, sleep: realSleep });
      routes = startSampler(async () => readFrmRoutes(await get("/getTrainStation", 10_000), await get("/getTrains", 10_000)), { intervalMs: ROUTES_POLL_MS, now: Date.now, sleep: realSleep });
      // S3's oracle, also from before the mod enrols: getTrainRails, getBelts/getLifts/getPipes and the drone ports (ids only).
      s3 = startS3Samplers(get);
      // S4-A's oracle, likewise from before the mod enrols: getMapMarkers, getDroneStation (full) and getCables.
      s4 = startS4Samplers(get);
      // S4-B's oracle, likewise: getExtractor, getGenerators, getTrainStation (rates) and getStorageInv.
      s4b = startS4bSamplers(get);
      // Enrolled after the world is up, so the mod's one resource-node post (about 8 minutes after enrolment, then every 30)
      // lands inside the 30-minute window that opens a minute after enrolment.
      enrolledAt = Date.now();
      await enrolPhase(ctx, host, recorder);
      await ctx.deps.sleep(MIN); // the world settles
      const from = Date.now();
      capture.windowStartedAt = iso(from);
      atStart = { power: power.counts(), players: players.counts(), nodes: nodes.counts() };
      s2Start = { factory: factory.counts(), trains: trains.counts(), stations: stations.counts(), routes: routes.counts() };
      s3Start = s3.counts();
      s4Start = s4.counts();
      s4bStart = s4b.counts();
      s2From = accepted(recorder).length;
      rounds = createRoundCollector({ reads: () => power!.reads, now: Date.now, startAt: s2From });
      let postsSeen = 0;
      let routePostsSeen = 0;
      // Counted from zero: a post that landed between enrolment and the window's start (the first native read) gets its sample at once too.
      const s3Seen = { rails: 0, conveyors: 0 };
      const s4Seen: Record<S4Layer, number> = { mapMarkers: 0, droneRoutes: 0, powerLines: 0 };
      const s4bSeen = newS4bSeen();
      while (Date.now() - from < Pass.WINDOW_MINUTES * MIN) {
        await ctx.deps.sleep(SECOND);
        rounds.feed(accepted(recorder));
        rounds.settle(false);
        // A node post is compared with an FRM sample taken right after it.
        const posts = recorder.entries.filter((e) => e.agent === "mod" && e.world?.layer === "resourceNodes" && e.status === 200);
        if (posts.length > postsSeen) {
          postsSeen = posts.length;
          await nodes.take();
        }
        // The trainRoutes layer is native only (the bridge never sent it): a pair of FRM reads right after each accepted post.
        const routeCount = recorder.entries.filter((e) => e.agent === "mod" && e.world?.layer === "trainRoutes" && e.status === 200).length;
        if (routeCount > routePostsSeen) {
          routePostsSeen = routeCount;
          await routes.take();
        }
        // S3: a fresh FRM sample right after each accepted rails or conveyors post, so the judge always has one within its gap.
        await takeAfterNewS3Posts(s3, recorder.entries, enrolledAt, s3Seen);
        // S4-A: the same for the markers, drone ports and power lines layers.
        await takeAfterNewS4Posts(s4, recorder.entries, enrolledAt, s4Seen);
        // S4-B: the same for each accepted snapshot carrying extractors, generators, station rates or storage.
        await takeAfterNewS4bPosts(s4b, recorder.entries, enrolledAt, s4bSeen);
      }
      capture.windowEndedAt = iso(Date.now());
      await ctx.deps.sleep(2 * SECOND); // the last sample's FRM neighbour after it
      rounds.feed(accepted(recorder));
      rounds.settle(true);
      await ctx.sampleMemory("withModAfterRounds");
    } finally {
      power?.stop();
      players?.stop();
      nodes?.stop();
      factory?.stop();
      trains?.stop();
      stations?.stop();
      routes?.stop();
      s3?.stop();
      s4?.stop();
      s4b?.stop();
      if (rounds) {
        capture.power.rounds = rounds.power; // a run that failed mid-window keeps what it saw (the verdict fails on the window)
        capture.players.rounds = rounds.players;
      }
      if (power) {
        capture.power.frm = sinceWindow(power.counts(), atStart.power);
        capture.power.mergedGroupsMax = mergedMax;
      }
      if (players) {
        capture.players.frm = sinceWindow(players.counts(), atStart.players);
        capture.players.frmSamples = players.samples.map((s) => ({ startedAt: iso(s.startedAt), endedAt: iso(s.endedAt), value: s.value }));
      }
      if (nodes) {
        capture.nodes.frm = sinceWindow(nodes.counts(), atStart.nodes);
        for (const s of nodes.samples) capture.nodes.distinct[s.value.hash] ??= s.value.mapped;
        capture.nodes.frmSamples = nodes.samples.map((s) => ({ startedAt: iso(s.startedAt), endedAt: iso(s.endedAt), value: s.value.hash }));
      }
      const mod = recorder.entries.filter((e) => e.agent === "mod");
      const from = capture.windowStartedAt ? Date.parse(capture.windowStartedAt) : 0;
      const snapshotPosts = mod.filter((e) => e.method === "POST" && e.path === "/agent/v1/snapshots");
      capture.accepted = snapshotPosts.filter((e) => e.status === 200).length;
      capture.unrecorded = snapshotPosts.filter((e) => e.status === 200 && e.snapshot === undefined).length;
      capture.refused = mod.filter((e) => e.method === "POST" && e.status !== 200 && Date.parse(e.at) >= from).map((e) => ({ at: e.at, status: e.status }));
      capture.nodes.posts = mod
        .filter((e) => e.world?.layer === "resourceNodes" && e.status === 200 && Date.parse(e.at) >= from)
        .map((e) => ({ at: e.at, status: e.status, data: e.worldData }));
      if (factory && trains && stations && routes) {
        const s2 = s2RoundsFrom(accepted(recorder).slice(s2From), table);
        const iv = (s: { startedAt: number; endedAt: number }) => ({ startedAt: iso(s.startedAt), endedAt: iso(s.endedAt) });
        const routeDistinct: Record<string, string[]> = {};
        for (const s of routes.samples) routeDistinct[s.value.hash] ??= s.value.canon;
        capture.s2 = {
          factory: {
            statics: table.statics,
            rounds: s2.factory,
            frmSamples: factory.samples.map((s) => ({ ...iv(s), ...s.value })),
            frm: sinceWindow(factory.counts(), s2Start.factory),
            somersloopMachines: sloops,
          },
          mapLive: {
            rounds: s2.mapLive,
            trains: trains.samples.map((s) => ({ ...iv(s), value: s.value })),
            stations: stations.samples.map((s) => ({ ...iv(s), value: s.value })),
            trainsFrm: sinceWindow(trains.counts(), s2Start.trains),
            stationsFrm: sinceWindow(stations.counts(), s2Start.stations),
          },
          routes: {
            posts: routePosts(recorder.entries, from),
            frmSamples: routes.samples.map((s) => ({ ...iv(s), value: s.value.hash })),
            distinct: routeDistinct,
            frm: sinceWindow(routes.counts(), s2Start.routes),
          },
        };
      }
      if (s3) capture.s3 = s3EvidenceFrom(s3, recorder.entries, enrolledAt, s3Start);
      if (s4) capture.s4 = s4EvidenceFrom(s4, recorder.entries, enrolledAt, s4Start);
      if (s4b) capture.s4b = s4bEvidenceFrom(s4b, recorder.entries, enrolledAt, s4bStart);
      capture.state = await host.state("mod").catch(() => null);
      ctx.captures["native-parity.json"] = capture;
      ctx.captures["readers.json"] = Pass.judgeReaders(capture);
      ctx.captures["recorder.json"] = publicEntries(recorder.entries);
      await recorder.close();
    }
  },
};
