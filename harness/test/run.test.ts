import { mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { RunnerConfigSchema } from "../src/config";
import type { Echo } from "../src/echo";
import { RefusalError, type RunDeps, runSpike } from "../src/run";
import type { ServerProcess } from "../src/server-control";
import type { TickSeries } from "../src/ticks";

let out = "";
afterEach(() => out && rmSync(out, { recursive: true, force: true }));

function fakeDeps(over: Partial<RunDeps> = {}, log = "LogTemp: satis-mod loaded\n") {
  out = mkdtempSync(join(tmpdir(), "run-"));
  const calls: string[] = [];
  const procs: ServerProcess[] = [];
  const stopped: ServerProcess[] = [];
  const echo: Echo = { url: "http://127.0.0.1:18080", port: 18080, requests: [], close: async () => void calls.push("echo.close") };
  const ticks: TickSeries = { samples: [{ at: "x", value: 30 }], errors: 0, median: 30 };
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
    build: async (s) => void calls.push(`build ${s}`),
    remove: (name) => void calls.push(`remove ${name}`),
    install: (dir) => (calls.push(`install ${dir}`), `SatisMod${dir.split("/").pop()}`),
    start: () => {
      const p: ServerProcess = { pid: 100 + procs.length, exited: new Promise(() => {}) };
      procs.push(p);
      calls.push("start");
      return p;
    },
    stop: async (p) => void (stopped.push(p), calls.push("stop")),
    waitHealthy: async () => void calls.push("healthy"),
    logOffset: () => (calls.push("offset"), 0),
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
      "build S0",
      "remove SatisModS0", "remove SatisModS1", "remove SatisModS1b", "remove SatisModS2",
      "install D:/b/S0",
      "offset", "start", "healthy", "wait satis-mod loaded", "stop",
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
    const { deps, calls } = fakeDeps({ install: () => "SomethingElse" });
    await expect(runSpike("S0", deps)).rejects.toThrow(/SomethingElse/);
    expect(calls).not.toContain("start");
  });

  it("S2: removes every spike mod before the baseline, then samples without and with the mod", async () => {
    const { deps, calls } = fakeDeps();
    await runSpike("S2", deps);
    expect(calls).toEqual([
      "build S2",
      "remove SatisModS0", "remove SatisModS1", "remove SatisModS1b", "remove SatisModS2",
      "start", "healthy", "ticks", "stop",
      "install D:/b/S2",
      "echo.start", "offset", "start", "healthy", "wait satis-mod loaded", "ticks", "wait satis-mod s2 done", "stop",
      "echo.close",
    ]);
  });

  it("stops the process it started, for every server start", async () => {
    const { deps, procs, stopped } = fakeDeps();
    await runSpike("S2", deps);
    expect(procs).toHaveLength(2);
    expect(stopped).toEqual(procs);
  });
});
