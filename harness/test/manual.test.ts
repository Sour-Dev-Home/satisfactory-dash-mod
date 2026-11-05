import { afterEach, beforeEach, describe, expect, it, type MockInstance, vi } from "vitest";
import type { Manual } from "../../runs/manual/pass";
import type { CoreContext } from "../src/core-run";
import { manual, manualMinutes, STATE_TIMEOUT_MS } from "../src/core-runs/manual";

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

  function context(over: { gameState?: () => Promise<{ running: boolean; session: string } | null>; exited?: Promise<unknown> } = {}) {
    let t = Date.parse("2026-10-21T10:00:00.000Z");
    const calls: string[] = [];
    const ctx = {
      captures: {} as Record<string, unknown>,
      proc: { exited: over.exited ?? new Promise(() => {}) },
      rememberAutoPause: (v: boolean) => void calls.push(`remember ${v}`),
      deps: {
        config: {},
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
  });

  it("makes no outbound request of its own", async () => {
    const fetchMock = vi.fn(async () => new Response("{}"));
    vi.stubGlobal("fetch", fetchMock);
    await context().drive();
    expect(fetchMock).not.toHaveBeenCalled();
  });
});
