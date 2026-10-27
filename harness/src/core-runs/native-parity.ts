import { createHash } from "node:crypto";
import * as Pass from "../../../runs/native-parity/pass";
import { FRM_BRACKET_GAP_MS } from "../../../runs/enrol/pass";
import { NATIVE_SOURCES_S3, type CoreContext, type CoreRunSpec } from "../core-run";
import { AgentFactorySchema } from "../../contract/src/agent";
import { MapLiveSchema, TrainRoutesLayerDataSchema } from "../../contract/src/map";
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

/**
 * The combined native-reader parity run for sprint S1 (runs/native-parity/pass.ts says what is judged). The mod enrols with
 * playersSource and resourceNodesSource native (spec.modConfig). Order: the world loads first (FRM is down before it,
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
  modConfig: NATIVE_SOURCES_S3,
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
    const recorder = await startRecorder(host.relayUrl, { keepWorldData: ["resourceNodes", "trainRoutes", "rails", "conveyors"], keepSnapshotParts: ["factory", "mapLive"] });
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
      s2From = accepted(recorder).length;
      rounds = createRoundCollector({ reads: () => power!.reads, now: Date.now, startAt: s2From });
      let postsSeen = 0;
      let routePostsSeen = 0;
      // Counted from zero: a post that landed between enrolment and the window's start (the first native read) gets its sample at once too.
      let railPostsSeen = 0;
      let conveyorPostsSeen = 0;
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
        const railCount = s3LayerPosts(recorder.entries, "rails", enrolledAt).length;
        if (railCount > railPostsSeen) {
          railPostsSeen = railCount;
          await s3.rails.take();
        }
        const conveyorCount = s3LayerPosts(recorder.entries, "conveyors", enrolledAt).length;
        if (conveyorCount > conveyorPostsSeen) {
          conveyorPostsSeen = conveyorCount;
          await s3.conveyors.take();
        }
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
      capture.state = await host.state("mod").catch(() => null);
      ctx.captures["native-parity.json"] = capture;
      ctx.captures["readers.json"] = Pass.judgeReaders(capture);
      ctx.captures["recorder.json"] = publicEntries(recorder.entries);
      await recorder.close();
    }
  },
};
