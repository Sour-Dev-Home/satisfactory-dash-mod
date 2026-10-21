import { existsSync } from "node:fs";
import * as Pass from "../../../runs/parity/pass";
import { randomCode } from "../backend";
import type { CoreRunSpec } from "../core-run";
import { startRecorder } from "../recorder";
import { createServerApi } from "../server-api";
import { enrolPhase } from "./enrol";
import { agentPorts, generateApiToken, invalidateApiTokens, type NodeAgent, startNodeAgent } from "./node-agent";

const MIN = 60_000;
const SNAPSHOTS = "/agent/v1/snapshots";

/**
 * Parity with the Node agent (pass.ts header): the world runs with auto-pause off; the mod enrols through the recorder;
 * the Node agent, set up with a fresh game API token and the "node" server's code, runs against the same recorder; then
 * 30 minutes side by side. Both stop, and the capture keeps both agents' accepted snapshots (hashed sessions, ids and MW
 * only), the posts that weren't accepted, and both servers' state.
 */
export const parity: CoreRunSpec = {
  requirements: Pass.requirements,
  backend: true,
  world: true,
  evaluate: Pass.evaluate,
  preflight(deps) {
    const agentCjs = deps.config.parity?.agentCjs;
    if (!agentCjs) throw new Error("parity needs parity.agentCjs in the runner config (docs/runner.md, \"The parity run's Node agent\")");
    if (!existsSync(agentCjs)) throw new Error(`parity.agentCjs doesn't exist or isn't readable by this account: build it as docs/runner.md says`);
    agentPorts(deps.config); // needs frmBaseUrl
  },
  async drive(ctx) {
    const { config } = ctx.deps;
    const ports = agentPorts(config);
    const api = createServerApi(config.apiBaseUrl);
    const host = await ctx.deps.backendHost!();
    const recorder = await startRecorder(host.relayUrl);
    const capture: Pass.Parity = {
      windowStartedAt: null, windowEndedAt: null, mod: [], node: [], refused: { mod: [], node: [] },
      state: { mod: null, node: null }, tokensInvalidated: null,
    };
    let agent: NodeAgent | null = null;
    let tokenAsked = false;
    try {
      await ctx.sampleMemory("withMod");
      await ctx.deps.loadWorld!();
      await ctx.deps.waitHealthy();
      await ctx.sampleMemory("withMod");
      ctx.rememberAutoPause(await ctx.deps.readAutoPause!());
      await ctx.deps.setAutoPause!(false); // the factory runs, so power moves (S3)
      const nodeCode = randomCode();
      await host.seedCode("node", nodeCode);
      ctx.noteSecret(nodeCode);
      await enrolPhase(ctx, host, recorder);
      tokenAsked = true; // from here the end prunes the server's application tokens, even if this call fails midway
      const apiToken = await generateApiToken(api);
      ctx.noteSecret(apiToken);
      agent = await startNodeAgent({ agentCjs: config.parity!.agentCjs, ...ports, apiToken, code: nodeCode, backendUrl: recorder.url });
      let exitCode: number | null | undefined;
      void agent.exited.then((c) => (exitCode = c));
      await ctx.waitFor("the Node agent's first accepted snapshot", () => (exitCode !== undefined ? true : recorder.entries.some((e) => e.agent === "node" && e.path === SNAPSHOTS && e.status === 200) || undefined), 3 * MIN);
      if (exitCode !== undefined) throw new Error(`the Node agent exited (${exitCode}) before its first accepted snapshot`);
      capture.windowStartedAt = new Date().toISOString();
      for (let i = 0; i < Pass.WINDOW_MINUTES; i++) {
        await ctx.deps.sleep(MIN);
        if (exitCode !== undefined) throw new Error(`the Node agent exited (${exitCode}) during the window`);
      }
      capture.windowEndedAt = new Date().toISOString();
      capture.nodeAgentMemory = ctx.deps.processMemory ? await ctx.deps.processMemory(agent.pid).catch(() => null) : null;
      await ctx.sampleMemory("withModAfterRounds");
    } finally {
      await agent?.stop().catch(() => undefined);
      if (tokenAsked) capture.tokensInvalidated = await invalidateApiTokens(api).then(() => true, () => false);
      capture.state = {
        mod: await host.state("mod").catch(() => null),
        node: await host.state("node").catch(() => null),
      };
      // The window's posts only: before it the agents were enrolling, after it they were being stopped.
      const from = capture.windowStartedAt;
      const to = capture.windowEndedAt;
      if (from && to) {
        for (const who of ["mod", "node"] as const) {
          const posts = recorder.entries.filter((e) => e.agent === who && e.method === "POST" && e.path === SNAPSHOTS && e.at >= from && e.at <= to);
          capture[who] = posts.filter((e) => e.status === 200).map((e) => Pass.trimSnapshot(e.at, e.snapshot)).filter((s): s is Pass.Snap => s !== null);
          capture.refused[who] = posts.filter((e) => e.status !== 200).map((e) => ({ at: e.at, status: e.status }));
        }
      }
      ctx.captures["parity.json"] = capture;
      ctx.captures["recorder.json"] = recorder.entries.map(({ snapshot: _, ...rest }) => rest);
      await recorder.close();
    }
  },
};
