import { execFile } from "node:child_process";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import * as S0 from "../../spikes/S0/pass";
import * as S1 from "../../spikes/S1/pass";
import * as S1b from "../../spikes/S1b/pass";
import * as S2 from "../../spikes/S2/pass";
import * as S4 from "../../spikes/S4/pass";
import { checkApi, scanSpikes, type Use } from "./check-api";
import { logPathOf, pluginDirFor, type RunnerConfig } from "./config";
import { type Echo, startEcho } from "./echo";
import { type Result, writeEvidence } from "./evidence";
import { modLines, readLogSince, resetLog, versionLines, waitForLine } from "./log-watch";
import { randomCredential, removeModConfig, sha1Prefix, writeModConfig } from "./mod-config";
import { createServerApi } from "./server-api";
import {
  installMod,
  killTree,
  removeSpikeMods,
  serverAnswering,
  type ServerProcess,
  startServer,
  stopServer,
  waitHealthy,
  whileRunning,
} from "./server-control";
import { halfMedians, readTickRate, sampleTicks, TICK_INTERVAL_MS, TICK_WARMUP_MS, TICK_WINDOW_MS, type TickSeries } from "./ticks";
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
  /** S4: the harness writes a fresh credential to the mod's SML config before the boot. */
  credential?: boolean;
}

const MIN = 60_000;
/** Partial while a spike's probes are still being built (S3, S5). */
export const SPIKES: Partial<Record<SpikeId, SpikeSpec>> = {
  S0: { claim: S0.claim, evaluate: S0.evaluate, echo: false, ticks: false, doneLine: /satis-mod loaded/ },
  S1: { claim: S1.claim, evaluate: S1.evaluate, echo: true, ticks: false, doneLine: /satis-mod s1 done/ },
  S1b: { claim: S1b.claim, evaluate: S1b.evaluate, echo: false, ticks: false, doneLine: /satis-mod s1b bad / },
  S2: { claim: S2.claim, evaluate: S2.evaluate, echo: true, ticks: true, doneLine: /satis-mod loaded/, finalLine: /satis-mod s2 done/ },
  S4: { claim: S4.claim, evaluate: S4.evaluate, echo: false, ticks: false, credential: true, doneLine: /satis-mod s4 (read|error)/ },
};
/** The spikes with a spec, which `run` accepts. */
export const RUNNABLE: SpikeId[] = SPIKE_IDS.filter((id) => SPIKES[id] !== undefined);
/** Run only from a local session, never offered on the self-hosted runner (owner, 2026-10-20). */
export const LOCAL_ONLY: SpikeId[] = ["S5"];
const DONE_TIMEOUT_MS = 5 * MIN;
const FINAL_TIMEOUT_MS = 3 * MIN;

export const modName = (spike: SpikeId) => `SatisMod${spike}`;

export interface RunDeps {
  config: RunnerConfig;
  toolchain: Toolchain;
  outDir: string;
  checkApi(): { missing: Use[]; badRows: string[] };
  /** Whether anything already answers the configured API port. */
  serverAnswering(): Promise<boolean>;
  build(spike: SpikeId): Promise<void>;
  removeSpikeMods(): void;
  /** Installs the built plugin, refusing (before copying) one not named `expected`. */
  install(pluginDir: string, expected: string): void;
  start(): ServerProcess;
  stop(proc: ServerProcess): Promise<void>;
  waitHealthy(): Promise<void>;
  /** Clears the previous run's log and returns the offset to read from (0). */
  resetLog(): number;
  waitForLine(pattern: RegExp, fromOffset: number, timeoutMs: number): Promise<void>;
  readLogSince(offset: number): string;
  startEcho(): Promise<Echo>;
  sampleTicks(): Promise<TickSeries>;
  /** Writes the spike's SML config file (S4, S5). */
  writeModConfig?(mod: string, values: Record<string, string>): void;
  removeModConfig?(mod: string): void;
  randomCredential?(): string;
  now(): Date;
  env: { commit: string; runUrl: string | null };
}

