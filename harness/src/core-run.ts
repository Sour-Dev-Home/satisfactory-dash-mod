import { execFile } from "node:child_process";
import { existsSync, readFileSync, rmSync } from "node:fs";
import { basename, dirname, join, resolve } from "node:path";
import { z } from "zod";
import { LOOPBACK_HOSTS, type RunnerConfig } from "./config";

/** The discard port on loopback: nothing answers there. */
export const BOOT_BACKEND_URL = "http://127.0.0.1:9";
import { redact, writeBundle } from "./evidence";
import { versionLines } from "./log-watch";
import { modConfigPath, writeModConfig } from "./mod-config";
import { lastLines, RefusalError, type RunDeps, realDeps, type WorldKey } from "./run";
import { type BackendImages, contractPin, imageDescends, realExec } from "./backend";
import { connectBackendHost, type HostClient } from "./backend-host";
import { createServerApi } from "./server-api";
import { readApiStatus } from "./status";
import { setListenPlan } from "./listen-port";
import { sampleContainerOutbound } from "./outbound-docker";
import { dockerSpawner, dockerStop, readContainerMemory, realExec as dockerExec } from "./server-host-docker";
import { type ProcessMemory, readProcessMemory, removeMod, type ServerProcess, startServer, whileRunning } from "./server-control";
import { readTickRate, sampleTicks, TICK_INTERVAL_MS, type TickSeries } from "./ticks";
import { type OutboundSample, resolveHost, sampleOutbound } from "./outbound";
import { type Toolchain, toolchainMismatches } from "./toolchain";
import type { Verdict } from "./verdict";

export const CORE_RUN_IDS = ["http-probe", "enrol", "revoke", "command", "outage", "tick-cost", "manual", "tick-cost-native", "s3-census", "soak-native"] as const;
export type CoreRunId = (typeof CORE_RUN_IDS)[number];
export const MOD = "SatisDashAgent";

/** The runner-config key that names the staged save for each world key. */
const WORLD_CONFIG_KEY = { s3: "s3World", s2: "s2World", sprint3: "sprint3World" } as const;

/**
 * The keys a run may add to the mod's config. Every reader is native and unconditional (R7), so the harness no longer writes
 * per-reader sources or the retired bridge's port and token (the mod ignores them and logs a warning).
 */
export interface ModConfigExtras {
  /** The mod's test-run diagnostics switch (owner 2026-11-01): thread cycles per step and conveyor call timing. Off in the mod unless a run writes true. */
  stepDiagnostics?: boolean;
}

/** The credential the mod writes (spec "Shape"; FPaths::ProjectSavedDir() on a dedicated server is FactoryGame/Saved). */
export const credentialPath = (serverDir: string) => join(serverDir, "FactoryGame", "Saved", "SatisDash", "credential.json");

export interface CoreEvidence {
  /** Every LogSatisDash line of the boot. */
  lines: string[];
  errorLines: string[];
  /** S8: whether any value that must stay secret shows up anywhere in the server log. secret is null when no credential was written. */
  leaks: { code: boolean; secret: boolean | null; bearer: boolean };
  captures: Record<string, unknown>;
}

/** One memory.json entry: game and session as QueryServerState said when the sample was taken (null: no answer). */
export interface MemorySample { game: boolean | null; session: string | null; memory: ProcessMemory | null }

export interface CoreContext {
  deps: CoreDeps;
  proc: ServerProcess;
  offset: number;
  /** Writes SatisDashAgent.cfg and remembers the code as a value the log must never carry. */
  writeConfig(values: { backendUrl: string; enrollmentCode: string }): void;
  /**
   * Reads the secret from credential.json now, so it is searched for in the log even if a halt deletes the file later.
   * With a value (a token or code the drive made, e.g. the parity run's game API token), notes that value instead.
   */
  noteSecret(value?: string): void;
  /** Remembers an enrolment code the drive gave another agent (the parity run's Node agent) as one the log must not carry. */
  noteCode(code: string): void;
  credentialExists(): boolean;
  /** Polls check every second until it returns a value, or fails the run naming what. */
  waitFor<T>(what: string, check: () => Promise<T | undefined> | T | undefined, timeoutMs: number): Promise<T>;
  captures: Record<string, unknown>;
  /** Auto-pause as GetServerOptions said before the run touched it; put back at the end. */
  rememberAutoPause(value: boolean): void;
  /**
   * Records the server's memory now in memory.json (the owner's goal; evidence, not a pass condition), under the stage
   * plus whether a game was running, as the server's API says at that moment: "withMod" becomes withModNoGame,
   * withModWithGame or withModGameUnknown. A repeated label gets "#2", "#3".
   */
  sampleMemory(stage: string): Promise<void>;
  /** What the spec's measureBaseline returned in the boot without the mod; undefined when there was none or it failed. */
  baseline?: unknown;
}

