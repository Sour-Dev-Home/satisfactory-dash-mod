import * as Pass from "../../../runs/command/pass";
import type { CoreContext, CoreRunSpec } from "../core-run";
import type { HostClient } from "../backend-host";
import { startRecorder, type Recorder } from "../recorder";
import { enrolPhase } from "./enrol";

const IN_FLIGHT = new Set(["pending", "sent"]);
const resultPosts = (rec: Recorder, id: string) =>
  rec.entries.filter((e) => e.agent === "mod" && e.method === "POST" && e.path === `/agent/v1/commands/${id}/result`).length;

/** Queues one set_auto_pause, waits up to 90 s for it to leave pending/sent, then reads the game's API. */
async function runCommand(ctx: CoreContext, host: HostClient, recorder: Recorder, params: Record<string, unknown>): Promise<Pass.Outcome> {
  const id = await host.queueCommand("mod", params);
  let last = { status: "pending", resultCode: null as string | null };
  await ctx
    .waitFor(`command ${id} to finish`, async () => {
      last = await host.command(id);
      return IN_FLIGHT.has(last.status) ? undefined : true;
    }, 90_000)
    .catch(() => undefined); // the outcome records the status it was left in; pass.ts fails it
  // The backend marks the command done while answering the result post, so let the recorder finish that entry.
  await ctx.waitFor("the result post", () => (resultPosts(recorder, id) ? true : undefined), 10_000).catch(() => undefined);
  const apiAfter = await ctx.deps.readAutoPause!().catch(() => null);
  return { id, status: last.status, resultCode: last.resultCode, apiAfter, results: resultPosts(recorder, id) };
}

/** The command run (C9 needs a game: S7 skipped the entry map). */
export const command: CoreRunSpec = {
  requirements: Pass.requirements,
  backend: true,
  world: true,
  emptyBoot: true,
  evaluate: Pass.evaluate,
  async drive(ctx) {
    const host = await ctx.deps.backendHost!();
    const recorder = await startRecorder(host.relayUrl);
    let capture: Pass.Command | null = null;
    try {
      await enrolPhase(ctx, host, recorder);
      await ctx.sampleMemory("withMod");
      await ctx.deps.loadWorld!();
      await ctx.deps.waitHealthy();
      await ctx.sampleMemory("withMod");
      const before = await ctx.deps.readAutoPause!();
      ctx.rememberAutoPause(before); // runCore puts it back whatever happens
      capture = { before, flipped: null, unsupported: null, restored: null };
      capture.flipped = await runCommand(ctx, host, recorder, { enabled: !before });
      // Not a boolean: the table's CHECK allows any object (1790985600000_agent_commands.sql); the mod must refuse it.
      capture.unsupported = await runCommand(ctx, host, recorder, { enabled: "yes" });
      capture.restored = await runCommand(ctx, host, recorder, { enabled: before });
      await ctx.sampleMemory("withModAfterRounds");
    } finally {
      if (capture) {
        // Counted again over the whole run, so a late duplicate result post is caught too.
        for (const o of [capture.flipped, capture.unsupported, capture.restored]) if (o) o.results = resultPosts(recorder, o.id);
        ctx.captures["command.json"] = capture;
      }
      ctx.captures["recorder.json"] = recorder.entries.map(({ snapshot: _, ...rest }) => rest);
      await recorder.close();
    }
  },
};
