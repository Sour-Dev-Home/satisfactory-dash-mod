import { readFileSync } from "node:fs";
import * as Pass from "../../../runs/enrol/pass";
import type { CoreContext, CoreRunSpec } from "../core-run";
import { MOD } from "../core-run";
import { randomCode } from "../backend";
import type { HostClient } from "../backend-host";
import { parseGetPowerFull, readAutoPause } from "../frm";
import { modConfigPath } from "../mod-config";
import { startRecorder, type Recorder } from "../recorder";
import { createServerApi } from "../server-api";
import { readApiStatus } from "../status";

const MIN = 60_000;
const accepted = (rec: Recorder) => rec.entries.filter((e) => e.agent === "mod" && e.path === "/agent/v1/snapshots" && e.status === 200);

/** Seeds a code, points the mod at the recorder, and waits for the enrolment and a first accepted snapshot. */
export async function enrolPhase(ctx: CoreContext, host: HostClient, recorder: Recorder, opts: { timeoutMs?: number } = {}): Promise<void> {
  const code = randomCode();
  await host.seedCode("mod", code);
  ctx.writeConfig({ backendUrl: recorder.url, enrollmentCode: code });
  await ctx.waitFor("an accepted snapshot", () => (accepted(recorder).length ? true : undefined), opts.timeoutMs ?? 3 * MIN);
  ctx.noteSecret(); // before any revoke deletes the file
}

/** Waits for each new accepted snapshot and reads the game's APIs right after it. */
async function collect(ctx: CoreContext, recorder: Recorder, world: boolean, want: number, frmBaseUrl: string | undefined): Promise<Pass.Round[]> {
  const api = createServerApi(ctx.deps.config.apiBaseUrl);
  const rounds: Pass.Round[] = [];
  let seen = accepted(recorder).length;
  while (rounds.length < want) {
    await ctx.waitFor("the next accepted snapshot", () => (accepted(recorder).length > seen ? true : undefined), MIN);
    const entry = accepted(recorder)[seen]!;
    seen = accepted(recorder).length;
    const [status, autoPause, frm] = await Promise.all([
      readApiStatus(api).catch(() => null),
      readAutoPause(api).catch(() => null),
      world && frmBaseUrl ? fetch(new URL("/getPower", frmBaseUrl), { signal: AbortSignal.timeout(2000) }).then(async (r) => parseGetPowerFull(await r.json())).catch(() => null) : Promise.resolve(null),
    ]);
    rounds.push({ at: entry.at, world, snapshot: entry.snapshot as Record<string, unknown>, api: status, autoPause, frm });
  }
  return rounds;
}

export const enrol: CoreRunSpec = {
  requirements: Pass.requirements,
  backend: true,
  world: true,
  emptyBoot: true,
  evaluate: Pass.evaluate,
  async drive(ctx) {
    const host = await ctx.deps.backendHost!();
    const recorder = await startRecorder(host.relayUrl);
    try {
      await enrolPhase(ctx, host, recorder);
      const rounds = await collect(ctx, recorder, false, 4, undefined);
      // The owner's memory goal (memory.json; each label says whether a game ran, as the API said then): the mod enrolled
      // and pushing with no game; then, at the stage the no-mod pre-boot was sampled (a save just loaded and healthy,
      // auto-pause as the server had it), the same with the mod; then after the world rounds.
      await ctx.sampleMemory("withMod");
      await ctx.deps.loadWorld!();
      await ctx.deps.waitHealthy();
      await ctx.sampleMemory("withMod");
      ctx.rememberAutoPause(await ctx.deps.readAutoPause!());
      await ctx.deps.setAutoPause!(false); // the factory runs, so power moves (S3)
      await ctx.deps.sleep(MIN); // the world settles
      rounds.push(...(await collect(ctx, recorder, true, 12, ctx.deps.config.frmBaseUrl)));
      await ctx.sampleMemory("withModAfterRounds");
      const cfg = JSON.parse(readFileSync(modConfigPath(ctx.deps.config.serverDir, MOD), "utf8")) as { enrollmentCode?: string };
      ctx.captures["enrol.json"] = {
        state: await host.state("mod"),
        codeBlanked: cfg.enrollmentCode === "",
        credentialWritten: ctx.credentialExists(),
        refused: recorder.entries.filter((e) => e.agent === "mod" && e.path === "/agent/v1/snapshots" && e.status >= 400).length,
        rounds,
      } satisfies Pass.Enrol;
    } finally {
      ctx.captures["recorder.json"] = recorder.entries.map(({ snapshot: _, ...rest }) => rest);
      await recorder.close();
    }
  },
};
