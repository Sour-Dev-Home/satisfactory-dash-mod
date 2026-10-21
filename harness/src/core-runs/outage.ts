import * as Pass from "../../../runs/outage/pass";
import type { CoreRunSpec } from "../core-run";
import { startRecorder } from "../recorder";
import { TICK_WARMUP_MS } from "../ticks";
import { accepted, enrolPhase } from "./enrol";

const MIN = 60_000;
const WINDOW_MS = 3 * MIN;

/**
 * The backend outage (spec "Testing" 2): the world runs, the mod is enrolled; ticks for 3 minutes; the relay stops for
 * 3 minutes (ticks sampled throughout); the relay starts again and the run waits 3 minutes (ticks sampled) for the
 * catch-up. The "before" window keeps TICK_WARMUP_MS of readings out of its median, as every other tick window does:
 * the server runs fast right after a boot or a world load (ticks.ts), which would make "before" read high.
 */
export const outage: CoreRunSpec = {
  requirements: Pass.requirements,
  backend: true,
  world: true,
  evaluate: Pass.evaluate,
  async drive(ctx) {
    const sample = ctx.deps.sampleTickWindow;
    if (!sample) throw new Error("no tick sampler (sampleTickWindow)");
    const host = await ctx.deps.backendHost!();
    const recorder = await startRecorder(host.relayUrl);
    const capture: Pass.Outage = { before: null, during: null, after: null, stoppedAt: null, startedAt: null, acceptedAfter: [] };
    let relayDown = false;
    try {
      await ctx.deps.loadWorld!();
      await ctx.deps.waitHealthy();
      ctx.rememberAutoPause(await ctx.deps.readAutoPause!());
      await ctx.deps.setAutoPause!(false);
      await enrolPhase(ctx, host, recorder);
      capture.before = await sample({ warmupMs: TICK_WARMUP_MS, durationMs: WINDOW_MS });
      relayDown = true;
      await host.stopRelay();
      // After the stop returned, so the relay was certainly down from here (pass.ts header).
      capture.stoppedAt = new Date().toISOString();
      capture.during = await sample({ warmupMs: 0, durationMs: WINDOW_MS });
      // Before the start call, for the same reason.
      capture.startedAt = new Date().toISOString();
      await host.startRelay();
      relayDown = false;
      capture.after = await sample({ warmupMs: 0, durationMs: WINDOW_MS });
      await ctx.sampleMemory("withModAfterRounds");
    } finally {
      // The backend host is the owner's; it is never left with its relay stopped.
      if (relayDown) await host.startRelay().catch(() => undefined);
      if (capture.startedAt) {
        const since = capture.startedAt;
        capture.acceptedAfter = accepted(recorder)
          .filter((e) => e.at >= since)
          .map((e) => (e.snapshot as { observedAt?: unknown } | undefined)?.observedAt)
          .filter((o): o is string => typeof o === "string");
      }
      ctx.captures["outage.json"] = capture;
      ctx.captures["recorder.json"] = recorder.entries.map(({ snapshot: _, ...rest }) => rest);
      await recorder.close();
    }
  },
};
