import { afterEach, beforeEach, describe, expect, it, type MockInstance, vi } from "vitest";
import type { Manual } from "../../runs/manual/pass";
import type { CoreContext } from "../src/core-run";
import { manual, manualMinutes, STATE_TIMEOUT_MS, trimBelts, trimPipes } from "../src/core-runs/manual";

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
  let log: MockInstance<typeof console.log>;
  beforeEach(() => {
    vi.stubEnv("MANUAL_MINUTES", "2");
    log = vi.spyOn(console, "log").mockImplementation(() => {});
  });
  afterEach(() => {
    vi.unstubAllEnvs();
    vi.unstubAllGlobals();
    vi.useRealTimers();
    log.mockRestore();
  });

  function context(over: { gameState?: () => Promise<{ running: boolean; session: string } | null>; exited?: Promise<unknown>; frmBaseUrl?: string } = {}) {
    let t = Date.parse("2026-10-21T10:00:00.000Z");
    const calls: string[] = [];
    const ctx = {
      captures: {} as Record<string, unknown>,
      proc: { exited: over.exited ?? new Promise(() => {}) },
      rememberAutoPause: (v: boolean) => void calls.push(`remember ${v}`),
      deps: {
        config: { frmBaseUrl: over.frmBaseUrl },
        now: () => new Date(t),
        sleep: async (ms: number) => void (t += ms),
        loadWorld: async () => void calls.push("load"),
        waitHealthy: async () => void calls.push("healthy"),
        readAutoPause: async () => true,
        setAutoPause: async (v: boolean) => void calls.push(`set ${v}`),
        gameState: over.gameState ?? (async () => ({ running: true, session: "s" })),
      },
    };
    return { ctx, calls, capture: () => ctx.captures["manual.json"] as Manual, drive: () => manual.drive(ctx as unknown as CoreContext) };
  }

  it("loads the world, turns auto-pause off, samples every 30 s for the window and records the capture", async () => {
    const h = context();
    await h.drive();
    expect(h.calls).toEqual(["load", "healthy", "remember true", "set false"]);
    const c = h.capture();
    expect(c.windowMs).toBe(120_000);
    expect(c.samples).toHaveLength(4);
    expect(c.samples.every((s) => s.answered)).toBe(true);
    expect(JSON.stringify(c.frm)).toMatch(/frmBaseUrl is not set/);
    expect(log.mock.calls.map((x) => x[0])).toEqual(["manual session: 2 min left", "manual session: 1 min left"]);
  });

  it("counts an answer saying the game isn't running, a failed call and no answer as unanswered (review of 93a6886)", async () => {
    const stuck = context({ gameState: async () => ({ running: false, session: "s" }) });
    await stuck.drive();
    expect(stuck.capture().samples.map((s) => s.answered)).toEqual([false, false, false, false]);
    const failing = context({ gameState: async () => { throw new Error("refused"); } });
    await failing.drive();
    expect(failing.capture().samples.every((s) => !s.answered)).toBe(true);

    vi.useFakeTimers({ toFake: ["setTimeout", "clearTimeout"] });
    const hung = context({ gameState: () => new Promise(() => {}) });
    let done = false;
    const run = hung.drive().finally(() => void (done = true));
    while (!done) await vi.advanceTimersByTimeAsync(STATE_TIMEOUT_MS);
    await run;
    expect(hung.capture().samples.map((s) => s.answered)).toEqual([false, false, false, false]);
  });

  it("throws when the server exits mid-session, without padding the samples", async () => {
    let exit: () => void = () => {};
    const exited = new Promise<void>((r) => void (exit = r));
    let n = 0;
    const h = context({ exited, gameState: async () => (++n === 2 && exit(), { running: true, session: "s" }) });
    await expect(h.drive()).rejects.toThrow(/server exited during the manual session/);
    expect(h.capture().samples).toHaveLength(2);
    expect(h.capture().frm.end).toEqual({ error: "not read" });
  });

  it("records FRM's trimmed belts and pipes at the start and the end, and a failed read as its error", async () => {
    const fetchMock = vi.fn(async (url: URL) =>
      url.pathname === "/getBelts"
        ? new Response(JSON.stringify([{ ID: "b1", ItemsPerMinute: 60, location: { x: 1 } }]))
        : new Response(JSON.stringify([{ ID: "p1", Speed: 2.5, Fluid: "Water" }])));
    vi.stubGlobal("fetch", fetchMock);
    const h = context({ frmBaseUrl: "http://127.0.0.1:18081" });
    await h.drive();
    const read = { belts: [{ ID: "b1", ItemsPerMinute: 60 }], pipes: [{ ID: "p1", Speed: 2.5 }] };
    expect(h.capture().frm).toEqual({ start: read, end: read });

    vi.stubGlobal("fetch", vi.fn(async () => new Response("no", { status: 503 })));
    const bad = context({ frmBaseUrl: "http://127.0.0.1:18081" });
    await bad.drive();
    expect(bad.capture().frm.start).toEqual({ error: expect.stringMatching(/returned 503/) });
  });
});
