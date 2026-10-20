import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { RunnerConfigSchema } from "../src/config";
import { decodeReport, judgeAutomationReport, readAutomationReport } from "../src/mod-test";

const counts = { succeeded: 3, succeededWithWarnings: 1, failed: 0, notRun: 0, inProcess: 0 };
let dir = "";
afterEach(() => dir && rmSync(dir, { recursive: true, force: true }));

describe("decodeReport", () => {
  it("reads UTF-16 with a BOM, UTF-8 with a BOM and plain UTF-8", () => {
    const text = '{"a":1}';
    expect(decodeReport(Buffer.concat([Buffer.from([0xff, 0xfe]), Buffer.from(text, "utf16le")]))).toBe(text);
    expect(decodeReport(Buffer.concat([Buffer.from([0xef, 0xbb, 0xbf]), Buffer.from(text)]))).toBe(text);
    expect(decodeReport(Buffer.from(text))).toBe(text);
  });
});

describe("readAutomationReport", () => {
  it("reads the counts from index.json and ignores other fields", () => {
    dir = mkdtempSync(join(tmpdir(), "mod-test-"));
    writeFileSync(join(dir, "index.json"), JSON.stringify({ ...counts, devices: [], totalDuration: 1.5 }));
    expect(readAutomationReport(dir)).toEqual(counts);
  });
});

describe("judgeAutomationReport", () => {
  it("passes when nothing failed, everything ran and enough tests passed", () => {
    expect(judgeAutomationReport(counts, 4)).toEqual({ pass: true, reason: "4 passed" });
  });
  it("fails on a failed test", () => {
    expect(judgeAutomationReport({ ...counts, failed: 1 }, 1).pass).toBe(false);
  });
  it("fails when a test didn't run or didn't finish", () => {
    expect(judgeAutomationReport({ ...counts, notRun: 1 }, 1).pass).toBe(false);
    expect(judgeAutomationReport({ ...counts, inProcess: 1 }, 1).pass).toBe(false);
  });
  it("fails when fewer tests passed than expected (a test silently not compiled)", () => {
    expect(judgeAutomationReport(counts, 5)).toEqual({ pass: false, reason: "4 passed, fewer than the 5 expected" });
  });
});

describe("RunnerConfigSchema.modTest", () => {
  const base = {
    serverDir: "s", apiBaseUrl: "https://127.0.0.1:7777", buildCommand: ["b"], builtPluginDir: "{spike}",
    installed: { engine: "5.6.1", sml: "3.12.0" },
  };
  it("is optional", () => expect(RunnerConfigSchema.safeParse(base).success).toBe(true));
  it("takes the SML project and the engine root", () => {
    expect(RunnerConfigSchema.safeParse({ ...base, modTest: { project: "p", engine: "e" } }).success).toBe(true);
  });
  it("refuses unknown keys", () => {
    expect(RunnerConfigSchema.safeParse({ ...base, modTest: { project: "p", engine: "e", x: 1 } }).success).toBe(false);
  });
});
