import { execFile } from "node:child_process";
import { readFileSync } from "node:fs";
import { homedir } from "node:os";
import { join } from "node:path";
import * as S0 from "../../spikes/S0/pass";
import * as S1 from "../../spikes/S1/pass";
import * as S1b from "../../spikes/S1b/pass";
import * as S2 from "../../spikes/S2/pass";
import * as S3 from "../../spikes/S3/pass";
import * as S4 from "../../spikes/S4/pass";
import * as S5 from "../../spikes/S5/pass";
import * as S6 from "../../spikes/S6/pass";
import * as S7 from "../../spikes/S7/pass";
import { collectAutoPauseSteps } from "./autopause";
import { type Backend, randomCode, realExec, startBackend } from "./backend";
import { checkApi, scanSources, type Use } from "./check-api";
import { logPathOf, pluginDirFor, type RunnerConfig } from "./config";
import { type Echo, startEcho } from "./echo";
import { collectPowerPairs, parseGetPower, readAutoPause, setAutoPause } from "./frm";
import { type Result, writeEvidence } from "./evidence";
import { lineReader, modLines, readLogSince, resetLog, versionLines, waitForLine } from "./log-watch";
import { randomCredential, removeModConfig, sha1Prefix, writeModConfig } from "./mod-config";
import { createServerApi } from "./server-api";
import { collectStatusPairs, readApiStatus } from "./status";
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
import { loadWorld, otherServerSettings, readAutoLoad, setAutoLoad } from "./world";
import { type AutoPauseStep, type Evidence, SPIKE_IDS, type SpikeId, type StatusPair, type Verdict } from "./verdict";

import { RefusalError } from "./refusal";

export { RefusalError };

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
  /** S5: the harness runs a throwaway satisfactory-dash stack and writes a one-time code to the mod's config. */
  backend?: boolean;
  /** S3, S6, S7: the staged save is uploaded and loaded (satis-runner only), and auto-pause is put back as it was. */
  world?: boolean;
  /** S3: auto-pause off once the world is loaded, so the factory runs. */
  pauseOff?: boolean;
  /** S3: each batch of the mod's power lines is paired with FRM's getPower. */
  power?: boolean;
  /** S6: status lines are paired with QueryServerState and HealthCheck, before the world loads and after. */
  status?: boolean;
  /** S6: a first boot clears the server's auto-load save, so the measured boot starts with no game; it is put back. */
  emptyBoot?: boolean;
  /** S7: each auto-pause step the mod logs is paired with GetServerOptions. */
  autoPauseSteps?: boolean;
}

