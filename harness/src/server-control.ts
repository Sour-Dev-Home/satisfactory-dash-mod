import { execFile, spawn } from "node:child_process";
import { cpSync, existsSync, mkdirSync, readdirSync, readFileSync, rmSync } from "node:fs";
import { basename, join } from "node:path";
import type { RunnerConfig } from "./config";
import type { ServerApi } from "./server-api";

/** dedicated-server-api.md ("Authorization"): lifts the token requirement for loopback callers only. */
export const INSECURE_LOCAL_ARG = "-ini:Engine:[SystemSettings]:FG.DedicatedServer.AllowInsecureLocalAccess=1";

/** sml-manual-install.md: GameFeature true -> Mods/GameFeatures/<name>; false or absent -> Mods/<name>. */
export function modTarget(serverDir: string, name: string, gameFeature: boolean): string {
  const mods = join(serverDir, "FactoryGame", "Mods");
  return gameFeature ? join(mods, "GameFeatures", name) : join(mods, name);
}

export function removeMod(name: string, serverDir: string): void {
  for (const gameFeature of [false, true]) rmSync(modTarget(serverDir, name, gameFeature), { recursive: true, force: true });
}

/** Removes every SatisMod* plugin, including misnamed ones from earlier runs, so no spike loads into another's run. */
export function removeSpikeMods(serverDir: string): void {
  const mods = join(serverDir, "FactoryGame", "Mods");
  for (const dir of [mods, join(mods, "GameFeatures")]) {
    if (!existsSync(dir)) continue;
    for (const entry of readdirSync(dir)) {
      if (entry.startsWith("SatisMod")) rmSync(join(dir, entry), { recursive: true, force: true });
    }
  }
}

/** Copies a built plugin into the test server, replacing any earlier copy. Returns the plugin's name. */
export function installMod(pluginDir: string, serverDir: string, expectedName?: string): string {
  const uplugins = readdirSync(pluginDir).filter((f) => f.endsWith(".uplugin"));
  if (uplugins.length !== 1) throw new Error(`expected exactly one .uplugin in ${pluginDir}, found ${uplugins.length}`);
  const file = uplugins[0]!;
  const name = basename(file, ".uplugin");
  if (expectedName !== undefined && name !== expectedName) {
    throw new Error(`the built plugin is ${name}, expected ${expectedName}`);
  }
  const meta = JSON.parse(readFileSync(join(pluginDir, file), "utf8").replace(/^﻿/, "")) as { GameFeature?: unknown };
  removeMod(name, serverDir);
  const target = modTarget(serverDir, name, meta.GameFeature === true);
  mkdirSync(target, { recursive: true });
  cpSync(pluginDir, target, { recursive: true });
  return name;
}

export interface ServerProcess { pid: number; exited: Promise<number | null> }
export type Spawner = (exe: string, args: string[], cwd: string) => ServerProcess;
export type Killer = (pid: number) => Promise<void>;

export const spawnServer: Spawner = (exe, args, cwd) => {
  const child = spawn(exe, args, { cwd, stdio: "ignore", windowsHide: true });
  const exited = new Promise<number | null>((resolve) => {
    child.once("exit", (code) => resolve(code));
    child.once("error", () => resolve(null));
  });
  if (child.pid === undefined) throw new Error(`could not start ${exe}`);
  return { pid: child.pid, exited };
};

/** Ends the process and its children by pid only, never by image name, so another server is never touched. */
export const killTree: Killer = (pid) =>
  new Promise((resolve) => execFile("taskkill", ["/PID", String(pid), "/T", "/F"], () => resolve()));

export function startServer(
  cfg: Pick<RunnerConfig, "serverDir" | "serverExe" | "serverArgs">,
  spawner: Spawner = spawnServer,
): ServerProcess {
  return spawner(join(cfg.serverDir, cfg.serverExe), ["-log", INSECURE_LOCAL_ARG, ...cfg.serverArgs], cfg.serverDir);
}

const realSleep = (ms: number) => new Promise<void>((r) => setTimeout(r, ms));

/** Whether anything answers the server API. HealthCheck needs no auth, so the owner's real server answers too. */
export async function serverAnswering(api: ServerApi): Promise<boolean> {
  try {
    await api.call("HealthCheck");
    return true;
  } catch {
    return false;
  }
}

