import { execFile, spawn } from "node:child_process";
import { cpSync, mkdirSync, readdirSync, readFileSync, rmSync } from "node:fs";
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

/** Copies a built plugin into the test server, replacing any earlier copy. Returns the plugin's name. */
export function installMod(pluginDir: string, serverDir: string): string {
  const uplugins = readdirSync(pluginDir).filter((f) => f.endsWith(".uplugin"));
  if (uplugins.length !== 1) throw new Error(`expected exactly one .uplugin in ${pluginDir}, found ${uplugins.length}`);
  const file = uplugins[0]!;
  const name = basename(file, ".uplugin");
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

/** Asks the server to shut down (the API's Shutdown function), then kills the spawned pid if it outlives graceMs. */
export async function stopServer(
  proc: ServerProcess,
  api: ServerApi,
  opts: { graceMs: number; kill: Killer; sleep?: (ms: number) => Promise<void> },
): Promise<"exited" | "killed"> {
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
