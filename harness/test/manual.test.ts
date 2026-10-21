import { describe, expect, it, vi } from "vitest";
import type { Manual } from "../../runs/manual/pass";
import type { CoreContext } from "../src/core-run";
import { manual, manualMinutes, trimBelts, trimPipes } from "../src/core-runs/manual";

describe("manualMinutes", () => {
  it("defaults to 45", () => {
    expect(manualMinutes(undefined)).toBe(45);
    expect(manualMinutes("")).toBe(45);
  });
  it("takes a positive whole number, at most the workflow's 90 minute timeout minus a margin", () => {
    expect(manualMinutes("10")).toBe(10);
    expect(() => manualMinutes("0")).toThrow(/MANUAL_MINUTES/);
    expect(() => manualMinutes("abc")).toThrow(/MANUAL_MINUTES/);
    expect(() => manualMinutes("1.5")).toThrow(/MANUAL_MINUTES/);
    expect(() => manualMinutes("80")).toThrow(/MANUAL_MINUTES/);
  });
});

describe("trimming FRM's lists", () => {
  it("keeps only ID, ClassName, ConnectedTo0, ConnectedTo1 and ItemsPerMinute for a belt, no location", () => {
    const out = trimBelts([{ ID: "b1", ClassName: "Build_ConveyorBeltMk1_C", ConnectedTo0: "x", ConnectedTo1: "y", ItemsPerMinute: 60, location: { x: 1 }, SplineData: [1], Length: 9 }]);
    expect(out).toEqual([{ ID: "b1", ClassName: "Build_ConveyorBeltMk1_C", ConnectedTo0: "x", ConnectedTo1: "y", ItemsPerMinute: 60 }]);
  });
  it("keeps only ID, ClassName, ConnectedTo0, ConnectedTo1 and Speed for a pipe", () => {
    const out = trimPipes([{ ID: "p1", ClassName: "Build_Pipeline_C", ConnectedTo0: "x", ConnectedTo1: "y", Speed: 2.5, location: { x: 1 }, Fluid: "Water" }]);
    expect(out).toEqual([{ ID: "p1", ClassName: "Build_Pipeline_C", ConnectedTo0: "x", ConnectedTo1: "y", Speed: 2.5 }]);
  });
  it("drops entries that are not objects and keeps absent fields absent", () => {
    expect(trimBelts([null, 3, { ID: "b" }])).toEqual([{ ID: "b" }]);
    expect(trimBelts("nope")).toEqual([]);
  });
});

describe("manual.drive", () => {
  it("loads the world, turns auto-pause off, samples every 30 s for the window and records the capture", async () => {
    vi.stubEnv("MANUAL_MINUTES", "2");
    const log = vi.spyOn(console, "log").mockImplementation(() => {});
    let t = Date.parse("2026-10-21T10:00:00.000Z");
    const calls: string[] = [];
    const ctx = {
      captures: {} as Record<string, unknown>,
      proc: { exited: new Promise(() => {}) },
      rememberAutoPause: (v: boolean) => void calls.push(`remember ${v}`),
      deps: {
        config: {},
        now: () => new Date(t),
        sleep: async (ms: number) => void (t += ms),
        loadWorld: async () => void calls.push("load"),
        waitHealthy: async () => void calls.push("healthy"),
        readAutoPause: async () => true,
        setAutoPause: async (v: boolean) => void calls.push(`set ${v}`),
        gameState: async () => ({ running: true, session: "s" }),
      },
    };
    await manual.drive(ctx as unknown as CoreContext);
    expect(calls).toEqual(["load", "healthy", "remember true", "set false"]);
    const c = ctx.captures["manual.json"] as Manual;
    expect(c.windowMs).toBe(120_000);
    expect(c.samples).toHaveLength(4);
    expect(c.samples.every((s) => s.answered)).toBe(true);
    expect(JSON.stringify(c.frm)).toMatch(/frmBaseUrl is not set/);
    expect(log.mock.calls.map((x) => x[0])).toEqual(["manual session: 2 min left", "manual session: 1 min left"]);
    vi.unstubAllEnvs();
    log.mockRestore();
  });
});
