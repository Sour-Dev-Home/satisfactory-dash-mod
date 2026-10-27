import * as Pass from "../../../runs/s3-census/pass";
import { NATIVE_SOURCES_S3, satisDashLines, type CoreContext, type CoreRunSpec } from "../core-run";
import { censusLines, logisticsRead } from "../reader-lines";
import { startRecorder } from "../recorder";
import { enrolPhase } from "./enrol";

const MIN = 60_000;
/** The first logistics pass lands about 8 minutes after the agent starts Running; the wait is bounded at 14. */
export const CENSUS_WAIT_MS = 14 * MIN;
/** The census lines follow the Logistics read line in the same pass; this is slack for them to reach the log file. */
export const CENSUS_SETTLE_MS = 20_000;
const LOGISTICS_HEAD = "Logistics read (game thread only):";

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
      await ctx.waitFor("the first Logistics read line", () => (lines().some((l) => l.includes(LOGISTICS_HEAD)) ? true : undefined), CENSUS_WAIT_MS);
      await ctx.deps.sleep(CENSUS_SETTLE_MS);
    } finally {
      const all = lines();
      ctx.captures["census.json"] = { ...censusLines(all), logistics: logisticsRead(all) };
      await recorder.close();
    }
  },
};
