import { describe, expect, it } from "vitest";
import { NATIVE_SOURCES } from "../src/core-run";
import type { CoreEvidence } from "../src/core-run";
import { TICK_WARMUP_MS, type TickSeries } from "../src/ticks";
import * as Native from "../../runs/tick-cost-native/pass";
import * as Base from "../../runs/tick-cost/pass";

const FROM = Date.parse("2026-10-26T12:00:00.000Z");
const iso = (ms: number) => new Date(ms).toISOString();

// The same clean capture harness/test/tick-cost-pass.test.ts uses (that file's `capture()`), so Base.evaluate passes.
const MB = 1024 * 1024;
const at = (s: number) => new Date(FROM + s * 1000).toISOString();
const series = (values: number[]): TickSeries => ({
  warmup: [], samples: values.map((value, i) => ({ at: at(i * 10), value })), errors: 0, median: [...values].sort((a, b) => a - b)[Math.floor(values.length / 2)]!,
});
const mem = (mbs: number[]): Base.MemoryPoint[] => mbs.map((m, i) => ({ at: at(i * 30), workingSetBytes: m * MB }));
const RECORDER = "127.0.0.1:13105";
const baseCapture = (): Base.TickCost => ({
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
});

function evidence(over: { sources?: unknown; parts?: unknown; posts?: unknown[] } = {}): CoreEvidence {
  return {
    lines: [], errorLines: [], leaks: { code: false, secret: false, bearer: false },
    captures: {
      "tick-cost.json": baseCapture(),
      "mod-config.json": "sources" in over ? over.sources : NATIVE_SOURCES,
      "snapshot-parts.json": over.parts ?? {
        measured: { from: iso(FROM), to: iso(FROM + TICK_WARMUP_MS + 10 * 60_000) },
        snapshots: [{ at: iso(FROM + TICK_WARMUP_MS + 60_000), playersAvailable: true }],
      },
      "recorder.json": over.posts ?? [{ at: iso(FROM + TICK_WARMUP_MS + 5 * 60_000), status: 200, agent: "mod", world: { layer: "resourceNodes", items: 600, bytes: 60000 } }],
    },
  };
}

describe("tick-cost-native judge", () => {
  it("passes when the existing limits pass and the native readers were live inside the measured window", () => {
    expect(Native.evaluate(evidence()).pass).toBe(true);
  });
  it("keeps the existing tick-cost verdict: a memory blow-up fails it", () => {
    const e = evidence();
    (e.captures["tick-cost.json"] as Base.TickCost).memoryWithMod = mem([6300, 6400, 6500]); // 490 MB above the baseline's end
    const v = Native.evaluate(e);
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toMatch(/memory/);
  });
  it("fails when the sources were not native", () => {
    expect(Native.evaluate(evidence({ sources: { playersSource: "bridge", resourceNodesSource: "native" } })).pass).toBe(false);
    expect(Native.evaluate(evidence({ sources: undefined })).pass).toBe(false);
  });
  it("fails when no snapshot in the window carried players", () => {
    const parts = { measured: { from: iso(FROM), to: iso(FROM + TICK_WARMUP_MS + 10 * 60_000) }, snapshots: [{ at: iso(FROM + TICK_WARMUP_MS + 60_000), playersAvailable: false }] };
    expect(Native.evaluate(evidence({ parts })).reasons.join("\n")).toMatch(/players/);
  });
  it("fails when the resourceNodes scan fell outside the measured window", () => {
    const early = [{ at: iso(FROM + 60_000), status: 200, agent: "mod", world: { layer: "resourceNodes", items: 600, bytes: 1 } }];
    const v = Native.evaluate(evidence({ posts: early }));
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toMatch(/resourceNodes/);
  });
  it("fails when the measured window was not recorded", () => {
    const v = Native.evaluate(evidence({ parts: { measured: null, snapshots: [] } }));
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toMatch(/window was not recorded/);
  });
  it("does not count a refused post, a Node agent's post or one after the window ended", () => {
    const w = { layer: "resourceNodes", items: 600, bytes: 1 };
    const t = iso(FROM + TICK_WARMUP_MS + 5 * 60_000);
    const posts = [
      { at: t, status: 503, agent: "mod", world: w },
      { at: t, status: 200, agent: "node", world: w },
      { at: iso(FROM + TICK_WARMUP_MS + 11 * 60_000), status: 200, agent: "mod", world: w },
    ];
    expect(Native.evaluate(evidence({ posts })).pass).toBe(false);
  });
});
