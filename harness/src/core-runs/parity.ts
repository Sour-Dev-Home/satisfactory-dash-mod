import { createHash } from "node:crypto";
import { existsSync, readFileSync } from "node:fs";
import * as Pass from "../../../runs/parity/pass";
import { contractPin, randomCode } from "../backend";
import type { CoreRunSpec } from "../core-run";
import { type RecordedCall, startRecorder } from "../recorder";
import { createServerApi } from "../server-api";
import { enrolPhase } from "./enrol";
import { agentPorts, generateApiToken, invalidateApiTokens, type NodeAgent, startNodeAgent } from "./node-agent";

const MIN = 60_000;
const SNAPSHOTS = "/agent/v1/snapshots";
/**
 * The Node agent's posts are kept this far either side of the window, so the window's first and last mod samples have
 * their power neighbours (Pass.PARITY_NEIGHBOUR_GAP_MS by observedAt; a post arrives a little after its observedAt).
 */
export const NODE_MARGIN_MS = Pass.PARITY_NEIGHBOUR_GAP_MS + 5000;

type Who = "mod" | "node";
type Captured = Pick<Pass.Parity, "mod" | "node" | "accepted" | "oversizedAccepted" | "unrecordedAccepted" | "refused">;

/**
 * What the judge gets from the recorder: each agent's accepted snapshot posts (the mod's in [from, to], the Node
 * agent's NODE_MARGIN_MS either side too), trimmed; how many were accepted, how many were over the recorder's cap
 * (kept trimmed) and how many it couldn't read; and the posts in [from, to] the backend didn't answer 200.
 */
export function captureWindow(entries: RecordedCall[], from: string, to: string): Captured {
  const out: Captured = {
    mod: [], node: [], accepted: { mod: 0, node: 0 }, oversizedAccepted: { mod: 0, node: 0 }, unrecordedAccepted: { mod: 0, node: 0 },
    refused: { mod: [], node: [] },
  };
  const t0 = Date.parse(from);
  const t1 = Date.parse(to);
  for (const who of ["mod", "node"] as const satisfies readonly Who[]) {
    const margin = who === "node" ? NODE_MARGIN_MS : 0;
    const posts = entries.filter((e) => e.agent === who && e.method === "POST" && e.path === SNAPSHOTS);
    const inSpan = (e: RecordedCall, m: number) => Date.parse(e.at) >= t0 - m && Date.parse(e.at) <= t1 + m;
    const accepted = posts.filter((e) => e.status === 200 && inSpan(e, margin));
    out.accepted[who] = accepted.length;
    out.oversizedAccepted[who] = accepted.filter((e) => e.oversized).length;
    for (const e of accepted) {
      const s = Pass.trimSnapshot(e.at, e.snapshot);
      if (s) out[who].push(s);
      else out.unrecordedAccepted[who]++;
    }
    out.refused[who] = posts.filter((e) => e.status !== 200 && inSpan(e, 0)).map((e) => ({ at: e.at, status: e.status }));
  }
  return out;
}

const sha256File = (path: string): string | null => {
  try {
    return createHash("sha256").update(readFileSync(path)).digest("hex");
  } catch {
    return null;
  }
};

