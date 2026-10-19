import { describe, expect, it } from "vitest";
import type { ServerApi } from "../src/server-api";
import { median, readTickRate, sampleTicks } from "../src/ticks";

// Shaped like satisfactory-dash raw-sources/captured-responses/vanilla-QueryServerState-2026-09-22-01-running.json.
const queryState = (tick: number) => ({ data: { serverGameState: { numConnectedPlayers: 0, techTier: 6, averageTickRate: tick } } });
const api = (status: number, body: unknown): ServerApi => ({ call: async () => ({ status, body }) });

describe("readTickRate", () => {
  it("reads serverGameState.averageTickRate", async () => {
    expect(await readTickRate(api(200, queryState(29.7)))).toBe(29.7);
  });

  it("throws on a non-200 and on a body without the field", async () => {
    await expect(readTickRate(api(401, {}))).rejects.toThrow(/401/);
    await expect(readTickRate(api(200, { data: {} }))).rejects.toThrow();
  });
});

describe("median", () => {
  it("handles odd, even and empty", () => {
    expect(median([3, 1, 2])).toBe(2);
    expect(median([4, 1, 3, 2])).toBe(2.5);
    expect(median([])).toBeNull();
  });
});

describe("sampleTicks", () => {
  it("samples every interval for the window, counting failed reads", async () => {
    let t = 0;
    let n = 0;
    const read = async () => {
      n++;
      if (n === 3) throw new Error("busy");
      return 30;
    };
    const series = await sampleTicks(read, { durationMs: 60_000, intervalMs: 10_000, now: () => t, sleep: async (ms) => void (t += ms) });
    expect(series.samples).toHaveLength(5);
    expect(series.errors).toBe(1);
    expect(series.median).toBe(30);
    expect(series.samples[0]!.at).toBe("1970-01-01T00:00:00.000Z");
  });
});
