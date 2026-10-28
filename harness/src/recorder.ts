// The recorder (ruling 11): a loopback proxy between an agent and the backend host's relay. It forwards every request
// unchanged and keeps a public record of what passed: never the Authorization value, an enrolment body (the code) or
// its answer (the secret), and the session name, player names and mapLive names only as their hash (bundles are public).
import { createServer, type IncomingHttpHeaders, type IncomingMessage, request, type ServerResponse } from "node:http";
import type { AddressInfo } from "node:net";
import { gunzipSync } from "node:zlib";
import { sessionHash } from "./status";

export interface RecordedCall {
  /** When the request arrived. */
  at: string;
  /** When the answer had finished being sent (or the exchange failed): the earliest the agent could know it. */
  doneAt: string;
  /** By User-Agent: the mod, satisfactory-dash's Node agent (parity run), or anything else. */
  agent: "mod" | "node" | "other";
  method: string;
  /** The path without its query. */
  path: string;
  /** 0 when the upstream couldn't be reached (the agent's socket was destroyed, as in a real outage). */
  status: number;
  authorization: boolean;
  snapshot?: unknown;
  /** The snapshot was over MAX_RECORDED and trimming was opted in: `snapshot` keeps only its small parts (OVERSIZED_KEPT). */
  oversized?: true;
  /** With `oversized`: the top-level keys that were dropped. */
  droppedKeys?: string[];
  commandIds?: string[];
  result?: unknown;
  /** A world-layer post (POST /agent/v1/world/:layer): which layer, how many items and how many unzipped bytes. The data itself is not kept. */
  world?: { layer: string; items: number; bytes: number };
  /** The posted `data` array, kept only for the layers RecorderOptions.keepWorldData names (never for snapshots). */
  worldData?: unknown[];
  /** The parts of an accepted snapshot RecorderOptions.keepSnapshotParts names, with mapLive names already hashed. In memory only: publicEntries leaves them out. */
  parts?: Partial<Record<SnapshotPart, unknown>>;
}
export type SnapshotPart = "factory" | "mapLive" | "power" | "storage";
export interface Recorder { url: string; entries: RecordedCall[]; close(): Promise<void> }

const MAX_RECORDED = 2 * 1024 * 1024;
/**
 * A snapshot post is read up to this size (raw and unzipped): above the backend's own limit (satisfactory-dash
 * backend/src/modules/agents/config.ts SNAPSHOT_MAX_BYTES, 5 MB), so every snapshot the backend accepted can be read.
 */
const SNAPSHOT_READ_LIMIT = 8 * 1024 * 1024;
/** What an oversized snapshot keeps when trimming is opted in: everything but the bulky parts (factory, players, mapLive). */
const OVERSIZED_KEPT = ["agentVersion", "observedAt", "reachable", "paused", "settings", "status", "power"];
const SNAPSHOTS = "/agent/v1/snapshots";
const COMMANDS = "/agent/v1/commands";
const RESULT = /^\/agent\/v1\/commands\/[^/]+\/result$/;
const WORLD = /^\/agent\/v1\/world\/([^/]+)$/;

/** Keeps at most `limit` bytes of a stream that is forwarded whole; over that, nothing is recorded. */
class Capped {
  private chunks: Buffer[] = [];
  private size = 0;
  over = false;
  constructor(private readonly limit = MAX_RECORDED) {}
  add(chunk: Buffer) {
    if (this.over) return;
    this.size += chunk.length;
    if (this.size > this.limit) {
      this.over = true;
      this.chunks = [];
    } else {
      this.chunks.push(chunk);
    }
  }
  /** The body as JSON (gunzipped if it says gzip) and its unzipped size, or undefined when too big or not JSON. */
  read(headers: IncomingHttpHeaders): { value: unknown; size: number } | undefined {
    if (this.over || !this.size) return undefined;
    try {
      let raw = Buffer.concat(this.chunks);
      if (String(headers["content-encoding"] ?? "").toLowerCase() === "gzip") raw = gunzipSync(raw, { maxOutputLength: this.limit });
      return { value: JSON.parse(raw.toString("utf8")), size: raw.length };
    } catch {
      return undefined;
    }
  }
  json(headers: IncomingHttpHeaders): unknown {
    return this.read(headers)?.value;
  }
}

