import { existsSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { RunnerConfigSchema } from "../src/config";
import { type CoreDeps, type CoreRunSpec, credentialPath, MOD, runCore } from "../src/core-run";
import { modConfigPath } from "../src/mod-config";
import { RefusalError } from "../src/run";
import type { ServerProcess } from "../src/server-control";

const SECRET = `${"s".repeat(42)}_`;
const CODE = "CODE-1234-ABCD";

let root = "";
afterEach(() => root && rmSync(root, { recursive: true, force: true }));

function fakeDeps(over: Partial<CoreDeps> = {}, log = () => "[x][1]LogSatisDash: Display: booted\n") {
  root = mkdtempSync(join(tmpdir(), "core-run-"));
  const serverDir = join(root, "server");
  const outDir = join(root, "out");
  mkdirSync(serverDir);
  const calls: string[] = [];
  const procs: ServerProcess[] = [];
  const stopped: ServerProcess[] = [];
  const deps: CoreDeps = {
    config: RunnerConfigSchema.parse({
      serverDir,
      apiBaseUrl: "https://127.0.0.1:17777",
      buildCommand: ["b"],
      builtPluginDir: "D:/b/{spike}",
      installed: { engine: "5.6.1", sml: "3.12.0" },
    }),
    toolchain: { engine: "5.6.1", sml: "3.12.0", source: "x" },
    outDir,
    checkApi: () => ({ missing: [], badRows: [] }),
    serverAnswering: async () => (calls.push("probe"), false),
    buildMod: async () => (calls.push("build"), "D:/b/SatisDashAgent"),
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
    readLogSince: () => log(),
    readAutoPause: async () => true,
    setAutoPause: async (on) => void calls.push(`auto-pause ${on}`),
    savesConflict: () => [],
    loadWorld: async () => void calls.push("load world"),
    sampleTicks: async () => ({ warmup: [], samples: [], errors: 0, median: 30 }),
    sleep: async () => undefined,
    now: () => new Date("2026-10-20T16:00:00.000Z"),
    env: { commit: "abc", runUrl: null },
    ...over,
  };
  return { deps, calls, procs, stopped, serverDir, outDir };
}

const spec = (over: Partial<CoreRunSpec> = {}): CoreRunSpec => ({
  requirements: ["S8"],
  backend: false,
  world: false,
  drive: async () => undefined,
  evaluate: () => ({ pass: true, reasons: ["fine"] }),
  ...over,
});

const writeCredential = (serverDir: string) => {
  const cred = credentialPath(serverDir);
  mkdirSync(dirname(cred), { recursive: true });
  writeFileSync(cred, JSON.stringify({ agentSecret: SECRET, serverId: "srv" }));
  writeFileSync(`${cred}.tmp`, "partial");
};

const bundleText = (dir: string) => readdirSync(dir).map((f) => readFileSync(join(dir, f), "utf8")).join("\n");

describe("runCore", () => {
  it("passes a clean run and writes its bundle", async () => {
    const { deps, calls } = fakeDeps();
    const { result, dir } = await runCore("http-probe", spec(), deps);
    expect(calls).toEqual(["probe", "build", "remove SatisMod*", "install D:/b/SatisDashAgent as SatisDashAgent", "reset log", "start", "healthy", "stop"]);
    expect(result).toMatchObject({ run: "http-probe", pass: true, reasons: ["fine"], modLines: ["[x][1]LogSatisDash: Display: booted"] });
    expect(dir).toMatch(/core-http-probe-2026-10-20T16-00-00-000Z$/);
    expect(JSON.parse(readFileSync(join(dir, "leaks.json"), "utf8"))).toEqual({ code: false, secret: null, bearer: false });
  });

  it("a drive that throws still stops the server, scrubs the config and credential, and restores auto-pause", async () => {
    const { deps, calls, procs, stopped, serverDir } = fakeDeps();
    const { result } = await runCore("http-probe", spec({
      drive: async (ctx) => {
        ctx.rememberAutoPause(false);
        ctx.rememberAutoPause(true); // only the first value counts
        ctx.writeConfig({ backendUrl: "http://127.0.0.1:1", enrollmentCode: CODE });
        writeCredential(serverDir);
        throw new Error("boom");
      },
    }), deps);
    expect(result.pass).toBe(false);
    expect(result.reasons[0]).toBe("boom");
    expect(stopped).toEqual(procs);
    expect(calls).toContain("auto-pause false");
    expect(calls.indexOf("auto-pause false")).toBeLessThan(calls.indexOf("stop"));
    const cred = credentialPath(serverDir);
    expect(existsSync(modConfigPath(serverDir, MOD))).toBe(false);
    expect(existsSync(cred)).toBe(false);
    expect(existsSync(`${cred}.tmp`)).toBe(false);
  });

  it("searches the log for the secret in credential.json and the code, and keeps only booleans", async () => {
    let serverDir = "";
    const made = fakeDeps({}, () => `LogSatisDash: Display: secret ${SECRET} code ${CODE}\n`);
    serverDir = made.serverDir;
    const { result, dir } = await runCore("http-probe", spec({
      drive: async (ctx) => {
        ctx.writeConfig({ backendUrl: "http://127.0.0.1:1", enrollmentCode: CODE });
        writeCredential(serverDir);
      },
    }), made.deps);
    expect(result.pass).toBe(false);
    expect(result.reasons).toContain("S8: the server log carries the agent secret");
    expect(result.reasons).toContain("S8: the server log carries an enrolment code");
    expect(JSON.parse(readFileSync(join(dir, "leaks.json"), "utf8"))).toEqual({ code: true, secret: true, bearer: false });
    const text = bundleText(dir);
    expect(text).not.toContain(SECRET);
    expect(text).not.toContain(CODE);
  });

  it("searches for a secret noted before a halt deleted the file", async () => {
    const made = fakeDeps({}, () => `LogSatisDash: Display: ${SECRET}\n`);
    const { result } = await runCore("http-probe", spec({
      drive: async (ctx) => {
        writeCredential(made.serverDir);
        ctx.noteSecret();
        expect(ctx.credentialExists()).toBe(true);
        rmSync(credentialPath(made.serverDir));
      },
    }), made.deps);
    expect(result.reasons).toContain("S8: the server log carries the agent secret");
  });

  it("fails a run whose log has an Error line from the mod, or the string Bearer anywhere", async () => {
    const errored = await runCore("http-probe", spec(), fakeDeps({}, () => "[x][1]LogSatisDash: Error: x\n").deps);
    expect(errored.result.pass).toBe(false);
    expect(errored.result.reasons).toContain("the mod logged at Error: [x][1]LogSatisDash: Error: x");
    const bearer = await runCore("http-probe", spec(), fakeDeps({}, () => "LogHttp: Authorization: Bearer xyz\n").deps);
    expect(bearer.result.pass).toBe(false);
    expect(bearer.result.reasons).toContain("S8: the server log carries the string Bearer");
  });

  it("a failing evaluate fails the run with its reasons", async () => {
    const { result } = await runCore("http-probe", spec({ evaluate: () => ({ pass: false, reasons: ["no"] }) }), fakeDeps().deps);
    expect(result).toMatchObject({ pass: false, reasons: ["no"] });
  });

  it("refuses a world run when another server shares the saves folder, and a backend run without the host", async () => {
    const shared = fakeDeps({ savesConflict: () => ["ServerSettings.7777.sav"] });
    await expect(runCore("command", spec({ world: true }), shared.deps)).rejects.toThrow(/another server keeps its settings.*ServerSettings\.7777\.sav/);
    expect(shared.calls).toEqual([]);
    const noHost = fakeDeps();
    await expect(runCore("enrol", spec({ backend: true }), noHost.deps)).rejects.toThrow(RefusalError);
    await expect(runCore("enrol", spec({ backend: true }), noHost.deps)).rejects.toThrow(/needs the backend host/);
    expect(noHost.calls).toEqual([]);
  });

  it("refuses when check-api fails or the build fails", async () => {
    const api = fakeDeps({ checkApi: () => ({ missing: [{ kind: "symbol", name: "X", path: "mod/a.cpp", line: 3 }], badRows: [] }) });
    await expect(runCore("http-probe", spec(), api.deps)).rejects.toThrow(/check-api failed: mod\/a\.cpp:3 X/);
    const build = fakeDeps({ buildMod: async () => Promise.reject(new Error("build-mod failed")) });
    await expect(runCore("http-probe", spec(), build.deps)).rejects.toThrow(RefusalError);
    expect(build.calls).toEqual(["probe"]);
  });
});
