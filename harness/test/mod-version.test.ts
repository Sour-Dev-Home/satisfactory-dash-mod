import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";

// The mod's version is written once, in its .uplugin (Version.cpp reads VersionName). The run judges expect it as a
// literal, so a version bump that misses one of them fails here, not in a game run.
const read = (path: string) => readFileSync(fileURLToPath(new URL(path, import.meta.url)), "utf8");
const plugin = JSON.parse(read("../../mod/SatisDashAgent/SatisDashAgent.uplugin"));
const version: string = plugin.VersionName;

describe("the core mod's version, against its .uplugin", () => {
  it("VersionName and SML's SemVersion agree", () => {
    expect(version).toMatch(/^\d+\.\d+\.\d+$/);
    expect(plugin.SemVersion).toBe(version);
  });
  it("the enrol run expects agentVersion mod/<VersionName>", () => {
    expect(read("../../runs/enrol/pass.ts")).toContain(`d.state.agentVersion !== "mod/${version}"`);
  });
  it("the http-probe run expects User-Agent satisfactory-dash-mod/<VersionName>", () => {
    expect(read("../../runs/http-probe/pass.ts")).toContain(`USER_AGENT = "satisfactory-dash-mod/${version}"`);
  });
});
