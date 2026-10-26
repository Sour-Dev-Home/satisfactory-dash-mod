import { createHash } from "node:crypto";
import * as Pass from "../../../runs/native-parity/pass";
import { FRM_BRACKET_GAP_MS } from "../../../runs/enrol/pass";
import { NATIVE_SOURCES, type CoreContext, type CoreRunSpec } from "../core-run";
import { type FrmRead, parseGetPowerFull, pickBracket, startFrmPoller } from "../frm";
import { canonNodes, countMergedGroups, mapFrmResourceNodes, playerKeys, startSampler, type CanonNode } from "../frm-native";
import { startRecorder, type RecordedCall } from "../recorder";
import { accepted, enrolPhase } from "./enrol";

const MIN = 60_000;
const SECOND = 1000;
const POWER_POLL_MS = 250; // the enrol run's pause between getPower reads
const PLAYERS_POLL_MS = 2 * SECOND;
const NODES_POLL_MS = MIN;
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

/** FRM reads made since the window began: a read that failed while the world was still coming up never reaches a judge. */
export const sinceWindow = (now: { reads: number; failed: number }, atStart: { reads: number; failed: number }): Pass.FrmCounts => ({
  reads: now.reads - atStart.reads,
  failed: now.failed - atStart.failed,
});

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
  worldKey: "s2",
  modConfig: NATIVE_SOURCES,
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
    const recorder = await startRecorder(host.relayUrl, { keepWorldData: ["resourceNodes"] });
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
    try {
      await ctx.deps.loadWorld!("s2");
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
      // Enrolled after the world is up, so the mod's one resource-node post (about 8 minutes after enrolment, then every 30)
      // lands inside the 30-minute window that opens a minute after enrolment.
      await enrolPhase(ctx, host, recorder);
      await ctx.deps.sleep(MIN); // the world settles
      const from = Date.now();
      capture.windowStartedAt = iso(from);
      atStart = { power: power.counts(), players: players.counts(), nodes: nodes.counts() };
      rounds = createRoundCollector({ reads: () => power!.reads, now: Date.now, startAt: accepted(recorder).length });
      let postsSeen = 0;
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
      capture.state = await host.state("mod").catch(() => null);
      ctx.captures["native-parity.json"] = capture;
      ctx.captures["readers.json"] = Pass.judgeReaders(capture);
      ctx.captures["recorder.json"] = recorder.entries.map(({ snapshot: _, worldData: __, ...rest }) => rest);
      await recorder.close();
    }
  },
};
