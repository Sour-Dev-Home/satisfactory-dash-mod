import { describe, expect, it } from "vitest";
import { evaluate, MEMORY_GROWTH_LIMIT_MB, MEMORY_SIZE_LIMIT_MB, type MemoryPoint, type TickCost } from "../../runs/tick-cost/pass";
import type { CoreEvidence } from "../src/core-run";
import type { TickSeries } from "../src/ticks";

const T0 = "2026-10-20T16:00:00.000Z";
const at = (s: number) => new Date(Date.parse(T0) + s * 1000).toISOString();
const MB = 1024 * 1024;
const series = (values: number[]): TickSeries => ({
  warmup: [], samples: values.map((value, i) => ({ at: at(i * 10), value })), errors: 0, median: values.length ? [...values].sort((a, b) => a - b)[Math.floor(values.length / 2)]! : null,
});
/** Working sets in MB, one every 30 s. */
const mem = (mbs: Array<number | null>): MemoryPoint[] => mbs.map((m, i) => ({ at: at(i * 30), workingSetBytes: m === null ? null : m * MB }));
const RECORDER = "127.0.0.1:13105";

function capture(over: Partial<TickCost> = {}): TickCost {
  return {
    baseline: series([22, 22, 22, 22]),
    withMod: series([22, 21.9, 22, 22]),
    outboundBaseline: ["203.0.113.7:443"],
    outboundWithMod: ["203.0.113.7:443", RECORDER],
    outboundSamples: { baseline: { ok: 21, failed: 0, dnsFailed: 0 }, withMod: { ok: 21, failed: 0, dnsFailed: 0 } },
    memoryBaseline: mem([6000, 6005, 6010]),
    memoryWithMod: mem([6040, 6050, 6060]),
    recorder: RECORDER,
    circuitGroups: 3,
    sessionBaseline: "a1b2c3d4e5f6",
    sessionWithMod: "a1b2c3d4e5f6",
    outboundNamesBaseline: { "203.0.113.7": ["api.example.com"] },
    outboundNamesWithMod: { "203.0.113.7": ["api.example.com"] },
    ...over,
  };
}
const ev = (c: TickCost | undefined): CoreEvidence => ({
  lines: [], errorLines: [], leaks: { code: false, secret: false, bearer: false }, captures: c ? { "tick-cost.json": c } : {},
});
const reasons = (over: Partial<TickCost>) => evaluate(ev(capture(over))).reasons;

