import { describe, expect, it } from "vitest";
import { RunnerConfigSchema, logPathOf } from "../src/config";
import { listenPortPlan } from "../src/listen-port";
import { credentialPath } from "../src/core-run";
import { modConfigPath } from "../src/mod-config";
import { INSECURE_LOCAL_ARG, modTarget, startServer, stopServer } from "../src/server-control";
import type { ServerApi } from "../src/server-api";
import {
  dockerKiller,
  dockerRunArgs,
  dockerSpawner,
  dockerStop,
  findServerPid,
  parseProcStatus,
  readContainerMemory,
  type Exec,
} from "../src/server-host-docker";

const cfg = RunnerConfigSchema.parse({
  serverDir: "D:\\satis-linux\\root",
  apiBaseUrl: "https://127.0.0.1:27777",
  serverArgs: ["-Port=27777", "-ReliablePort=28888"],
  buildCommand: ["build.cmd"],
  builtPluginDir: "D:/b/{spike}",
  installed: { engine: "5.6.1", sml: "3.12.0" },
  serverHost: "docker",
  docker: {
    container: "satis-linux-ds",
    image: "satis-linux-ds:local",
    volume: "satis-linux-server",
    apiRelay: "27778:27777",
    publish: ["127.0.0.1:27777:27778/tcp", "127.0.0.1:27777:27777/udp", "127.0.0.1:28888:28888/tcp"],
    forwardPorts: [27801, 27802, 27803],
  },
});

const ok = (stdout = "") => ({ code: 0, stdout, stderr: "" });

describe("dockerRunArgs", () => {
  const args = dockerRunArgs(cfg);
  it("runs detached under the configured name", () => {
    expect(args.slice(0, 4)).toEqual(["run", "-d", "--name", "satis-linux-ds"]);
  });
  it("mounts the volume and the three folders of serverDir", () => {
    expect(args).toContain("satis-linux-server:/srv/satis/server");
    for (const d of ["Saved", "Mods", "Configs"]) {
      expect(args).toContain(`D:\\satis-linux\\root\\FactoryGame\\${d}:/srv/satis/server/FactoryGame/${d}`);
    }
  });
  it("publishes only on 127.0.0.1", () => {
    const ps = args.flatMap((a, i) => (args[i - 1] === "-p" ? [a] : []));
    expect(ps).toHaveLength(3);
    for (const p of ps) expect(p.startsWith("127.0.0.1:")).toBe(true);
  });
  it("passes the relay and the forward ports as environment", () => {
    expect(args).toContain("API_RELAY=27778:27777");
    expect(args).toContain("FORWARD_PORTS=27801 27802 27803");
  });
  it("ends with the image, then the insecure-local argument, then serverArgs in order", () => {
    const i = args.indexOf("satis-linux-ds:local");
    expect(args.slice(i)).toEqual(["satis-linux-ds:local", INSECURE_LOCAL_ARG, "-Port=27777", "-ReliablePort=28888"]);
  });
  it("omits FORWARD_PORTS and API_RELAY when none are configured", () => {
    const bare = dockerRunArgs({ ...cfg, docker: { ...cfg.docker!, forwardPorts: [], apiRelay: undefined } });
    expect(bare.some((a) => a.startsWith("FORWARD_PORTS") || a.startsWith("API_RELAY"))).toBe(false);
  });
  it("refuses a publish that is not loopback", () => {
    expect(() => dockerRunArgs({ ...cfg, docker: { ...cfg.docker!, publish: ["27777:27777/udp"] } })).toThrow(/127\.0\.0\.1/);
  });
});

describe("dockerSpawner", () => {
  it("removes a stale container, runs, then exits with docker wait's code", async () => {
    const calls: string[][] = [];
    const exec: Exec = async (_c, a) => {
      calls.push(a);
      return a[0] === "wait" ? ok("0\n") : ok();
    };
    const proc = startServer(cfg, dockerSpawner(exec, cfg));
    expect(await proc.exited).toBe(0);
    expect(calls.map((a) => a[0])).toEqual(["rm", "run", "wait"]);
    expect(calls[0]).toEqual(["rm", "-f", "satis-linux-ds"]);
    expect(calls[1]).toEqual(dockerRunArgs(cfg));
    expect(calls[2]).toEqual(["wait", "satis-linux-ds"]);
  });
  it("exits null when docker run fails", async () => {
    const exec: Exec = async (_c, a) => (a[0] === "run" ? { code: 125, stdout: "", stderr: "boom" } : ok());
    expect(await startServer(cfg, dockerSpawner(exec, cfg)).exited).toBeNull();
  });
});

