// S6 (claim C8): the mod's native status readings, paired with QueryServerState and HealthCheck read right after.
import { createHash } from "node:crypto";
import type { StatusPair, StatusReading } from "./verdict";

/** The first 12 hex characters of the name's SHA-1 over UTF-8. The mod logs the same, so names stay out of bundles. */
export const sessionHash = (name: string) => createHash("sha1").update(name, "utf8").digest("hex").slice(0, 12);

const S6 = /satis-mod s6 running=([01]) paused=([01]) players=(\d+) limit=(\d+) duration=(\d+) tick=([\d.]+) health=(\S*) session=([0-9a-f]{12})/;

export function parseS6Line(line: string): StatusReading | null {
  const m = S6.exec(line);
  if (!m) return null;
  return {
    running: m[1] === "1",
    paused: m[2] === "1",
    players: Number(m[3]),
    limit: Number(m[4]),
    duration: Number(m[5]),
    tickRate: Number(m[6]),
    health: m[7]!,
    session: m[8]!,
  };
}

type Api = { call(fn: string, data?: Record<string, unknown>): Promise<{ status: number; body: unknown }> };

/** As satisfactory-dash reads them (packages/game-adapter/src/satisfactoryServerAdapter.ts:146-171). */
export async function readApiStatus(api: Api): Promise<StatusReading> {
  const q = await api.call("QueryServerState");
  if (q.status < 200 || q.status > 299) throw new Error(`QueryServerState returned ${q.status}`);
  const s = (q.body as { data?: { serverGameState?: Record<string, unknown> } } | null)?.data?.serverGameState;
  if (!s) throw new Error("QueryServerState has no serverGameState");
  const h = await api.call("HealthCheck", { ClientCustomData: "" });
  if (h.status < 200 || h.status > 299) throw new Error(`HealthCheck returned ${h.status}`);
  const health = (h.body as { data?: { health?: unknown } } | null)?.data?.health;
  if (typeof health !== "string") throw new Error("HealthCheck has no health");
  // The tick rate is rounded as the mod logs it (%.2f), so equal values compare equal.
  return {
    running: s.isGameRunning === true,
    paused: s.isGamePaused === true,
    players: Number(s.numConnectedPlayers),
    limit: Number(s.playerLimit),
    duration: Number(s.totalGameDuration),
    tickRate: Number(Number(s.averageTickRate).toFixed(2)),
    health,
    session: sessionHash(String(s.activeSessionName ?? "")),
  };
}

export async function collectStatusPairs(opts: {
  readNew: () => string[];
  api: () => Promise<StatusReading>;
  sleep: (ms: number) => Promise<void>;
  now: () => number;
  durationMs: number;
  intervalMs: number;
  want: number;
  /** Discard what the first read finds: those lines were logged before this phase's collector started. */
  skipBacklog: boolean;
}): Promise<StatusPair[]> {
  const pairs: StatusPair[] = [];
  const end = opts.now() + opts.durationMs;
  let first = true;
  while (opts.now() < end && pairs.length < opts.want) {
    const lines = opts.readNew();
    if (!(first && opts.skipBacklog)) {
      for (const l of lines) {
        const mod = parseS6Line(l);
        if (!mod) continue;
        let api: StatusReading | null = null;
        try { api = await opts.api(); } catch { api = null; }
        pairs.push({ at: new Date(opts.now()).toISOString(), mod, api });
      }
    }
    first = false;
    if (pairs.length < opts.want) await opts.sleep(opts.intervalMs);
  }
  return pairs;
}