/**
 * Parity with the Node agent (pass.ts header): the world runs with auto-pause off; the mod enrols through the recorder;
 * the Node agent, set up with a fresh game API token and the "node" server's code (seeded just before it enrols), runs
 * against the same recorder; then 30 minutes side by side. Both stop, and the capture keeps both agents' accepted
 * snapshots (hashed sessions, ids and MW only), the posts that weren't accepted, and both servers' state.
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
    const now = () => ctx.deps.now().toISOString();
    const ports = agentPorts(config);
    const agentCjs = config.parity!.agentCjs;
    const api = createServerApi(config.apiBaseUrl);
    const host = await ctx.deps.backendHost!();
    const recorder = await startRecorder(host.relayUrl);
    const capture: Pass.Parity = {
      windowStartedAt: null, windowEndedAt: null, mod: [], node: [], accepted: { mod: 0, node: 0 },
      oversizedAccepted: { mod: 0, node: 0 }, unrecordedAccepted: { mod: 0, node: 0 }, refused: { mod: [], node: [] },
      state: { mod: null, node: null }, tokensInvalidated: null,
      nodeAgent: { agentCjsSha256: sha256File(agentCjs), pin: contractPin() }, cleanup: null,
    };
    const acceptedBy = (who: Who) => recorder.entries.filter((e) => e.agent === who && e.path === SNAPSHOTS && e.status === 200);
    const observed = (e: RecordedCall) => String((e.snapshot as { observedAt?: unknown } | undefined)?.observedAt ?? "");
    let agent: NodeAgent | null = null;
    let tokenAsked = false;
    try {
      await ctx.sampleMemory("withMod");
      await ctx.deps.loadWorld!();
      await ctx.deps.waitHealthy();
      await ctx.sampleMemory("withMod");
      ctx.rememberAutoPause(await ctx.deps.readAutoPause!());
      await ctx.deps.setAutoPause!(false); // the factory runs, so power moves (S3)
      await enrolPhase(ctx, host, recorder);
      tokenAsked = true; // from here the end prunes the server's application tokens, even if this call fails midway
      const apiToken = await generateApiToken(api);
      ctx.noteSecret(apiToken);
      // Seeded right before the agent enrols, so its 10-minute expiry can't lapse first (review of 0dee72a, minor 1).
      const nodeCode = randomCode();
      ctx.noteCode(nodeCode);
      await host.seedCode("node", nodeCode);
      agent = await startNodeAgent({ agentCjs, ...ports, apiToken, code: nodeCode, backendUrl: recorder.url });
      let exitCode: number | null | undefined;
      void agent.exited.then((c) => (exitCode = c));
      await ctx.waitFor("the Node agent's first accepted snapshot", () => (exitCode !== undefined || acceptedBy("node").length ? true : undefined), 3 * MIN);
      if (exitCode !== undefined) throw new Error(`the Node agent exited (${exitCode}) before its first accepted snapshot`);
      capture.windowStartedAt = now();
      for (let i = 0; i < Pass.WINDOW_MINUTES; i++) {
        await ctx.deps.sleep(MIN);
        if (exitCode !== undefined) throw new Error(`the Node agent exited (${exitCode}) during the window`);
      }
      capture.windowEndedAt = now();
      // The last mod sample's power neighbour after it: wait for the Node agent's next reading before stopping it.
      const lastMod = acceptedBy("mod").map(observed).filter((o) => o <= capture.windowEndedAt!).sort().at(-1);
      if (lastMod) {
        await ctx.waitFor("the Node agent's reading after the mod's last sample", () => (acceptedBy("node").some((e) => observed(e) >= lastMod) ? true : undefined), 20_000)
          .catch(() => undefined); // the judge fails that round if it never came
      }
      capture.nodeAgentMemory = ctx.deps.processMemory ? await ctx.deps.processMemory(agent.pid).catch(() => null) : null;
      await ctx.sampleMemory("withModAfterRounds");
    } finally {
      if (agent) {
        const stopped = await agent.stop().then(() => true, () => false);
        capture.cleanup = { stopped, homeLeft: existsSync(agent.home) };
      }
      if (tokenAsked) capture.tokensInvalidated = await invalidateApiTokens(api).then(() => true, () => false);
      capture.state = {
        mod: await host.state("mod").catch(() => null),
        node: await host.state("node").catch(() => null),
      };
      // A run that failed mid-window still keeps what it saw up to now (the verdict fails on the missing window anyway).
      if (capture.windowStartedAt) Object.assign(capture, captureWindow(recorder.entries, capture.windowStartedAt, capture.windowEndedAt ?? now()));
      ctx.captures["parity.json"] = capture;
      ctx.captures["recorder.json"] = recorder.entries.map(({ snapshot: _, ...rest }) => rest);
      await recorder.close();
    }
  },
};
