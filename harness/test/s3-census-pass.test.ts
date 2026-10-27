import { describe, expect, it } from "vitest";
import { CORE_RUN_IDS, NATIVE_SOURCES_S3, type CoreContext, type CoreDeps, type CoreEvidence } from "../src/core-run";
import { CORE_RUNS } from "../src/core-runs";
import { CENSUS_SETTLE_MS, CENSUS_WAIT_MS, s3Census } from "../src/core-runs/s3-census";
import { censusLines, logisticsRead } from "../src/reader-lines";
import * as Pass from "../../runs/s3-census/pass";

const P = "[2026.10.27-12:00:05:123][  0]LogSatisDash: Display: ";
const logistics = (rails: number, conveyors: number) =>
  `${P}Logistics read (game thread only): layers rails=${rails} conveyors=${conveyors}, 900 buildables, 12 tracks, 340 belts, 6 lifts, 80 pipes, 14 open ends, 3 ownerless ends in 40 steps, longest step 1.20 ms, total 30.10 ms, expected tracks 12, dropped 0, over cap 0`;
const tier = (kind: string, name: string, speed: string, count: number) => `${P}Conveyor tier census: ${kind} | ${name} | speed ${speed} | count ${count}`;
const KINDS = `${P}Conveyor kinds: 340 ends checked, 2 differ from the class table (first classes: Build_A_C, Build_B_C)`;
const PORTS = `${P}Conveyor drone ports: 3 edges end at a port, 2 start at one`;
const ev = (lines: string[]): CoreEvidence => ({ lines, errorLines: [], leaks: { code: false, secret: null, bearer: false }, captures: {} });
const GOOD = [logistics(1, 1), KINDS, PORTS, tier("belt", "Conveyor Belt Mk.1", "120.000", 200), tier("belt", "Conveyor Belt Mk.2", "240.000", 100), tier("pipe", "Pipeline Mk.1", "5.000", 60)];

describe("censusLines", () => {
  it("parses tiers, kinds and ports from the mod's lines", () => {
    expect(censusLines(GOOD)).toEqual({
      tiers: [
        { kind: "belt", name: "Conveyor Belt Mk.1", speed: 120, count: 200 },
        { kind: "belt", name: "Conveyor Belt Mk.2", speed: 240, count: 100 },
        { kind: "pipe", name: "Pipeline Mk.1", speed: 5, count: 60 },
      ],
      kinds: { checked: 340, differ: 2, classes: ["Build_A_C", "Build_B_C"] },
      ports: { into: 3, outOf: 2 },
    });
  });
  it("reads an empty class list as none and ignores unparsable lines", () => {
    const c = censusLines([`${P}Conveyor kinds: 5 ends checked, 0 differ from the class table (first classes: )`, `${P}Conveyor tier census: belt | broken`, `${P}Conveyor tier census: wire | X | speed 1 | count 1`]);
    expect(c).toEqual({ tiers: [], kinds: { checked: 5, differ: 0, classes: [] }, ports: null });
  });
  it("reads the Logistics read counts", () => {
    expect(logisticsRead(GOOD)).toMatchObject({ rails: 1, conveyors: 1, belts: 340, lifts: 6, pipes: 80, openEnds: 14, ownerlessEnds: 3 });
    expect(logisticsRead([KINDS])).toBeNull();
  });
});