/** The boot without the mod (CoreRunSpec.baseline): its process, for sampling by pid. */
export interface BaselineContext { deps: CoreDeps; proc: ServerProcess }

export interface CoreRunSpec {
  requirements: string[];
  /** Needs the backend host (ruling 10). */
  backend: boolean;
  /** Loads the staged save (satis-runner only, savesConflict). */
  world: boolean;
  /** Which staged save the world is: "s3" (s3World, the default), "s2" (s2World, the S2 native runs) or "sprint3" (sprint3World, the S3 native runs). */
  worldKey?: WorldKey;
  /**
   * Boots once without the mod first to clear the auto-load session, so the run's boot has no game (S6 run 1: the test
   * server auto-loads its last save). The session is put back at the end, whatever happens.
   */
  emptyBoot?: boolean;
  /** Keys added to every config the run writes. Recorded in mod-config.json. */
  modConfig?: ModConfigExtras;
  /**
   * Boots once without the mod before the mod's boot, as runSpike does for S2 (run.ts, spec.ticks): the staged world
   * loaded, auto-pause off (put back before that server stops), then measureBaseline. Its result is ctx.baseline. Needs
   * world and measureBaseline.
   */
  baseline?: boolean;
  measureBaseline?(b: BaselineContext): Promise<unknown>;
  /** Checks what the run needs before the (long) build; a throw refuses the run. */
  preflight?(deps: CoreDeps): void;
  drive(ctx: CoreContext): Promise<void>;
  evaluate(e: CoreEvidence): Verdict;
}

export type CoreDeps = Pick<RunDeps,
  "config" | "toolchain" | "outDir" | "checkApi" | "serverAnswering" | "install" | "removeSpikeMods" | "start" | "stop" |
  "waitHealthy" | "resetLog" | "readLogSince" | "loadWorld" | "savesConflict" | "readAutoPause" | "setAutoPause" | "sampleTicks" | "now" | "env" |
  "readAutoLoad" | "setAutoLoad"> & {
  /** Runs scripts/build-mod.ps1 and returns the unpacked plugin's folder. */
  buildMod(): Promise<string>;
  /** Uninstalls a mod from the test server's Mods folder. */
  removeMod(name: string): void;
  sleep(ms: number): Promise<void>;
  /** Present when the runner config names the backend host's file (ruling 10). */
  backendHost?: () => Promise<HostClient>;
  /** Whether an image's commit is this run's contract pin or a descendant (backend.ts imageDescends); present with s5's checkout. */
  imageDescends?: (pin: string, revision: string) => Promise<boolean | null | "no-git">;
  /** The spawned server's process tree's memory; null when it can't be read. */
  processMemory?: (pid: number) => Promise<ProcessMemory | null>;
  /** Whether a game is running and its session hash, from QueryServerState now; null when the API doesn't answer. */
  gameState?: () => Promise<{ running: boolean; session: string } | null>;
  /** sampleTicks (ticks.ts) over QueryServerState's tick rate every TICK_INTERVAL_MS, with the run's own window. */
  sampleTickWindow?: (opts: { warmupMs: number; durationMs: number; stopped?: () => boolean }) => Promise<TickSeries>;
  /** S12: the outbound endpoints of the process tree under pid now (outbound.ts); rejects when they can't be read. */
  sampleOutbound?: (pid: number) => Promise<OutboundSample>;
  /** S12 (owner, 2026-10-27): the addresses the system resolver gives a name now; rejects when the lookup fails. */
  resolveHost?: (name: string) => Promise<string[]>;
};

