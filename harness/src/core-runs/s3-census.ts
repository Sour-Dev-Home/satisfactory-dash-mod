import * as Pass from "../../../runs/s3-census/pass";
import { NATIVE_SOURCES_S3, satisDashLines, type CoreContext, type CoreRunSpec } from "../core-run";
import { censusLines, logisticsRead } from "../reader-lines";
import { startRecorder } from "../recorder";
import { enrolPhase } from "./enrol";

const MIN = 60_000;
/** The first logistics pass lands about 8 minutes after the agent starts Running; the wait is bounded at 14. */
export const CENSUS_WAIT_MS = 14 * MIN;
const LOGISTICS_HEAD = "Logistics read (game thread only):";
const PORTS_HEAD = "Conveyor drone ports:";
const TIER_HEAD = "Conveyor tier census:";

/**
 * The one-off census (Task 10; the owner approves each dispatch): Test3 loaded with every native reader on, the mod enrolled, then
 * a bounded wait for the first Logistics read line. Read-only; it needs no FRM and judges none. census.json holds the parsed
 * census lines and the Logistics read line's counts.
 */
export const s3Census: CoreRunSpec = {
  requirements: Pass.requirements,
  backend: true,
  world: true,
  worldKey: "sprint3",
  modConfig: NATIVE_SOURCES_S3,
  evaluate: Pass.evaluate,
  drive: async (ctx: CoreContext) => {
    const host = await ctx.deps.backendHost!();
    const recorder = await startRecorder(host.relayUrl);
    const lines = () => satisDashLines(ctx.deps.readLogSince(ctx.offset));
    try {
      await ctx.deps.loadWorld!("sprint3");
      await ctx.deps.waitHealthy();
      ctx.rememberAutoPause(await ctx.deps.readAutoPause!());
      await ctx.deps.setAutoPause!(false);
      await enrolPhase(ctx, host, recorder);
      // One bound for the whole wait. WorldSender.cpp logs, in one pass: the Logistics read line, then `Conveyor kinds:`, then
      // `Conveyor drone ports:`, then (once per run) the tier census lines. So the ports line is not the last of the family: after
      // it the drive waits for the first tier line. A timeout anywhere falls through to judging what the log holds.
      const start = ctx.deps.now().getTime();
      const left = () => Math.max(1000, CENSUS_WAIT_MS - (ctx.deps.now().getTime() - start));
      const has = (head: string) => () => (lines().some((l) => l.includes(head)) ? true : undefined);
      for (const [what, head] of [["the first Logistics read line", LOGISTICS_HEAD], ["the Conveyor drone ports line", PORTS_HEAD], ["a Conveyor tier census line", TIER_HEAD]] as const) {
        try {
          await ctx.waitFor(what, has(head), left());
        } catch {
          break;
        }
      }
    } finally {
      const all = lines();
      ctx.captures["census.json"] = { ...censusLines(all), logistics: logisticsRead(all) };
      await recorder.close();
    }
  },
};
