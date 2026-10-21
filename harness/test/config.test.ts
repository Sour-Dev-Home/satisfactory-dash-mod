import { join } from "node:path";
import { describe, expect, it } from "vitest";
import { RunnerConfigSchema, logPathOf, pluginDirFor } from "../src/config";
import { loadToolchain, toolchainMismatches } from "../src/toolchain";

const valid = {
  serverDir: "D:/satis-test/server",
  apiBaseUrl: "https://127.0.0.1:17777",
  buildCommand: ["build.cmd", "{spike}"],
  builtPluginDir: "D:/satis-build/{spike}/Windows",
  installed: { engine: "5.6.1", sml: "3.12.0" },
};

describe("RunnerConfigSchema", () => {
  it("accepts a loopback https config and fills defaults", () => {
    const cfg = RunnerConfigSchema.parse(valid);
    expect(cfg.serverExe).toBe("FactoryServer.exe");
    expect(cfg.serverArgs).toEqual([]);
  });

  it("refuses plain http", () => {
    expect(() => RunnerConfigSchema.parse({ ...valid, apiBaseUrl: "http://127.0.0.1:17777" })).toThrow(/loopback/);
  });

  it("refuses a non-loopback host", () => {
    expect(() => RunnerConfigSchema.parse({ ...valid, apiBaseUrl: "https://192.168.1.5:7777" })).toThrow(/loopback/);
  });

  it("refuses an empty build command and says where it comes from", () => {
    expect(() => RunnerConfigSchema.parse({ ...valid, buildCommand: [] })).toThrow(/Alpakit/);
  });

  it("takes the backend host's file and port, and refuses a privileged port or extra keys", () => {
    const cfg = RunnerConfigSchema.parse({ ...valid, backendHost: { file: "D:/satis-runner/backend-host.json", port: 13102 } });
    expect(cfg.backendHost).toEqual({ file: "D:/satis-runner/backend-host.json", port: 13102 });
    expect(() => RunnerConfigSchema.parse({ ...valid, backendHost: { file: "x", port: 80 } })).toThrow();
    expect(() => RunnerConfigSchema.parse({ ...valid, backendHost: { file: "", port: 13102 } })).toThrow();
    expect(() => RunnerConfigSchema.parse({ ...valid, backendHost: { file: "x", port: 13102, token: "t" } })).toThrow();
  });

  it("refuses a plugin dir without {spike}", () => {
    expect(() => RunnerConfigSchema.parse({ ...valid, builtPluginDir: "D:/satis-build" })).toThrow();
  });

  it("defaults the log path to FactoryGame/Saved/Logs under the server", () => {
    const cfg = RunnerConfigSchema.parse(valid);
    expect(logPathOf(cfg)).toBe(join("D:/satis-test/server", "FactoryGame", "Saved", "Logs", "FactoryGame.log"));
  });

  it("fills {spike} in the plugin dir", () => {
    expect(pluginDirFor(RunnerConfigSchema.parse(valid), "S1b")).toBe("D:/satis-build/S1b/Windows");
  });
});

describe("toolchain", () => {
  it("pins the versions the captured SML docs state", () => {
    const t = loadToolchain(join(import.meta.dirname, "..", "toolchain.json"));
    expect([t.engine, t.sml]).toEqual(["5.6.1", "3.12.0"]);
  });

  it("reports each mismatch and nothing when they match", () => {
    const t = { engine: "5.6.1", sml: "3.12.0", source: "x" };
    expect(toolchainMismatches(t, { engine: "5.6.1", sml: "3.12.0" })).toEqual([]);
    expect(toolchainMismatches(t, { engine: "5.5.0", sml: "3.11.0" })).toEqual([
      "engine: required 5.6.1, installed 5.5.0",
      "sml: required 3.12.0, installed 3.11.0",
    ]);
  });
});
