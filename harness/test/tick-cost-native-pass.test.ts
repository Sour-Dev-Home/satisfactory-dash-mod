import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { snapshotParts } from "../src/core-runs/tick-cost";
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

// The mod's log lines as the evidence carries them. Every reader is native and unconditional (R7), so a passing log has the
// Readers line with all thirteen fields and a step line for every sliced reader inside the measured window.
const PREFIX = "[2026.10.26-12:00:05:123][  0]LogSatisDash: Display: ";
// UE's log timestamp, UTC: [YYYY.MM.DD-HH.MM.SS:mmm]. IN is a moment inside the measured window (warmup + 5 minutes after the start).
const stamp = (ms: number) => {
  const d = new Date(ms);
  const p = (n: number, w = 2) => String(n).padStart(w, "0");
  return `[${d.getUTCFullYear()}.${p(d.getUTCMonth() + 1)}.${p(d.getUTCDate())}-${p(d.getUTCHours())}.${p(d.getUTCMinutes())}.${p(d.getUTCSeconds())}:${p(d.getUTCMilliseconds(), 3)}][  0]LogSatisDash: Display: `;
};
const IN = stamp(FROM + TICK_WARMUP_MS + 5 * 60_000);
const READERS = `${PREFIX}Readers: players=native resourceNodes=native factory=native trains=native rails=native conveyors=native mapMarkers=native droneRoutes=native powerLines=native extractors=native generators=native stationRates=native storage=native`;
const scanLine = (longest = "1.75", total = "41.50") => `${PREFIX}Resource node scan (game thread only): 180000 actors in 90 steps, 612 nodes, longest step ${longest} ms, total ${total} ms`;
const stepLine = (name: string, longest: string, prefix = IN) => `${prefix}${name} (game thread only): 900 items in 30 steps, longest step ${longest} ms, total 9.00 ms`;
const logistics = (longest = "0.80", prefix = IN) =>
  `${prefix}Logistics read (game thread only): layers rails=1 conveyors=1 powerLines=1, 900 buildables, 4 tracks, 500 belts, 20 lifts, 30 pipes, 3 open ends, 0 ownerless ends, wires 843, expected wires 843, longest step ${longest} ms, total 12.00 ms`;
const markers = (longest = "0.50", prefix = IN) => `${prefix}Map markers read (game thread only): 120 markers, 0 dropped in 3 steps, longest step ${longest} ms, total 1.00 ms`;
const drones = (longest = "0.60", prefix = IN) => `${prefix}Drone routes read (game thread only): 8 ports, 0 dropped in 2 steps, longest step ${longest} ms, total 1.00 ms`;
const buildables = (longest = "0.70", prefix = IN) => `${prefix}Buildables read (game thread only): 4 elements, 1 extractors, 0 generators, 0 containers in 3 steps, longest step ${longest} ms, total 1.00 ms`;
const rates = (read = 6, failed = 0) => `${IN}Station rates (reflection read): ${read} platforms read, 0 fluid platforms skipped, ${failed} lookups failed`;

interface Over { readers?: string; scan?: string; factory?: string; trains?: string; routes?: string; logistics?: string; markers?: string; drones?: string; buildables?: string; rates?: string }
/** A complete passing log; each override replaces one line (an empty string drops it). */
function fullLines(o: Over = {}): string[] {
  const pick = (v: string | undefined, d: string) => (v === undefined ? d : v);
  return [
    pick(o.readers, READERS), pick(o.scan, scanLine()),
    pick(o.factory, stepLine("Factory read", "1.10")), pick(o.trains, stepLine("Trains read", "0.40")), pick(o.routes, stepLine("Train routes read", "0.60")),
    pick(o.logistics, logistics()), pick(o.markers, markers()), pick(o.drones, drones()), pick(o.buildables, buildables()), pick(o.rates, rates()),
  ].filter((l) => l !== "");
}

