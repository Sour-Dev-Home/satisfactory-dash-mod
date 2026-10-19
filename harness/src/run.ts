import { execFile } from "node:child_process";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import * as S0 from "../../spikes/S0/pass";
import * as S1 from "../../spikes/S1/pass";
import * as S1b from "../../spikes/S1b/pass";
import * as S2 from "../../spikes/S2/pass";
import { checkApi, scanSpikes, type Use } from "./check-api";
import { logPathOf, pluginDirFor, type RunnerConfig } from "./config";
import { type Echo, startEcho } from "./echo";
import { type Result, writeEvidence } from "./evidence";
import { logOffset, modLines, readLogSince, versionLines, waitForLine } from "./log-watch";
import { createServerApi } from "./server-api";
import { installMod, killTree, removeMod, type ServerProcess, startServer, stopServer, waitHealthy } from "./server-control";
import { readTickRate, sampleTicks, TICK_INTERVAL_MS, TICK_WINDOW_MS, type TickSeries } from "./ticks";
import { type Toolchain, toolchainMismatches } from "./toolchain";
import { type Evidence, SPIKE_IDS, type SpikeId, type Verdict } from "./verdict";

/** A precondition failed: nothing ran, and no evidence is written. */
export class RefusalError extends Error {}

export interface SpikeSpec {
  claim: string;
  evaluate(e: Evidence): Verdict;
  echo: boolean;
  ticks: boolean;
  /** The line that says the mod has done its work (or, for S2, has started). */
  doneLine: RegExp;
  /** S2 only: the line after the 40th post. */
  finalLine?: RegExp;
}

const MIN = 60_000;
export const SPIKES: Record<SpikeId, SpikeSpec> = {
  S0: { claim: S0.claim, evaluate: S0.evaluate, echo: false, ticks: false, doneLine: /satis-mod loaded/ },
  S1: { claim: S1.claim, evaluate: S1.evaluate, echo: true, ticks: false, doneLine: /satis-mod s1 done/ },
  S1b: { claim: S1b.claim, evaluate: S1b.evaluate, echo: false, ticks: false, doneLine: /satis-mod s1b bad / },
  S2: { claim: S2.claim, evaluate: S2.evaluate, echo: true, ticks: true, doneLine: /satis-mod loaded/, finalLine: /satis-mod s2 done/ },
};
const DONE_TIMEOUT_MS = 5 * MIN;
const FINAL_TIMEOUT_MS = 3 * MIN;

export const modName = (spike: SpikeId) => `SatisMod${spike}`;

export interface RunDeps {
  config: RunnerConfig;
  toolchain: Toolchain;
  outDir: string;
  checkApi(): { missing: Use[]; badRows: string[] };
  build(spike: SpikeId): Promise<void>;
  remove(name: string): void;
  install(pluginDir: string): string;
  start(): ServerProcess;
  stop(proc: ServerProcess): Promise<void>;
  waitHealthy(): Promise<void>;
  logOffset(): number;
  waitForLine(pattern: RegExp, fromOffset: number, timeoutMs: number): Promise<void>;
  readLogSince(offset: number): string;
  startEcho(): Promise<Echo>;
  sampleTicks(): Promise<TickSeries>;
  now(): Date;
  env: { commit: string; runUrl: string | null };
}

const message = (e: unknown) => (e instanceof Error ? e.message : String(e));