/** ADR-0029: player names are personal data about third parties; evidence keeps a hash of each, never the name. */
function hashPlayers(snapshot: unknown): unknown {
  const players = (snapshot as { players?: { players?: unknown } } | null)?.players;
  const list = players?.players;
  if (!Array.isArray(list)) return snapshot;
  const hashed = list.map((p) => (p && typeof p === "object" && typeof (p as { name?: unknown }).name === "string" ? { ...p, name: sessionHash((p as { name: string }).name) } : p));
  return { ...(snapshot as object), players: { ...players, players: hashed } };
}

/** Train and station names are labels a player typed, so evidence keeps a hash of each, as it does for player names and the session. */
function hashMapLive(snapshot: unknown): unknown {
  const live = (snapshot as { mapLive?: { trains?: unknown; stations?: unknown } } | null)?.mapLive;
  if (!live || typeof live !== "object") return snapshot;
  const hashNames = (list: unknown): unknown =>
    Array.isArray(list)
      ? list.map((item) => (item && typeof item === "object" && typeof (item as { name?: unknown }).name === "string" ? { ...item, name: sessionHash((item as { name: string }).name) } : item))
      : list;
  return { ...(snapshot as object), mapLive: { ...live, trains: hashNames(live.trains), stations: hashNames(live.stations) } };
}

function hashSession(snapshot: unknown): unknown {
  const status = (snapshot as { status?: Record<string, unknown> } | null)?.status;
  if (!status || typeof status !== "object" || typeof status.sessionName !== "string") return snapshot;
  return { ...(snapshot as object), status: { ...status, sessionName: sessionHash(status.sessionName) } };
}

function commandIds(body: unknown): string[] | undefined {
  const list = (body as { commands?: unknown } | null)?.commands;
  if (!Array.isArray(list)) return undefined;
  return list.map((c) => (c as { id?: unknown } | null)?.id).filter((id): id is string => typeof id === "string");
}

export interface RecorderOptions {
  /**
   * The agents whose snapshot posts over MAX_RECORDED are kept trimmed to OVERSIZED_KEPT instead of not recorded. Off by
   * default: a run that judges a whole snapshot (enrol's S13) must see an oversized one fail. Only parity opts in, and
   * only for the Node agent (review of 9b60268).
   */
  trimOversizedFor?: ReadonlyArray<RecordedCall["agent"]>;
  /**
   * World layers whose posted `data` array is kept in `worldData` (a layer so named must not carry personal data: resourceNodes carries resource names;
   * rails and conveyors carry only ids, class names and tier names). S4: mapMarkers and droneRoutes carry player text (marker and port names), so
   * they may be kept in memory only: publicEntries drops `worldData`, and the drive replaces every name with sessionHash before anything is captured
   * (s4LayerPosts). powerLines carries ids and coordinates only.
   */
  keepWorldData?: readonly string[];
  /**
   * S2: the parts of each accepted snapshot (within the 2 MB recording cap) that the native-parity judge compares, kept on
   * `RecordedCall.parts` so the run does not dig them out of `snapshot` again. A snapshot over the cap is still `unrecorded` and
   * fails the run: the cap is deliberately not raised (a 340-machine factory part is a few hundred KB, see the recorder test).
   */
  keepSnapshotParts?: readonly SnapshotPart[];
}

/** The entries as recorder.json carries them: never the snapshot, the kept world data or the kept parts (all in memory only). */
export function publicEntries(entries: readonly RecordedCall[]): Array<Omit<RecordedCall, "snapshot" | "worldData" | "parts">> {
  return entries.map(({ snapshot: _s, worldData: _w, parts: _p, ...rest }) => rest);
}

