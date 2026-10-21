import { readFileSync } from "node:fs";
import * as Pass from "../../../runs/enrol/pass";
import { FRM_BRACKET_GAP_MS } from "../../../runs/enrol/pass";
import type { CoreContext, CoreRunSpec } from "../core-run";
import { MOD } from "../core-run";
import { randomCode } from "../backend";
import type { HostClient } from "../backend-host";
import { parseGetPowerFull, pickBracket, readAutoPause, startFrmPoller } from "../frm";
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

/** The pause between FRM reads: one failed read still leaves another inside the 1 s window (pass.ts header). */
const FRM_POLL_INTERVAL_MS = 250;
const realSleep = (ms: number) => new Promise<void>((r) => setTimeout(r, ms));
type FrmPoller = ReturnType<typeof startFrmPoller>;

/** Reads FRM back to back until stopped (owner, 2026-10-20, after run 39517898558). */
function pollFrm(frmBaseUrl: string): FrmPoller {
  return startFrmPoller(
    () => fetch(new URL("/getPower", frmBaseUrl), { signal: AbortSignal.timeout(2000) }).then(async (r) => parseGetPowerFull(await r.json())),
    { now: Date.now, sleep: realSleep, intervalMs: FRM_POLL_INTERVAL_MS },
  );
}

/**
 * Waits for each new accepted snapshot and reads the game's APIs right after it. In the world, the poller has been
 * reading FRM since before the settle: the mod reads power in the same game-thread tick it stamps observedAt
 * (Driver.cpp TickSample), so the last FRM read that ended by observedAt and the first that started after it, each
 * within FRM_BRACKET_GAP_MS (pass.ts checks the same from the recorded times), bracket the mod's sample. Both clocks
 * are the same machine's.
 */
async function collect(ctx: CoreContext, recorder: Recorder, world: boolean, want: number, poller: FrmPoller | null): Promise<Pass.Round[]> {
  const api = createServerApi(ctx.deps.config.apiBaseUrl);
  const rounds: Pass.Round[] = [];
  {
    let seen = accepted(recorder).length;
    while (rounds.length < want) {
      await ctx.waitFor("the next accepted snapshot", () => (accepted(recorder).length > seen ? true : undefined), MIN);
      const entry = accepted(recorder)[seen]!;
      seen = accepted(recorder).length;
      const [status, autoPause] = await Promise.all([readApiStatus(api).catch(() => null), readAutoPause(api).catch(() => null)]);
      let bracket: ReturnType<typeof pickBracket> = { before: null, after: null };
      const observed = Date.parse(String((entry.snapshot as { observedAt?: unknown } | undefined)?.observedAt ?? ""));
      if (world && poller && Number.isFinite(observed)) {
        // The push follows the sample, so the read after it is usually in already; wait a little for it if not.
        await ctx.waitFor("an FRM read after the sample", () => (poller.reads.some((r) => r.startedAt >= observed) || Date.now() - observed > FRM_BRACKET_GAP_MS ? true : undefined), 10_000).catch(() => undefined);
        bracket = pickBracket(poller.reads, observed, FRM_BRACKET_GAP_MS);
      }
      const iso = (ms: number | undefined) => (ms === undefined ? null : new Date(ms).toISOString());
      rounds.push({
        at: entry.at, world, snapshot: entry.snapshot as Record<string, unknown>, api: status, autoPause,
        frmBefore: bracket.before?.groups ?? null,
        frmAfter: bracket.after?.groups ?? null,
        frmBeforeEndedAt: iso(bracket.before?.endedAt),
        frmAfterStartedAt: iso(bracket.after?.startedAt),
      });
    }
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
    let poller: FrmPoller | null = null;
    try {
      await enrolPhase(ctx, host, recorder);
      const rounds = await collect(ctx, recorder, false, 4, null);
      // The owner's memory goal (memory.json; each label says whether a game ran, as the API said then): the mod enrolled
      // and pushing with no game; then, at the stage the no-mod pre-boot was sampled (a save just loaded and healthy,
      // auto-pause as the server had it), the same with the mod; then after the world rounds.
      await ctx.sampleMemory("withMod");
      await ctx.deps.loadWorld!();
      await ctx.deps.waitHealthy();
      await ctx.sampleMemory("withMod");
      ctx.rememberAutoPause(await ctx.deps.readAutoPause!());
      await ctx.deps.setAutoPause!(false); // the factory runs, so power moves (S3)
      // Started before the settle, so the first world round already has a read before its sample (re-review 2, minor 1).
      // It loads the server too: withModAfterRounds and the world rounds' tick figures are taken while it runs.
      const frmBaseUrl = ctx.deps.config.frmBaseUrl;
      if (frmBaseUrl) poller = pollFrm(frmBaseUrl);
      await ctx.deps.sleep(MIN); // the world settles
      rounds.push(...(await collect(ctx, recorder, true, 12, poller)));
      await ctx.sampleMemory("withModAfterRounds");
      poller?.stop();
      const frmPolling = poller ? { intervalMs: FRM_POLL_INTERVAL_MS, ...poller.counts() } : undefined;
      const cfg = JSON.parse(readFileSync(modConfigPath(ctx.deps.config.serverDir, MOD), "utf8")) as { enrollmentCode?: string };
      ctx.captures["enrol.json"] = {
        state: await host.state("mod"),
        codeBlanked: cfg.enrollmentCode === "",
        credentialWritten: ctx.credentialExists(),
        refused: recorder.entries.filter((e) => e.agent === "mod" && e.path === "/agent/v1/snapshots" && e.status >= 400).length,
        rounds,
        ...(frmPolling ? { frmPolling } : {}),
      } satisfies Pass.Enrol;
    } finally {
      poller?.stop();
      ctx.captures["recorder.json"] = recorder.entries.map(({ snapshot: _, ...rest }) => rest);
      await recorder.close();
    }
  },
};
