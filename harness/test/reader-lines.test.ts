import { describe, expect, it } from "vitest";
import { MAX_NATIVE_STEP_MS, STEP_LINE_NAMES, conveyorsLogProblems, droneRoutesLogProblems, extractorsLogProblems, factoryLogProblems, generatorsLogProblems, stationRatesLogProblems, storageLogProblems, logisticsLayers, logisticsLineProblems, logisticsRead, mapMarkersLogProblems, powerLinesLogProblems, railsLogProblems, readerSources, scanCosts, stepCosts, stepProblems, trainsLogProblems } from "../src/reader-lines";

const P = "[2026.10.26-12:00:05:123][  0]LogSatisDash: Display: ";
const line = (name: string, longest: string) => `${P}${name} (game thread only): 1000 items in 40 steps, longest step ${longest} ms, total 12.50 ms`;
const LINES = [line("Factory read", "0.30"), line("Trains read", "1.70"), line("Train routes read", "2.05")];

describe("step costs", () => {
  it("names the eight sliced readers and the ceiling", () => {
    expect(STEP_LINE_NAMES).toEqual(["Resource node scan", "Factory read", "Trains read", "Train routes read", "Logistics read", "Map markers read", "Drone routes read", "Buildables read"]);
    expect(MAX_NATIVE_STEP_MS).toBe(2.0);
  });
  it("parses the Buildables line and holds it to the ceiling", () => {
    const sample = `${P}Buildables read (game thread only): 4000 elements, 120 extractors, 0 generators, 0 containers in 6 steps, longest step 0.90 ms, total 4.10 ms`;
    expect(stepCosts([sample])).toEqual([{ reader: "Buildables read", longestStepMs: 0.9, totalMs: 4.1 }]);
    expect(stepProblems([sample], ["Buildables read"])).toEqual([]);
    const slow = `${P}Buildables read (game thread only): 4000 elements, 120 extractors, 0 generators, 0 containers in 6 steps, longest step 2.05 ms, total 4.10 ms`;
    expect(stepProblems([slow], ["Buildables read"]).length).toBeGreaterThan(0);
  });
  it("parses the map markers line and holds it to the ceiling", () => {
    const sample = `${P}Map markers read (game thread only): 12 markers, 1 dropped in 3 steps, longest step 0.40 ms, total 1.10 ms`;
    expect(stepCosts([sample])).toEqual([{ reader: "Map markers read", longestStepMs: 0.4, totalMs: 1.1 }]);
    expect(stepProblems([sample], ["Map markers read"])).toEqual([]);
    const slow = `${P}Map markers read (game thread only): 12 markers, 0 dropped in 3 steps, longest step 2.05 ms, total 3.00 ms`;
    expect(stepProblems([slow], ["Map markers read"]).length).toBeGreaterThan(0);
  });
  it("parses the shared logistics line and holds it to the ceiling", () => {
    const sample = `${P}Logistics read (game thread only): layers rails=1 conveyors=0, 5 buildables, 2 tracks, 0 belts, 0 lifts, 0 pipes, 0 open ends, 0 ownerless ends in 3 steps, longest step 1.20 ms, total 3.40 ms`;
    expect(stepCosts([sample])).toEqual([{ reader: "Logistics read", longestStepMs: 1.2, totalMs: 3.4 }]);
    expect(stepProblems([sample], ["Logistics read"])).toEqual([]);
    const over = stepProblems([sample.replace("1.20 ms", "2.05 ms")], ["Logistics read"]);
    expect(over).toHaveLength(1);
    expect(over[0]).toMatch(/Logistics read/);
  });
  it("parses the S4-A logistics line with powerLines and the wires counts, still ending in the step cost", () => {
    const s3 = `${P}Logistics read (game thread only): layers rails=1 conveyors=1, 900 buildables, 12 tracks, 340 belts, 6 lifts, 80 pipes, 14 open ends, 3 ownerless ends in 40 steps, longest step 1.20 ms, total 30.10 ms, expected tracks 12, dropped 0, over cap 0`;
    const s4 = `${P}Logistics read (game thread only): layers rails=1 conveyors=1 powerLines=1, 900 buildables, 12 tracks, 340 belts, 6 lifts, 80 pipes, 14 open ends, 3 ownerless ends, wires 843, expected wires 850 in 40 steps, longest step 1.30 ms, total 31.20 ms, expected tracks 12, dropped 0, over cap 0`;
    // S3 evidence (no powerLines= group) still parses, and says nothing about the new layer.
    expect(logisticsLayers([s3])).toEqual({ rails: 1, conveyors: 1 });
    expect(logisticsRead([s3])).toMatchObject({ rails: 1, conveyors: 1, buildables: 900, ownerlessEnds: 3 });
    expect(logisticsRead([s3])).not.toHaveProperty("wires");
    expect(logisticsLineProblems([s3], "rails")).toEqual([]);
    expect(logisticsLineProblems([s3], "powerLines")).toEqual([`the mod's "Logistics read" line says powerLines=0, not 1: the pass did not serve the powerLines layer`]);
    // The new line yields powerLines: 1 and the wires count.
    expect(logisticsLayers([s4])).toEqual({ rails: 1, conveyors: 1, powerLines: 1 });
    expect(logisticsRead([s4])).toMatchObject({ rails: 1, conveyors: 1, powerLines: 1, buildables: 900, belts: 340, ownerlessEnds: 3, wires: 843 });
    expect(logisticsLineProblems([s4], "powerLines")).toEqual([]);
    expect(logisticsLineProblems([s4.replace("powerLines=1", "powerLines=0")], "powerLines")).toHaveLength(1);
    // The step regex still reads the cost off the end of the line.
    expect(stepCosts([s4])).toEqual([{ reader: "Logistics read", longestStepMs: 1.3, totalMs: 31.2 }]);
    expect(stepProblems([s4], ["Logistics read"])).toEqual([]);
    expect(stepProblems([s4.replace("1.30 ms", "2.05 ms")], ["Logistics read"])).toHaveLength(1);
  });
  it("parses one StepCost per matching line, including S1's scan line", () => {
    expect(stepCosts(LINES)).toEqual([
      { reader: "Factory read", longestStepMs: 0.3, totalMs: 12.5 },
      { reader: "Trains read", longestStepMs: 1.7, totalMs: 12.5 },
      { reader: "Train routes read", longestStepMs: 2.05, totalMs: 12.5 },
    ]);
    const scan = `${P}Resource node scan (game thread only): 180000 actors in 90 steps, 612 nodes, longest step 1.75 ms, total 41.50 ms`;
    expect(stepCosts([scan])).toEqual([{ reader: "Resource node scan", longestStepMs: 1.75, totalMs: 41.5 }]);
    expect(stepCosts([`${P}Agent is running`])).toEqual([]);
  });
  it("accepts a step at or under 2 ms and rejects one over", () => {
    expect(stepProblems([LINES[1]!], ["Trains read"])).toEqual([]);
    expect(stepProblems([line("Factory read", "2.00")], ["Factory read"])).toEqual([]);
    const over = stepProblems(LINES, ["Train routes read"]);
    expect(over).toHaveLength(1);
    expect(over[0]).toMatch(/Train routes read/);
    expect(over[0]).toMatch(/2\.05 ms exceeds the 2 ms ceiling/);
  });
  it("lists a required reader with no line", () => {
    const p = stepProblems([LINES[0]!], ["Factory read", "Trains read"]);
    expect(p).toHaveLength(1);
    expect(p[0]).toMatch(/no "Trains read \(game thread only\):" line/);
  });
  it("fails closed on a malformed value, never printing NaN", () => {
    for (const bad of [".", "1.2.3", "..5", "", "NaN"]) {
      const p = stepProblems([line("Factory read", bad)], ["Factory read"]);
      expect(p.length).toBeGreaterThan(0);
      expect(p.join(" ")).toMatch(/Factory read/);
      expect(p.join(" ")).not.toMatch(/NaN ms/);
      expect(stepProblems([line("Trains read", bad)], [])).not.toEqual([]);
    }
    expect(stepCosts([line("Factory read", "1.2.3")])).toEqual([]);
    expect(stepCosts([line("Factory read", "0.30")])).toHaveLength(1);
  });
  it("judges every logged line even when not required", () => {
    expect(stepProblems(LINES, [])).toHaveLength(1);
  });
  it("does not read the longest-step diagnostic line as a read line", () => {
    const names = ["Logistics", "Resource node", "Factory", "Trains", "Train routes", "Map markers", "Drone routes"];
    const lines = names.map((n) => `[2026.10.27-12.00.00:000][  0]LogSatisDash: ${n} read longest step: 9.71 ms over 1 elements, phase 2 to 2`);
    expect(stepCosts(lines)).toEqual([]);
    expect(scanCosts(lines)).toEqual([]);
    expect(stepProblems(lines, [])).toEqual([]);
  });
});

