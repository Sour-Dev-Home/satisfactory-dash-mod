import * as Pass from "../../../runs/manual/pass";
import type { CoreContext, CoreRunSpec } from "../core-run";

const MIN = 60_000;
const DEFAULT_MINUTES = 45;
/** The workflow times out at 90 minutes; the build, boot, world load and shutdown take the rest. */
const MAX_MINUTES = 75;
/** A QueryServerState that hangs this long counts as unanswered, so one stuck call can't stall the window. */
export const STATE_TIMEOUT_MS = 10_000;

/** The game's answer, null if it failed or took longer than STATE_TIMEOUT_MS. */
async function stateWithin(gameState: () => Promise<{ running: boolean } | null>) {
  let timer: ReturnType<typeof setTimeout> | undefined;
  const late = new Promise<null>((resolve) => void (timer = setTimeout(() => resolve(null), STATE_TIMEOUT_MS)));
  try {
    return await Promise.race([gameState().catch(() => null), late]);
  } finally {
    clearTimeout(timer);
  }
}

/** MANUAL_MINUTES: a whole number of minutes, 1 to MAX_MINUTES; unset or empty is 45. */
export function manualMinutes(value: string | undefined): number {
  if (value === undefined || value === "") return DEFAULT_MINUTES;
  const n = Number(value);
  if (!/^\d+$/.test(value) || n < 1 || n > MAX_MINUTES) throw new Error(`MANUAL_MINUTES must be a whole number from 1 to ${MAX_MINUTES}, got "${value}"`);
  return n;
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
    const capture: Pass.Manual = { windowMs, samples };
    ctx.captures["manual.json"] = capture;
    await ctx.deps.loadWorld!();
    await ctx.deps.waitHealthy();
    ctx.rememberAutoPause(await ctx.deps.readAutoPause!());
    await ctx.deps.setAutoPause!(false);
    const startedMs = ctx.deps.now().getTime();
    let lastAnnounced = -1;
    let gone = false;
    void ctx.proc.exited.then(() => void (gone = true), () => void (gone = true));
    for (let k = 0; ; k++) {
      const at = startedMs + k * Pass.SAMPLE_INTERVAL_MS;
      if (at - startedMs >= windowMs) break;
      if (gone) break;
      while (!gone && ctx.deps.now().getTime() < at) await ctx.deps.sleep(Math.min(1000, at - ctx.deps.now().getTime()));
      if (gone) break;
      const state = ctx.deps.gameState ? await stateWithin(ctx.deps.gameState) : null;
      // A server that answers but says the game isn't running is not available to play on.
      samples.push({ at: ctx.deps.now().toISOString(), answered: state?.running === true });
      const left = Math.ceil((windowMs - (at - startedMs)) / MIN);
      if (left !== lastAnnounced) {
        lastAnnounced = left;
        console.log(`manual session: ${left} min left`);
      }
    }
    if (gone) throw new Error("the server exited during the manual session");
  },
};
