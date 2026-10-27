import * as Pass from "../../../runs/s4-capture/pass";
import type { CoreContext, CoreRunSpec } from "../core-run";

const MIN = 60_000;
/** Time after the world is healthy for the trains to start moving. */
export const CAPTURE_SETTLE_MS = 2 * MIN;
export const CAPTURE_INTERVAL_MS = 30_000;
export const CAPTURE_WINDOW_MS = 15 * MIN;
export const GET_TIMEOUT_MS = 15_000;
export const { S4_CAPTURE_ENDPOINTS, RATE_SAMPLES_MAX } = Pass;

/**
 * The S4 capture (ADR-0058 Sprint S4 prerequisite): Test3 loaded with FRM running and the mod idle (no config is written, as in
 * manual), then every endpoint in S4_CAPTURE_ENDPOINTS read every CAPTURE_INTERVAL_MS for CAPTURE_WINDOW_MS after a settle.
 * s4-capture.json keeps FRM's bodies unmodified; they never go into the repo. No backend.
 */
export const s4Capture: CoreRunSpec = {
  requirements: Pass.requirements,
  backend: false,
  world: true,
  worldKey: "sprint3",
  evaluate: Pass.evaluate,
  preflight(deps) {
    if (!deps.config.frmBaseUrl) throw new Error("s4-capture needs frmBaseUrl in the runner config: FRM is what is captured (docs/runner.md)");
  },
  async drive(ctx: CoreContext) {
    const frmBase = ctx.deps.config.frmBaseUrl;
    if (!frmBase) throw new Error("s4-capture needs frmBaseUrl in the runner config: FRM is what is captured (docs/runner.md)");
    const endpoints: Record<string, Pass.EndpointCapture> = {};
    for (const path of Pass.S4_CAPTURE_ENDPOINTS) endpoints[path] = { first: undefined, reads: 0, failed: 0, keys: [], rateSamples: [], rateHits: 0 };
    const capture: Pass.S4Capture = { settleMs: CAPTURE_SETTLE_MS, intervalMs: CAPTURE_INTERVAL_MS, windowMs: CAPTURE_WINDOW_MS, endpoints };
    ctx.captures["s4-capture.json"] = capture;
    await ctx.deps.loadWorld!("sprint3");
    await ctx.deps.waitHealthy();
    ctx.rememberAutoPause(await ctx.deps.readAutoPause!());
    await ctx.deps.setAutoPause!(false);
    let gone = false;
    void ctx.proc.exited.then(() => void (gone = true), () => void (gone = true));
    const waitUntil = async (ms: number) => {
      while (!gone && ctx.deps.now().getTime() < ms) await ctx.deps.sleep(Math.min(1000, ms - ctx.deps.now().getTime()));
    };
    const read = async (path: string) => {
      const c = endpoints[path]!;
      try {
        const res = await fetch(new URL(path, frmBase), { signal: AbortSignal.timeout(GET_TIMEOUT_MS) });
        if (!res.ok) throw new Error(`${path} returned ${res.status}`);
        const body = (await res.json()) as unknown;
        c.reads++;
        if (c.reads === 1) c.first = body;
        for (const k of Pass.recordKeys(body)) if (!c.keys.includes(k)) c.keys.push(k);
        c.keys.sort();
        if (Pass.RATE_ENDPOINTS.includes(path) && Pass.hasNonZeroRate(body)) {
          c.rateHits++;
          if (c.rateSamples.length < Pass.RATE_SAMPLES_MAX) c.rateSamples.push({ at: ctx.deps.now().toISOString(), body });
        }
      } catch {
        c.failed++;
      }
    };
    await waitUntil(ctx.deps.now().getTime() + CAPTURE_SETTLE_MS);
    const startedMs = ctx.deps.now().getTime();
    for (let k = 0; !gone && k * CAPTURE_INTERVAL_MS < CAPTURE_WINDOW_MS; k++) {
      await waitUntil(startedMs + k * CAPTURE_INTERVAL_MS);
      if (gone) break;
      await Promise.all(Pass.S4_CAPTURE_ENDPOINTS.map(read));
    }
    if (gone) throw new Error("the server exited during the s4 capture");
  },
};