const ResultSchema = z.strictObject({
  run: z.enum(CORE_RUN_IDS),
  requirements: z.array(z.string().min(1)).min(1),
  pass: z.boolean(),
  reasons: z.array(z.string()).min(1),
  startedAt: z.iso.datetime(),
  endedAt: z.iso.datetime(),
  toolchain: z.strictObject({ required: z.strictObject({ engine: z.string(), sml: z.string() }), declared: z.strictObject({ engine: z.string(), sml: z.string() }) }),
  versionLines: z.array(z.string()),
  modLines: z.array(z.string()),
  harnessCommit: z.string().min(1),
  /** Where the server ran: memory figures differ (Windows working set vs the Linux server process's RSS). */
  serverHost: z.enum(["windows", "docker"]),
  runUrl: z.string().nullable(),
});
export type CoreResult = z.infer<typeof ResultSchema>;

export function writeCoreEvidence(dir: string, result: CoreResult, captures: Record<string, unknown>): string {
  const parsed = ResultSchema.parse(JSON.parse(redact(JSON.stringify(result))));
  return writeBundle(dir, `core-${parsed.run}`, parsed.startedAt, parsed, captures);
}

/** Lines of LogSatisDash: "[time][frame]LogSatisDash: Display: text". */
export const satisDashLines = (log: string) => log.split(/\r?\n/).filter((l) => /\bLogSatisDash: /.test(l));

/** The rules every core run is judged by, besides its own pass.ts (Global Constraints). */
export function commonReasons(e: CoreEvidence): string[] {
  const reasons: string[] = [];
  if (e.leaks.code) reasons.push("S8: the server log carries an enrolment code");
  if (e.leaks.secret) reasons.push("S8: the server log carries the agent secret");
  if (e.leaks.bearer) reasons.push("S8: the server log carries the string Bearer");
  if (e.errorLines.length) reasons.push(`the mod logged at Error: ${e.errorLines[0]}`);
  return reasons;
}

const message = (e: unknown) => (e instanceof Error ? e.message : String(e));