export async function runSpike(spike: SpikeId, deps: RunDeps): Promise<{ result: Result; dir: string }> {
  const spec = SPIKES[spike];
  const mismatches = toolchainMismatches(deps.toolchain, deps.config.installed);
  if (mismatches.length) throw new RefusalError(`toolchain mismatch: ${mismatches.join("; ")}`);
  const api = deps.checkApi();
  if (api.missing.length || api.badRows.length) {
    const list = [...api.badRows, ...api.missing.map((u) => `${u.path}:${u.line} ${u.name}`)];
    throw new RefusalError(`check-api failed: ${list.join(", ")}`);
  }

  const startedAt = deps.now().toISOString();
  await deps.build(spike);
  for (const id of SPIKE_IDS) deps.remove(modName(id));

  const runReasons: string[] = [];
  let baseline: TickSeries | undefined;
  let withMod: TickSeries | undefined;
  let logText = "";
  let echo: Echo | undefined;

  if (spec.ticks) {
    const proc = deps.start();
    try {
      await deps.waitHealthy();
      baseline = await deps.sampleTicks();
    } catch (e) {
      runReasons.push(`baseline: ${message(e)}`);
    } finally {
      await deps.stop(proc);
    }
  }

  const installed = deps.install(pluginDirFor(deps.config, spike));
  if (installed !== modName(spike)) throw new RefusalError(`the built plugin is ${installed}, expected ${modName(spike)}`);

  try {
    if (spec.echo) echo = await deps.startEcho();
    const offset = deps.logOffset();
    const proc = deps.start();
    try {
      await deps.waitHealthy();
      await deps.waitForLine(spec.doneLine, offset, DONE_TIMEOUT_MS);
      if (spec.ticks) withMod = await deps.sampleTicks();
      if (spec.finalLine) await deps.waitForLine(spec.finalLine, offset, FINAL_TIMEOUT_MS);
    } catch (e) {
      runReasons.push(message(e));
    } finally {
      await deps.stop(proc);
      logText = deps.readLogSince(offset);
    }
  } finally {
    await echo?.close();
  }

  const evidence: Evidence = {
    modLines: modLines(logText),
    echo: echo?.requests ?? [],
    ...(baseline && withMod ? { ticks: { baseline, withMod } } : {}),
  };
  const verdict = spec.evaluate(evidence);
  const result: Result = {
    spike,
    claim: spec.claim,
    pass: runReasons.length === 0 && verdict.pass,
    reasons: [...runReasons, ...verdict.reasons],
    startedAt,
    endedAt: deps.now().toISOString(),
    toolchain: {
      required: { engine: deps.toolchain.engine, sml: deps.toolchain.sml },
      declared: deps.config.installed,
    },
    versionLines: versionLines(logText),
    modLines: evidence.modLines,
    harnessCommit: deps.env.commit,
    runUrl: deps.env.runUrl,
  };
  const captures: Record<string, unknown> = {};
  if (spec.echo) captures["echo.json"] = evidence.echo;
  if (spec.ticks) captures["ticks.json"] = { baseline: baseline ?? null, withMod: withMod ?? null };
  // The full server log stays on the runner: it can name the machine, and bundles are public artifacts.
  return { result, dir: writeEvidence(deps.outDir, result, captures) };
}

const sleep = (ms: number) => new Promise<void>((r) => setTimeout(r, ms));

export function realDeps(config: RunnerConfig, toolchain: Toolchain, outDir: string, repoRoot: string): RunDeps {
  const api = createServerApi(config.apiBaseUrl);
  const logPath = logPathOf(config);
  return {
    config,
    toolchain,
    outDir,
    checkApi: () => checkApi(scanSpikes(repoRoot), readFileSync(join(repoRoot, "api-sources.md"), "utf8")),
    build: (spike) =>
      new Promise((resolve, reject) => {
        const [exe, ...args] = config.buildCommand.map((a) => a.replaceAll("{spike}", spike));
        execFile(exe!, args, { cwd: repoRoot, maxBuffer: 64 * 1024 * 1024 }, (err) => (err ? reject(new Error(`build failed: ${err.message}`)) : resolve()));
      }),
    remove: (name) => removeMod(name, config.serverDir),
    install: (pluginDir) => installMod(pluginDir, config.serverDir),
    start: () => startServer(config),
    stop: async (proc) => void (await stopServer(proc, api, { graceMs: 60_000, kill: killTree })),
    waitHealthy: async () => void (await waitHealthy(api, { timeoutMs: 10 * MIN })),
    logOffset: () => logOffset(logPath),
    waitForLine: async (pattern, fromOffset, timeoutMs) => void (await waitForLine(logPath, pattern, { fromOffset, timeoutMs })),
    readLogSince: (offset) => readLogSince(logPath, offset),
    startEcho: () => startEcho(),
    sampleTicks: () => sampleTicks(() => readTickRate(api), { durationMs: TICK_WINDOW_MS, intervalMs: TICK_INTERVAL_MS, sleep, now: Date.now }),
    now: () => new Date(),
    env: {
      commit: process.env.GITHUB_SHA ?? "local",
      runUrl: process.env.GITHUB_RUN_ID
        ? `${process.env.GITHUB_SERVER_URL}/${process.env.GITHUB_REPOSITORY}/actions/runs/${process.env.GITHUB_RUN_ID}`
        : null,
    },
  };
}
