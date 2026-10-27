import { existsSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, isAbsolute, join, relative } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { contractPin } from "../src/backend";
import type { HostClient } from "../src/backend-host";
import { RunnerConfigSchema } from "../src/config";
import { type CoreDeps, type CoreRunSpec, coreBuildDir, credentialPath, MOD, runCore } from "../src/core-run";
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
      s3World: "D:/satis-test/saves/s3-world.sav",
    }),
    toolchain: { engine: "5.6.1", sml: "3.12.0", source: "x" },
    outDir,
    checkApi: () => ({ missing: [], badRows: [] }),
    serverAnswering: async () => (calls.push("probe"), false),
    buildMod: async () => (calls.push("build"), "D:/b/SatisDashAgent"),
    removeSpikeMods: () => void calls.push("remove SatisMod*"),
    install: (dir, expected) => void calls.push(`install ${dir} as ${expected}`),
    removeMod: (name) => void calls.push(`uninstall ${name}`),
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
    expect(calls).toEqual(["probe", "build", "remove SatisMod*", "install D:/b/SatisDashAgent as SatisDashAgent", "reset log", "start", "healthy", "stop", "uninstall SatisDashAgent"]);
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
    expect(calls.at(-1)).toBe("uninstall SatisDashAgent");
    expect(calls).toContain("auto-pause false");
    expect(calls.indexOf("auto-pause false")).toBeLessThan(calls.indexOf("stop"));
    const cred = credentialPath(serverDir);
    expect(existsSync(modConfigPath(serverDir, MOD))).toBe(false);
    expect(existsSync(cred)).toBe(false);
    expect(existsSync(`${cred}.tmp`)).toBe(false);
  });

  it("writes frmPort into the mod config from the runner config's frmBaseUrl, and none when it is unset", async () => {
    const written: (Record<string, unknown> | null)[] = [];
    for (const frmBaseUrl of ["http://127.0.0.1:18081", undefined]) {
      const { deps, serverDir } = fakeDeps();
      if (frmBaseUrl) deps.config.frmBaseUrl = frmBaseUrl;
      else delete (deps.config as { frmBaseUrl?: string }).frmBaseUrl;
      await runCore("http-probe", spec({
        drive: async (ctx) => {
          ctx.writeConfig({ backendUrl: "http://127.0.0.1:1", enrollmentCode: "" });
          written.push(JSON.parse(readFileSync(modConfigPath(serverDir, MOD), "utf8")));
        },
      }), deps);
    }
    expect(written[0]).toMatchObject({ backendUrl: "http://127.0.0.1:1", frmPort: 18081 });
    expect(written[1]).not.toHaveProperty("frmPort");
  });

  it("adds the spec's modConfig to every config it writes and records it in mod-config.json; none without one", async () => {
    const written: Record<string, unknown>[] = [];
    const dirs: string[] = [];
    for (const modConfig of [{ playersSource: "native" as const }, undefined]) {
      const { deps, serverDir } = fakeDeps();
      const { dir } = await runCore("http-probe", spec({
        modConfig,
        drive: async (ctx) => {
          ctx.writeConfig({ backendUrl: "http://127.0.0.1:1", enrollmentCode: "" });
          written.push(JSON.parse(readFileSync(modConfigPath(serverDir, MOD), "utf8")));
        },
      }), deps);
      dirs.push(dir);
      if (modConfig) expect(JSON.parse(readFileSync(join(dir, "mod-config.json"), "utf8"))).toEqual({ playersSource: "native" });
      else expect(existsSync(join(dir, "mod-config.json"))).toBe(false);
    }
    expect(written[0]).toMatchObject({ backendUrl: "http://127.0.0.1:1", playersSource: "native" });
    expect(written[1]).not.toHaveProperty("playersSource");
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

  it("searches for a value the drive notes itself (the parity run's game API token), and keeps it out of the bundle", async () => {
    const TOKEN = "eyJwbCI6IkFQSVRva2VuIn0.0123456789ABCDEF";
    const made = fakeDeps({}, () => `LogSatisDash: Display: ${TOKEN}\n`);
    const { result, dir } = await runCore("http-probe", spec({ drive: async (ctx) => ctx.noteSecret(TOKEN) }), made.deps);
    expect(result.reasons).toContain("S8: the server log carries the agent secret");
    expect(bundleText(dir)).not.toContain(TOKEN);
    const clean = await runCore("http-probe", spec({ drive: async (ctx) => ctx.noteSecret(TOKEN) }), fakeDeps().deps);
    expect(clean.result.pass).toBe(true);
  });

  it("searches for a code the drive notes itself (the Node agent's), reported as a code, not as the agent secret", async () => {
    const NODE_CODE = "QQQQ-2222-ZZZZ";
    const made = fakeDeps({}, () => `LogSatisDash: Display: ${NODE_CODE}\n`);
    const { result, dir } = await runCore("http-probe", spec({ drive: async (ctx) => ctx.noteCode(NODE_CODE) }), made.deps);
    expect(result.reasons).toContain("S8: the server log carries an enrolment code");
    expect(result.reasons).not.toContain("S8: the server log carries the agent secret");
    expect(bundleText(dir)).not.toContain(NODE_CODE);
  });

  it("records the images the backend host runs (backend.json) in every backend run, and fails when it can't read them", async () => {
    const images = {
      app: { image: "ghcr.io/sour-dev-home/satisfactory-dash-backend:main", revision: "0123456789abcdef0123456789abcdef01234567" },
      ops: { image: "ghcr.io/sour-dev-home/satisfactory-dash-ops:main", revision: "0123456789abcdef0123456789abcdef01234567" },
    };
    const host = (read: () => Promise<typeof images>): HostClient => ({
      relayUrl: "http://127.0.0.1:13101",
      seedCode: async () => undefined,
      revoke: async () => undefined,
      queueCommand: async () => "",
      command: async () => ({ status: "pending", resultCode: null }),
      state: async () => ({ codeConsumed: false, agentVersion: null, lastSeen: false, revoked: false }),
      stopRelay: async () => undefined,
      startRelay: async () => undefined,
      images: read,
    });
    const good = await runCore("enrol", spec({ backend: true }), fakeDeps({ backendHost: async () => host(async () => images) }).deps);
    expect(good.result.pass).toBe(true);
    expect(JSON.parse(readFileSync(join(good.dir, "backend.json"), "utf8"))).toEqual({ images, pin: contractPin() });
    const bad = await runCore("enrol", spec({ backend: true }), fakeDeps({ backendHost: async () => host(async () => { throw new Error("backend host: GET /images answered 404"); }) }).deps);
    expect(bad.result.pass).toBe(false);
    expect(bad.result.reasons).toContain("reading the backend's images: backend host: GET /images answered 404");
    const none = await runCore("http-probe", spec(), fakeDeps().deps);
    expect(existsSync(join(none.dir, "backend.json"))).toBe(false);
  });

  it("refuses a backend run, before building the mod, whose images predate this run's contract pin (run 40123626401)", async () => {
    const images = {
      app: { image: "ghcr.io/sour-dev-home/satisfactory-dash-backend:main", revision: "26e4c72ea9e28de3839fc67d42d8077c584b4a92" },
      ops: { image: "ghcr.io/sour-dev-home/satisfactory-dash-ops:main", revision: "26e4c72ea9e28de3839fc67d42d8077c584b4a92" },
    };
    const host = { relayUrl: "http://127.0.0.1:13101", images: async () => images } as unknown as HostClient;
    const asked: string[] = [];
    const run = (answer: boolean | null | "no-git") => {
      const f = fakeDeps({ backendHost: async () => host, imageDescends: async (pin, revision) => (asked.push(`${pin} ${revision}`), answer) });
      return { f, run: runCore("enrol", spec({ backend: true }), f.deps) };
    };
    const old = run(false);
    await expect(old.run).rejects.toThrow(`the backend's APP_IMAGE image ${images.app.image} was built from ${images.app.revision}, older than this run's contract pin ${contractPin()}: docker pull it, restart the backend host, then run again`);
    expect(asked[0]).toBe(`${contractPin()} ${images.app.revision}`);
    expect(old.f.calls).not.toContain("build");
    await expect(run(null).run).rejects.toThrow(/can't tell whether the backend's APP_IMAGE image .* git fetch in the satisfactory-dash checkout/);
    await expect(run("no-git").run).rejects.toThrow(/can't tell whether/);
    expect((await run(true).run).result.pass).toBe(true);
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

  it("uninstalls the mod even when installing it fails", async () => {
    const { deps, calls } = fakeDeps({ install: () => { throw new Error("copy failed"); } });
    await expect(runCore("http-probe", spec(), deps)).rejects.toThrow(/copy failed/);
    expect(calls.at(-1)).toBe("uninstall SatisDashAgent");
  });

  const autoLoad = (calls: string[]) => ({
    readAutoLoad: async () => (calls.push("read auto-load"), "Owner Session"),
    setAutoLoad: async (name: string) => void calls.push(`auto-load "${name}"`),
  });

  it("emptyBoot: clears the auto-load session in a boot without the mod, and puts it back while the run's server is up", async () => {
    const { deps, calls } = fakeDeps();
    Object.assign(deps, autoLoad(calls));
    const { result } = await runCore("enrol", spec({ emptyBoot: true }), deps);
    expect(result.pass).toBe(true);
    expect(calls).toEqual([
      "probe", "build", "remove SatisMod*",
      "start", "healthy", "read auto-load", 'auto-load ""', "stop", "probe",
      "install D:/b/SatisDashAgent as SatisDashAgent", "reset log", "start", "healthy",
      'auto-load "Owner Session"', "stop", "uninstall SatisDashAgent",
    ]);
  });

  it("emptyBoot: the auto-load session is restored when drive throws", async () => {
    const { deps, calls } = fakeDeps();
    Object.assign(deps, autoLoad(calls));
    const { result } = await runCore("enrol", spec({ emptyBoot: true, drive: async () => { throw new Error("boom"); } }), deps);
    expect(result.pass).toBe(false);
    expect(result.reasons[0]).toBe("boom");
    expect(calls.filter((c) => c.startsWith("auto-load"))).toEqual(['auto-load ""', 'auto-load "Owner Session"']);
    expect(calls.lastIndexOf('auto-load "Owner Session"')).toBeLessThan(calls.lastIndexOf("stop"));
  });

  it("emptyBoot: when the run's server never comes up, a boot without the mod puts the session back", async () => {
    let healthChecks = 0;
    const { deps, calls } = fakeDeps({
      waitHealthy: async () => {
        healthChecks++;
        calls.push("healthy");
        if (healthChecks === 2) throw new Error("never healthy");
      },
    });
    Object.assign(deps, autoLoad(calls));
    const { result } = await runCore("enrol", spec({ emptyBoot: true }), deps);
    expect(result.pass).toBe(false);
    expect(result.reasons).toContain("never healthy");
    const after = calls.slice(calls.indexOf("uninstall SatisDashAgent"));
    expect(after).toEqual(["uninstall SatisDashAgent", "probe", "start", "healthy", 'auto-load "Owner Session"', "stop"]);
  });

  it("emptyBoot is refused without the auto-load deps", async () => {
    const { deps, calls } = fakeDeps();
    await expect(runCore("enrol", spec({ emptyBoot: true }), deps)).rejects.toThrow(/auto-load/);
    expect(calls).toEqual([]);
  });

  it("baseline: boots once without the mod, world loaded and auto-pause off, measures it, and hands the result to drive", async () => {
    const { deps, calls } = fakeDeps();
    let seen: unknown;
    const { result } = await runCore("tick-cost", spec({
      world: true,
      baseline: true,
      measureBaseline: async ({ proc }) => (calls.push(`measure ${proc.pid}`), { median: 22 }),
      drive: async (ctx) => void (seen = ctx.baseline),
    }), deps);
    expect(result.pass).toBe(true);
    expect(seen).toEqual({ median: 22 });
    expect(calls).toEqual([
      "probe", "build", "remove SatisMod*",
      "start", "healthy", "load world", "healthy", "auto-pause false", "measure 100", "auto-pause true", "stop", "probe",
      "install D:/b/SatisDashAgent as SatisDashAgent", "reset log", "start", "healthy", "stop", "uninstall SatisDashAgent",
    ]);
  });

  it("baseline: a failed measurement fails the run, restores auto-pause and stops the server; drive still runs with no baseline", async () => {
    const { deps, calls } = fakeDeps();
    let seen: unknown = "unset";
    const { result } = await runCore("tick-cost", spec({
      world: true,
      baseline: true,
      measureBaseline: async () => { throw new Error("no ticks"); },
      drive: async (ctx) => void (seen = ctx.baseline),
    }), deps);
    expect(result.pass).toBe(false);
    expect(result.reasons).toContain("baseline: no ticks");
    expect(seen).toBeUndefined();
    expect(calls.slice(calls.indexOf("auto-pause false"), calls.indexOf("auto-pause false") + 3)).toEqual(["auto-pause false", "auto-pause true", "stop"]);
  });

  it("worldKey: a spec for the S2 world is refused without a loader, naming the key", async () => {
    const { deps, calls } = fakeDeps();
    delete (deps as { loadWorld?: unknown }).loadWorld;
    await expect(runCore("native-parity", spec({ world: true, worldKey: "s2" }), deps)).rejects.toThrow(/native-parity needs s2World in the runner config/);
    await expect(runCore("tick-cost", spec({ world: true }), deps)).rejects.toThrow(/tick-cost needs s3World in the runner config/);
    expect(calls).toEqual([]);
  });

  it("worldKey: s2 is refused when the runner config names no s2World, even with a loader (s3World alone)", async () => {
    const { deps, calls } = fakeDeps();
    deps.config.s3World = "x.sav";
    await expect(runCore("native-parity", spec({ world: true, worldKey: "s2" }), deps)).rejects.toThrow(/needs s2World/);
    deps.config.s2World = "y.sav";
    await runCore("native-parity", spec({ world: true, worldKey: "s2" }), deps);
    expect(calls).toContain("build");
  });

  it("worldKey: an s3-keyed run is refused at preflight when only s2World is configured (the loader exists for s2)", async () => {
    const { deps, calls } = fakeDeps();
    delete (deps.config as { s3World?: string }).s3World;
    deps.config.s2World = "y.sav";
    await expect(runCore("tick-cost", spec({ world: true }), deps)).rejects.toThrow(/tick-cost needs s3World in the runner config/);
    await expect(runCore("tick-cost", spec({ world: true, worldKey: "s3" }), deps)).rejects.toThrow(/needs s3World/);
    expect(calls).toEqual([]);
  });

  it("worldKey: sprint3 is refused without sprint3World and loads the sprint3 key with it", async () => {
    const keys: unknown[] = [];
    const { deps, calls } = fakeDeps({ loadWorld: async (key) => void keys.push(key) });
    deps.config.s3World = "x.sav";
    deps.config.s2World = "y.sav";
    await expect(runCore("native-parity", spec({ world: true, worldKey: "sprint3" }), deps)).rejects.toThrow(/native-parity needs sprint3World in the runner config/);
    expect(calls).toEqual([]);
    deps.config.sprint3World = "z.sav";
    await runCore("native-parity", spec({ world: true, baseline: true, worldKey: "sprint3", measureBaseline: async () => 1 }), deps);
    expect(keys).toContain("sprint3");
  });

  it("worldKey: the baseline boot loads the spec's key (none when unset)", async () => {
    for (const [worldKey, expected] of [["s2", "s2"], [undefined, undefined]] as const) {
      const keys: unknown[] = [];
      const { deps } = fakeDeps({ loadWorld: async (key) => void keys.push(key) });
      deps.config.s2World = "y.sav";
      await runCore("tick-cost", spec({ world: true, baseline: true, worldKey, measureBaseline: async () => 1 }), deps);
      expect(keys).toEqual([expected]);
    }
  });

  it("baseline is refused without the world or a measurement", async () => {
    const { deps, calls } = fakeDeps();
    await expect(runCore("tick-cost", spec({ baseline: true, measureBaseline: async () => 1 }), deps)).rejects.toThrow(/baseline/);
    await expect(runCore("tick-cost", spec({ world: true, baseline: true }), deps)).rejects.toThrow(/baseline/);
    expect(calls).toEqual([]);
  });

  it("records the server's memory with whether a game ran, read at the time, in each sample's label (review of 66e1f0d)", async () => {
    const sample = { totalWorkingSetBytes: 1, processes: [{ name: "FactoryServer-Win64-Shipping-Cmd.exe", workingSetBytes: 1, peakWorkingSetBytes: 2 }] };
    // The pre-boot runs the auto-loaded save; the run's boot has none until drive loads the world.
    const games = [{ running: true, session: "aaaaaaaaaaaa" }, { running: false, session: "da39a3ee5e6b" }, { running: true, session: "bbbbbbbbbbbb" }, null];
    const { deps, calls } = fakeDeps({ processMemory: async () => sample, gameState: async () => games.shift() ?? null });
    Object.assign(deps, autoLoad(calls));
    const { dir } = await runCore("enrol", spec({
      emptyBoot: true,
      drive: async (ctx) => {
        await ctx.sampleMemory("withMod");
        await ctx.sampleMemory("withMod");
      },
    }), deps);
    expect(JSON.parse(readFileSync(join(dir, "memory.json"), "utf8"))).toEqual({
      noModWithGame: { game: true, session: "aaaaaaaaaaaa", memory: sample },
      withModNoGame: { game: false, session: "da39a3ee5e6b", memory: sample },
      withModWithGame: { game: true, session: "bbbbbbbbbbbb", memory: sample },
      endGameUnknown: { game: null, session: null, memory: sample },
    });
  });

  it("never overwrites a memory sample: a repeated label gets a number", async () => {
    const sample = { totalWorkingSetBytes: 1, processes: [] };
    const { deps } = fakeDeps({ processMemory: async () => sample, gameState: async () => ({ running: false, session: "x" }) });
    const { dir } = await runCore("http-probe", spec({ drive: async (ctx) => { await ctx.sampleMemory("withMod"); await ctx.sampleMemory("withMod"); } }), deps);
    expect(Object.keys(JSON.parse(readFileSync(join(dir, "memory.json"), "utf8")))).toEqual(["withModNoGame", "withModNoGame#2", "endNoGame"]);
  });

  it("an evaluate that throws fails the run and still writes the bundle", async () => {
    const { result, dir } = await runCore("http-probe", spec({ evaluate: () => { throw new TypeError("x is undefined"); } }), fakeDeps().deps);
    expect(result.pass).toBe(false);
    expect(result.reasons).toContain("judging the evidence failed: x is undefined");
    expect(readdirSync(dir).length).toBeGreaterThan(0);
  });

  it("an evaluate that throws replaces the drive's first readers.json with a failure marker", async () => {
    const drive = async (ctx: { captures: Record<string, unknown> }) => { ctx.captures["readers.json"] = { stations: { pass: true } }; };
    const { dir } = await runCore("http-probe", spec({ drive, evaluate: () => { throw new TypeError("x is undefined"); } } as never), fakeDeps().deps);
    const readers = JSON.parse(readFileSync(join(dir, "readers.json"), "utf8"));
    expect(readers).toEqual({ failed: "judging the evidence failed: x is undefined" });
  });

  it("builds outside the out dir, so nothing of the build is ever uploaded with the evidence", () => {
    for (const out of [join(tmpdir(), "core-run"), join(tmpdir(), "core-run") + "/", "relative/out"]) {
      const rel = relative(out, coreBuildDir(out));
      expect(rel.startsWith("..") || isAbsolute(rel), `${out} -> ${coreBuildDir(out)}`).toBe(true);
      expect(coreBuildDir(out).endsWith(MOD)).toBe(true);
    }
  });

  it("refuses when check-api fails or the build fails", async () => {
    const api = fakeDeps({ checkApi: () => ({ missing: [{ kind: "symbol", name: "X", path: "mod/a.cpp", line: 3 }], badRows: [] }) });
    await expect(runCore("http-probe", spec(), api.deps)).rejects.toThrow(/check-api failed: mod\/a\.cpp:3 X/);
    const build = fakeDeps({ buildMod: async () => Promise.reject(new Error("build-mod failed")) });
    await expect(runCore("http-probe", spec(), build.deps)).rejects.toThrow(RefusalError);
    expect(build.calls).toEqual(["probe"]);
  });

  it("refuses before building when the spec's preflight throws", async () => {
    const f = fakeDeps();
    const s = spec({ preflight: () => { throw new Error("needs a thing"); } });
    await expect(runCore("http-probe", s, f.deps)).rejects.toThrow(RefusalError);
    await expect(runCore("http-probe", s, f.deps)).rejects.toThrow(/needs a thing/);
    expect(f.calls).not.toContain("build");
  });

  it("hands the preflight the run's deps, so it can check the runner config", async () => {
    const f = fakeDeps();
    let seen: CoreDeps | undefined;
    await runCore("http-probe", spec({ preflight: (d) => void (seen = d) }), f.deps);
    expect(seen).toBe(f.deps);
  });
});
