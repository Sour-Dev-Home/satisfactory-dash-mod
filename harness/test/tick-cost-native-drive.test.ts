import { describe, expect, it } from "vitest";
import { CORE_RUN_IDS, NATIVE_SOURCES_S4B } from "../src/core-run";
import type { CoreContext, CoreDeps } from "../src/core-run";
import { CORE_RUNS } from "../src/core-runs";
import { tickCost } from "../src/core-runs/tick-cost";
import { tickCostNative } from "../src/core-runs/tick-cost-native";
import { TICK_WARMUP_MS } from "../src/ticks";

const START = Date.parse("2026-10-26T12:00:00.000Z");

/** Runs a spec's drive against fakes and returns the order of the steps and the captures. */
async function drive(spec: typeof tickCost, extraDeps: Partial<CoreDeps> = {}, baseline?: unknown) {
  const steps: string[] = [];
  const keys: (string | undefined)[] = [];
  let t = START;
  const yieldTurn = () => new Promise<void>((r) => setImmediate(r));
  const deps = {
    now: () => new Date(t),
    sleep: yieldTurn,
    config: { frmBaseUrl: "http://127.0.0.1:18081" },
    backendHost: async () => ({ relayUrl: "http://127.0.0.1:9", seedCode: async () => void steps.push("seedCode") }),
    loadWorld: async (key?: string) => void (steps.push("loadWorld"), keys.push(key)),
    waitHealthy: async () => void steps.push("waitHealthy"),
    readAutoPause: async () => true,
    setAutoPause: async () => void steps.push("autoPauseOff"),
    sampleTickWindow: async (o: { warmupMs: number; durationMs: number }) => {
      steps.push("measure");
      t += o.warmupMs + o.durationMs;
      return { warmup: [], samples: [{ at: new Date(t).toISOString(), value: 22 }], errors: 0, median: 22 };
    },
    ...extraDeps,
  } as unknown as CoreDeps;
  const captures: Record<string, unknown> = {};
  const ctx = {
    deps, captures, baseline, proc: { pid: 1, exited: new Promise(() => undefined) },
    writeConfig: () => void steps.push("enrol"),
    waitFor: async () => true,
    noteSecret: () => undefined,
    rememberAutoPause: () => undefined,
  } as unknown as CoreContext;
  await spec.drive(ctx);
  return { steps: steps.filter((s) => s !== "seedCode"), captures, keys };
}

describe("tick-cost-native: spec and drive", () => {
  it("is registered, in the run ids, and writes native sources into the mod's config", () => {
    expect(CORE_RUN_IDS).toContain("tick-cost-native");
    expect(CORE_RUNS["tick-cost-native"]).toBe(tickCostNative);
    expect(tickCostNative.modConfig).toEqual(NATIVE_SOURCES_S4B);
    expect(tickCostNative.baseline).toBe(true);
    expect(tickCost.modConfig).toBeUndefined();
  });
  it("loads the Test3 world (sprint3) in both boots; tick-cost keeps the default world", async () => {
    expect(tickCostNative.worldKey).toBe("sprint3");
    expect(tickCost.worldKey).toBeUndefined();
    expect((await drive(tickCostNative)).keys).toEqual(["sprint3"]);
    expect((await drive(tickCost)).keys).toEqual([undefined]);
  });
  it("enrols after the world is up and auto-pause is off, right before the measurement", async () => {
    const { steps } = await drive(tickCostNative);
    expect(steps).toEqual(["loadWorld", "waitHealthy", "autoPauseOff", "enrol", "measure"]);
  });
  it("leaves tick-cost's own order unchanged: enrol first", async () => {
    const { steps } = await drive(tickCost);
    expect(steps).toEqual(["enrol", "loadWorld", "waitHealthy", "autoPauseOff", "measure"]);
  });
  it("records the measured window (start to end) so the judge can add the warmup", async () => {
    const { captures } = await drive(tickCostNative);
    const parts = captures["snapshot-parts.json"] as { measured: { from: string; to: string }; snapshots: unknown[] };
    expect(parts.measured.from).toBe(new Date(START).toISOString());
    expect(Date.parse(parts.measured.to) - Date.parse(parts.measured.from)).toBe(TICK_WARMUP_MS + 10 * 60_000);
    expect(parts.snapshots).toEqual([]);
  });

  it("forward-resolves the names the baseline's DNS cache gave to addresses it contacted, with a failed lookup as [] (owner, 2026-10-27)", async () => {
    const asked: string[] = [];
    const resolveHost = async (name: string) => {
      asked.push(name);
      if (name === "down.example.com") throw new Error("ENOTFOUND");
      return ["104.26.12.205", "104.26.13.205", "104.26.12.205"];
    };
    const base = {
      outbound: ["104.26.13.205:443"],
      outboundNames: { "104.26.13.205": ["api.ipify.org", "down.example.com"], "198.51.100.1": ["never-contacted.example.com"] },
    };
    const { captures } = await drive(tickCostNative, { resolveHost }, base);
    expect((captures["tick-cost.json"] as { outboundForwardWithMod: unknown }).outboundForwardWithMod).toEqual({
      "api.ipify.org": ["104.26.12.205", "104.26.13.205"],
      "down.example.com": [],
    });
    expect(asked).toEqual(["api.ipify.org", "down.example.com"]);
  });
  it("records no forward set when the deps have no resolver", async () => {
    const { captures } = await drive(tickCostNative);
    expect((captures["tick-cost.json"] as { outboundForwardWithMod?: unknown }).outboundForwardWithMod).toBeUndefined();
  });

  it("records FRM's endpoint from the runner config for S12 (owner, 2026-10-26)", async () => {
    const { captures } = await drive(tickCostNative);
    expect((captures["tick-cost.json"] as { frm: string | null }).frm).toBe("127.0.0.1:18081");
  });
});