describe("tick-cost pass", () => {
  it("declares the owner's memory limits", () => {
    expect(MEMORY_SIZE_LIMIT_MB).toBe(100);
    expect(MEMORY_GROWTH_LIMIT_MB).toBe(25);
  });

  it("passes a clean capture and states the medians, both memory deltas in MB and the circuit groups", () => {
    const v = evaluate(ev(capture()));
    expect(v.pass).toBe(true);
    expect(v.reasons[0]).toContain("tick median 22 without the mod, 22 with it");
    expect(v.reasons[0]).toContain("working set at the end 50.0 MB above the baseline's (limit 100)");
    expect(v.reasons[0]).toContain("grew 20.0 MB vs 10.0 MB, 10.0 MB more (limit 25)");
    expect(v.reasons[0]).toContain("3 circuit group(s)");
    expect(v.reasons[0]).toContain("1 new endpoint(s), all the recorder's");
  });

  it("fails without a capture", () => {
    expect(evaluate(ev(undefined))).toEqual({ pass: false, reasons: ["no tick-cost capture"] });
  });

  describe("C4", () => {
    it("fails when the with-mod median is below 95% of the baseline's", () => {
      expect(reasons({ baseline: series([22, 22, 22]), withMod: series([20.8, 20.8, 20.8]) })).toContain("tick median dropped from 22 without the mod to 20.8 with it, more than 5%");
      expect(evaluate(ev(capture({ baseline: series([22, 22, 22]), withMod: series([21, 21, 21]) }))).pass).toBe(true);
    });
    it("fails when either window has no samples", () => {
      expect(reasons({ baseline: series([]) })).toContain("the baseline tick window has no samples");
      expect(reasons({ withMod: null })).toContain("the with-mod tick window has no samples");
    });
  });

  describe("S12", () => {
    // Run 39538061390's shape: the game's public-IP lookup (api.ipify.org) on rotating Cloudflare addresses.
    const RUN_RECORDER = "127.0.0.1:65118";
    const run39538061390: Partial<TickCost> = {
      recorder: RUN_RECORDER,
      outboundBaseline: ["104.26.12.205:443"],
      outboundWithMod: [RUN_RECORDER, "172.67.74.152:443"],
      outboundNamesBaseline: { "104.26.12.205": ["api.ipify.org"] },
      outboundNamesWithMod: { "127.0.0.1": ["kubernetes.docker.internal"], "172.67.74.152": ["api.ipify.org"] },
      outboundSamples: { baseline: { ok: 28, failed: 0, dnsFailed: 0 }, withMod: { ok: 28, failed: 0, dnsFailed: 0 } },
    };

    it("passes run 39538061390's shape, matching the new address by its DNS name (owner, 2026-10-21), and says so", () => {
      const v = evaluate(ev(capture(run39538061390)));
      expect(v.pass).toBe(true);
      expect(v.reasons[0]).toContain("matched by DNS name: 172.67.74.152:443 as api.ipify.org (baseline 104.26.12.205:443)");
    });
    it("fails on a new address whose name the baseline didn't contact", () => {
      expect(reasons({ ...run39538061390, outboundNamesWithMod: { "172.67.74.152": ["telemetry.example.com"] } }))
        .toContain(`S12: the mod's boot contacted 172.67.74.152:443 (telemetry.example.com), which the baseline didn't, isn't the recorder (${RUN_RECORDER}), and none of its names was contacted in the baseline`);
    });
    it("fails on a new address with no DNS name", () => {
      expect(reasons({ outboundWithMod: ["203.0.113.7:443", RECORDER, "198.51.100.4:443"] }))
        .toContain("S12: the mod's boot contacted 198.51.100.4:443, which the baseline didn't, isn't the recorder (127.0.0.1:13105), and has no DNS name");
    });
    it("fails when a DNS read failed in either boot", () => {
      expect(reasons({ ...run39538061390, outboundSamples: { baseline: { ok: 28, failed: 0, dnsFailed: 0 }, withMod: { ok: 28, failed: 0, dnsFailed: 1 } } }))
        .toContain("S12: 1 DNS read(s) of the with-mod window failed");
      expect(reasons({ outboundSamples: { baseline: { ok: 21, failed: 0, dnsFailed: 2 }, withMod: { ok: 21, failed: 0, dnsFailed: 0 } } }))
        .toContain("S12: 2 DNS read(s) of the baseline window failed");
    });
    it("fails when the name is shared only within the with-mod boot (baseline names come from the baseline boot only)", () => {
      const v = reasons({
        ...run39538061390,
        outboundBaseline: ["104.26.12.205:443"],
        outboundNamesBaseline: {},
        outboundWithMod: [RUN_RECORDER, "172.67.74.152:443", "104.21.0.1:443"],
        outboundNamesWithMod: { "172.67.74.152": ["api.ipify.org"], "104.21.0.1": ["api.ipify.org"], "104.26.12.205": ["api.ipify.org"] },
      });
      expect(v).toContain(`S12: the mod's boot contacted 172.67.74.152:443 (api.ipify.org), which the baseline didn't, isn't the recorder (${RUN_RECORDER}), and none of its names was contacted in the baseline`);
    });
    it("a baseline name counts only for an endpoint the baseline contacted, on the same port", () => {
      expect(reasons({ ...run39538061390, outboundBaseline: ["198.51.100.9:443"], outboundNamesBaseline: { "104.26.12.205": ["api.ipify.org"] } }))
        .toContain(`S12: the mod's boot contacted 172.67.74.152:443 (api.ipify.org), which the baseline didn't, isn't the recorder (${RUN_RECORDER}), and none of its names was contacted in the baseline`);
      expect(reasons({ ...run39538061390, outboundWithMod: [RUN_RECORDER, "172.67.74.152:80"] }))
        .toContain(`S12: the mod's boot contacted 172.67.74.152:80 (api.ipify.org), which the baseline didn't, isn't the recorder (${RUN_RECORDER}), and none of its names was contacted in the baseline`);
    });
    it("fails when the sampler never saw the mod's connection to the recorder (review I1: no vacuous pass)", () => {
      expect(reasons({ outboundWithMod: ["203.0.113.7:443"] })).toContain("S12: the sampler never saw the mod's connection to the recorder (127.0.0.1:13105)");
      expect(reasons({ outboundBaseline: [], outboundWithMod: [] })).toContain("S12: the sampler never saw the mod's connection to the recorder (127.0.0.1:13105)");
      expect(reasons({ ...run39538061390, outboundWithMod: ["172.67.74.152:443"] })).toContain(`S12: the sampler never saw the mod's connection to the recorder (${RUN_RECORDER})`);
    });
    it("fails when the recorder is not a loopback host:port", () => {
      expect(reasons({ recorder: "" })).toContain("S12: the recorder's endpoint is not 127.0.0.1:<port> ()");
      expect(reasons({ recorder: "10.0.0.1:13105" })).toContain("S12: the recorder's endpoint is not 127.0.0.1:<port> (10.0.0.1:13105)");
    });
    it("fails closed on a failed outbound sample or fewer than 2 per window", () => {
      expect(reasons({ outboundSamples: { baseline: { ok: 21, failed: 1, dnsFailed: 0 }, withMod: { ok: 21, failed: 0, dnsFailed: 0 } } })).toContain("S12: 1 outbound sample(s) of the baseline window failed");
      expect(reasons({ outboundSamples: { baseline: { ok: 21, failed: 0, dnsFailed: 0 }, withMod: { ok: 1, failed: 0, dnsFailed: 0 } } })).toContain("S12: the with-mod window has 1 outbound sample(s), fewer than 2");
    });
  });

  describe("memory (owner, 2026-10-20)", () => {
    it("fails when the with-mod working set ends more than 100 MB above the baseline's", () => {
      expect(evaluate(ev(capture({ memoryWithMod: mem([6100, 6105, 6110] ) }))).pass).toBe(true);
      expect(reasons({ memoryWithMod: mem([6101, 6106, 6111]) })).toContain("memory: the with-mod working set ended 101.0 MB above the baseline's, more than 100 MB");
    });
    it("fails when it grows more than 25 MB beyond the baseline's growth", () => {
      expect(evaluate(ev(capture({ memoryWithMod: mem([6000, 6020, 6035]) }))).pass).toBe(true);
      expect(reasons({ memoryWithMod: mem([6000, 6020, 6036]) })).toContain("memory: the with-mod working set grew 36.0 MB vs the baseline's 10.0 MB, 26.0 MB more, more than 25 MB");
    });
    it("a baseline that shrinks makes the with-mod growth count against it", () => {
      expect(reasons({ memoryBaseline: mem([6010, 6000]), memoryWithMod: mem([6000, 6016]) })).toContain("memory: the with-mod working set grew 16.0 MB vs the baseline's -10.0 MB, 26.0 MB more, more than 25 MB");
    });
    it("fails closed on a missing sample or fewer than 2 per window", () => {
      expect(reasons({ memoryBaseline: mem([6000, null, 6010]) })).toContain("memory: 1 sample(s) of the baseline window could not be read");
      expect(reasons({ memoryWithMod: mem([6040]) })).toContain("memory: the with-mod window has 1 sample(s), fewer than 2");
      expect(reasons({ memoryBaseline: [] })).toContain("memory: the baseline window has 0 sample(s), fewer than 2");
    });
  });

  describe("same save (controller ruling, review of 1ca0065)", () => {
    it("fails when the two boots ran different sessions", () => {
      expect(reasons({ sessionWithMod: "ffffffffffff" })).toContain("the two boots ran different saves (session a1b2c3d4e5f6 without the mod, ffffffffffff with it): the comparison is valid only on the same save");
    });
    it("fails when a boot's session was not recorded", () => {
      expect(reasons({ sessionBaseline: null })).toContain("the session of the baseline boot was not recorded");
      expect(reasons({ sessionWithMod: null })).toContain("the session of the with-mod boot was not recorded");
    });
    it("states the session in the pass reason", () => {
      expect(evaluate(ev(capture())).reasons[0]).toContain("same save in both boots (session a1b2c3d4e5f6)");
    });
  });

  it("keeps each memory point's per-process breakdown in the capture's type", () => {
    const point: MemoryPoint = { at: T0, workingSetBytes: 2, processes: [{ name: "FactoryServer.exe", workingSetBytes: 1, peakWorkingSetBytes: 1 }] };
    expect(evaluate(ev(capture({ memoryWithMod: [point, point], memoryBaseline: [point, point] }))).pass).toBe(true);
  });

  it("says when the circuit groups are unknown, without failing on it", () => {
    const v = evaluate(ev(capture({ circuitGroups: null })));
    expect(v.pass).toBe(true);
    expect(v.reasons[0]).toContain("circuit groups unknown");
  });
});