const message = (e: unknown) => (e instanceof Error ? e.message : String(e));

/** Above this gap between a tick window's half medians, the window measured two server states. */
const STEADY_TOLERANCE = 0.1;

/** At most 50 matching log lines: enough to show a cause, small enough for a public bundle. */
const pickLines = (text: string, pattern: RegExp) =>
  text.split(/\r?\n/).filter((l) => pattern.test(l)).slice(0, 50);

/** The last lines of a build's output: UAT and Build.bat write their errors to stdout. */
export const lastLines = (text: string, n: number) => text.trimEnd().split(/\r?\n/).slice(-n).join("\n");

export async function runSpike(spike: SpikeId, deps: RunDeps): Promise<{ result: Result; dir: string }> {
  const spec = SPIKES[spike];
  if (!spec) throw new RefusalError(`no spec for ${spike} yet`);
  const mismatches = toolchainMismatches(deps.toolchain, deps.config.installed);
  if (mismatches.length) throw new RefusalError(`toolchain mismatch: ${mismatches.join("; ")}`);
  const api = deps.checkApi();
  if (api.missing.length || api.badRows.length) {
    const list = [...api.badRows, ...api.missing.map((u) => `${u.path}:${u.line} ${u.name}`)];
    throw new RefusalError(`check-api failed: ${list.join(", ")}`);
  }

  // HealthCheck needs no auth, so the owner's real server would pass for ours and could receive our Shutdown.
  const refuseIfAnswering = async () => {
    if (await deps.serverAnswering()) {
      throw new RefusalError(`something already answers at ${deps.config.apiBaseUrl}: stop the other server first`);
    }
  };
  const startFresh = async () => {
    await refuseIfAnswering();
    return deps.start();
  };
  await refuseIfAnswering();

  const startedAt = deps.now().toISOString();
  try {
    await deps.build(spike);
  } catch (e) {
    throw new RefusalError(message(e));
  }
  deps.removeSpikeMods();
  let credential: string | undefined;
  if (spec.credential) {
    if (!deps.writeModConfig || !deps.removeModConfig || !deps.randomCredential) throw new RefusalError("S4 needs the mod-config deps");
    credential = deps.randomCredential();
    deps.writeModConfig(modName(spike), { credential });
  }

  const runReasons: string[] = [];
  let baseline: TickSeries | undefined;
  let withMod: TickSeries | undefined;
  let baselineLog = "";
  let logText = "";
  let echo: Echo | undefined;

  if (spec.ticks) {
    // The with-mod boot resets the log, so the baseline boot's lines are read before that.
    const baseOffset = deps.resetLog();
    const proc = await startFresh();
    try {
      await whileRunning(proc, deps.waitHealthy(), "waiting for health");
      baseline = await whileRunning(proc, deps.sampleTicks(), "sampling the baseline");
    } catch (e) {
      runReasons.push(`baseline: ${message(e)}`);
    } finally {
      await deps.stop(proc);
      baselineLog = deps.readLogSince(baseOffset);
    }
  }

  // The credential file is removed whatever happens after it was written (Review Focus 1).
  try {
    try {
      deps.install(pluginDirFor(deps.config, spike), modName(spike));
    } catch (e) {
      throw new RefusalError(message(e));
    }

    try {
      if (spec.echo) echo = await deps.startEcho();
      const offset = deps.resetLog();
      const proc = await startFresh();
      try {
        await whileRunning(proc, deps.waitHealthy(), "waiting for health");
        await whileRunning(proc, deps.waitForLine(spec.doneLine, offset, DONE_TIMEOUT_MS), `waiting for ${spec.doneLine}`);
        if (spec.ticks) withMod = await whileRunning(proc, deps.sampleTicks(), "sampling with the mod");
        if (spec.finalLine) {
          await whileRunning(proc, deps.waitForLine(spec.finalLine, offset, FINAL_TIMEOUT_MS), `waiting for ${spec.finalLine}`);
        }
      } catch (e) {
        runReasons.push(message(e));
      } finally {
        await deps.stop(proc);
        logText = deps.readLogSince(offset);
      }
    } finally {
      await echo?.close();
    }
  } finally {
    if (spec.credential) deps.removeModConfig!(modName(spike));
  }

  // A level shift between the with-mod window's halves means it measured two server states (the boot phase runs at
  // ~30, then ~22), and its median could then hide a real drop: a false PASS (final review, finding 3). The same
  // shift in the baseline only raises the baseline, which makes the check stricter, so it isn't a reason here;
  // ticks.json keeps both windows for a reader.
  const halves = withMod && halfMedians(withMod.samples);
  if (halves && Math.abs(halves.first - halves.second) > STEADY_TOLERANCE * Math.max(halves.first, halves.second)) {
    const r = (n: number) => Number(n.toFixed(2));
    runReasons.push(`with-mod window isn't steady: first half ${r(halves.first)}, second half ${r(halves.second)}`);
  }

  const evidence: Evidence = {
    modLines: modLines(logText),
    echo: echo?.requests ?? [],
    ...(baseline && withMod ? { ticks: { baseline, withMod } } : {}),
    ...(credential ? { credential: { length: credential.length, sha1Prefix: sha1Prefix(credential), inLog: logText.includes(credential) } } : {}),
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
  if (spec.ticks) {
    captures["ticks.json"] = { baseline: baseline ?? null, withMod: withMod ?? null };
    captures["baseline-log.json"] = pickLines(baselineLog, /LogServer|LogInit: Net CL/);
  }
  // S1b's mod only sees EHttpFailureReason ("Other" for a TLS failure); the engine's lines show the cause.
  if (spike === "S1b") captures["http.json"] = pickLines(logText, /LogHttp|bVerifyPeer/);
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
    serverAnswering: () => serverAnswering(api),
    build: (spike) =>
      new Promise((resolve, reject) => {
        const [exe, ...args] = config.buildCommand.map((a) => a.replaceAll("{spike}", spike));
        execFile(exe!, args, { cwd: repoRoot, maxBuffer: 64 * 1024 * 1024 }, (err, stdout, stderr) =>
          err ? reject(new Error(`build failed: ${err.message}\n${lastLines(`${stdout}\n${stderr}`, 40)}`)) : resolve(),
        );
      }),
    removeSpikeMods: () => removeSpikeMods(config.serverDir),
    install: (pluginDir, expected) => void installMod(pluginDir, config.serverDir, expected),
    start: () => startServer(config),
    stop: async (proc) => void (await stopServer(proc, api, { graceMs: 60_000, kill: killTree })),
    waitHealthy: async () => void (await waitHealthy(api, { timeoutMs: 10 * MIN })),
    resetLog: () => resetLog(logPath),
    waitForLine: async (pattern, fromOffset, timeoutMs) => void (await waitForLine(logPath, pattern, { fromOffset, timeoutMs })),
    readLogSince: (offset) => readLogSince(logPath, offset),
    startEcho: () => startEcho(),
    sampleTicks: () =>
      sampleTicks(() => readTickRate(api), {
        warmupMs: TICK_WARMUP_MS, durationMs: TICK_WINDOW_MS, intervalMs: TICK_INTERVAL_MS, sleep, now: Date.now,
      }),
    writeModConfig: (mod, values) => void writeModConfig(config.serverDir, mod, values),
    removeModConfig: (mod) => removeModConfig(config.serverDir, mod),
    randomCredential,
    now: () => new Date(),
    env: {
      commit: process.env.GITHUB_SHA ?? "local",
      runUrl: process.env.GITHUB_RUN_ID
        ? `${process.env.GITHUB_SERVER_URL}/${process.env.GITHUB_REPOSITORY}/actions/runs/${process.env.GITHUB_RUN_ID}`
        : null,
    },
  };
}