export async function runCore(id: CoreRunId, spec: CoreRunSpec, deps: CoreDeps): Promise<{ result: CoreResult; dir: string }> {
  const mismatches = toolchainMismatches(deps.toolchain, deps.config.installed);
  if (mismatches.length) throw new RefusalError(`toolchain mismatch: ${mismatches.join("; ")}`);
  const api = deps.checkApi();
  if (api.missing.length || api.badRows.length) throw new RefusalError(`check-api failed: ${[...api.badRows, ...api.missing.map((u) => `${u.path}:${u.line} ${u.name}`)].join(", ")}`);
  if (spec.backend && !deps.backendHost) throw new RefusalError(`${id} needs the backend host (backendHost in the runner config; docs/runner.md)`);
  if (spec.emptyBoot && (!deps.readAutoLoad || !deps.setAutoLoad)) throw new RefusalError(`${id} needs the auto-load save deps`);
  if (spec.baseline && (!spec.world || !spec.measureBaseline)) throw new RefusalError(`${id}: a baseline boot needs the world and a measureBaseline`);
  if (spec.world) {
    const key = spec.worldKey ?? "s3";
    if (!deps.loadWorld || !deps.readAutoPause || !deps.setAutoPause || !deps.savesConflict || !deps.config[WORLD_CONFIG_KEY[key]]) {
      throw new RefusalError(`${id} needs ${WORLD_CONFIG_KEY[key]} in the runner config`);
    }
    const shared = deps.savesConflict();
    if (shared.length) throw new RefusalError(`another server keeps its settings in this account's saves folder (${shared.join(", ")}): ${id} runs only as satis-runner`);
  }
  try {
    spec.preflight?.(deps);
  } catch (e) {
    throw new RefusalError(message(e));
  }
  if (await deps.serverAnswering()) throw new RefusalError(`something already answers at ${deps.config.apiBaseUrl}: stop the other server first`);
  if (spec.backend && deps.backendHost && deps.imageDescends) {
    // The host guarded its images against its own checkout's contract pin, which can be older than this run's. Run
    // 40123626401's backend was 58 commits older and refused every snapshot. So refuse here, before anything boots.
    const pin = contractPin();
    let images: BackendImages;
    try {
      images = await (await deps.backendHost()).images();
    } catch (e) {
      throw new RefusalError(`reading the backend's images: ${message(e)}`);
    }
    for (const [role, img] of [["APP_IMAGE", images.app], ["OPS_IMAGE", images.ops]] as const) {
      const descends = await deps.imageDescends(pin, img.revision);
      if (descends === false) {
        throw new RefusalError(`the backend's ${role} image ${img.image} was built from ${img.revision}, older than this run's contract pin ${pin}: docker pull it, restart the backend host, then run again`);
      }
      if (descends !== true) {
        throw new RefusalError(`can't tell whether the backend's ${role} image ${img.image} (${img.revision}) descends from this run's contract pin ${pin}: git fetch in the satisfactory-dash checkout, then run again`);
      }
    }
  }

  const startedAt = deps.now().toISOString();
  let pluginDir: string;
  try {
    pluginDir = await deps.buildMod();
  } catch (e) {
    throw new RefusalError(message(e));
  }
  deps.removeSpikeMods();
  const cfg = modConfigPath(deps.config.serverDir, MOD);
  const cred = credentialPath(deps.config.serverDir);
  const scrub = () => {
    rmSync(cfg, { force: true });
    rmSync(cred, { force: true });
    rmSync(`${cred}.tmp`, { force: true });
  };
  scrub();

  const codes: string[] = [];
  const secrets: string[] = [];
  const captures: Record<string, unknown> = {};
  if (spec.modConfig) captures["mod-config.json"] = { ...spec.modConfig };
  const reasons: string[] = [];
  let autoPauseBefore: boolean | undefined;
  let log = "";
  // The secret is read only to search the log for it; it is never written anywhere.
  const noteSecret = (value?: string) => {
    if (value !== undefined) {
      if (value && !secrets.includes(value)) secrets.push(value);
      return;
    }
    try {
      const s = (JSON.parse(readFileSync(cred, "utf8")) as { agentSecret?: unknown }).agentSecret;
      if (typeof s === "string" && s && !secrets.includes(s)) secrets.push(s);
    } catch {
      // no file, or not yet valid: nothing to note
    }
  };
  // Each sample says whether a game ran when it was taken, read from the API then, never assumed from the stage: the
  // pre-boot runs the auto-loaded save (review of 66e1f0d, Important 1), so its no-mod figure includes a game.
  const memory: Record<string, MemorySample> = {};
  const sampleMemory = async (proc: ServerProcess, stage: string) => {
    if (!deps.processMemory) return;
    const [mem, game] = await Promise.all([
      deps.processMemory(proc.pid).catch(() => null),
      deps.gameState ? deps.gameState().catch(() => null) : Promise.resolve(null),
    ]);
    const base = `${stage}${game === null ? "GameUnknown" : game.running ? "WithGame" : "NoGame"}`;
    let label = base;
    for (let n = 2; label in memory; n++) label = `${base}#${n}`;
    memory[label] = { game: game?.running ?? null, session: game?.session ?? null, memory: mem };
  };
  // The auto-load session the server had before the run (emptyBoot), and whether it is back.
  let autoLoadBefore: string | undefined;
  let autoLoadRestored = false;
  let baseline: unknown;
  const restoreAutoLoad = async () => {
    if (autoLoadBefore === undefined || autoLoadRestored) return;
    try {
      await deps.setAutoLoad!(autoLoadBefore);
      autoLoadRestored = true;
    } catch (e) {
      reasons.push(`restoring the auto-load save: ${message(e)}`);
    }
  };
  try {
    if (spec.backend) {
      // Which backend this run judged against (the image guard's revisions, checked when the host started it).
      try {
        // With this harness's own contract pin: a host started from an older checkout would have guarded an older one.
        captures["backend.json"] = { images: await (await deps.backendHost!()).images(), pin: contractPin() };
      } catch (e) {
        reasons.push(`reading the backend's images: ${message(e)}`);
      }
    }
    if (spec.emptyBoot) {
      // Before the mod is installed, so this boot also measures the server without it (the owner's memory goal).
      const pre = deps.start();
      try {
        await whileRunning(pre, deps.waitHealthy(), "waiting for health before clearing the auto-load save");
        autoLoadBefore = await whileRunning(pre, deps.readAutoLoad!(), "reading the auto-load save");
        await whileRunning(pre, deps.setAutoLoad!(""), "clearing the auto-load save");
        await sampleMemory(pre, "noMod");
      } catch (e) {
        reasons.push(message(e));
      } finally {
        await deps.stop(pre);
      }
      if (await deps.serverAnswering()) throw new Error(`something still answers at ${deps.config.apiBaseUrl} after the empty boot`);
    }
    if (spec.baseline) {
      // Before the mod is installed (S2's baseline, run.ts): same save, world loaded, auto-pause off.
      const pre = deps.start();
      let autoPause: boolean | undefined;
      try {
        await whileRunning(pre, deps.waitHealthy(), "waiting for health before the baseline");
        await whileRunning(pre, deps.loadWorld!(spec.worldKey), "loading the world for the baseline");
        await whileRunning(pre, deps.waitHealthy(), "waiting for health after loading the world for the baseline");
        autoPause = await whileRunning(pre, deps.readAutoPause!(), "reading auto-pause for the baseline");
        await whileRunning(pre, deps.setAutoPause!(false), "turning auto-pause off for the baseline");
        baseline = await whileRunning(pre, spec.measureBaseline!({ deps, proc: pre }), "measuring the baseline");
      } catch (e) {
        reasons.push(`baseline: ${message(e)}`);
      } finally {
        if (autoPause !== undefined) {
          try {
            await deps.setAutoPause!(autoPause);
          } catch (e) {
            reasons.push(`restoring auto-pause after the baseline: ${message(e)}`);
          }
        }
        await deps.stop(pre);
      }
      if (await deps.serverAnswering()) throw new Error(`something still answers at ${deps.config.apiBaseUrl} after the baseline boot`);
    }
    deps.install(pluginDir, MOD);
    // Spec 2026-11-09 §4: a missing cfg means the production backend (Config.cpp:220-221), and since 0.3.0 the mod pairs
    // from boot. Every boot starts on a loopback address nothing answers, until the run points it at its recorder.
    // Config.cpp IsAllowedBackendUrl accepts plain http for the host 127.0.0.1 (any port 1-65535), so the cfg is valid:
    // the mod sees connection errors on :9 and backs off. (Were it refused, the cfg would be Broken and the mod idle.)
    writeModConfig(deps.config.serverDir, MOD, { backendUrl: BOOT_BACKEND_URL, enrollmentCode: "", ...(spec.modConfig ?? {}) });
    const offset = deps.resetLog();
    const proc = deps.start();
    let healthy = false;
    try {
      await deps.waitHealthy();
      healthy = true;
      const ctx: CoreContext = {
        deps, proc, offset, captures,
        writeConfig: (v) => {
          if (typeof v.backendUrl === "string" && !LOOPBACK_HOSTS.has(new URL(v.backendUrl).hostname)) {
            throw new Error(`backendUrl ${new URL(v.backendUrl).hostname} is not loopback: runs talk to 127.0.0.1 only`);
          }
          if (v.enrollmentCode) codes.push(v.enrollmentCode);
          writeModConfig(deps.config.serverDir, MOD, { ...v, ...(spec.modConfig ?? {}) });
        },
        noteSecret,
        noteCode: (code) => void (code && !codes.includes(code) && codes.push(code)),
        credentialExists: () => existsSync(cred),
        waitFor: async (what, check, timeoutMs) => {
          const end = Date.now() + timeoutMs;
          for (;;) {
            const v = await check();
            if (v !== undefined) return v;
            if (Date.now() > end) throw new Error(`timed out waiting for ${what}`);
            await deps.sleep(1000);
          }
        },
        rememberAutoPause: (v) => void (autoPauseBefore ??= v),
        sampleMemory: (stage) => sampleMemory(proc, stage),
        baseline,
      };
      await spec.drive(ctx);
    } catch (e) {
      reasons.push(message(e));
    } finally {
      if (autoPauseBefore !== undefined) {
        try {
          await deps.setAutoPause!(autoPauseBefore);
        } catch (e) {
          reasons.push(`restoring auto-pause: ${message(e)}`);
        }
      }
      // While this server is up, as S6 does; a server that never came up gets a restoring boot below.
      if (healthy) await restoreAutoLoad();
      await sampleMemory(proc, "end");
      await deps.stop(proc);
      log = deps.readLogSince(offset);
    }
  } finally {
    // The file goes with the config whatever happened.
    noteSecret();
    scrub();
    // The core mod never stays on the test server: a later spike run would boot with it loaded.
    deps.removeMod(MOD);
    if (autoLoadBefore !== undefined && !autoLoadRestored) {
      // A boot without the mod whose only job is to put the session back (the option needs a running server).
      try {
        if (await deps.serverAnswering()) throw new Error(`something answers at ${deps.config.apiBaseUrl}`);
        const restore = deps.start();
        try {
          await whileRunning(restore, deps.waitHealthy(), "waiting for health to restore the auto-load save");
          await restoreAutoLoad();
        } finally {
          await deps.stop(restore);
        }
      } catch (e) {
        reasons.push(`restoring the auto-load save: ${message(e)}`);
      }
    }
  }
  if (Object.keys(memory).length) captures["memory.json"] = memory;

  const leaks = { code: codes.some((c) => log.includes(c)), secret: secrets.length ? secrets.some((s) => log.includes(s)) : null, bearer: /Bearer/.test(log) };
  // A leaked value is reported as a boolean, never copied into the bundle through the lines that carry it.
  const shown = [...secrets.map((s) => [s, "<secret>"]), ...codes.map((c) => [c, "<code>"])].reduce((t, [v, as]) => t.replaceAll(v!, as!), log);
  const lines = satisDashLines(shown);
  const evidence: CoreEvidence = {
    lines,
    errorLines: lines.filter((l) => /LogSatisDash: Error:/.test(l)),
    leaks,
    captures,
  };
  // A judge that throws fails the run; the bundle is still written, so the failure comes with its evidence (rules 3, 4).
  let verdict: Verdict;
  try {
    verdict = spec.evaluate(evidence);
  } catch (e) {
    verdict = { pass: false, reasons: [`judging the evidence failed: ${message(e)}`] };
    // The drive's first readers.json has no log checks (evaluate would have replaced it with final verdicts), so it must not sit
    // in the bundle looking green beside a failing result.json: it becomes a failure marker.
    if ("readers.json" in captures) captures["readers.json"] = { failed: verdict.reasons[0] };
  }
  const allReasons = [...reasons, ...commonReasons(evidence), ...verdict.reasons];
  const result: CoreResult = {
    run: id,
    requirements: spec.requirements,
    pass: reasons.length === 0 && commonReasons(evidence).length === 0 && verdict.pass,
    reasons: allReasons,
    startedAt,
    endedAt: deps.now().toISOString(),
    toolchain: { required: { engine: deps.toolchain.engine, sml: deps.toolchain.sml }, declared: deps.config.installed },
    versionLines: versionLines(shown),
    modLines: lines.slice(0, 200),
    harnessCommit: deps.env.commit,
    serverHost: deps.config.serverHost,
    runUrl: deps.env.runUrl,
  };
  return { result, dir: writeCoreEvidence(deps.outDir, result, { ...captures, "leaks.json": evidence.leaks }) };
}

