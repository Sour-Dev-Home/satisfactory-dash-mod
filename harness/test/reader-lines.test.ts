import { describe, expect, it } from "vitest";
import { MAX_NATIVE_STEP_MS, STEP_LINE_NAMES, factoryLogProblems, readerSources, stepCosts, stepProblems, trainsLogProblems } from "../src/reader-lines";

const P = "[2026.10.26-12:00:05:123][  0]LogSatisDash: Display: ";
const line = (name: string, longest: string) => `${P}${name} (game thread only): 1000 items in 40 steps, longest step ${longest} ms, total 12.50 ms`;
const LINES = [line("Factory read", "0.30"), line("Trains read", "1.70"), line("Train routes read", "2.05")];

describe("step costs", () => {
  it("names the four sliced readers and the ceiling", () => {
    expect(STEP_LINE_NAMES).toEqual(["Resource node scan", "Factory read", "Trains read", "Train routes read"]);
    expect(MAX_NATIVE_STEP_MS).toBe(2.0);
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
});

describe("the Readers line", () => {
  const S1 = `${P}Readers: players=native resourceNodes=native`;
  const S2 = `${P}Readers: players=native resourceNodes=native factory=native trains=bridge`;
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
});
