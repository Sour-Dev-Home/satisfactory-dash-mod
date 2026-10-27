import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { snapshotParts } from "../src/core-runs/tick-cost";
import { NATIVE_SOURCES, NATIVE_SOURCES_S2, NATIVE_SOURCES_S3, NATIVE_SOURCES_S4 } from "../src/core-run";
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

// The mod's log lines as the evidence carries them (e.lines): native readers, and a bridge-mode run that has neither line.
const PREFIX = "[2026.10.26-12:00:05:123][  0]LogSatisDash: Display: ";
const NATIVE_LINES = [
  `${PREFIX}Readers: players=native resourceNodes=native`,
  `${PREFIX}Resource node scan (game thread only): 180000 actors in 90 steps, 612 nodes, longest step 1.75 ms, total 41.50 ms`,
];
const BRIDGE_LINES = [`${PREFIX}Readers: players=bridge resourceNodes=bridge`, `${PREFIX}Agent is running`];

function evidence(over: { sources?: unknown; parts?: unknown; posts?: unknown[]; lines?: string[] } = {}): CoreEvidence {
  return {
    lines: "lines" in over ? (over.lines as string[]) : NATIVE_LINES, errorLines: [], leaks: { code: false, secret: false, bearer: false },
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
  it("fails a bridge-mode run whose output looks native: the Readers line says bridge and no scan line was logged", () => {
    const v = Native.evaluate(evidence({ lines: BRIDGE_LINES }));
    expect(v.pass).toBe(false);
    const text = v.reasons.join("\n");
    expect(text).toMatch(/players=bridge/);
    expect(text).toMatch(/resourceNodes=bridge/);
    expect(text).toMatch(/Resource node scan \(game thread only\):/);
  });
  it("names the missing Readers line when the log has none, and a missing scan line when only that is absent", () => {
    expect(Native.evaluate(evidence({ lines: [] })).reasons.join("\n")).toMatch(/no "Readers:" line/);
    const noScan = Native.evaluate(evidence({ lines: [NATIVE_LINES[0]!] }));
    expect(noScan.pass).toBe(false);
    expect(noScan.reasons.join("\n")).toMatch(/no "Resource node scan \(game thread only\):" line/);
    expect(noScan.reasons.join("\n")).not.toMatch(/Readers:/);
  });
  it("records the scan's longest step and total time in the reasons; the total is not judged", () => {
    const slow = [NATIVE_LINES[0]!, `${PREFIX}Resource node scan (game thread only): 180000 actors in 90 steps, 612 nodes, longest step 1.99 ms, total 9000.50 ms`];
    const v = Native.evaluate(evidence({ lines: slow }));
    expect(v.pass).toBe(true);
    expect(v.reasons.join("\n")).toMatch(/longest single step 1\.99 ms; longest total 9000\.5 ms/);
    expect(Native.evaluate(evidence()).reasons.join("\n")).toMatch(/longest single step 1\.75 ms; longest total 41\.5 ms/);
  });
  it("maps recorded snapshots to parts: players.available true counts, a snapshot without players does not", () => {
    const fixture = (name: string) => JSON.parse(readFileSync(fileURLToPath(new URL(`../../mod/SatisDashAgent/Source/SatisDashCoreTests/Fixtures/${name}`, import.meta.url)), "utf8"));
    const withPlayers = fixture("snapshot-frm.json");
    expect(withPlayers.players.available).toBe(true);
    const parts = snapshotParts({ from: iso(FROM), to: iso(FROM + 1000) }, [
      { at: iso(FROM + 1), snapshot: withPlayers },
      { at: iso(FROM + 2), snapshot: fixture("snapshot-world.json") },
      { at: iso(FROM + 3) },
    ]);
    expect(parts.snapshots.map((s) => s.playersAvailable)).toEqual([true, false, false]);
  });
  it("does not count a players snapshot taken during the warmup (the inWindow filter)", () => {
    const parts = {
      measured: { from: iso(FROM), to: iso(FROM + TICK_WARMUP_MS + 10 * 60_000) },
      snapshots: [{ at: iso(FROM + 1000), playersAvailable: true }],
    };
    const v = Native.evaluate(evidence({ parts }));
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toMatch(/no accepted snapshot in the measured window carried an available players part/);
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

describe("tick-cost-native judge: the 2 ms step ceiling (owner, 2026-10-26)", () => {
  const S2_SOURCES = NATIVE_SOURCES_S2;
  // UE's log timestamp, UTC: [YYYY.MM.DD-HH.MM.SS:mmm]. IN is a moment inside the measured window (warmup + 5 minutes after the start).
  const stamp = (ms: number) => {
    const d = new Date(ms);
    const p = (n: number, w = 2) => String(n).padStart(w, "0");
    return `[${d.getUTCFullYear()}.${p(d.getUTCMonth() + 1)}.${p(d.getUTCDate())}-${p(d.getUTCHours())}.${p(d.getUTCMinutes())}.${p(d.getUTCSeconds())}:${p(d.getUTCMilliseconds(), 3)}][  0]LogSatisDash: Display: `;
  };
  const IN = stamp(FROM + TICK_WARMUP_MS + 5 * 60_000);
  const stepLine = (name: string, longest: string, prefix = IN) => `${prefix}${name} (game thread only): 900 items in 30 steps, longest step ${longest} ms, total 9.00 ms`;
  const READERS_S2 = `${PREFIX}Readers: players=native resourceNodes=native factory=native trains=native`;
  const s2Lines = (factory = "1.10", trains = "0.40", routes = "0.60") => [READERS_S2, NATIVE_LINES[1]!, stepLine("Factory read", factory), stepLine("Trains read", trains), stepLine("Train routes read", routes)];

  it("judges S1-shaped evidence as before: only the node scan is required", () => {
    const v = Native.evaluate(evidence());
    expect(v.pass).toBe(true);
    expect(v.reasons.join("\n")).toMatch(/step ceiling 2 ms held.*Resource node scan longest step 1\.75 ms/);
  });
  it("fails a node scan step over 2 ms, naming the reader and the value", () => {
    const over = [NATIVE_LINES[0]!, `${PREFIX}Resource node scan (game thread only): 180000 actors in 90 steps, 612 nodes, longest step 2.01 ms, total 41.50 ms`];
    const v = Native.evaluate(evidence({ lines: over }));
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toMatch(/Resource node scan: longest step 2\.01 ms exceeds the 2 ms ceiling/);
  });
  it("passes S2-shaped evidence at 1.9 ms or less and lists the longest step per reader", () => {
    const v = Native.evaluate(evidence({ sources: S2_SOURCES, lines: s2Lines("1.90", "0.40", "0.60") }));
    expect(v.pass).toBe(true);
    const text = v.reasons.join("\n");
    expect(text).toMatch(/Factory read longest step 1\.9 ms/);
    expect(text).toMatch(/Trains read longest step 0\.4 ms/);
    expect(text).toMatch(/Train routes read longest step 0\.6 ms/);
  });
  it("fails 2.1 ms on any one reader", () => {
    const f = Native.evaluate(evidence({ sources: S2_SOURCES, lines: s2Lines("2.10") }));
    expect(f.pass).toBe(false);
    expect(f.reasons.join("\n")).toMatch(/Factory read: longest step 2\.1 ms exceeds the 2 ms ceiling/);
    expect(Native.evaluate(evidence({ sources: S2_SOURCES, lines: s2Lines("1.0", "2.10") })).reasons.join("\n")).toMatch(/Trains read: longest step 2\.1 ms/);
    expect(Native.evaluate(evidence({ sources: S2_SOURCES, lines: s2Lines("1.0", "0.4", "2.10") })).reasons.join("\n")).toMatch(/Train routes read: longest step 2\.1 ms/);
  });
  it("fails when a native S2 reader logged nothing", () => {
    const v = Native.evaluate(evidence({ sources: S2_SOURCES, lines: [READERS_S2, NATIVE_LINES[1]!] }));
    expect(v.pass).toBe(false);
    const text = v.reasons.join("\n");
    expect(text).toMatch(/no "Factory read \(game thread only\):" line/);
    expect(text).toMatch(/no "Trains read \(game thread only\):" line/);
    expect(text).toMatch(/no "Train routes read \(game thread only\):" line/);
  });
  it("fails a missing Train routes read line", () => {
    const v = Native.evaluate(evidence({ sources: S2_SOURCES, lines: s2Lines().slice(0, 4) }));
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toMatch(/no "Train routes read \(game thread only\):" line/);
  });
  it("fails a read line outside the measured window (before the warmup ended)", () => {
    const early = stamp(FROM + 60_000);
    const lines = [...s2Lines().slice(0, 4), stepLine("Train routes read", "0.60", early)];
    const v = Native.evaluate(evidence({ sources: S2_SOURCES, lines }));
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toMatch(/Train routes read.*inside the measured window/);
  });
  it("fails factorySource bridge (or trainsSource bridge) in the captured config", () => {
    const f = Native.evaluate(evidence({ sources: { ...S2_SOURCES, factorySource: "bridge" }, lines: s2Lines() }));
    expect(f.pass).toBe(false);
    expect(f.reasons.join("\n")).toMatch(/factorySource/);
    const t = Native.evaluate(evidence({ sources: { ...S2_SOURCES, trainsSource: "bridge" }, lines: s2Lines() }));
    expect(t.pass).toBe(false);
    expect(t.reasons.join("\n")).toMatch(/trainsSource/);
  });
  it("fails when the mod's Readers line says factory is bridge", () => {
    const lines = [`${PREFIX}Readers: players=native resourceNodes=native factory=bridge trains=native`, ...s2Lines().slice(1)];
    const v = Native.evaluate(evidence({ sources: S2_SOURCES, lines }));
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toMatch(/factory=bridge/);
  });
  it("describes the scan as judged against 2 ms, not 'recorded not judged'", () => {
    const text = Native.evaluate(evidence()).reasons.join("\n");
    expect(text).not.toMatch(/not judged/);
    expect(text).toMatch(/judged against the 2 ms ceiling/);
  });

  describe("S3: the shared Logistics read line (Task 14)", () => {
    const S3_SOURCES = NATIVE_SOURCES_S3;
    const READERS_S3 = `${PREFIX}Readers: players=native resourceNodes=native factory=native trains=native rails=native conveyors=native`;
    const logistics = (longest: string, prefix = IN) =>
      `${prefix}Logistics read (game thread only): layers rails=1 conveyors=1, 900 buildables, 4 tracks, 500 belts, 20 lifts, 30 pipes, 3 open ends, 0 ownerless ends, longest step ${longest} ms, total 12.00 ms`;
    const s3Lines = (longest = "0.80", prefix = IN) => [READERS_S3, ...s2Lines().slice(1), logistics(longest, prefix)];

    it("passes S3-shaped evidence when everything is native and each longest step is 1.9 ms or less", () => {
      const v = Native.evaluate(evidence({ sources: S3_SOURCES, lines: s3Lines("1.90") }));
      expect(v.pass).toBe(true);
      expect(v.reasons.join("\n")).toMatch(/Logistics read longest step 1\.9 ms/);
    });
    it("fails a Logistics read step of 2.1 ms", () => {
      const v = Native.evaluate(evidence({ sources: S3_SOURCES, lines: s3Lines("2.10") }));
      expect(v.pass).toBe(false);
      expect(v.reasons.join("\n")).toMatch(/Logistics read: longest step 2\.1 ms exceeds the 2 ms ceiling/);
    });
    it("fails a missing Logistics read line when railsSource is native", () => {
      const v = Native.evaluate(evidence({ sources: S3_SOURCES, lines: s3Lines().slice(0, -1) }));
      expect(v.pass).toBe(false);
      expect(v.reasons.join("\n")).toMatch(/no "Logistics read \(game thread only\):" line/);
    });
    it("fails a Logistics read line outside the measured window", () => {
      const v = Native.evaluate(evidence({ sources: S3_SOURCES, lines: s3Lines("0.80", stamp(FROM + 60_000)) }));
      expect(v.pass).toBe(false);
      expect(v.reasons.join("\n")).toMatch(/Logistics read.*inside the measured window/);
    });
    it("fails conveyorsSource bridge in the captured config of an S3 run; a rails-only config still requires Logistics read", () => {
      const sources = { ...S3_SOURCES, conveyorsSource: "bridge" };
      const v = Native.evaluate(evidence({ sources, lines: s3Lines() }));
      expect(v.pass).toBe(false);
      expect(v.reasons.join("\n")).toMatch(/conveyorsSource/);
      expect(Native.requiredStepReaders(sources)).toContain("Logistics read");
      expect(Native.requiredStepReaders({ conveyorsSource: "native" })).toContain("Logistics read");
      expect(Native.requiredStepReaders(NATIVE_SOURCES_S2)).not.toContain("Logistics read");
    });
    it("fails when the mod's Readers line says rails or conveyors is bridge", () => {
      const lines = [`${PREFIX}Readers: players=native resourceNodes=native factory=native trains=native rails=bridge conveyors=native`, ...s3Lines().slice(1)];
      const v = Native.evaluate(evidence({ sources: S3_SOURCES, lines }));
      expect(v.pass).toBe(false);
      expect(v.reasons.join("\n")).toMatch(/rails=bridge/);
    });
    it("S1- and S2-shaped evidence needs no Logistics read line", () => {
      expect(Native.evaluate(evidence()).pass).toBe(true);
      expect(Native.evaluate(evidence({ sources: S2_SOURCES, lines: s2Lines() })).pass).toBe(true);
    });

    describe("S4-A: the markers and drone steps and the powerLines pass (Task 16)", () => {
      const S4_SOURCES = NATIVE_SOURCES_S4;
      const READERS_S4 = `${PREFIX}Readers: players=native resourceNodes=native factory=native trains=native rails=native conveyors=native mapMarkers=native droneRoutes=native powerLines=native`;
      const logisticsS4 = (longest = "0.80", prefix = IN) =>
        `${prefix}Logistics read (game thread only): layers rails=1 conveyors=1 powerLines=1, 900 buildables, 4 tracks, 500 belts, 20 lifts, 30 pipes, 3 open ends, 0 ownerless ends, wires 843, expected wires 843, longest step ${longest} ms, total 12.00 ms`;
      const markers = (longest = "0.50", prefix = IN) => `${prefix}Map markers read (game thread only): 120 markers, 0 dropped in 3 steps, longest step ${longest} ms, total 1.00 ms`;
      const drones = (longest = "0.60", prefix = IN) => `${prefix}Drone routes read (game thread only): 8 ports, 0 dropped in 2 steps, longest step ${longest} ms, total 1.00 ms`;
      const s4Lines = (m = "0.50", d = "0.60", logLongest = "0.80") => [READERS_S4, ...s2Lines().slice(1), logisticsS4(logLongest), markers(m), drones(d)];

      it("passes S4-shaped evidence when all lines are present and each longest step is 1.9 ms or less", () => {
        const v = Native.evaluate(evidence({ sources: S4_SOURCES, lines: s4Lines("1.90", "1.90", "1.90") }));
        expect(v.reasons.join("\n")).toMatch(/Map markers read longest step 1\.9 ms/);
        expect(v.pass).toBe(true);
        expect(v.reasons.join("\n")).toMatch(/Drone routes read longest step 1\.9 ms/);
      });
      it("fails a Drone routes read step of 2.1 ms, and a Map markers read step of 2.1 ms", () => {
        const d = Native.evaluate(evidence({ sources: S4_SOURCES, lines: s4Lines("0.5", "2.10") }));
        expect(d.pass).toBe(false);
        expect(d.reasons.join("\n")).toMatch(/Drone routes read: longest step 2\.1 ms exceeds the 2 ms ceiling/);
        expect(Native.evaluate(evidence({ sources: S4_SOURCES, lines: s4Lines("2.10") })).reasons.join("\n")).toMatch(/Map markers read: longest step 2\.1 ms/);
      });
      it("fails a missing Map markers read line (or Drone routes read line) when the key is native", () => {
        const noMarkers = s4Lines().filter((l) => !l.includes("Map markers read"));
        const v = Native.evaluate(evidence({ sources: S4_SOURCES, lines: noMarkers }));
        expect(v.pass).toBe(false);
        expect(v.reasons.join("\n")).toMatch(/no "Map markers read \(game thread only\):" line/);
        const noDrones = s4Lines().filter((l) => !l.includes("Drone routes read"));
        expect(Native.evaluate(evidence({ sources: S4_SOURCES, lines: noDrones })).reasons.join("\n")).toMatch(/no "Drone routes read \(game thread only\):" line/);
      });
      it("does not count a Map markers read line outside the measured span (the S2 window rule)", () => {
        const lines = [...s4Lines().filter((l) => !l.includes("Map markers read")), markers("0.5", stamp(FROM + 60_000))];
        const v = Native.evaluate(evidence({ sources: S4_SOURCES, lines }));
        expect(v.pass).toBe(false);
        expect(v.reasons.join("\n")).toMatch(/Map markers read.*inside the measured window/);
      });
      it("fails powerLinesSource bridge (or any S4 key not native) in an S4-shaped config", () => {
        const p = Native.evaluate(evidence({ sources: { ...S4_SOURCES, powerLinesSource: "bridge" }, lines: s4Lines() }));
        expect(p.pass).toBe(false);
        expect(p.reasons.join("\n")).toMatch(/powerLinesSource/);
        expect(Native.evaluate(evidence({ sources: { ...S4_SOURCES, mapMarkersSource: "bridge" }, lines: s4Lines() })).reasons.join("\n")).toMatch(/mapMarkersSource/);
        expect(Native.evaluate(evidence({ sources: { ...S4_SOURCES, droneRoutesSource: "bridge" }, lines: s4Lines() })).reasons.join("\n")).toMatch(/droneRoutesSource/);
      });
      it("fails when the Readers line says powerLines is bridge, or the Logistics read line did not serve powerLines", () => {
        const bridge = [READERS_S4.replace("powerLines=native", "powerLines=bridge"), ...s4Lines().slice(1)];
        expect(Native.evaluate(evidence({ sources: S4_SOURCES, lines: bridge })).reasons.join("\n")).toMatch(/powerLines=bridge/);
        expect(Native.evaluate(evidence({ sources: S4_SOURCES, lines: [...s4Lines().slice(0, 5), logistics("0.8"), markers(), drones()] })).reasons.join("\n")).toMatch(/powerLines=0/);
      });
      it("requiredStepReaders adds the S4 lines only when the S4 keys are native; S2- and S3-shaped sources are unchanged", () => {
        expect(Native.requiredStepReaders(S4_SOURCES)).toEqual(["Resource node scan", "Factory read", "Trains read", "Train routes read", "Logistics read", "Map markers read", "Drone routes read"]);
        expect(Native.requiredStepReaders(NATIVE_SOURCES_S3)).toEqual(["Resource node scan", "Factory read", "Trains read", "Train routes read", "Logistics read"]);
        expect(Native.requiredStepReaders(NATIVE_SOURCES_S2)).toEqual(["Resource node scan", "Factory read", "Trains read", "Train routes read"]);
        expect(Native.requiredStepReaders({ powerLinesSource: "native" })).toEqual(["Resource node scan", "Logistics read"]);
        expect(Native.requiredStepReaders({ ...S4_SOURCES, mapMarkersSource: "bridge", droneRoutesSource: "bridge" })).not.toContain("Map markers read");
      });
    });
  });
});