const sleep = (ms: number) => new Promise<void>((r) => setTimeout(r, ms));

/**
 * Where the mod is built: a sibling of the out dir (<out>-build/SatisDashAgent), never inside it. The out dir is the
 * public artifact, and a packaged plugin (its PDB holds the build machine's paths) must never be uploaded.
 */
export function coreBuildDir(outDir: string): string {
  const out = resolve(outDir);
  return join(dirname(out), `${basename(out)}-build`, MOD);
}

/** The spike runner's real dependencies that a core run uses, plus the mod's own build (scripts/build-mod.ps1). */
export function realCoreDeps(config: RunnerConfig, toolchain: Toolchain, outDir: string, repoRoot: string): CoreDeps {
  const d = realDeps(config, toolchain, outDir, repoRoot);
  const built = coreBuildDir(outDir);
  const docker = config.serverHost === "docker";
  if (docker) setListenPlan(config.docker?.forwardPorts ?? []);
  const hostDeps: Pick<CoreDeps, "start" | "stop" | "processMemory"> = docker
    ? {
        start: () => startServer(config, dockerSpawner(dockerExec, config)),
        stop: (proc) => dockerStop(proc, createServerApi(config.apiBaseUrl), dockerExec, config, { graceMs: 60_000 }),
        processMemory: () => readContainerMemory(dockerExec, config),
      }
    : { start: d.start, stop: d.stop, processMemory: readProcessMemory };
  return {
    config: d.config,
    toolchain: d.toolchain,
    outDir: d.outDir,
    checkApi: d.checkApi,
    serverAnswering: d.serverAnswering,
    install: d.install,
    removeSpikeMods: d.removeSpikeMods,
    removeMod: (name) => removeMod(name, config.serverDir),
    start: hostDeps.start,
    stop: hostDeps.stop,
    waitHealthy: d.waitHealthy,
    resetLog: d.resetLog,
    readLogSince: d.readLogSince,
    // The Linux server keeps its saves in its container's home (/srv/satis/.config/Epic/FactoryGame/Saved/SaveGames, its
    // log, 2026-11-07), outside the volume and the D: mounts, and each run starts a fresh container: no other server can
    // share that folder. The Windows check reads satis-runner's own saves folder, where the Windows test server's
    // settings live, so on the docker host it would refuse every world run.
    savesConflict: docker ? () => [] : d.savesConflict,
    readAutoPause: d.readAutoPause,
    setAutoPause: d.setAutoPause,
    sampleTicks: d.sampleTicks,
    now: d.now,
    env: d.env,
    ...(d.loadWorld ? { loadWorld: d.loadWorld } : {}),
    ...(d.readAutoLoad && d.setAutoLoad ? { readAutoLoad: d.readAutoLoad, setAutoLoad: d.setAutoLoad } : {}),
    ...(config.backendHost ? { backendHost: () => connectBackendHost(config.backendHost!.file) } : {}),
    ...(config.s5 ? { imageDescends: (pin: string, revision: string) => imageDescends({ checkout: config.s5!.checkout, exec: realExec }, pin, revision) } : {}),
    processMemory: hostDeps.processMemory,
    gameState: async () => {
      const s = await readApiStatus(createServerApi(config.apiBaseUrl));
      return { running: s.running, session: s.session };
    },
    sampleTickWindow: (opts) => {
      const api = createServerApi(config.apiBaseUrl);
      return sampleTicks(() => readTickRate(api), { ...opts, intervalMs: TICK_INTERVAL_MS, sleep, now: Date.now });
    },
    // S12 on the docker host reads the server's sockets inside its container (outbound-docker.ts); the pid is the
    // Windows tree's root, which the docker host doesn't have.
    sampleOutbound: docker ? () => sampleContainerOutbound(dockerExec, config) : (pid) => sampleOutbound(pid),
    resolveHost: (name) => resolveHost(name),
    sleep,
    buildMod: () =>
      new Promise((resolve, reject) => {
        const modTest = config.modTest;
        if (!modTest) return reject(new Error("modTest is not set in the runner config: core runs build the mod with build-mod.ps1 (docs/runner.md)"));
        const plan = coreModBuild(repoRoot, modTest, built, config.serverHost);
        execFile("pwsh", plan.args, { cwd: repoRoot, maxBuffer: 64 * 1024 * 1024 }, (err, stdout, stderr) =>
          err ? reject(new Error(`build-mod failed: ${err.message}\n${lastLines(`${stdout}\n${stderr}`, 40)}`)) : resolve(plan.pluginDir),
        );
      }),
  };
}

/**
 * The core run's build-mod.ps1 call and the plugin folder it installs. On the Windows host, the Windows server's plugin at
 * -Out's root (as always). On the docker host (the Linux test server), both server platforms, and the LinuxServer tree
 * (build-mod.ps1 -Platforms Win64+Linux; plan 2026-11-06-linux-server-build, Task 7).
 */
export function coreModBuild(
  repoRoot: string,
  modTest: { project: string; engine: string },
  built: string,
  serverHost: RunnerConfig["serverHost"],
): { args: string[]; pluginDir: string } {
  const args = ["-NoProfile", "-File", join(repoRoot, "scripts", "build-mod.ps1"), "-Project", modTest.project, "-Engine", modTest.engine, "-Out", built];
  if (serverHost !== "docker") return { args, pluginDir: built };
  return { args: [...args, "-Platforms", "Win64+Linux"], pluginDir: join(built, "LinuxServer") };
}
