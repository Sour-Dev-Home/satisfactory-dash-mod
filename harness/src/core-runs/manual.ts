import * as Pass from "../../../runs/manual/pass";
import type { CoreContext, CoreRunSpec } from "../core-run";

const MIN = 60_000;
const DEFAULT_MINUTES = 45;
/** The workflow times out at 90 minutes; the build, boot, world load and shutdown take the rest. */
const MAX_MINUTES = 75;

/** MANUAL_MINUTES: a whole number of minutes, 1 to MAX_MINUTES; unset or empty is 45. */
export function manualMinutes(value: string | undefined): number {
  if (value === undefined || value === "") return DEFAULT_MINUTES;
  const n = Number(value);
  if (!/^\d+$/.test(value) || n < 1 || n > MAX_MINUTES) throw new Error(`MANUAL_MINUTES must be a whole number from 1 to ${MAX_MINUTES}, got "${value}"`);
  return n;
}

const objects = (list: unknown): Record<string, unknown>[] =>
  Array.isArray(list) ? list.filter((x): x is Record<string, unknown> => typeof x === "object" && x !== null && !Array.isArray(x)) : [];

/** Only the named fields, and only those present: no location or geometry reaches the evidence. */
function pick<T>(list: unknown, fields: readonly string[]): T[] {
  return objects(list).map((o) => Object.fromEntries(fields.filter((f) => f in o).map((f) => [f, o[f]])) as T);
}
export const trimBelts = (list: unknown) => pick<Pass.TrimmedBelt>(list, ["ID", "ClassName", "ConnectedTo0", "ConnectedTo1", "ItemsPerMinute"]);
export const trimPipes = (list: unknown) => pick<Pass.TrimmedPipe>(list, ["ID", "ClassName", "ConnectedTo0", "ConnectedTo1", "Speed"]);

async function readFrm(baseUrl: string | undefined): Promise<Pass.FrmRead> {
  if (!baseUrl) return { error: "frmBaseUrl is not set in the runner config" };
  try {
    const get = async (path: string) => {
      const res = await fetch(new URL(path, baseUrl), { signal: AbortSignal.timeout(15_000) });
      if (!res.ok) throw new Error(`${path} returned ${res.status}`);
      return (await res.json()) as unknown;
    };
    const [belts, pipes] = await Promise.all([get("/getBelts"), get("/getPipes")]);
    return { belts: trimBelts(belts), pipes: trimPipes(pipes) };
  } catch (e) {
    return { error: e instanceof Error ? e.message : String(e) };
  }
}

/**
 * The test server up for a person to play on (the dash's G-B1 and G-B2). runCore still builds and installs the mod
 * (CoreRunSpec has no way to skip it), but no config is written, so the mod stays idle. No backend.
 */
export const manual: CoreRunSpec = {
  requirements: Pass.requirements,
  backend: false,
  world: true,
  evaluate: Pass.evaluate,
  preflight() {
    manualMinutes(process.env.MANUAL_MINUTES);
  },
  async drive(ctx: CoreContext) {
    const minutes = manualMinutes(process.env.MANUAL_MINUTES);
    const windowMs = minutes * MIN;
    const samples: Pass.Manual["samples"] = [];
    const capture: Pass.Manual = { windowMs, samples, frm: { start: { error: "not read" }, end: { error: "not read" } } };
    ctx.captures["manual.json"] = capture;
    await ctx.deps.loadWorld!();
    await ctx.deps.waitHealthy();
    ctx.rememberAutoPause(await ctx.deps.readAutoPause!());
    await ctx.deps.setAutoPause!(false);
    capture.frm.start = await readFrm(ctx.deps.config.frmBaseUrl);
    const startedMs = ctx.deps.now().getTime();
    let lastAnnounced = -1;
    let gone = false;
    void ctx.proc.exited.then(() => void (gone = true), () => void (gone = true));
    for (let k = 0; ; k++) {
      const at = startedMs + k * Pass.SAMPLE_INTERVAL_MS;
      if (at - startedMs >= windowMs) break;
      if (gone) break;
      while (ctx.deps.now().getTime() < at) await ctx.deps.sleep(Math.min(1000, at - ctx.deps.now().getTime()));
      const state = ctx.deps.gameState ? await ctx.deps.gameState().catch(() => null) : null;
      samples.push({ at: ctx.deps.now().toISOString(), answered: state !== null });
      const left = Math.ceil((windowMs - (at - startedMs)) / MIN);
      if (left !== lastAnnounced) {
        lastAnnounced = left;
        console.log(`manual session: ${left} min left`);
      }
    }
    if (gone) throw new Error("the server exited during the manual session");
    capture.frm.end = await readFrm(ctx.deps.config.frmBaseUrl);
  },
};