describe("s3-census judge", () => {
  it("passes and prints every value; belt Mk.1 at speed 120 is 60 match, Mk.2 at 240 is 120 match", () => {
    const v = Pass.evaluate(ev(GOOD));
    expect(v.pass).toBe(true);
    const text = v.reasons.join("\n");
    expect(text).toContain("speed / 2 = 60 match (expected 60)");
    expect(text).toContain("speed / 2 = 120 match (expected 120)");
    expect(text).toContain("flow x 60 = 300 match (expected 300)");
    expect(text).toContain("3 edges end at a port, 2 start at one");
    expect(text).toContain("Build_A_C, Build_B_C");
  });
  it("prints NO MATCH for a belt Mk.1 at speed 240 and for a wrong pipe flow", () => {
    const v = Pass.evaluate(ev([logistics(1, 1), tier("belt", "Conveyor Belt Mk.1", "240", 1), tier("pipe", "Pipeline Mk.2", "5", 1)]));
    expect(v.pass).toBe(true);
    expect(v.reasons.join("\n")).toContain("speed / 2 = 120 NO MATCH (expected 60)");
    expect(v.reasons.join("\n")).toContain("flow x 60 = 300 NO MATCH (expected 600)");
  });
  it("prints a tier with no Mk name without a verdict", () => {
    const v = Pass.evaluate(ev([logistics(1, 1), tier("lift", "Odd Lift", "100", 1)]));
    expect(v.pass).toBe(true);
    expect(v.reasons.join("\n")).not.toMatch(/NO MATCH|match \(/);
  });
  it("fails without a Logistics read line", () => {
    const v = Pass.evaluate(ev([KINDS, PORTS]));
    expect(v.pass).toBe(false);
    expect(v.reasons[0]).toMatch(/no "Logistics read/);
  });
  it("fails when a layer is not native (conveyors=0 or rails=0)", () => {
    for (const l of [logistics(1, 0), logistics(0, 1)]) {
      const v = Pass.evaluate(ev([l, KINDS]));
      expect(v.pass).toBe(false);
      expect(v.reasons[0]).toMatch(/not rails=1 conveyors=1/);
    }
  });
  it("fails with no census line at all", () => {
    const v = Pass.evaluate(ev([logistics(1, 1)]));
    expect(v.pass).toBe(false);
    expect(v.reasons.join(" ")).toMatch(/no conveyor census line/);
  });
  it("is not fatal on unparsable census lines", () => {
    expect(Pass.evaluate(ev([logistics(1, 1), PORTS, `${P}Conveyor tier census: garbage`])).pass).toBe(true);
  });
});

describe("s3-census spec and drive", () => {
  it("is registered, needs no FRM, and loads sprint3 with every reader native", () => {
    expect(CORE_RUN_IDS).toContain("s3-census");
    expect(CORE_RUNS["s3-census"]).toBe(s3Census);
    expect(s3Census).toMatchObject({ world: true, worldKey: "sprint3", backend: true, modConfig: NATIVE_SOURCES_S3 });
    expect(s3Census.baseline).toBeUndefined();
    expect(CENSUS_WAIT_MS).toBe(14 * 60_000);
    expect(CENSUS_SETTLE_MS).toBe(20_000);
  });
  it("loads the world, enrols, waits for the Logistics line, settles, and writes census.json", async () => {
    const steps: string[] = [];
    let log = "";
    const deps = {
      backendHost: async () => ({ relayUrl: "http://127.0.0.1:9", seedCode: async () => void steps.push("seedCode") }),
      loadWorld: async (key?: string) => void steps.push(`loadWorld:${key}`),
      waitHealthy: async () => void steps.push("waitHealthy"),
      readAutoPause: async () => true,
      setAutoPause: async () => void steps.push("autoPauseOff"),
      readLogSince: () => log,
      sleep: async (ms: number) => void steps.push(`sleep:${ms}`),
    } as unknown as CoreDeps;
    const waits: Array<[string, number]> = [];
    const captures: Record<string, unknown> = {};
    const ctx = {
      deps, captures, offset: 0,
      writeConfig: () => void steps.push("enrol"),
      noteSecret: () => undefined,
      rememberAutoPause: () => undefined,
      waitFor: async (what: string, check: () => unknown, ms: number) => {
        waits.push([what, ms]);
        if (what.includes("Logistics")) {
          expect(check()).toBeUndefined();
          log = GOOD.join("\n");
          expect(check()).toBe(true);
        }
        return true;
      },
    } as unknown as CoreContext;
    await s3Census.drive(ctx);
    expect(steps.filter((s) => s !== "seedCode")).toEqual(["loadWorld:sprint3", "waitHealthy", "autoPauseOff", "enrol", `sleep:${CENSUS_SETTLE_MS}`]);
    expect(waits.at(-1)).toEqual(["the first Logistics read line", CENSUS_WAIT_MS]);
    const c = captures["census.json"] as { tiers: unknown[]; logistics: { belts: number } };
    expect(c.tiers).toHaveLength(3);
    expect(c.logistics.belts).toBe(340);
  });
});