describe("the Readers line", () => {
  const S1 = `${P}Readers: players=native resourceNodes=native`;
  const S2 = `${P}Readers: players=native resourceNodes=native factory=native trains=bridge`;
  const S3Bridge = `${P}Readers: players=native resourceNodes=native factory=native trains=native rails=bridge`;
  const S3Native = `${P}Readers: players=native resourceNodes=native factory=native trains=native rails=native`;
  it("still parses the S1 two-field line, with factory and trains undefined", () => {
    expect(readerSources([S1])).toEqual({ players: "native", resourceNodes: "native" });
  });
  it("parses the S2 four-field line", () => {
    expect(readerSources([S2])).toEqual({ players: "native", resourceNodes: "native", factory: "native", trains: "bridge" });
  });
  it("judges factory and trains like players", () => {
    expect(factoryLogProblems([S2])).toEqual([]);
    expect(trainsLogProblems([S2])).toEqual([expect.stringMatching(/trains=bridge, not native/)]);
    expect(factoryLogProblems([S1])).toEqual([expect.stringMatching(/no factory= field/)]);
    expect(trainsLogProblems([])).toEqual([expect.stringMatching(/no "Readers:" line/)]);
  });
  it("parses the S3 five-field line and leaves rails undefined on the older lines", () => {
    expect(readerSources([S1, S2])).toEqual({ players: "native", resourceNodes: "native", factory: "native", trains: "bridge" });
    expect(readerSources([S3Bridge])).toEqual({ players: "native", resourceNodes: "native", factory: "native", trains: "native", rails: "bridge" });
    expect(readerSources([S3Native])?.rails).toBe("native");
  });
  it("judges rails like trains", () => {
    expect(railsLogProblems([S2])).toEqual([expect.stringMatching(/has no rails= field/)]);
    expect(railsLogProblems([S3Native])).toEqual([]);
    expect(railsLogProblems([S3Bridge])).toEqual([expect.stringMatching(/rails=bridge, not native/)]);
    expect(railsLogProblems([])).toEqual([expect.stringMatching(/no "Readers:" line/)]);
  });
  const S3BothBridge = `${P}Readers: players=native resourceNodes=native factory=native trains=native rails=native conveyors=bridge`;
  const S3BothNative = `${P}Readers: players=native resourceNodes=native factory=native trains=native rails=native conveyors=native`;
  it("parses the six-field line, and the five-field line still leaves conveyors undefined", () => {
    expect(readerSources([S3Native])).toEqual({ players: "native", resourceNodes: "native", factory: "native", trains: "native", rails: "native" });
    expect(readerSources([S3Native])?.conveyors).toBeUndefined();
    expect(readerSources([S3BothBridge])).toEqual({ players: "native", resourceNodes: "native", factory: "native", trains: "native", rails: "native", conveyors: "bridge" });
    expect(readerSources([S3BothNative])?.conveyors).toBe("native");
    expect(readerSources([S3Native, S3BothNative])?.conveyors).toBe("native");
  });
  it("judges conveyors like rails", () => {
    expect(conveyorsLogProblems([S3Native])).toEqual([expect.stringMatching(/has no conveyors= field/)]);
    expect(conveyorsLogProblems([S3BothNative])).toEqual([]);
    expect(conveyorsLogProblems([S3BothBridge])).toEqual([expect.stringMatching(/conveyors=bridge, not native/)]);
    expect(conveyorsLogProblems([])).toEqual([expect.stringMatching(/no "Readers:" line/)]);
  });
  const S4Native = `${S3BothNative} mapMarkers=native droneRoutes=native powerLines=native`;
  const S4Bridge = `${S3BothNative} mapMarkers=bridge droneRoutes=bridge powerLines=bridge`;
  it("parses the nine-field line; the six-field line leaves the S4-A fields undefined; a partial group is no line", () => {
    expect(readerSources([S3BothNative])?.mapMarkers).toBeUndefined();
    expect(readerSources([S3BothNative])?.droneRoutes).toBeUndefined();
    expect(readerSources([S3BothNative])?.powerLines).toBeUndefined();
    expect(readerSources([S4Native])).toEqual({ players: "native", resourceNodes: "native", factory: "native", trains: "native", rails: "native", conveyors: "native", mapMarkers: "native", droneRoutes: "native", powerLines: "native" });
    expect(readerSources([S4Bridge])?.droneRoutes).toBe("bridge");
    expect(readerSources([`${S3BothNative} mapMarkers=native`])).toBeNull();
    expect(readerSources([`${S3BothNative} mapMarkers=native droneRoutes=native`])).toBeNull();
  });
  it("judges mapMarkers, droneRoutes and powerLines like conveyors", () => {
    expect(mapMarkersLogProblems([S3BothNative])).toEqual([expect.stringMatching(/has no mapMarkers= field/)]);
    expect(droneRoutesLogProblems([S3BothNative])).toEqual([expect.stringMatching(/has no droneRoutes= field/)]);
    expect(powerLinesLogProblems([S3BothNative])).toEqual([expect.stringMatching(/has no powerLines= field/)]);
    expect(mapMarkersLogProblems([S4Native])).toEqual([]);
    expect(droneRoutesLogProblems([S4Native])).toEqual([]);
    expect(powerLinesLogProblems([S4Native])).toEqual([]);
    expect(mapMarkersLogProblems([S4Bridge])).toEqual([expect.stringMatching(/mapMarkers=bridge, not native/)]);
    expect(droneRoutesLogProblems([S4Bridge])).toEqual([expect.stringMatching(/droneRoutes=bridge, not native/)]);
    expect(powerLinesLogProblems([S4Bridge])).toEqual([expect.stringMatching(/powerLines=bridge, not native/)]);
    expect(powerLinesLogProblems([])).toEqual([expect.stringMatching(/no "Readers:" line/)]);
  });
  const S4BNative = `${S4Native} extractors=native generators=native stationRates=native storage=native`;
  const S4BBridge = `${S4Native} extractors=bridge generators=bridge stationRates=bridge storage=bridge`;
  it("parses the thirteen-field line; the nine-field line leaves the S4-B fields undefined; a partial group is no line", () => {
    for (const k of ["extractors", "generators", "stationRates", "storage"] as const) expect(readerSources([S4Native])?.[k]).toBeUndefined();
    expect(readerSources([S4BNative])).toEqual({ players: "native", resourceNodes: "native", factory: "native", trains: "native", rails: "native", conveyors: "native", mapMarkers: "native", droneRoutes: "native", powerLines: "native", extractors: "native", generators: "native", stationRates: "native", storage: "native" });
    expect(readerSources([S4BBridge])?.stationRates).toBe("bridge");
    expect(readerSources([`${S4Native} extractors=native`])).toBeNull();
    expect(readerSources([`${S4Native} extractors=native generators=native stationRates=native`])).toBeNull();
  });
  it("judges extractors, generators, stationRates and storage like conveyors", () => {
    const judges = [["extractors", extractorsLogProblems], ["generators", generatorsLogProblems], ["stationRates", stationRatesLogProblems], ["storage", storageLogProblems]] as const;
    for (const [key, judge] of judges) {
      expect(judge([S4Native])).toEqual([expect.stringMatching(new RegExp(`has no ${key}= field`))]);
      expect(judge([S4BNative])).toEqual([]);
      expect(judge([S4BBridge])).toEqual([expect.stringMatching(new RegExp(`${key}=bridge, not native`))]);
      expect(judge([])).toEqual([expect.stringMatching(/no "Readers:" line/)]);
    }
  });
});
