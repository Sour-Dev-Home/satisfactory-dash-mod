import { existsSync, mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { modConfigPath, randomCredential, removeModConfig, sha1Prefix, writeModConfig } from "../src/mod-config";

let dir = "";
afterEach(() => dir && rmSync(dir, { recursive: true, force: true }));

describe("mod config files", () => {
  it("puts <ModReference>.cfg under FactoryGame/Configs, as SML's ConfigManager.cpp:309-318 does", () => {
    expect(modConfigPath("D:/t", "SatisModS4")).toBe(join("D:/t", "FactoryGame", "Configs", "SatisModS4.cfg"));
  });

  it("writes the values as one JSON object keyed by property name, and removes it", () => {
    dir = mkdtempSync(join(tmpdir(), "cfg-"));
    const path = writeModConfig(dir, "SatisModS4", { credential: "ABC" });
    expect(JSON.parse(readFileSync(path, "utf8"))).toEqual({ credential: "ABC" });
    removeModConfig(dir, "SatisModS4");
    expect(existsSync(path)).toBe(false);
    expect(() => removeModConfig(dir, "SatisModS4")).not.toThrow();
  });

  it("makes 32-character base32 credentials that differ per call", () => {
    const a = randomCredential();
    expect(a).toMatch(/^[A-Z2-7]{32}$/);
    expect(randomCredential()).not.toBe(a);
  });

  it("gives the first 12 hex characters of the UTF-8 SHA-1", () => {
    expect(sha1Prefix("abc")).toBe("a9993e364706"); // FIPS 180 test vector
  });
});
