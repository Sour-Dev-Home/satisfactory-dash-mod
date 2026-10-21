import { describe, expect, it } from "vitest";
import type { CoreDeps } from "../src/core-run";
import { measureWindow, TICK_COST_SAMPLE_MS, TICK_COST_WINDOW_MS } from "../src/core-runs/tick-cost";
import { TICK_WARMUP_MS } from "../src/ticks";

const START = "2026-10-20T16:00:00.000Z";

/**
 * A fake clock that only the tick sampler moves (10 s per reading); deps.sleep just yields a macrotask, so the 30 s
 * sampler polls until the clock reaches its next slot.
 */
function fakeDeps(over: Partial<CoreDeps> = {}) {
  let t = Date.parse(START);
  const yieldTurn = () => new Promise<void>((r) => setImmediate(r));
  const deps = {
    now: () => new Date(t),
    sleep: yieldTurn,
    sampleTickWindow: async (opts: { warmupMs: number; durationMs: number; stopped?: () => boolean }) => {
      const end = t + opts.warmupMs + opts.durationMs;
      const samples = [];
      while (t < end && !opts.stopped?.()) {
        samples.push({ at: new Date(t).toISOString(), value: 22 });
        await yieldTurn();
        t += 10_000;
      }
      return { warmup: [], samples, errors: 0, median: 22 };
    },
    sampleOutbound: async () => ({ endpoints: ["127.0.0.1:13105", "203.0.113.7:443"], names: { "203.0.113.7": ["api.example.com"] }, dnsFailed: false }),
    processMemory: async () => ({ totalWorkingSetBytes: 3, processes: [{ name: "FactoryServer.exe", workingSetBytes: 1, peakWorkingSetBytes: 1 }, { name: "Game-Cmd.exe", workingSetBytes: 2, peakWorkingSetBytes: 2 }] }),
    gameState: async () => ({ running: true, session: "a1b2c3d4e5f6" }),
    ...over,
  } as unknown as CoreDeps;
  return deps;
}

describe("measureWindow", () => {
  it("samples outbound throughout and memory from the end of the warmup, every 30 s, with the per-process figures and the session", async () => {
    const m = await measureWindow(fakeDeps(), 100);
    expect(m.session).toBe("a1b2c3d4e5f6");
    expect(m.outbound).toEqual(["127.0.0.1:13105", "203.0.113.7:443"]);
    expect(m.outboundNames).toEqual({ "203.0.113.7": ["api.example.com"] });
    const slots = (TICK_WARMUP_MS + TICK_COST_WINDOW_MS) / TICK_COST_SAMPLE_MS;
    // One per 30 s slot from the start (the last slot may fall on the window's end), plus one at the end.
    expect(m.outboundSamples.failed).toBe(0);
    expect(m.outboundSamples.ok).toBeGreaterThanOrEqual(slots + 1);
    expect(m.outboundSamples.ok).toBeLessThanOrEqual(slots + 2);
    const inWindow = TICK_COST_WINDOW_MS / TICK_COST_SAMPLE_MS;
    expect(m.memory.length).toBeGreaterThanOrEqual(inWindow + 1);
    expect(m.memory.length).toBeLessThanOrEqual(inWindow + 2);
    expect(Date.parse(m.memory[0]!.at)).toBeGreaterThanOrEqual(Date.parse(START) + TICK_WARMUP_MS);
    expect(Date.parse(m.memory[0]!.at)).toBeLessThan(Date.parse(START) + TICK_WARMUP_MS + TICK_COST_SAMPLE_MS);
    expect(m.memory.at(-1)!.at).toBe(new Date(Date.parse(START) + TICK_WARMUP_MS + TICK_COST_WINDOW_MS).toISOString());
    expect(m.memory[0]!.processes).toHaveLength(2);
    expect(m.memory[0]!.workingSetBytes).toBe(3);
  });

  it("a failed outbound read is a failed sample, and a failed session read is null", async () => {
    const m = await measureWindow(fakeDeps({ sampleOutbound: async () => Promise.reject(new Error("CIM failed")), gameState: async () => null }), 100);
    expect(m.outboundSamples.ok).toBe(0);
    expect(m.outboundSamples.failed).toBeGreaterThan(0);
    expect(m.session).toBeNull();
  });

  it("records no session when no game runs, so two empty sessions can't look like the same save", async () => {
    const m = await measureWindow(fakeDeps({ gameState: async () => ({ running: false, session: "da39a3ee5e6b" }) }), 100);
    expect(m.session).toBeNull();
  });

  it("stops and rejects when the server exits, instead of measuring on (review M1)", async () => {
    let ticksStopped = false;
    const deps = fakeDeps();
    const inner = deps.sampleTickWindow!;
    deps.sampleTickWindow = async (opts) => {
      const r = await inner(opts);
      ticksStopped = opts.stopped?.() ?? false;
      return r;
    };
    let exit!: () => void;
    const exited = new Promise<void>((r) => (exit = r));
    setTimeout(() => exit(), 0);
    await expect(measureWindow(deps, 100, exited)).rejects.toThrow(/exited/);
    expect(ticksStopped).toBe(true);
  });
});