/** Starts the recorder on 127.0.0.1 in front of upstream (the relay's URL). */
export async function startRecorder(upstream: string, opts: RecorderOptions = {}): Promise<Recorder> {
  const target = new URL(upstream);
  const trimFor = new Set(opts.trimOversizedFor ?? []);
  const keepWorld = new Set(opts.keepWorldData ?? []);
  const keepParts = [...(opts.keepSnapshotParts ?? [])];
  const entries: RecordedCall[] = [];

  const handle = (req: IncomingMessage, res: ServerResponse) => {
    const path = new URL(req.url ?? "/", "http://127.0.0.1").pathname;
    const method = req.method ?? "GET";
    const ua = String(req.headers["user-agent"] ?? "");
    const entry: RecordedCall = {
      at: new Date().toISOString(),
      doneAt: "",
      // satisfactory-dash agent/src/backendClient.ts sends satisfactory-dash-agent/<version>.
      agent: ua.startsWith("satisfactory-dash-mod/") ? "mod" : ua.startsWith("satisfactory-dash-agent/") ? "node" : "other",
      method,
      path,
      status: 0,
      authorization: req.headers.authorization !== undefined,
    };
    const sent = new Capped(method === "POST" && path === SNAPSHOTS ? SNAPSHOT_READ_LIMIT : MAX_RECORDED);
    const got = new Capped();
    let done = false;
    const finish = () => {
      if (done) return;
      done = true;
      entry.doneAt = new Date().toISOString();
      if (entry.status !== 0) {
        if (method === "POST" && path === SNAPSHOTS) {
          const s = sent.read(req.headers);
          if (s !== undefined && s.size > MAX_RECORDED) {
            // Over the cap: not recorded (the judge fails it), unless trimming was opted in for this agent.
            if (trimFor.has(entry.agent) && s.value !== null && typeof s.value === "object" && !Array.isArray(s.value)) {
              const all = Object.entries(s.value);
              entry.snapshot = hashSession(hashPlayers(hashMapLive(Object.fromEntries(all.filter(([k]) => OVERSIZED_KEPT.includes(k))))));
              entry.oversized = true;
              entry.droppedKeys = all.map(([k]) => k).filter((k) => !OVERSIZED_KEPT.includes(k));
            }
          } else if (s !== undefined) {
            entry.snapshot = hashSession(hashPlayers(hashMapLive(s.value)));
            if (keepParts.length > 0 && entry.agent === "mod" && entry.snapshot !== null && typeof entry.snapshot === "object") {
              const kept: Partial<Record<SnapshotPart, unknown>> = {};
              for (const part of keepParts) {
                const value = (entry.snapshot as Record<string, unknown>)[part];
                if (value !== undefined) kept[part] = value;
              }
              if (Object.keys(kept).length > 0) entry.parts = kept;
            }
          }
        } else if (method === "GET" && path === COMMANDS) {
          const ids = commandIds(got.json(upRes?.headers ?? {}));
          if (ids) entry.commandIds = ids;
        } else if (method === "POST" && RESULT.test(path)) {
          const r = sent.json(req.headers);
          if (r !== undefined) entry.result = r;
        } else if (method === "POST" && WORLD.test(path)) {
          // A world body is at most 2,000,000 bytes (map.ts), under the 2 MiB this reads.
          const w = sent.read(req.headers);
          const data = (w?.value as { data?: unknown } | null | undefined)?.data;
          if (w !== undefined && Array.isArray(data)) {
            const layer = WORLD.exec(path)![1]!;
            entry.world = { layer, items: data.length, bytes: w.size };
            if (keepWorld.has(layer)) entry.worldData = data;
          }
        }
      }
      entries.push(entry);
    };

    let upRes: IncomingMessage | undefined;
    const up = request(
      { protocol: target.protocol, hostname: target.hostname, port: target.port, method, path: req.url, headers: { ...req.headers, host: target.host } },
      (r) => {
        upRes = r;
        entry.status = r.statusCode ?? 0;
        res.writeHead(entry.status, r.headers);
        r.on("data", (c: Buffer) => got.add(c));
        r.on("end", finish);
        r.on("error", () => (res.destroy(), finish()));
        r.pipe(res);
      },
    );
    up.on("error", () => {
      // No answer at all, as the mod would see a real outage: no status line, the socket just goes.
      entry.status = upRes ? entry.status : 0;
      req.socket.destroy();
      res.destroy();
      finish();
    });
    req.on("data", (c: Buffer) => sent.add(c));
    req.pipe(up);
  };

  const server = createServer(handle);
  await new Promise<void>((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", () => resolve());
  });
  return {
    url: `http://127.0.0.1:${(server.address() as AddressInfo).port}`,
    entries,
    close: () =>
      new Promise<void>((resolve) => {
        server.closeAllConnections();
        server.close(() => resolve());
      }),
  };
}
