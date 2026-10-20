import { mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { RunnerConfigSchema } from "../src/config";
import type { Echo } from "../src/echo";
import { sha1Prefix } from "../src/mod-config";
import { RefusalError, type RunDeps, runSpike } from "../src/run";
import type { ServerProcess } from "../src/server-control";
import type { TickSeries } from "../src/ticks";
import type { AutoPauseStep, StatusPair, StatusReading } from "../src/verdict";

let out = "";
afterEach(() => out && rmSync(out, { recursive: true, force: true }));

function fakeDeps(over: Partial<RunDeps> = {}, log = "LogTemp: satis-mod loaded\n") {
  out = mkdtempSync(join(tmpdir(), "run-"));
  const calls: string[] = [];
  const procs: ServerProcess[] = [];
  const stopped: ServerProcess[] = [];
  const echo: Echo = { url: "http://127.0.0.1:18080", port: 18080, requests: [], close: async () => void calls.push("echo.close") };
  const ticks: TickSeries = { warmup: [], samples: [{ at: "x", value: 30 }], errors: 0, median: 30 };
  const deps: RunDeps = {
    config: RunnerConfigSchema.parse({
      serverDir: "D:/t",
      apiBaseUrl: "https://127.0.0.1:17777",
      buildCommand: ["b"],
      builtPluginDir: "D:/b/{spike}",
      installed: { engine: "5.6.1", sml: "3.12.0" },
    }),
    toolchain: { engine: "5.6.1", sml: "3.12.0", source: "x" },
    outDir: out,
    checkApi: () => ({ missing: [], badRows: [] }),
    serverAnswering: async () => (calls.push("probe"), false),
    build: async (s) => void calls.push(`build ${s}`),
    removeSpikeMods: () => void calls.push("remove SatisMod*"),
    install: (dir, expected) => void calls.push(`install ${dir} as ${expected}`),
    start: () => {
      const p: ServerProcess = { pid: 100 + procs.length, exited: new Promise(() => {}) };
      procs.push(p);
      calls.push("start");
      return p;
    },
    stop: async (p) => void (stopped.push(p), calls.push("stop")),
    waitHealthy: async () => void calls.push("healthy"),
    resetLog: () => (calls.push("reset log"), 0),
    waitForLine: async (re) => void calls.push(`wait ${re.source}`),
    readLogSince: () => log,
    startEcho: async () => (calls.push("echo.start"), echo),
    sampleTicks: async () => (calls.push("ticks"), ticks),
    now: () => new Date("2026-10-19T16:00:00.000Z"),
    env: { commit: "abc", runUrl: null },
    ...over,
  };
  return { deps, calls, procs, stopped, echo };
}

describe("runSpike", () => {
  it("S0: builds, clears old mods, installs, starts, waits, stops, and writes a passing result", async () => {
    const { deps, calls } = fakeDeps();
    const { result, dir } = await runSpike("S0", deps);
    expect(calls).toEqual([
      "probe",
      "build S0",
      "remove SatisMod*",
      "install D:/b/S0 as SatisModS0",
      "reset log", "probe", "start", "healthy", "wait satis-mod loaded", "stop",
    ]);
    expect(result).toMatchObject({ spike: "S0", claim: "C1", pass: true, modLines: ["LogTemp: satis-mod loaded"] });
    expect(JSON.parse(readFileSync(join(dir, "result.json"), "utf8")).pass).toBe(true);
  });

  it("records a timeout as a failed run and still stops the process it started", async () => {
    const { deps, procs, stopped } = fakeDeps({ waitForLine: async () => Promise.reject(new Error("timed out after 300000 ms")) }, "");
    const { result } = await runSpike("S0", deps);
    expect(result.pass).toBe(false);
    expect(result.reasons.join(" ")).toMatch(/timed out/);
    expect(stopped).toEqual(procs);
  });

  it("refuses before building when the toolchain differs", async () => {
    const { deps, calls } = fakeDeps({ toolchain: { engine: "5.6.2", sml: "3.12.0", source: "x" } });
    await expect(runSpike("S0", deps)).rejects.toBeInstanceOf(RefusalError);
    expect(calls).toEqual([]);
  });

  it("refuses before building when check-api finds a use without a row", async () => {
    const { deps, calls } = fakeDeps({
      checkApi: () => ({ missing: [{ kind: "symbol", name: "FMade::Up", path: "spikes/S0/x.cpp", line: 3 }], badRows: [] }),
    });
    await expect(runSpike("S0", deps)).rejects.toThrow(/FMade::Up/);
    expect(calls).toEqual([]);
  });

  it("refuses when the built plugin has the wrong name, without starting a server", async () => {
    const { deps, calls } = fakeDeps({
      install: () => {
        throw new Error("the built plugin is SomethingElse, expected SatisModS0");
      },
    });
    const err = await runSpike("S0", deps).catch((e: unknown) => e);
    expect(err).toBeInstanceOf(RefusalError);
    expect((err as Error).message).toMatch(/SomethingElse/);
    expect(calls).not.toContain("start");
  });

  it("refuses before building when something already answers on the API port (the owner's real server)", async () => {
    const { deps, calls } = fakeDeps({ serverAnswering: async () => (calls.push("probe"), true) });
    await expect(runSpike("S0", deps)).rejects.toThrow(/already answers/);
    expect(calls).toEqual(["probe"]);
  });

  it("records a failed run when the test server exits while waiting for health, and still stops it", async () => {
    const { deps, stopped } = fakeDeps({
      start: () => ({ pid: 9, exited: Promise.resolve(1) }),
      waitHealthy: () => new Promise(() => {}),
    });
    const { result } = await runSpike("S0", deps);
    expect(result.pass).toBe(false);
    expect(result.reasons.join(" ")).toMatch(/exited \(code 1\)/);
    expect(stopped).toHaveLength(1);
  });

  it("S2: removes every spike mod before the baseline, then samples without and with the mod", async () => {
    const { deps, calls } = fakeDeps();
    await runSpike("S2", deps);
    expect(calls).toEqual([
      "probe",
      "build S2",
      "remove SatisMod*",
      "reset log", "probe", "start", "healthy", "ticks", "stop",
      "install D:/b/S2 as SatisModS2",
      "echo.start", "reset log", "probe", "start", "healthy", "wait satis-mod loaded", "ticks", "wait satis-mod s2 done", "stop",
      "echo.close",
    ]);
  });

  it("refuses, installing nothing, when the build fails (final review, finding 1)", async () => {
    const { deps, calls } = fakeDeps({ build: async () => Promise.reject(new Error("build failed: UAT exit 25\nCook failed.")) });
    const err = await runSpike("S0", deps).catch((e: unknown) => e);
    expect(err).toBeInstanceOf(RefusalError);
    expect((err as Error).message).toMatch(/Cook failed/);
    expect(calls.some((c) => c.startsWith("install") || c === "start")).toBe(false);
  });

  it("S2: keeps the baseline boot's server lines, which the with-mod boot's log reset would erase (finding 4)", async () => {
    const { deps } = fakeDeps({}, "[t]LogServer: Display: Server startup time elapsed\nLogInit: Net CL: 513885\nLogTemp: other\n");
    const { dir } = await runSpike("S2", deps);
    const lines = JSON.parse(readFileSync(join(dir, "baseline-log.json"), "utf8")) as string[];
    expect(lines).toEqual(["[t]LogServer: Display: Server startup time elapsed", "LogInit: Net CL: 513885"]);
  });

  // A boot phase (~30, then ~22) in the with-mod window raises its median and could hide a real drop: a false PASS.
  // In the baseline it can only raise the baseline, which makes the check stricter, so it doesn't fail the run.
  const series = (values: number[]): TickSeries => ({
    warmup: [],
    samples: values.map((value) => ({ at: "2026-10-19T16:00:00.000Z", value })),
    errors: 0,
    median: [...values].sort((a, b) => a - b)[Math.floor(values.length / 2)]!,
  });
  const shifted = series([30, 30, 30, 30, 22, 22, 22, 22]);
  const steady = series([22, 22, 22, 22, 22, 22, 22, 22]);

  it("S2: fails when the with-mod window isn't steady, so a boot phase can't hide a drop (finding 3)", async () => {
    const windows = [steady, shifted];
    const { deps } = fakeDeps({ sampleTicks: async () => windows.shift()! });
    const { result } = await runSpike("S2", deps);
    expect(result.pass).toBe(false);
    expect(result.reasons.join(" ")).toMatch(/with-mod window isn't steady: first half 30, second half 22/);
  });

  it("S2: an unsteady baseline doesn't fail the run on its own (it only makes the check stricter)", async () => {
    const windows = [shifted, steady];
    const { deps } = fakeDeps({ sampleTicks: async () => windows.shift()! });
    const { result } = await runSpike("S2", deps);
    expect(result.reasons.join(" ")).not.toMatch(/isn't steady/);
  });

  it("S1b: keeps the engine's HTTP and TLS lines, which show why the bad host failed (finding 2)", async () => {
    const log = [
      "LogInit:  - bVerifyPeer = true  - Libcurl will verify peer certificate",
      "LogSatisMod: Display: satis-mod s1b good status=200",
      "LogHttp: Warning: 0x1: request failed, libcurl error: 60 (SSL peer certificate or SSH remote key was not OK)",
      "LogSatisMod: Display: satis-mod s1b bad error=Other",
    ].join("\n");
    const { deps } = fakeDeps({}, log);
    const { dir } = await runSpike("S1b", deps);
    const lines = JSON.parse(readFileSync(join(dir, "http.json"), "utf8")) as string[];
    expect(lines).toEqual([log.split("\n")[0], log.split("\n")[2]]);
  });

  it("S4: writes a fresh credential before the boot, removes it after, and judges what the mod logged", async () => {
    const written: Record<string, string>[] = [];
    let removed = 0;
    const { deps, calls } = fakeDeps({
      writeModConfig: (mod, values) => void (calls.push(`cfg ${mod}`), written.push(values)),
      removeModConfig: () => void removed++,
      randomCredential: () => "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567",
    }, "LogSatisMod: Display: satis-mod s4 read length=32 sha1=" + sha1Prefix("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567") + "\n");
    const { result } = await runSpike("S4", deps);
    expect(written).toEqual([{ credential: "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567" }]);
    expect(calls.indexOf("cfg SatisModS4")).toBeLessThan(calls.indexOf("start"));
    expect(removed).toBe(1);
    expect(result.pass).toBe(true);
  });

  it("S4: removes the credential even when the server never becomes healthy (Review Focus 1)", async () => {
    let removed = 0;
    const { deps } = fakeDeps({
      writeModConfig: () => undefined,
      removeModConfig: () => void removed++,
      randomCredential: () => "X".repeat(32),
      start: () => ({ pid: 9, exited: Promise.resolve(1) }),
      waitHealthy: () => new Promise(() => {}),
    });
    const { result } = await runSpike("S4", deps);
    expect(result.pass).toBe(false);
    expect(removed).toBe(1);
  });

  it("S3: turns auto-pause off after health, pairs each batch of s3 lines with FRM, and judges them", async () => {
    const seen: string[] = [];
    const lines = Array.from({ length: 10 }, (_, i) => `[${i}]LogSatisMod: Display: satis-mod s3 circuit=0 production=1000 consumption=700 capacity=1200`);
    const pairs = lines.map((_, i) => ({
      at: `t${i}`, circuitId: 0,
      mod: { production: 1000, consumption: 700, capacity: 1200 },
      frm: { production: 1000, consumption: 690, capacity: 1200 },
    }));
    const { deps, calls } = fakeDeps({
      savesConflict: () => [],
      loadWorld: async () => void seen.push("load world"),
      waitHealthy: async () => void seen.push("healthy"),
      readAutoPause: async () => (seen.push("read autopause"), true),
      setAutoPause: async (on) => void seen.push(`autopause ${on}`),
      collectPower: async () => (seen.push("collect"), { pairs, frmCircuits: [0], modCircuits: [0] }),
      stop: async () => void (seen.push("stop"), calls.push("stop")),
    }, `LogSatisMod: Display: satis-mod loaded\n${lines.join("\n")}\n`);
    const { result, dir } = await runSpike("S3", deps);
    expect(seen).toEqual(["healthy", "load world", "healthy", "read autopause", "autopause false", "collect", "autopause true", "stop"]);
    expect(result.pass).toBe(true);
    // The bundle shows what was compared (run #11's had only the summary).
    expect(JSON.parse(readFileSync(join(dir, "power.json"), "utf8"))).toEqual({ pairs, frmCircuits: [0], modCircuits: [0] });
  });

  it("S3: refuses before building when another server keeps its settings in this account's saves folder", async () => {
    const { deps, calls } = fakeDeps({
      savesConflict: () => ["ServerSettings.7777.sav"],
      loadWorld: async () => undefined,
      readAutoPause: async () => true,
      setAutoPause: async () => undefined,
      collectPower: async () => ({ pairs: [], frmCircuits: [], modCircuits: [] }),
    });
    await expect(runSpike("S3", deps)).rejects.toThrow(/ServerSettings\.7777\.sav.*satis-runner/);
    expect(calls.some((c) => c.startsWith("build"))).toBe(false);
  });

  it("S3: refuses before building without FRM's URL or the world (final review, minor 7)", async () => {
    const { deps, calls } = fakeDeps({ savesConflict: () => [] });
    await expect(runSpike("S3", deps)).rejects.toThrow(/S3 needs frmBaseUrl and s3World/);
    expect(calls.some((c) => c.startsWith("build"))).toBe(false);
  });

  it("S3: restores auto-pause even when collecting fails (final review, finding 4)", async () => {
    const seen: string[] = [];
    const { deps } = fakeDeps({
      savesConflict: () => [],
      loadWorld: async () => undefined,
      readAutoPause: async () => false,
      setAutoPause: async (on) => void seen.push(`autopause ${on}`),
      collectPower: async () => { throw new Error("FRM gone"); },
    });
    const { result } = await runSpike("S3", deps);
    expect(seen).toEqual(["autopause false", "autopause false"]);
    expect(result.pass).toBe(false);
  });

  const reading = (running: boolean): StatusReading => ({ running, paused: false, players: 0, limit: 4, duration: running ? 100 : 0, tickRate: 30, health: "healthy", session: "da39a3ee5e6b" });
  const statusPairs = (running: boolean): StatusPair[] => Array.from({ length: 10 }, (_, i) => ({ at: `t${i}`, mod: reading(running), api: reading(running) }));

  const autoLoadDeps = (seen: string[], set: (name: string) => Promise<void> = async () => undefined) => ({
    readAutoLoad: async () => (seen.push("read autoload"), "w-1"),
    setAutoLoad: async (name: string) => (seen.push(`autoload "${name}"`), set(name)),
  });

  it("S6: boots once to clear the auto-load save, then pairs status before the world loads and after, and restores both settings", async () => {
    const seen: string[] = [];
    const { deps } = fakeDeps({
      savesConflict: () => [],
      start: () => (seen.push("start"), { pid: 1, exited: new Promise(() => {}) }),
      loadWorld: async () => void seen.push("load world"),
      waitHealthy: async () => void seen.push("healthy"),
      readAutoPause: async () => (seen.push("read autopause"), true),
      setAutoPause: async (on) => void seen.push(`autopause ${on}`),
      ...autoLoadDeps(seen),
      collectStatus: async (from) => (seen.push(`collect status ${from}`), from === 0 ? { pairs: statusPairs(false), next: 7 } : { pairs: statusPairs(true), next: 9 }),
      stop: async () => void seen.push("stop"),
    });
    const { result, dir } = await runSpike("S6", deps);
    expect(seen).toEqual([
      "start", "healthy", "read autoload", 'autoload ""', "stop",
      "start", "healthy", "collect status 0", "load world", "healthy", "read autopause", "collect status 7", "autopause true", 'autoload "w-1"', "stop",
    ]);
    expect(result.pass).toBe(true);
    expect(JSON.parse(readFileSync(join(dir, "status.json"), "utf8"))).toHaveLength(20);
  });

  it("S6: records a failure to clear the auto-load save, still stops that boot, and puts the setting back", async () => {
    const seen: string[] = [];
    const { deps } = fakeDeps({
      savesConflict: () => [],
      loadWorld: async () => undefined,
      readAutoPause: async () => true,
      setAutoPause: async () => undefined,
      ...autoLoadDeps(seen, async (name) => { if (name === "") throw new Error("SetAutoLoadSessionName returned 400"); }),
      collectStatus: async (from) => ({ pairs: statusPairs(from !== 0), next: 1 }),
      stop: async () => void seen.push("stop"),
    });
    const { result } = await runSpike("S6", deps);
    expect(result.pass).toBe(false);
    expect(result.reasons[0]).toMatch(/SetAutoLoadSessionName returned 400/);
    expect(seen).toEqual(["read autoload", 'autoload ""', "stop", 'autoload "w-1"', "stop"]);
  });

  it("S6: refuses before building without the auto-load deps", async () => {
    const { deps, calls } = fakeDeps({ savesConflict: () => [], loadWorld: async () => undefined, readAutoPause: async () => true, setAutoPause: async () => undefined, collectStatus: async () => ({ pairs: [], next: 0 }) });
    await expect(runSpike("S6", deps)).rejects.toThrow(/S6 needs the auto-load save deps/);
    expect(calls.some((c) => c.startsWith("build"))).toBe(false);
  });

  it("S6: refuses before building without the staged world", async () => {
    const { deps, calls } = fakeDeps({ savesConflict: () => [] });
    await expect(runSpike("S6", deps)).rejects.toThrow(/S6 needs s3World/);
    expect(calls.some((c) => c.startsWith("build"))).toBe(false);
  });

  it("S6: runs with s3World and no frmBaseUrl", async () => {
    const { deps } = fakeDeps({
      savesConflict: () => [],
      loadWorld: async () => undefined,
      readAutoPause: async () => true,
      setAutoPause: async () => undefined,
      ...autoLoadDeps([]),
      collectStatus: async (from) => ({ pairs: statusPairs(from !== 0), next: 1 }),
    });
    expect((await runSpike("S6", deps)).result.pass).toBe(true);
  });

  const steps = (orig: boolean): AutoPauseStep[] =>
    ([["read", orig, false], ["set", !orig, null], ["check", !orig, false], ["restore", orig, null], ["final", orig, false]] as const)
      .map(([step, mod, pending]) => ({ at: step, step, mod, pending, api: mod }));

  it("S7: loads the world, records auto-pause, collects the mod's steps, restores, and writes autopause.json", async () => {
    const seen: string[] = [];
    const { deps } = fakeDeps({
      savesConflict: () => [],
      loadWorld: async () => void seen.push("load world"),
      waitHealthy: async () => void seen.push("healthy"),
      readAutoPause: async () => (seen.push("read autopause"), true),
      setAutoPause: async (on) => void seen.push(`autopause ${on}`),
      collectAutoPause: async () => (seen.push("collect autopause"), steps(true)),
      stop: async () => void seen.push("stop"),
    });
    const { result, dir } = await runSpike("S7", deps);
    expect(seen).toEqual(["healthy", "load world", "healthy", "read autopause", "collect autopause", "autopause true", "stop"]);
    expect(result.pass).toBe(true);
    expect(JSON.parse(readFileSync(join(dir, "autopause.json"), "utf8"))).toEqual(steps(true));
  });

  it("S7: fails when the mod's first read isn't what the harness recorded before it acted (final review, finding 1)", async () => {
    const { deps } = fakeDeps({
      savesConflict: () => [],
      loadWorld: async () => undefined,
      readAutoPause: async () => false,
      setAutoPause: async () => undefined,
      collectAutoPause: async () => steps(true),
    });
    const { result } = await runSpike("S7", deps);
    expect(result.pass).toBe(false);
    expect(result.reasons).toContain("read: the mod read True, but the harness recorded False before the mod acted");
  });

  it("S7: restores auto-pause even when collecting fails", async () => {
    const seen: string[] = [];
    const { deps } = fakeDeps({
      savesConflict: () => [],
      loadWorld: async () => undefined,
      readAutoPause: async () => false,
      setAutoPause: async (on) => void seen.push(`autopause ${on}`),
      collectAutoPause: async () => { throw new Error("server gone"); },
    });
    const { result } = await runSpike("S7", deps);
    expect(seen).toEqual(["autopause false"]);
    expect(result.pass).toBe(false);
    expect(result.reasons[0]).toMatch(/server gone/);
  });

  const fakeBackend = (order: string[]) => async () => (order.push("up"), {
    url: "http://127.0.0.1:13101",
    seedCode: async () => void order.push("seed"),
    state: async () => ({ codeConsumed: true, agentVersion: "spike-s5", lastSeen: true }),
    down: async () => void order.push("down"),
  });

  it("S5: starts the backend, writes the code and URL into the mod's config, judges the database, and always tears down", async () => {
    const order: string[] = [];
    const written: Record<string, string>[] = [];
    const { deps } = fakeDeps({
      writeModConfig: (_m, v) => void (order.push("cfg"), written.push(v)),
      removeModConfig: () => void order.push("rm cfg"),
      startBackend: fakeBackend(order),
      randomCode: () => "AB3D-7XQ2",
    }, "LogSatisMod: Display: satis-mod s5 enroll=201 snapshot=200\n");
    const { result } = await runSpike("S5", deps);
    expect(order).toEqual(["up", "seed", "cfg", "rm cfg", "down"]);
    expect(written).toEqual([{ code: "AB3D-7XQ2", backendUrl: "http://127.0.0.1:13101" }]);
    expect(result.pass).toBe(true);
  });

  it("S5: tears the stack down even when the install refuses (Review Focus 2)", async () => {
    const order: string[] = [];
    const { deps } = fakeDeps({
      writeModConfig: () => void order.push("cfg"),
      removeModConfig: () => void order.push("rm cfg"),
      startBackend: fakeBackend(order),
      randomCode: () => "AB3D-7XQ2",
      install: () => { throw new Error("wrong plugin name"); },
    });
    await expect(runSpike("S5", deps)).rejects.toThrow(/wrong plugin name/);
    expect(order).toEqual(["up", "seed", "cfg", "rm cfg", "down"]);
  });

  it("stops the process it started, for every server start", async () => {
    const { deps, procs, stopped } = fakeDeps();
    await runSpike("S2", deps);
    expect(procs).toHaveLength(2);
    expect(stopped).toEqual(procs);
  });
});