const MIN = 60_000;
/** Partial while a spike's probes are still being built (S3, S5). */
export const SPIKES: Partial<Record<SpikeId, SpikeSpec>> = {
  S0: { claim: S0.claim, evaluate: S0.evaluate, echo: false, ticks: false, doneLine: /satis-mod loaded/ },
  S1: { claim: S1.claim, evaluate: S1.evaluate, echo: true, ticks: false, doneLine: /satis-mod s1 done/ },
  S1b: { claim: S1b.claim, evaluate: S1b.evaluate, echo: false, ticks: false, doneLine: /satis-mod s1b bad / },
  S2: { claim: S2.claim, evaluate: S2.evaluate, echo: true, ticks: true, doneLine: /satis-mod loaded/, finalLine: /satis-mod s2 done/ },
  S3: { claim: S3.claim, evaluate: S3.evaluate, echo: false, ticks: false, world: true, pauseOff: true, power: true, doneLine: /satis-mod s3 circuit=/ },
  S4: { claim: S4.claim, evaluate: S4.evaluate, echo: false, ticks: false, credential: true, doneLine: /satis-mod s4 (read|error)/ },
  S5: { claim: S5.claim, evaluate: S5.evaluate, echo: false, ticks: false, backend: true, doneLine: /satis-mod s5 (enroll=|error)/ },
  S6: { claim: S6.claim, evaluate: S6.evaluate, echo: false, ticks: false, world: true, status: true, emptyBoot: true, doneLine: /satis-mod s6 / },
  S7: { claim: S7.claim, evaluate: S7.evaluate, echo: false, ticks: false, world: true, autoPauseSteps: true, doneLine: /satis-mod s7 (step=read|error)/ },
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
  /** S5 only: present when the runner config has `s5` (local runs). */
  startBackend?(): Promise<Backend>;
  randomCode?(): string;
  /** S3, S6, S7: uploads and loads the staged save; and lists other servers' settings in the saves folder it would use. */
  loadWorld?(): Promise<void>;
  savesConflict?(): string[];
  readAutoPause?(): Promise<boolean>;
  setAutoPause?(enabled: boolean): Promise<void>;
  collectPower?(offset: number): Promise<NonNullable<Evidence["power"]>>;
  /** S6: pairs the s6 lines from `from` on; `next` is where the following phase starts reading. */
  collectStatus?(from: number): Promise<{ pairs: StatusPair[]; next: number }>;
  /** S7: pairs each s7 step line from `from` on with GetServerOptions, until the mod logs done. */
  collectAutoPause?(from: number): Promise<AutoPauseStep[]>;
  /** S6: the session the server loads on startup ("" for none), and setting it. */
  readAutoLoad?(): Promise<string>;
  setAutoLoad?(name: string): Promise<void>;
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
  if (spec.power && (!deps.collectPower || !deps.loadWorld)) throw new RefusalError("S3 needs frmBaseUrl and s3World in the runner config");
  if (spec.world) {
    if (!deps.loadWorld || !deps.readAutoPause || !deps.setAutoPause || !deps.savesConflict) {
      throw new RefusalError(`${spike} needs s3World in the runner config`);
    }
    if (spec.status && !deps.collectStatus) throw new RefusalError("S6 needs the status collector");
    if (spec.emptyBoot && (!deps.readAutoLoad || !deps.setAutoLoad)) throw new RefusalError("S6 needs the auto-load save deps");
    if (spec.autoPauseSteps && !deps.collectAutoPause) throw new RefusalError("S7 needs the auto-pause collector");
    const shared = deps.savesConflict();
    if (shared.length) {
      throw new RefusalError(`another server keeps its settings in this account's saves folder (${shared.join(", ")}): the loaded save's autosaves would join its rotation, so ${spike} runs only as satis-runner`);
    }
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
  let backend: Backend | undefined;
  let code: string | undefined;
  if (spec.backend) {
    if (!deps.startBackend || !deps.randomCode || !deps.writeModConfig || !deps.removeModConfig) throw new RefusalError("S5 needs the backend deps (local runs only)");
    backend = await deps.startBackend();
    code = deps.randomCode();
    try {
      await backend.seedCode("s5", code);
    } catch (e) {
      await backend.down();
      throw new RefusalError(`seeding the throwaway database failed: ${message(e)}`);
    }
    deps.writeModConfig(modName(spike), { code, backendUrl: backend.url });
  }
  // S5's evidence keeps its shape: the revoked flag is for the core runs only.
  let backendState: Omit<Awaited<ReturnType<Backend["state"]>>, "revoked"> | undefined;

  const runReasons: string[] = [];
  let baseline: TickSeries | undefined;
  let withMod: TickSeries | undefined;
  let baselineLog = "";
  let logText = "";
  let echo: Echo | undefined;
  let power: Evidence["power"];
  const statusPairs: StatusPair[] = [];
  let autoPause: AutoPauseStep[] | undefined;
  // World spikes: GetServerOptions' value before the mod acted, restored at the end and given to S7's verdict.
  let autoPauseBefore: boolean | undefined;
  // S6: the session the server auto-loaded before the run, put back at the end.
  let autoLoadBefore: string | undefined;

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

  // The config file is removed, and the S5 stack read and torn down, whatever happens after they were set up
  // (Review Focus 1 and 2).
  try {
    try {
      deps.install(pluginDirFor(deps.config, spike), modName(spike));
    } catch (e) {
      throw new RefusalError(message(e));
    }

    try {
      if (spec.emptyBoot) {
        // The test server auto-loads its last save, so without this every boot already has a game (S6 run 1). The mod is
        // installed for this boot too; its lines go with the log, which the measured boot resets.
        const pre = await startFresh();
        try {
          await whileRunning(pre, deps.waitHealthy(), "waiting for health before clearing the auto-load save");
          autoLoadBefore = await whileRunning(pre, deps.readAutoLoad!(), "reading the auto-load save");
          await whileRunning(pre, deps.setAutoLoad!(""), "clearing the auto-load save");
        } catch (e) {
          runReasons.push(message(e));
        } finally {
          await deps.stop(pre);
        }
      }
      if (spec.echo) echo = await deps.startEcho();
      const offset = deps.resetLog();
      const proc = await startFresh();
      try {
        await whileRunning(proc, deps.waitHealthy(), "waiting for health");
        let statusFrom = offset;
        if (spec.status) {
          const entry = await whileRunning(proc, deps.collectStatus!(offset), "collecting s6 readings without a world");
          statusPairs.push(...entry.pairs);
          statusFrom = entry.next;
        }
        if (spec.world) {
          // The server boots into its empty entry map; the save is uploaded and loaded, and the API is down meanwhile.
          await whileRunning(proc, deps.loadWorld!(), "loading the staged world");
          await whileRunning(proc, deps.waitHealthy(), "waiting for health after the load");
          autoPauseBefore = await whileRunning(proc, deps.readAutoPause!(), "reading auto-pause");
          if (spec.pauseOff) await whileRunning(proc, deps.setAutoPause!(false), "turning auto-pause off");
        }
        await whileRunning(proc, deps.waitForLine(spec.doneLine, offset, DONE_TIMEOUT_MS), `waiting for ${spec.doneLine}`);
        if (spec.power) power = await whileRunning(proc, deps.collectPower!(offset), "collecting s3 readings");
        if (spec.status) {
          const withWorld = await whileRunning(proc, deps.collectStatus!(statusFrom), "collecting s6 readings with the world");
          statusPairs.push(...withWorld.pairs);
        }
        if (spec.autoPauseSteps) autoPause = await whileRunning(proc, deps.collectAutoPause!(offset), "collecting s7 steps");
        if (spec.ticks) withMod = await whileRunning(proc, deps.sampleTicks(), "sampling with the mod");
        if (spec.finalLine) {
          await whileRunning(proc, deps.waitForLine(spec.finalLine, offset, FINAL_TIMEOUT_MS), `waiting for ${spec.finalLine}`);
        }
      } catch (e) {
        runReasons.push(message(e));
      } finally {
        // The option persists on the test server, so later runs (S2's baselines) must find it as it was (final review, finding 4).
        if (autoPauseBefore !== undefined) {
          try {
            await deps.setAutoPause!(autoPauseBefore);
          } catch (e) {
            runReasons.push(`restoring auto-pause: ${message(e)}`);
          }
        }
        if (autoLoadBefore !== undefined) {
          try {
            await deps.setAutoLoad!(autoLoadBefore);
          } catch (e) {
            runReasons.push(`restoring the auto-load save: ${message(e)}`);
          }
        }
        await deps.stop(proc);
        logText = deps.readLogSince(offset);
      }
    } finally {
      await echo?.close();
    }
  } finally {
    if (spec.credential || spec.backend) deps.removeModConfig!(modName(spike));
    if (backend) {
      try {
        const { revoked: _revoked, ...s5State } = await backend.state("s5");
        backendState = s5State;
      } catch (e) {
        runReasons.push(`reading the throwaway database: ${message(e)}`);
      } finally {
        await backend.down();
      }
    }
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
    ...(backendState && code ? { backend: { ...backendState, codeInLog: logText.includes(code), bearerInLog: /Bearer/.test(logText) } } : {}),
    ...(power ? { power } : {}),
    ...(spec.status ? { status: { pairs: statusPairs } } : {}),
    ...(autoPause ? { autoPause: { before: autoPauseBefore ?? null, steps: autoPause } } : {}),
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
  // S3: every mod reading with the FRM figures it was compared against (circuit IDs and MW only).
  if (spec.power) captures["power.json"] = power ?? null;
  // S6: every native reading with the API's (the session as a hash only); S7: every step with GetServerOptions' value.
  if (spec.status) captures["status.json"] = statusPairs;
  if (spec.autoPauseSteps) captures["autopause.json"] = autoPause ?? null;
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
    checkApi: () => checkApi(scanSources(repoRoot), readFileSync(join(repoRoot, "api-sources.md"), "utf8")),
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
    ...(config.s5 ? {
      startBackend: () => startBackend({
        checkout: config.s5!.checkout,
        relayFile: join(repoRoot, "harness", "compose", "s5-relay.yaml"),
        port: config.s5!.port,
        exec: realExec,
      }),
      randomCode,
    } : {}),
    readAutoPause: () => readAutoPause(api),
    setAutoPause: (on) => setAutoPause(api, on),
    savesConflict: () =>
      // A service account may not have LOCALAPPDATA set; its profile's AppData\Local is the same folder.
      otherServerSettings(
        join(process.env.LOCALAPPDATA ?? join(homedir(), "AppData", "Local"), "FactoryGame", "Saved", "SaveGames"),
        Number(new URL(config.apiBaseUrl).port),
      ),
    ...(config.s3World ? {
      loadWorld: () => loadWorld(api, `w-${Date.now()}`, readFileSync(config.s3World!)),
      readAutoLoad: () => readAutoLoad(api),
      setAutoLoad: (name: string) => setAutoLoad(api, name),
      collectStatus: async (from: number) => {
        const reader = lineReader(logPath, from, "satis-mod s6 ");
        const pairs = await collectStatusPairs({
          readNew: reader.readNew,
          api: () => readApiStatus(api),
          sleep,
          now: Date.now,
          durationMs: 200_000, // 15 readings 10 s apart, plus margin
          intervalMs: 500,
          want: 15,
          skipBacklog: true,
        });
        return { pairs, next: reader.offset() };
      },
      collectAutoPause: (from: number) => {
        const reader = lineReader(logPath, from, "satis-mod s7 ");
        return collectAutoPauseSteps({ readNew: reader.readNew, api: () => readAutoPause(api), sleep, now: Date.now, durationMs: 180_000, intervalMs: 500 });
      },
    } : {}),
    ...(config.frmBaseUrl ? {
      collectPower: (offset: number) => {
        const reader = lineReader(logPath, offset, "satis-mod s3 circuit=");
        return collectPowerPairs({
          // Only the lines added since the last poll: one batch is one round of the mod's readings.
          readNew: reader.readNew,
          frm: async () => parseGetPower(await (await fetch(new URL("/getPower", config.frmBaseUrl), { signal: AbortSignal.timeout(2000) })).json()),
          sleep,
          now: Date.now,
          durationMs: 150_000, // the mod's 12 readings: 60 s, then every 10 s, plus margin
          intervalMs: 500,
        });
      },
    } : {}),
    now: () => new Date(),
    env: {
      commit: process.env.GITHUB_SHA ?? "local",
      runUrl: process.env.GITHUB_RUN_ID
        ? `${process.env.GITHUB_SERVER_URL}/${process.env.GITHUB_REPOSITORY}/actions/runs/${process.env.GITHUB_RUN_ID}`
        : null,
    },
  };
}