function evidence(over: { parts?: unknown; posts?: unknown[]; lines?: string[] } = {}): CoreEvidence {
  return {
    lines: "lines" in over ? (over.lines as string[]) : fullLines(), errorLines: [], leaks: { code: false, secret: false, bearer: false },
    captures: {
      "tick-cost.json": baseCapture(),
      "snapshot-parts.json": over.parts ?? {
        measured: { from: iso(FROM), to: iso(FROM + TICK_WARMUP_MS + 10 * 60_000) },
        snapshots: [{ at: iso(FROM + TICK_WARMUP_MS + 60_000), playersAvailable: true }],
      },
      "recorder.json": over.posts ?? [{ at: iso(FROM + TICK_WARMUP_MS + 5 * 60_000), status: 200, agent: "mod", world: { layer: "resourceNodes", items: 600, bytes: 60000 } }],
    },
  };
}

describe("tick-cost-native judge", () => {
  it("passes when the existing limits pass and every native reader was live inside the measured window", () => {
    expect(Native.evaluate(evidence()).pass).toBe(true);
  });
  it("keeps the existing tick-cost verdict: a memory blow-up fails it", () => {
    const e = evidence();
    (e.captures["tick-cost.json"] as Base.TickCost).memoryWithMod = mem([6300, 6400, 6500]); // 490 MB above the baseline's end
    const v = Native.evaluate(e);
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toMatch(/memory/);
  });
  it("needs no mod-config capture: every reader is required whatever the run wrote", () => {
    const e = evidence();
    expect(e.captures["mod-config.json"]).toBeUndefined();
    expect(Native.evaluate(e).pass).toBe(true);
    expect(Native.evaluate({ ...e, captures: { ...e.captures, "mod-config.json": { stepDiagnostics: true } } }).pass).toBe(true);
  });
  it("requiredStepReaders is every sliced reader, explicitly", () => {
    expect(Native.requiredStepReaders()).toEqual(["Resource node scan", "Factory read", "Trains read", "Train routes read", "Logistics read", "Map markers read", "Drone routes read", "Buildables read"]);
    expect(Native.REQUIRED_STEP_READERS).toEqual(Native.requiredStepReaders());
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
  it("fails a log that says bridge for every reader and carries no scan line", () => {
    const bridge = READERS.replace(/=native/g, "=bridge");
    const v = Native.evaluate(evidence({ lines: [bridge, `${PREFIX}Agent is running`] }));
    expect(v.pass).toBe(false);
    const text = v.reasons.join("\n");
    expect(text).toMatch(/players=bridge/);
    expect(text).toMatch(/factory=bridge/);
    expect(text).toMatch(/storage=bridge/);
    expect(text).toMatch(/Resource node scan \(game thread only\):/);
  });
  it("names the missing Readers line when the log has none, and a missing scan line when only that is absent", () => {
    expect(Native.evaluate(evidence({ lines: [] })).reasons.join("\n")).toMatch(/no "Readers:" line/);
    const noScan = Native.evaluate(evidence({ lines: fullLines({ scan: "" }) }));
    expect(noScan.pass).toBe(false);
    expect(noScan.reasons.join("\n")).toMatch(/no "Resource node scan \(game thread only\):" line/);
    expect(noScan.reasons.join("\n")).not.toMatch(/Readers:/);
  });
  it("records the scan's longest step and total time in the reasons; the total is not judged", () => {
    const v = Native.evaluate(evidence({ lines: fullLines({ scan: scanLine("1.99", "9000.50") }) }));
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
  it("passes a full log and lists the longest step per reader", () => {
    const v = Native.evaluate(evidence());
    expect(v.pass).toBe(true);
    expect(v.reasons.join("\n")).toMatch(/step ceiling 2 ms held.*Resource node scan longest step 1\.75 ms/);
    expect(v.reasons.join("\n")).toMatch(/Buildables read longest step 0\.7 ms/);
  });
  it("fails a node scan step over 2 ms, naming the reader and the value", () => {
    const v = Native.evaluate(evidence({ lines: fullLines({ scan: scanLine("2.01") }) }));
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toMatch(/Resource node scan: longest step 2\.01 ms exceeds the 2 ms ceiling/);
  });
  it("adds a report-only diagnostic line with the thread cycles to a failing verdict, and changes nothing else", () => {
    const over = fullLines({ scan: scanLine("2.01") });
    const diag = `${PREFIX}Logistics read longest step: 9.12 ms over 67 elements, phase 1 to 2, longest element 9.00 ms at 67 (phase 2), thread cycles per ms 41000 in this step, 3000000 in 10 other steps`;
    const without = Native.evaluate(evidence({ lines: over }));
    const withDiag = Native.evaluate(evidence({ lines: [...over, diag] }));
    expect(withDiag.pass).toBe(false);
    expect(withDiag.reasons).toEqual([...without.reasons, "diagnostic (report-only): Logistics longest step 9.12 ms, longest element 9.00 ms at 67 (phase 2); thread cycles per ms 41000 in this step vs 3000000 in 10 other steps (1.4%)"]);
  });
  it("a diagnostic line with the conveyor-calls tail prints the five maxima after the cycles, and pass is unchanged", () => {
    const over = fullLines({ scan: scanLine("2.01") });
    const diag = `${PREFIX}Logistics read longest step: 9.12 ms over 67 elements, phase 1 to 2, longest element 9.00 ms at 67 (phase 2), thread cycles per ms 41000 in this step, 3000000 in 10 other steps, conveyor calls max: get 0.40 ms at 3, name 0.00 ms at 0, text 8.90 ms at 7, speed 0.10 ms at 12, ends 1.23 ms at 11`;
    const without = Native.evaluate(evidence({ lines: over }));
    const withDiag = Native.evaluate(evidence({ lines: [...over, diag] }));
    expect(withDiag.pass).toBe(false);
    expect(withDiag.reasons).toEqual([...without.reasons, "diagnostic (report-only): Logistics longest step 9.12 ms, longest element 9.00 ms at 67 (phase 2); thread cycles per ms 41000 in this step vs 3000000 in 10 other steps (1.4%); conveyor calls max: get 0.40 ms at 3, name 0.00 ms at 0, text 8.90 ms at 7, speed 0.10 ms at 12, ends 1.23 ms at 11"]);
  });
  it("a diagnostic line without the cycles tail reports the step only", () => {
    const over = [...fullLines({ scan: scanLine("2.01") }), `${PREFIX}Logistics read longest step: 9.12 ms over 67 elements, phase 1 to 2`];
    expect(Native.evaluate(evidence({ lines: over })).reasons).toContain("diagnostic (report-only): Logistics longest step 9.12 ms");
  });
  it("leaves a passing verdict unchanged by a diagnostic line", () => {
    const diag = `${PREFIX}Logistics read longest step: 1.90 ms over 67 elements, phase 1 to 2, thread cycles per ms 41000 in this step, 3000000 in 10 other steps`;
    expect(Native.evaluate(evidence({ lines: [...fullLines(), diag] }))).toEqual(Native.evaluate(evidence()));
  });
  it("passes every reader at 1.9 ms or less", () => {
    const v = Native.evaluate(evidence({ lines: fullLines({
      scan: scanLine("1.90"), factory: stepLine("Factory read", "1.90"), trains: stepLine("Trains read", "1.90"), routes: stepLine("Train routes read", "1.90"),
      logistics: logistics("1.90"), markers: markers("1.90"), drones: drones("1.90"), buildables: buildables("1.90"),
    }) }));
    expect(v.pass).toBe(true);
    const text = v.reasons.join("\n");
    for (const name of ["Factory read", "Trains read", "Train routes read", "Logistics read", "Map markers read", "Drone routes read", "Buildables read"]) {
      expect(text).toMatch(new RegExp(`${name} longest step 1\\.9 ms`));
    }
  });
  it.each([
    ["Factory read", { factory: stepLine("Factory read", "2.10") }],
    ["Trains read", { trains: stepLine("Trains read", "2.10") }],
    ["Train routes read", { routes: stepLine("Train routes read", "2.10") }],
    ["Logistics read", { logistics: logistics("2.10") }],
    ["Map markers read", { markers: markers("2.10") }],
    ["Drone routes read", { drones: drones("2.10") }],
    ["Buildables read", { buildables: buildables("2.10") }],
  ] as const)("fails 2.1 ms on %s", (name, over) => {
    const v = Native.evaluate(evidence({ lines: fullLines(over) }));
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toContain(`${name}: longest step 2.1 ms exceeds the 2 ms ceiling`);
  });
  it.each([
    ["Factory read", { factory: "" }],
    ["Trains read", { trains: "" }],
    ["Train routes read", { routes: "" }],
    ["Logistics read", { logistics: "" }],
    ["Map markers read", { markers: "" }],
    ["Drone routes read", { drones: "" }],
    ["Buildables read", { buildables: "" }],
  ] as const)("fails a missing %s line", (name, over) => {
    const v = Native.evaluate(evidence({ lines: fullLines(over) }));
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toContain(`no "${name} (game thread only):" line`);
  });
  it.each([
    ["Factory read", (p: string) => ({ factory: stepLine("Factory read", "1.10", p) })],
    ["Trains read", (p: string) => ({ trains: stepLine("Trains read", "0.40", p) })],
    ["Train routes read", (p: string) => ({ routes: stepLine("Train routes read", "0.60", p) })],
    ["Logistics read", (p: string) => ({ logistics: logistics("0.80", p) })],
    ["Map markers read", (p: string) => ({ markers: markers("0.50", p) })],
    ["Drone routes read", (p: string) => ({ drones: drones("0.60", p) })],
    ["Buildables read", (p: string) => ({ buildables: buildables("0.70", p) })],
  ] as const)("fails a %s line outside the measured window (before the warmup ended)", (name, make) => {
    const v = Native.evaluate(evidence({ lines: fullLines(make(stamp(FROM + 60_000))) }));
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toMatch(new RegExp(`${name}.*inside the measured window`));
  });
  it("fails when the mod's Readers line says any reader is bridge, or has an older shape without the field", () => {
    for (const field of ["factory", "trains", "rails", "conveyors", "mapMarkers", "droneRoutes", "powerLines", "extractors", "generators", "stationRates", "storage"]) {
      const v = Native.evaluate(evidence({ lines: fullLines({ readers: READERS.replace(`${field}=native`, `${field}=bridge`) }) }));
      expect(v.pass, field).toBe(false);
      expect(v.reasons.join("\n"), field).toContain(`${field}=bridge`);
    }
    const nine = Native.evaluate(evidence({ lines: fullLines({ readers: READERS.replace(/ extractors=native generators=native stationRates=native storage=native/, "") }) }));
    expect(nine.pass).toBe(false);
  });
  it("fails when the Logistics read line did not serve powerLines", () => {
    const noWires = `${IN}Logistics read (game thread only): layers rails=1 conveyors=1 powerLines=0, 900 buildables, 4 tracks, 500 belts, 20 lifts, 30 pipes, 3 open ends, 0 ownerless ends, longest step 0.80 ms, total 12.00 ms`;
    expect(Native.evaluate(evidence({ lines: fullLines({ logistics: noWires }) })).reasons.join("\n")).toMatch(/powerLines=0/);
  });
  it("fails a missing, failed or empty station rates counts line", () => {
    const none = Native.evaluate(evidence({ lines: fullLines({ rates: "" }) }));
    expect(none.pass).toBe(false);
    expect(none.reasons.join("\n")).toMatch(/no "Station rates \(reflection read\):" line/);
    const failed = Native.evaluate(evidence({ lines: fullLines({ rates: rates(6, 2) }) }));
    expect(failed.pass).toBe(false);
    expect(failed.reasons.join("\n")).toMatch(/2 lookups failed/);
    const empty = Native.evaluate(evidence({ lines: fullLines({ rates: rates(0, 0) }) }));
    expect(empty.pass).toBe(false);
    expect(empty.reasons.join("\n")).toMatch(/read 0 platforms/);
  });
  it("describes the scan as judged against 2 ms, not 'recorded not judged'", () => {
    const text = Native.evaluate(evidence()).reasons.join("\n");
    expect(text).not.toMatch(/not judged/);
    expect(text).toMatch(/judged against the 2 ms ceiling/);
  });
});