describe("stopping the docker host (review focus 3)", () => {
  it("sends Shutdown, then docker stop, then docker rm -f", async () => {
    const log: string[] = [];
    const api: ServerApi = { call: async (fn) => (log.push(`api ${fn}`), { status: 204, body: null }) };
    const exec: Exec = async (_c, a) => (log.push(`docker ${a.join(" ")}`), ok());
    let exit!: (n: number) => void;
    const exited = new Promise<number | null>((r) => (exit = r));
    const sleepers: Array<() => void> = [];
    const stopping = dockerStop({ pid: 0, exited }, api, exec, cfg, {
      graceMs: 60_000,
      sleep: () => new Promise<void>((r) => sleepers.push(r)),
    });
    await new Promise((r) => setTimeout(r, 5));
    expect(log).toEqual(["api Shutdown"]);
    exit(0);
    await stopping;
    expect(log).toEqual(["api Shutdown", "docker stop satis-linux-ds", "docker rm -f satis-linux-ds"]);
  });
  it("waits the grace period when the server does not exit, then still stops and removes", async () => {
    const log: string[] = [];
    const api: ServerApi = { call: async (fn) => (log.push(`api ${fn}`), { status: 204, body: null }) };
    const exec: Exec = async (_c, a) => (log.push(`docker ${a[0]}`), ok());
    await dockerStop({ pid: 0, exited: new Promise(() => {}) }, api, exec, cfg, { graceMs: 5, sleep: async () => {} });
    expect(log).toEqual(["api Shutdown", "docker stop", "docker rm"]);
  });
  it("dockerKiller is stop then rm -f", async () => {
    const log: string[] = [];
    await dockerKiller(async (_c, a) => (log.push(a.slice(0, 2).join(" ")), ok()), cfg)(0);
    expect(log).toEqual(["stop satis-linux-ds", "rm -f"]);
  });
  it("is compatible with stopServer's contract", async () => {
    const api: ServerApi = { call: async () => ({ status: 204, body: null }) };
    expect(await stopServer({ pid: 0, exited: Promise.resolve(0) }, api, { graceMs: 1, kill: dockerKiller(async () => ok(), cfg) })).toBe("exited");
  });
});

describe("parseProcStatus (review focus 4)", () => {
  it("reads VmRSS and VmHWM in kB", () => {
    expect(parseProcStatus("VmHWM:\t 812345 kB\nVmRSS:\t 700000 kB\n")).toEqual({ rssBytes: 716_800_000, peakBytes: 831_841_280 });
  });
  it("is null without them", () => {
    expect(parseProcStatus("Name:\tx\n")).toBeNull();
    expect(parseProcStatus("VmRSS:\t 1 kB\n")).toBeNull();
  });
});

describe("findServerPid and readContainerMemory", () => {
  const ps = [
    "    1 /bin/sh /srv/satis/server/FactoryServer.sh -log -unattended",
    "    7 socat TCP-LISTEN:27778,fork,reuseaddr TCP:127.0.0.1:27777",
    "   15 /srv/satis/server/Engine/Binaries/Linux/FactoryServer-Linux-Shipping FactoryGame -log -unattended",
    "   29 /srv/satis/server/Engine/Binaries/Linux/crashpad_handler --no-periodic-tasks",
  ].join("\n");
  it("picks the Shipping binary, not the shell script or crashpad", () => {
    expect(findServerPid(ps)).toBe(15);
    expect(findServerPid("    1 sh\n")).toBeNull();
  });
  it("reads the server process's status, never docker stats", async () => {
    const calls: string[][] = [];
    const exec: Exec = async (_c, a) => {
      calls.push(a);
      return a.includes("ps") ? ok(ps) : ok("Name:\tFactoryServer-L\nVmHWM:\t 200 kB\nVmRSS:\t 100 kB\n");
    };
    const mem = await readContainerMemory(exec, cfg);
    expect(mem).toEqual({
      totalWorkingSetBytes: 102_400,
      processes: [{ name: "FactoryServer-Linux-Shipping", workingSetBytes: 102_400, peakWorkingSetBytes: 204_800 }],
    });
    expect(calls.flat()).not.toContain("stats");
    expect(calls[1]).toEqual(["exec", "satis-linux-ds", "cat", "/proc/15/status"]);
  });
  it("is null when the container has no server or exec fails", async () => {
    expect(await readContainerMemory(async () => ok("    1 sh\n"), cfg)).toBeNull();
    expect(await readContainerMemory(async () => ({ code: 1, stdout: "", stderr: "" }), cfg)).toBeNull();
  });
});

describe("paths under the docker config (review focus 2)", () => {
  const norm = (p: string) => p.replaceAll("\\", "/");
  it("log, credential, mod config and mod folder end in the exact case-sensitive names", () => {
    expect(norm(logPathOf(cfg))).toMatch(/\/FactoryGame\/Saved\/Logs\/FactoryGame\.log$/);
    expect(norm(credentialPath(cfg.serverDir))).toMatch(/\/FactoryGame\/Saved\/SatisDash\/credential\.json$/);
    expect(norm(modConfigPath(cfg.serverDir, "SatisDashAgent"))).toMatch(/\/FactoryGame\/Configs\/SatisDashAgent\.cfg$/);
    expect(norm(modTarget(cfg.serverDir, "SatisDashAgent", false))).toMatch(/\/FactoryGame\/Mods\/SatisDashAgent$/);
  });
});

describe("listenPortPlan", () => {
  it("hands out the configured ports in order, wrapping, and 0 when there is no plan", () => {
    const plan = listenPortPlan([5, 6]);
    expect([plan(), plan(), plan()]).toEqual([5, 6, 5]);
    expect(listenPortPlan([])()).toBe(0);
  });
});
