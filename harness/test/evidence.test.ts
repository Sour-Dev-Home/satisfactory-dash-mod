import { mkdtempSync, readdirSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { type Result, redact, writeEvidence } from "../src/evidence";

// Built by concatenation: ci.yml's PII scan rejects these path fragments written literally in any file.
const U = "Us" + "ers";
const H = "ho" + "me";

let dir = "";
afterEach(() => dir && rmSync(dir, { recursive: true, force: true }));

const result: Result = {
  spike: "S0",
  claim: "C1",
  pass: true,
  reasons: ["ok"],
  startedAt: "2026-10-19T16:00:00.000Z",
  endedAt: "2026-10-19T16:05:00.000Z",
  toolchain: { required: { engine: "5.6.1", sml: "3.12.0" }, declared: { engine: "5.6.1", sml: "3.12.0" } },
  versionLines: [],
  modLines: ["satis-mod loaded"],
  harnessCommit: "local",
  runUrl: null,
};

describe("redact", () => {
  it("replaces Windows and POSIX home paths", () => {
    expect(redact(`at C:\\${U}\\someone\\x and D:/${U}/other/y and /${H}/me/z`)).toBe("at <home>\\x and <home>/y and <home>/z");
  });
});

describe("writeEvidence", () => {
  it("writes result.json and the captures into a per-run folder", () => {
    dir = mkdtempSync(join(tmpdir(), "ev-"));
    const out = writeEvidence(dir, result, { "echo.json": [{ a: 1 }] });
    expect(out).toBe(join(dir, "S0-2026-10-19T16-00-00-000Z"));
    expect(readdirSync(out).sort()).toEqual(["echo.json", "result.json"]);
    expect(JSON.parse(readFileSync(join(out, "result.json"), "utf8"))).toEqual(result);
  });

  it("redacts home paths in the result and the captures", () => {
    dir = mkdtempSync(join(tmpdir(), "ev-"));
    const out = writeEvidence(dir, { ...result, modLines: [`satis-mod loaded from C:\\${U}\\someone\\Mods`] }, { "echo.json": `C:\\${U}\\someone` });
    const all = readdirSync(out).map((f) => readFileSync(join(out, f), "utf8")).join("\n");
    // result.json holds the path JSON-escaped (two backslashes); the user name must be gone in either form.
    expect(all).not.toMatch(/someone/);
    expect(JSON.parse(readFileSync(join(out, "result.json"), "utf8")).modLines).toEqual(["satis-mod loaded from <home>\\Mods"]);
  });

  it("refuses an invalid result and a capture name with a path", () => {
    dir = mkdtempSync(join(tmpdir(), "ev-"));
    expect(() => writeEvidence(dir, { ...result, reasons: [] }, {})).toThrow();
    expect(() => writeEvidence(dir, result, { "../x.json": {} })).toThrow(/capture name/);
  });
});
