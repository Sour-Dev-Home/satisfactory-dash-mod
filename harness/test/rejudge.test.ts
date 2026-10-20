import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { rejudge } from "../src/rejudge";
import type { StatusPair, StatusReading } from "../src/verdict";

let dir = "";
afterEach(() => dir && rmSync(dir, { recursive: true, force: true }));

const reading = (running: boolean, tickRate = 30): StatusReading => ({ running, paused: false, players: 0, limit: 4, duration: 0, tickRate, health: "healthy", session: "da39a3ee5e6b" });
const pairs = (running: boolean): StatusPair[] => Array.from({ length: 10 }, (_, i) => ({ at: `t${i}`, mod: reading(running), api: reading(running) }));

function bundle(result: object, files: Record<string, unknown>) {
  dir = mkdtempSync(join(tmpdir(), "rejudge-"));
  writeFileSync(join(dir, "result.json"), JSON.stringify(result));
  for (const [name, value] of Object.entries(files)) writeFileSync(join(dir, name), JSON.stringify(value));
  return dir;
}

describe("rejudge", () => {
  it("re-applies S6's current rule to the bundle's own status.json and writes rejudge.json beside it", () => {
    const world = pairs(true);
    world[0] = { ...world[0]!, mod: reading(true, 24.18), api: reading(true, 28.4) };
    const d = bundle({ spike: "S6", claim: "C8", pass: false, reasons: ["t0: tick rate 24.18 vs API 28.4"], modLines: [] }, { "status.json": [...pairs(false), ...world] });
    const out = rejudge(d, { now: new Date("2026-10-20T14:00:00.000Z"), commit: "abc1234" });
    expect(out.verdict.pass).toBe(true);
    expect(JSON.parse(readFileSync(join(d, "rejudge.json"), "utf8"))).toEqual({
      spike: "S6",
      claim: "C8",
      rejudgedAt: "2026-10-20T14:00:00.000Z",
      ruleCommit: "abc1234",
      original: { pass: false, reasons: ["t0: tick rate 24.18 vs API 28.4"] },
      verdict: out.verdict,
    });
  });

  it("refuses a spike whose evidence isn't all in the bundle", () => {
    const d = bundle({ spike: "S5", claim: "C7", pass: false, reasons: [], modLines: [] }, {});
    expect(() => rejudge(d, { now: new Date(), commit: "abc1234" })).toThrow(/S5 can't be re-judged from its bundle/);
  });

  it("refuses a bundle without its capture", () => {
    const d = bundle({ spike: "S6", claim: "C8", pass: false, reasons: [], modLines: [] }, {});
    expect(() => rejudge(d, { now: new Date(), commit: "abc1234" })).toThrow(/status\.json/);
  });
});
