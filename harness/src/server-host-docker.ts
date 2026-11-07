import { execFile } from "node:child_process";
import { join } from "node:path";
import type { RunnerConfig } from "./config";
import type { ServerApi } from "./server-api";
import { INSECURE_LOCAL_ARG, type Killer, type ProcessMemory, type ServerProcess, type Spawner, stopServer } from "./server-control";

/**
 * The Linux test server container (docker/linux-server). Everything here goes through an injectable Exec, so tests never
 * start docker. Only the container named in the config is ever touched.
 */
export type Exec = (cmd: string, args: string[]) => Promise<{ code: number; stdout: string; stderr: string }>;

export const realExec: Exec = (cmd, args) =>
  new Promise((resolve) =>
    execFile(cmd, args, { maxBuffer: 16 * 1024 * 1024, windowsHide: true }, (err, stdout, stderr) =>
      resolve({ code: err ? (typeof err.code === "number" ? err.code : 1) : 0, stdout: String(stdout), stderr: String(stderr) }),
    ));

const ROOT = "/srv/satis/server";
const MOUNTED = ["Saved", "Mods", "Configs"] as const;

const dockerOf = (cfg: Pick<RunnerConfig, "docker">) => {
  if (!cfg.docker) throw new Error('serverHost "docker" needs a docker block in the runner config');
  return cfg.docker;
};

/**
 * Arguments of `docker run`: the named volume, the three bind mounts under serverDir/FactoryGame (the mod install, log,
 * credential and mod config paths then resolve unchanged), loopback-only publishes, the image's environment, and after the
 * image the server's arguments in the Windows host's order (INSECURE_LOCAL_ARG, then serverArgs; the entrypoint adds -log).
 */
export function dockerRunArgs(cfg: Pick<RunnerConfig, "docker" | "serverDir" | "serverArgs">): string[] {
  const d = dockerOf(cfg);
  for (const p of d.publish) if (!p.startsWith("127.0.0.1:")) throw new Error(`docker publish must bind 127.0.0.1: ${p}`);
  const args = ["run", "-d", "--name", d.container];
  if (d.apiRelay) args.push("-e", `API_RELAY=${d.apiRelay}`);
  if (d.forwardPorts.length) args.push("-e", `FORWARD_PORTS=${d.forwardPorts.join(" ")}`);
  args.push("-v", `${d.volume}:${ROOT}`);
  for (const dir of MOUNTED) args.push("-v", `${join(cfg.serverDir, "FactoryGame", dir)}:${ROOT}/FactoryGame/${dir}`);
  for (const p of d.publish) args.push("-p", p);
  args.push(d.image, INSECURE_LOCAL_ARG, ...cfg.serverArgs);
  return args;
}

/**
 * Starts the container. Spawner is synchronous, so the process has no real pid (0; the docker killer and memory reader
 * address the container by name) and `exited` does the asynchronous work: remove a stale container of the same name, run,
 * then resolve with `docker wait`'s exit code (null when run or wait fail).
 */
export const dockerSpawner = (exec: Exec, cfg: RunnerConfig): Spawner => () => {
  const name = dockerOf(cfg).container;
  const exited = (async () => {
    await exec("docker", ["rm", "-f", name]);
    const run = await exec("docker", dockerRunArgs(cfg));
    if (run.code !== 0) return null;
    const waited = await exec("docker", ["wait", name]);
    const code = Number.parseInt(waited.stdout.trim(), 10);
    return waited.code === 0 && Number.isFinite(code) ? code : null;
  })().catch(() => null);
  return { pid: 0, exited } satisfies ServerProcess;
};

/** docker stop, then docker rm -f. Meant for after the API Shutdown and its grace period (review focus 3). */
export const dockerKiller = (exec: Exec, cfg: RunnerConfig): Killer => async () => {
  const name = dockerOf(cfg).container;
  await exec("docker", ["stop", name]);
  await exec("docker", ["rm", "-f", name]);
};

/**
 * API Shutdown, the same grace wait as on Windows, then always docker stop and docker rm -f (a cleanly exited container
 * still has to be removed so the next run can use the name).
 */
export async function dockerStop(
  proc: ServerProcess,
  api: ServerApi,
  exec: Exec,
  cfg: RunnerConfig,
  opts: { graceMs: number; sleep?: (ms: number) => Promise<void> },
): Promise<void> {
  await stopServer(proc, api, { graceMs: opts.graceMs, kill: async () => {}, ...(opts.sleep ? { sleep: opts.sleep } : {}) });
  await dockerKiller(exec, cfg)(0);
}

const SERVER_BINARY = "FactoryServer-Linux-Shipping";

/** The game process in `ps -eo pid,args`: the one whose command is the Shipping binary (the script and crashpad are not). */
export function findServerPid(ps: string): number | null {
  for (const line of ps.split(/\r?\n/)) {
    const m = /^\s*(\d+)\s+(\S+)/.exec(line);
    if (m && m[2]!.split("/").pop() === SERVER_BINARY) return Number(m[1]);
  }
  return null;
}

/** VmRSS and VmHWM of /proc/<pid>/status (kB), in bytes. Null when either is missing. */
export function parseProcStatus(text: string): { rssBytes: number; peakBytes: number } | null {
  const kb = (key: string) => {
    const m = new RegExp(`^${key}:\\s+(\\d+)\\s*kB`, "m").exec(text);
    return m ? Number(m[1]) * 1024 : null;
  };
  const rssBytes = kb("VmRSS");
  const peakBytes = kb("VmHWM");
  return rssBytes === null || peakBytes === null ? null : { rssBytes, peakBytes };
}

/**
 * The server process's resident set, through docker exec (never docker stats, which counts page cache). Shaped like the
 * Windows reader's result so memory.json keeps one form. Null when it can't be read; it is evidence, not a pass condition.
 */
export const readContainerMemory = async (exec: Exec, cfg: RunnerConfig): Promise<ProcessMemory | null> => {
  try {
    const name = dockerOf(cfg).container;
    const ps = await exec("docker", ["exec", name, "ps", "-eo", "pid,args"]);
    if (ps.code !== 0) return null;
    const pid = findServerPid(ps.stdout);
    if (pid === null) return null;
    const status = await exec("docker", ["exec", name, "cat", `/proc/${pid}/status`]);
    const parsed = status.code === 0 ? parseProcStatus(status.stdout) : null;
    if (!parsed) return null;
    return {
      totalWorkingSetBytes: parsed.rssBytes,
      processes: [{ name: SERVER_BINARY, workingSetBytes: parsed.rssBytes, peakWorkingSetBytes: parsed.peakBytes }],
    };
  } catch {
    return null;
  }
};
