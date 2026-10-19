import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import type { ServerApi } from "../src/server-api";
import {
  INSECURE_LOCAL_ARG,
  installMod,
  removeMod,
  removeSpikeMods,
  serverAnswering,
  startServer,
  stopServer,
  waitHealthy,
  whileRunning,
} from "../src/server-control";

let root = "";
let server = "";
beforeEach(() => {
  root = mkdtempSync(join(tmpdir(), "srv-"));
  server = join(root, "server");
  mkdirSync(server);
});
afterEach(() => rmSync(root, { recursive: true, force: true }));

function plugin(name: string, uplugin: object | string) {
  const dir = join(root, "build", name);
  mkdirSync(join(dir, "Binaries"), { recursive: true });
  writeFileSync(join(dir, `${name}.uplugin`), typeof uplugin === "string" ? uplugin : JSON.stringify(uplugin));
  writeFileSync(join(dir, "Binaries", "x.dll"), "bin");
  return dir;
}

describe("installMod", () => {
  it("copies a plugin without GameFeature into FactoryGame/Mods/<name>", () => {
    expect(installMod(plugin("SatisModS0", { FriendlyName: "x" }), server)).toBe("SatisModS0");
    expect(readFileSync(join(server, "FactoryGame", "Mods", "SatisModS0", "Binaries", "x.dll"), "utf8")).toBe("bin");
  });

  it("copies a GameFeature plugin into Mods/GameFeatures/<name>", () => {
    installMod(plugin("SatisModS1", { GameFeature: true }), server);
    expect(existsSync(join(server, "FactoryGame", "Mods", "GameFeatures", "SatisModS1", "SatisModS1.uplugin"))).toBe(true);
  });

  it("reads a .uplugin with a byte-order mark", () => {
    expect(installMod(plugin("SatisModS2", '﻿{"GameFeature":false}'), server)).toBe("SatisModS2");
  });

  it("replaces an earlier copy instead of merging into it", () => {
    const stale = join(server, "FactoryGame", "Mods", "SatisModS0", "old.txt");
    mkdirSync(join(stale, ".."), { recursive: true });
    writeFileSync(stale, "old");
    installMod(plugin("SatisModS0", {}), server);
    expect(existsSync(stale)).toBe(false);
  });

  it("refuses a folder with no .uplugin or with two", () => {
    const dir = plugin("A", {});
    writeFileSync(join(dir, "B.uplugin"), "{}");
    expect(() => installMod(dir, server)).toThrow(/exactly one .uplugin.*found 2/);
  });

  it("refuses a plugin with an unexpected name before copying anything", () => {
    expect(() => installMod(plugin("SatisModS3", {}), server, "SatisModS0")).toThrow(/SatisModS3.*SatisModS0/);
    expect(existsSync(join(server, "FactoryGame", "Mods"))).toBe(false);
  });

  it("removeSpikeMods clears every SatisMod* folder in both locations and leaves other mods", () => {
    installMod(plugin("SatisModS3", {}), server);
    installMod(plugin("SatisModS1", { GameFeature: true }), server);
    installMod(plugin("OtherMod", {}), server);
    removeSpikeMods(server);
    expect(existsSync(join(server, "FactoryGame", "Mods", "SatisModS3"))).toBe(false);
    expect(existsSync(join(server, "FactoryGame", "Mods", "GameFeatures", "SatisModS1"))).toBe(false);
    expect(existsSync(join(server, "FactoryGame", "Mods", "OtherMod"))).toBe(true);
  });

  it("removeSpikeMods is a no-op on a server with no Mods folder", () => {
    expect(() => removeSpikeMods(server)).not.toThrow();
  });

  it("removeMod clears both possible locations", () => {
    installMod(plugin("SatisModS1", { GameFeature: true }), server);
    removeMod("SatisModS1", server);
    expect(existsSync(join(server, "FactoryGame", "Mods", "GameFeatures", "SatisModS1"))).toBe(false);
  });
});

describe("startServer", () => {
  it("launches the configured exe with -log, the loopback-access argument, then the config's args", () => {
    const seen: unknown[] = [];
    startServer({ serverDir: server, serverExe: "FactoryServer.exe", serverArgs: ["-Port=17777"] }, (exe, args, cwd) => {
      seen.push(exe, args, cwd);
      return { pid: 1, exited: new Promise(() => {}) };
    });
    expect(seen).toEqual([join(server, "FactoryServer.exe"), ["-log", INSECURE_LOCAL_ARG, "-Port=17777"], server]);
  });
});

describe("waitHealthy", () => {
  it("polls HealthCheck until a 200", async () => {
    let n = 0;
    const api: ServerApi = { call: async () => (++n < 3 ? Promise.reject(new Error("refused")) : { status: 200, body: { health: "healthy" } }) };
    let t = 0;
    const body = await waitHealthy(api, { timeoutMs: 60_000, pollMs: 1000, now: () => t, sleep: async (ms) => void (t += ms) });
    expect(body).toEqual({ health: "healthy" });
    expect(n).toBe(3);
  });

  it("times out", async () => {
    const api: ServerApi = { call: async () => ({ status: 503, body: null }) };
    let t = 0;
    await expect(waitHealthy(api, { timeoutMs: 3000, pollMs: 1000, now: () => t, sleep: async (ms) => void (t += ms) })).rejects.toThrow(
      /not healthy after 3000 ms/,
    );
  });
});

describe("serverAnswering", () => {
  it("is true for any HTTP answer and false when nothing listens", async () => {
    expect(await serverAnswering({ call: async () => ({ status: 503, body: null }) })).toBe(true);
    expect(await serverAnswering({ call: async () => Promise.reject(new Error("ECONNREFUSED")) })).toBe(false);
  });
});

describe("whileRunning", () => {
  it("rejects as soon as the spawned process exits", async () => {
    const proc = { pid: 7, exited: Promise.resolve(1) };
    await expect(whileRunning(proc, new Promise(() => {}), "waiting for health")).rejects.toThrow(/exited \(code 1\) while waiting for health/);
  });

  it("passes the value through while the process runs", async () => {
    expect(await whileRunning({ pid: 7, exited: new Promise(() => {}) }, Promise.resolve(42), "x")).toBe(42);
  });
});

describe("stopServer", () => {
  const api: ServerApi = { call: async () => ({ status: 204, body: null }) };

  it("never sends Shutdown when the spawned process has already exited (the port may be someone else's)", async () => {
    const calls: string[] = [];
    const spy: ServerApi = { call: async (fn) => (calls.push(fn), { status: 204, body: null }) };
    const r = await stopServer({ pid: 42, exited: Promise.resolve(1) }, spy, { graceMs: 1000, kill: async () => {} });
    expect(r).toBe("exited");
    expect(calls).toEqual([]);
  });

  it("returns 'exited' when Shutdown ends the process within the grace period", async () => {
    const killed: number[] = [];
    const r = await stopServer({ pid: 42, exited: Promise.resolve(0) }, api, { graceMs: 1000, kill: async (p) => void killed.push(p) });
    expect(r).toBe("exited");
    expect(killed).toEqual([]);
  });

  it("kills only the spawned pid when it does not exit", async () => {
    const killed: number[] = [];
    const r = await stopServer({ pid: 42, exited: new Promise(() => {}) }, api, {
      graceMs: 1000,
      kill: async (p) => void killed.push(p),
      sleep: async () => {},
    });
    expect(r).toBe("killed");
    expect(killed).toEqual([42]);
  });
});