/** Rejects as soon as the spawned process exits, so another server answering the same port can't stand in for it. */
export function whileRunning<T>(proc: ServerProcess, work: Promise<T>, what: string): Promise<T> {
  const exited = proc.exited.then((code): never => {
    throw new Error(`the test server exited (code ${code}) while ${what}`);
  });
  // The race observes the rejection; this keeps a later, expected exit (at stop) from being an unhandled rejection.
  exited.catch(() => {});
  return Promise.race([work, exited]);
}

/** True when the process has already exited. A timer tick lets an already-settled exit promise win the race. */
async function hasExited(proc: ServerProcess): Promise<boolean> {
  return Promise.race([proc.exited.then(() => true), new Promise<boolean>((r) => setTimeout(() => r(false), 0))]);
}

/** HealthCheck needs no authentication (dedicated-server-api.md); any 200 means the API is up. */
export async function waitHealthy(
  api: ServerApi,
  opts: { timeoutMs: number; pollMs?: number; sleep?: (ms: number) => Promise<void>; now?: () => number },
): Promise<unknown> {
  const sleep = opts.sleep ?? realSleep;
  const now = opts.now ?? Date.now;
  const deadline = now() + opts.timeoutMs;
  for (;;) {
    try {
      const { status, body } = await api.call("HealthCheck");
      if (status === 200) return body;
    } catch {
      // not listening yet
    }
    if (now() >= deadline) throw new Error(`server not healthy after ${opts.timeoutMs} ms`);
    await sleep(opts.pollMs ?? 2000);
  }
}

/**
 * Asks the server to shut down (the API's Shutdown function), then kills the spawned pid if it outlives graceMs.
 * Shutdown goes to a port, not a pid: it is sent only while the spawned process is alive, never to whatever else answers.
 */
export async function stopServer(
  proc: ServerProcess,
  api: ServerApi,
  opts: { graceMs: number; kill: Killer; sleep?: (ms: number) => Promise<void> },
): Promise<"exited" | "killed"> {
  if (await hasExited(proc)) return "exited";
  try {
    await api.call("Shutdown");
  } catch {
    // already down, or not up yet
  }
  const sleep = opts.sleep ?? realSleep;
  const outcome = await Promise.race([proc.exited.then(() => "exited" as const), sleep(opts.graceMs).then(() => "timeout" as const)]);
  if (outcome === "exited") return "exited";
  await opts.kill(proc.pid);
  return "killed";
}

export interface ProcessRow { ProcessId: number; ParentProcessId: number; Name: string; WorkingSetSize: number; PeakWorkingSetSize: number }
export interface ProcessMemory {
  /** The tree's working set now, summed. */
  totalWorkingSetBytes: number;
  /** Each process of the tree: FactoryServer.exe launches the game as a child process, which holds the memory. */
  processes: Array<{ name: string; workingSetBytes: number; peakWorkingSetBytes: number }>;
}

/** The spawned process and its descendants, from Win32_Process rows (PeakWorkingSetSize is in KB, WorkingSetSize in bytes). */
export function treeMemory(rows: ProcessRow[], rootPid: number): ProcessMemory | null {
  const tree = rows.filter((r) => r.ProcessId === rootPid);
  if (!tree.length) return null;
  for (let i = 0; i < tree.length; i++) {
    const pid = tree[i]!.ProcessId;
    tree.push(...rows.filter((r) => r.ParentProcessId === pid && r.ProcessId !== pid && !tree.includes(r)));
  }
  const processes = tree.map((r) => ({ name: r.Name, workingSetBytes: Number(r.WorkingSetSize), peakWorkingSetBytes: Number(r.PeakWorkingSetSize) * 1024 }));
  return { totalWorkingSetBytes: processes.reduce((t, p) => t + p.workingSetBytes, 0), processes };
}

/** Reads the tree's memory by pid (never by image name). Null when it can't be read; it is evidence, not a pass condition. */
export const readProcessMemory = (pid: number): Promise<ProcessMemory | null> =>
  new Promise((resolve) =>
    execFile(
      "pwsh",
      ["-NoProfile", "-NonInteractive", "-Command",
        "Get-CimInstance Win32_Process | Select-Object ProcessId,ParentProcessId,Name,WorkingSetSize,PeakWorkingSetSize | ConvertTo-Json -Compress"],
      { maxBuffer: 16 * 1024 * 1024, windowsHide: true },
      (err, stdout) => {
        if (err) return resolve(null);
        try {
          const rows = JSON.parse(stdout) as ProcessRow[] | ProcessRow;
          resolve(treeMemory(Array.isArray(rows) ? rows : [rows], pid));
        } catch {
          resolve(null);
        }
      },
    ));
