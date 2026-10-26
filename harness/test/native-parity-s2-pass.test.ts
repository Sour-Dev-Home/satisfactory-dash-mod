import { describe, expect, it } from "vitest";
import { NATIVE_SOURCES, NATIVE_SOURCES_S2 } from "../src/core-run";
import type { CoreEvidence } from "../src/core-run";
import { canonRoutes, createFactoryEncoder, FactoryTable, type CanonMachine, type RouteRecord } from "../src/frm-s2";
import * as Pass from "../../runs/native-parity/pass";
import * as S2 from "../../runs/native-parity/pass-s2";

const T0 = Date.parse("2026-10-26T12:00:00.000Z");
const iso = (ms: number) => new Date(ms).toISOString();
const ROUNDS = 60;
const text = (v: Pass.NativeParity | { reasons: string[] }) => JSON.stringify(v);

// ------------------------------------------------------------------------------------------------------------------ factory

const machine = (i: number, over: Partial<CanonMachine> = {}): CanonMachine => ({
  id: `Build_ConstructorMk1_C_${i}`, name: "Constructor", className: "Build_ConstructorMk1_C", recipe: "Iron Plate",
  isProducing: true, isPaused: false, isBackedUp: false, inputsFull: false, circuitGroupId: 0,
  location: { xM: i * 10, yM: 5, zM: 1.5, rotationDeg: 90 }, clockSpeedPercent: 100,
  production: [{ name: "Iron Plate", className: "Desc_IronPlate_C", currentPerMinute: 10 + (i % 3), maxPerMinute: 20, percent: 50 + (i % 3) }],
  ingredients: [{ name: "Iron Ingot", className: "Desc_IronIngot_C", currentPerMinute: 15, maxPerMinute: 30, percent: 50 }],
  ...over,
});
const fleet = (n: number) => Array.from({ length: n }, (_, i) => machine(i));

interface FactoryOpts {
  n?: number; rounds?: number; somersloops?: number;
  mod?: (round: number, ms: CanonMachine[]) => CanonMachine[];
  frm?: (sample: number, ms: CanonMachine[]) => CanonMachine[];
  frmFrom?: number; frmTo?: number; dropFrmNear?: number;
}
/** A factory capture: FRM read every 5 s from 60 s before the first round to 10 s after the last, a mod part every 30 s. */
function factoryEv(o: FactoryOpts = {}): S2.FactoryEvidence {
  const base = fleet(o.n ?? 6);
  const rounds = o.rounds ?? ROUNDS;
  const table = new FactoryTable();
  const enc = createFactoryEncoder(table);
  const frmSamples: S2.FactoryFrmSample[] = [];
  const from = o.frmFrom ?? T0 - 60_000;
  const to = o.frmTo ?? T0 + rounds * 30_000 + 10_000;
  for (let t = from, j = 0; t <= to; t += 5000, j++) {
    if (o.dropFrmNear !== undefined && Math.abs(t - (T0 + o.dropFrmNear * 30_000)) <= 50_000) continue;
    frmSamples.push({ startedAt: iso(t), endedAt: iso(t + 200), ...enc.push(o.frm ? o.frm(j, base) : base) });
  }
  const modRounds = Array.from({ length: rounds }, (_, i) => ({ observedAt: iso(T0 + i * 30_000), rows: (o.mod ? o.mod(i, base) : base).map((m) => table.row(m)) }));
  return { statics: table.statics, rounds: modRounds, frmSamples, frm: { reads: frmSamples.length, failed: 0 }, somersloopMachines: o.somersloops ?? 0 };
}
const scaled = (k: number) => (ms: CanonMachine[]) => ms.map((m) => ({ ...m, production: m.production.map((r) => ({ ...r, currentPerMinute: r.currentPerMinute * k, percent: r.percent * k })), ingredients: m.ingredients!.map((r) => ({ ...r, currentPerMinute: r.currentPerMinute * k, percent: r.percent * k })) }));

describe("R2a factory", () => {
  it("passes on equal data and names what the world never showed only in rates", () => {
    const v = S2.judgeFactory(factoryEv());
    expect(v.reasons.join("\n")).toBe(v.reasons[0]);
    expect(v.pass).toBe(true);
    // The plain fleet is configured, running, at 100%, not backed up, inputs not full, no fluid: every other case is reported.
    expect(v.never).toEqual([
      "an unconfigured machine (recipe null)", "a paused machine", "a clock other than 100%", "a machine backed up (isBackedUp true)",
      "a machine with inputs full (inputsFull true)", "a fluid machine (no item of a fluid class)",
    ]);
  });
  it("an empty world reports that no machine was ever listed", () => {
    const v = S2.judgeFactory(factoryEv({ n: 0 }));
    expect(v.pass).toBe(true);
    expect(v.never).toContain("any machine (neither side listed one)");
  });
  it("a world that showed every case reports none of the machine lines", () => {
    const rich = (ms: CanonMachine[]): CanonMachine[] => [
      ...ms,
      machine(100, { recipe: null }), machine(101, { isPaused: true }), machine(102, { clockSpeedPercent: 50 }),
      machine(103, { isBackedUp: true }), machine(104, { inputsFull: true }),
      machine(105, { production: [{ name: "Water", className: "Desc_Water_C", currentPerMinute: 10, maxPerMinute: 20, percent: 50 }] }),
    ];
    const v = S2.judgeFactory(factoryEv({ mod: (_, ms) => rich(ms), frm: (_, ms) => rich(ms) }));
    expect(v.pass).toBe(true);
    expect(v.never).toEqual([]);
  });
  it("a case shown only by the two sides in the window counts, and the others stay reported", () => {
    const v = S2.judgeFactory(factoryEv({ frm: (_, ms) => [...ms, machine(100, { recipe: null })], mod: (_, ms) => [...ms, machine(100, { recipe: null })] }));
    expect(v.never).not.toContain("an unconfigured machine (recipe null)");
    expect(v.never).toContain("a paused machine");
  });
  it("fails on one missing machine id, naming its className", () => {
    const v = S2.judgeFactory(factoryEv({ mod: (i, ms) => (i === 20 ? ms.slice(1) : ms) }));
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toMatch(/1 in FRM only/);
    expect(v.reasons.join("\n")).toContain("Build_ConstructorMk1_C_0 (Build_ConstructorMk1_C)");
  });
  it("fails on an id only the mod has", () => {
    const v = S2.judgeFactory(factoryEv({ mod: (i, ms) => (i === 20 ? [...ms, machine(99)] : ms) }));
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toMatch(/1 in the mod only[\s\S]*Build_ConstructorMk1_C_99/);
  });
  it("location is a distance, not per axis: 0.9 m along a diagonal passes, 1.1 m fails", () => {
    const diag = (d: number) => (_: number, ms: CanonMachine[]) => ms.map((m) => ({ ...m, location: { ...m.location!, xM: m.location!.xM + d / Math.SQRT2, yM: m.location!.yM + d / Math.SQRT2 } }));
    expect(S2.judgeFactory(factoryEv({ mod: diag(0.9) })).pass).toBe(true);
    expect(S2.judgeFactory(factoryEv({ mod: diag(1.1) })).pass).toBe(false);
  });
  it("static fields: name or recipe differ fails; location within 1 m and clock within 0.01 pass", () => {
    expect(S2.judgeFactory(factoryEv({ mod: (i, ms) => (i === 5 ? ms.map((m, k) => (k === 0 ? { ...m, name: "Assembler" } : m)) : ms) })).pass).toBe(false);
    expect(S2.judgeFactory(factoryEv({ mod: (i, ms) => (i === 5 ? ms.map((m, k) => (k === 0 ? { ...m, recipe: null } : m)) : ms) })).pass).toBe(false);
    expect(S2.judgeFactory(factoryEv({ mod: (_, ms) => ms.map((m) => ({ ...m, location: { ...m.location!, xM: m.location!.xM + 0.9 }, clockSpeedPercent: 100.005 })) })).pass).toBe(true);
    const far = S2.judgeFactory(factoryEv({ mod: (_, ms) => ms.map((m) => ({ ...m, location: { ...m.location!, xM: m.location!.xM + 1.5 } })) }));
    expect(far.pass).toBe(false);
    expect(far.reasons.join("\n")).toContain("location");
    expect(S2.judgeFactory(factoryEv({ mod: (_, ms) => ms.map((m) => ({ ...m, clockSpeedPercent: 100.02 })) })).pass).toBe(false);
  });
  it("dynamic flags: a value FRM showed in the window for at least 99% of machine-rounds (360 machine-rounds: 3 misses pass, 4 fail), mismatches listed", () => {
    const flip = (misses: number) => (i: number, ms: CanonMachine[]) => ms.map((m, k) => (k === 0 && i < misses ? { ...m, isBackedUp: true } : m));
    expect(S2.judgeFactory(factoryEv({ mod: flip(3) })).pass).toBe(true);
    const v = S2.judgeFactory(factoryEv({ mod: flip(4) }));
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toMatch(/dynamic flags matched .* 98\.89% of 360 machine-rounds, need 99%/);
    expect(v.reasons.join("\n")).toContain("isBackedUp mod true vs FRM false");
  });
  it("a flag FRM showed at any point in the window counts (the part's age)", () => {
    const v = S2.judgeFactory(factoryEv({ frm: (j, ms) => ms.map((m) => ({ ...m, isProducing: j % 2 === 0 })), mod: (_, ms) => ms.map((m) => ({ ...m, isProducing: false })) }));
    expect(v.pass).toBe(true);
  });
  it("fewer than the round floor fails; a single round with no FRM read in its window is skipped, not failed", () => {
    expect(S2_FLOOR).toBe(40);
    const few = S2.judgeFactory(factoryEv({ rounds: 39 }));
    expect(few.pass).toBe(false);
    expect(few.reasons.join("\n")).toMatch(/compared in only 39 rounds.*need 40/);
    expect(S2.judgeFactory(factoryEv({ rounds: 40 })).pass).toBe(true);
    const late = S2.judgeFactory(factoryEv({ dropFrmNear: 30 }));
    expect(late.pass).toBe(true);
  });
  it("fails when too many of the harness's FRM reads failed, and notes a few as a thinner window", () => {
    const ev = factoryEv();
    expect(S2.judgeFactory({ ...ev, frm: { reads: 100, failed: 3 } }).reasons.join("\n")).toContain("3 of 103 FRM getFactory reads failed");
    expect(S2.judgeFactory({ ...ev, frm: { reads: 100, failed: 3 } }).pass).toBe(true);
    expect(S2.judgeFactory({ ...ev, frm: { reads: 90, failed: 10 } }).pass).toBe(false);
  });
  it("an empty factory is a real answer, not unreadable", () => {
    const v = S2.judgeFactory(factoryEv({ n: 0 }));
    expect(v.pass).toBe(true);
  });
  it("a part that is not valid is reported (rows null)", () => {
    const ev = factoryEv();
    ev.rounds[3] = { observedAt: ev.rounds[3]!.observedAt, rows: null };
    const v = S2.judgeFactory(ev);
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toContain("not valid against the pinned contract");
  });
});

const S2_FLOOR = S2.S2_NEED_ROUNDS;

describe("R2b rates", () => {
  it("passes on equal data; reports a Somersloop and idle-world case as NOT EXERCISED", () => {
    const v = S2.judgeRates(factoryEv());
    expect(v.pass).toBe(true);
    expect(v.never.join("\n")).toContain("Somersloop");
    expect(S2.judgeRates(factoryEv({ somersloops: 2 })).never.join("\n")).not.toContain("Somersloop");
  });
  it("reports a missing fluid item, and stops reporting it once a fluid class is in the world", () => {
    expect(S2.judgeRates(factoryEv()).never.join("\n")).toContain("a fluid item");
    const water = (ms: CanonMachine[]) => [...ms, machine(100, { production: [{ name: "Water", className: "Desc_Water_C", currentPerMinute: 10, maxPerMinute: 20, percent: 50 }] })];
    const v = S2.judgeRates(factoryEv({ mod: (_, ms) => water(ms), frm: (_, ms) => water(ms) }));
    expect(v.pass).toBe(true);
    expect(v.never.join("\n")).not.toContain("a fluid item");
  });
  it("reports an idle world: no current above zero", () => {
    const idle = (ms: CanonMachine[]) => ms.map((m) => ({ ...m, production: m.production.map((r) => ({ ...r, currentPerMinute: 0, percent: 0 })), ingredients: m.ingredients!.map((r) => ({ ...r, currentPerMinute: 0, percent: 0 })) }));
    const v = S2.judgeRates(factoryEv({ mod: (_, ms) => idle(ms), frm: (_, ms) => idle(ms) }));
    expect(v.pass).toBe(true);
    expect(v.never.join("\n")).toContain("currentPerMinute above zero");
  });
  it("maxPerMinute off by 0.2% fails (no fallback text: max is what missed); off by 0.05% passes", () => {
    const off = (k: number) => (_: number, ms: CanonMachine[]) => ms.map((m, i) => (i === 0 ? { ...m, production: m.production.map((r) => ({ ...r, maxPerMinute: r.maxPerMinute * k })) } : m));
    const bad = S2.judgeRates(factoryEv({ mod: off(1.002) }));
    expect(bad.pass).toBe(false);
    expect(bad.reasons.join("\n")).toContain("maxPerMinute");
    expect(bad.reasons.join("\n")).not.toContain("fallback");
    expect(S2.judgeRates(factoryEv({ mod: off(1.0005) })).pass).toBe(true);
  });
  it("items are matched by class, not position: the same items in another order pass; a missing or extra item fails", () => {
    const two = (ms: CanonMachine[]) => ms.map((m) => ({ ...m, production: [m.production[0]!, { ...m.production[0]!, className: "Desc_Second_C", name: "Second", maxPerMinute: 7 }] }));
    const order = S2.judgeRates(factoryEv({ frm: (_, ms) => two(ms), mod: (_, ms) => two(ms).map((m) => ({ ...m, production: [...m.production].reverse() })) }));
    expect(order.reasons.join("\n")).toBe(order.reasons[0]);
    expect(order.pass).toBe(true);
    const missing = S2.judgeRates(factoryEv({ frm: (_, ms) => two(ms), mod: (_, ms) => ms }));
    expect(missing.pass).toBe(false);
    expect(missing.reasons.join("\n")).toContain("Desc_Second_C");
    const extra = S2.judgeRates(factoryEv({ frm: (_, ms) => ms, mod: (_, ms) => two(ms) }));
    expect(extra.pass).toBe(false);
  });
  it("max within 0.001 absolute passes even where the relative error is large", () => {
    const tiny = (v: number) => (ms: CanonMachine[]) => ms.map((m) => ({ ...m, production: m.production.map((r) => ({ ...r, maxPerMinute: v })) }));
    expect(S2.judgeRates(factoryEv({ frm: (_, ms) => tiny(0.0004)(ms), mod: (_, ms) => tiny(0.0012)(ms) })).pass).toBe(true);
  });
  it("current at +2.9% of FRM's class total passes and at +3.2% fails", () => {
    expect(S2.judgeRates(factoryEv({ mod: (_, ms) => scaled(1.029)(ms) })).pass).toBe(true);
    const v = S2.judgeRates(factoryEv({ mod: (_, ms) => scaled(1.032)(ms) }));
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toMatch(/item class .* total currentPerMinute/);
  });
  it("item-class checks need 99% (owner, 2026-10-26): one round's plate total off passes, two rounds' fail", () => {
    // 60 rounds x 2 classes x (current, percent) = 240 checks. Plates off by +50% in a round is 2 misses (and 6 building-rounds).
    const plates = (bad: number) => (i: number, ms: CanonMachine[]) =>
      i < bad ? ms.map((m) => ({ ...m, production: m.production.map((r) => ({ ...r, currentPerMinute: r.currentPerMinute * 1.5, percent: r.percent * 1.5 })) })) : ms;
    const one = S2.judgeRates(factoryEv({ mod: plates(1) }));
    expect(one.pass).toBe(true);
    expect(one.reasons.join("\n")).toMatch(/2 of 240 item-class checks outside .* \(99\.17% within, need 99%\) \(first 2: .*item class 0:Desc_IronPlate_C total currentPerMinute/);
    const two = S2.judgeRates(factoryEv({ mod: plates(2) }));
    expect(two.pass).toBe(false);
    expect(two.reasons.join("\n")).toMatch(/current\/percent: 4 of 240 item-class checks outside .* \(98\.33% within, need 99%\)/);
    expect(two.reasons).toContain(S2.S2_RATES_FALLBACK);
  });
  it("the bracket is FRM's range over the window (a total that moved)", () => {
    const wave = (j: number, ms: CanonMachine[]) => scaled(j % 2 === 0 ? 1 : 1.2)(ms);
    expect(S2.judgeRates(factoryEv({ frm: wave, mod: (_, ms) => scaled(1.1)(ms) })).pass).toBe(true);
    expect(S2.judgeRates(factoryEv({ frm: wave, mod: (_, ms) => scaled(1.3)(ms) })).pass).toBe(false);
  });
  it("only current missing: fails with the owner's fallback text, the median ratio and the raw productivity; R2a is unaffected", () => {
    const ev = factoryEv({ mod: (_, ms) => scaled(0.5)(ms) });
    const v = S2.judgeRates(ev, { min: 0.1, max: 0.9 });
    expect(v.pass).toBe(false);
    expect(v.reasons).toContain("R2b: max matches, averaged current does not; fallback = set DefaultRatesMode to Instantaneous (Task 6) and the owner rules on the tolerance");
    expect(v.reasons.join("\n")).toMatch(/median mod\/FRM current ratio 0\.5;.*raw productivity 0\.1\.\.0\.9/);
    expect(S2.judgeFactory(ev).pass).toBe(true);
  });
  it("a raw productivity above 1.0001 in the log fails with an explicit scale reason", () => {
    const v = S2.judgeRates(factoryEv(), { min: 0, max: 100 });
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toMatch(/scale: .*raw productivity of 100/);
    expect(S2.judgeRates(factoryEv(), { min: 0, max: 1.0001 }).pass).toBe(true);
  });
  it("reads the productivity range from the Factory read lines", () => {
    const lines = [
      "LogSatisDash: Display: Factory read (game thread only): 900 buildings in 400 steps, 340 machines, longest step 0.80 ms, total 120.00 ms, expected 340, boosted 0, productivity 0.000..0.870",
      "LogSatisDash: Display: Factory read (game thread only): 900 buildings in 400 steps, 340 machines, longest step 0.80 ms, total 120.00 ms, expected 340, boosted 0, productivity 0.100..1.000",
    ];
    expect(S2.productivityRange(lines)).toEqual({ min: 0, max: 1 });
    expect(S2.productivityRange(["nothing"])).toBeNull();
  });
  it("per building within 5% for at least 95%: one machine in twenty off passes, two fail (current in every class total still within 3%)", () => {
    const one = (k: number) => (_: number, ms: CanonMachine[]) => ms.map((m, i) => (i < k ? scaled(1.2)([m])[0]! : m));
    // 40 machines, 1 off by +20%: the class total moves 0.5%, 97.5% of buildings are fine.
    expect(S2.judgeRates(factoryEv({ n: 40, mod: one(1) })).pass).toBe(true);
    const v = S2.judgeRates(factoryEv({ n: 40, mod: one(3) }));
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toMatch(/92\.5% of 2400 building-rounds within 5%/);
    expect(v.reasons).toContain(S2.S2_RATES_FALLBACK);
  });
  it("fewer than the round floor fails", () => {
    expect(S2.judgeRates(factoryEv({ rounds: 39 })).reasons.join("\n")).toMatch(/rates compared in only 39 rounds.*need 40/);
  });
});

describe("evidence size", () => {
  it("a 340-machine world over 60 rounds, FRM read every 5 s with every rate changing each read, stays under 8 MB", () => {
    const n = 340;
    const jitter = (j: number, ms: CanonMachine[]) =>
      ms.map((m, i) => ({
        ...m,
        production: [{ ...m.production[0]!, currentPerMinute: 10 + ((i * 7 + j * 13) % 1000) / 777.77, percent: 50 + ((i * 3 + j * 11) % 1000) / 33.33 }],
        ingredients: [
          { ...m.ingredients![0]!, currentPerMinute: 15 + ((i * 5 + j * 17) % 1000) / 613.31, percent: 50 + ((i * 9 + j * 7) % 1000) / 41.17 },
          { ...m.ingredients![0]!, className: "Desc_Other_C", currentPerMinute: 3 + ((i * 11 + j * 19) % 1000) / 913.13, percent: 10 + ((i * 13 + j * 3) % 1000) / 57.77 },
        ],
      }));
    const ev = factoryEv({ n, frm: jitter, mod: (i, ms) => jitter(i * 6, ms) });
    const bytes = Buffer.byteLength(JSON.stringify(ev));
    expect(ev.frmSamples.length).toBeGreaterThan(360);
    expect(bytes).toBeLessThan(8 * 1024 * 1024);
    expect(S2.judgeFactory(ev).pass).toBe(true);
  });
  it("the mod's factory part for 340 machines is far under the recorder's 2 MB-per-snapshot cap, so the cap stays as it is", () => {
    const part = JSON.stringify({ buildings: fleet(340) });
    expect(Buffer.byteLength(part)).toBeLessThan(512 * 1024);
  });
});

// ------------------------------------------------------------------------------------------------------------ trains, stations

type Train = S2.MapLiveEvidence["rounds"][number]["trains"] extends infer T ? (T extends Array<infer U> ? U : never) : never;
const train = (id: string, name: string, x: number, y: number, status: string): Train => ({ id, name, x, y, status });
/** T-moving: self-driving at 2 m/s east (x = t_seconds * 2); T-parked: parked at (500, 500). */
const movingAt = (t: number) => Math.round(((t - T0) / 1000) * 2);
function liveEv(o: {
  rounds?: number;
  modTrains?: (i: number, base: Train[]) => Train[];
  frmTrains?: (t: number, base: Train[]) => Train[];
  modStations?: (i: number) => S2.MapLiveEvidence["rounds"][number]["stations"];
  frmStations?: (t: number) => NonNullable<S2.MapLiveEvidence["rounds"][number]["stations"]>;
  parkedToo?: boolean;
} = {}): S2.MapLiveEvidence {
  const rounds = o.rounds ?? ROUNDS;
  const at = (t: number): Train[] => [train("T-moving", "hash-m", movingAt(t), 0, "Self-Driving"), ...(o.parkedToo === false ? [] : [train("T-parked", "hash-p", 500, 500, "Parked")])];
  const trains: Array<S2.Bracket<Train[]>> = [];
  const stations: Array<S2.Bracket<NonNullable<S2.MapLiveEvidence["rounds"][number]["stations"]>>> = [];
  for (let t = T0 - 60_000; t <= T0 + rounds * 30_000 + 10_000; t += 2000) trains.push({ startedAt: iso(t), endedAt: iso(t + 100), value: o.frmTrains ? o.frmTrains(t, at(t)) : at(t) });
  for (let t = T0 - 60_000; t <= T0 + rounds * 30_000 + 10_000; t += 30_000) stations.push({ startedAt: iso(t), endedAt: iso(t + 100), value: o.frmStations ? o.frmStations(t) : [{ id: "S1", name: "hash-s", x: 10, y: 20 }] });
  const modRounds = Array.from({ length: rounds }, (_, i) => {
    const obs = T0 + i * 30_000;
    const base = at(obs - 3000); // the part was read a few seconds before the snapshot
    return { observedAt: iso(obs), trains: o.modTrains ? o.modTrains(i, base) : base, stations: o.modStations ? o.modStations(i) : [{ id: "S1", name: "hash-s", x: 10, y: 20 }] };
  });
  return { rounds: modRounds, trains, stations, trainsFrm: { reads: trains.length, failed: 0 }, stationsFrm: { reads: stations.length, failed: 0 } };
}

describe("R3 trains", () => {
  it("passes on equal data and reports what the world did not exercise", () => {
    const v = S2.judgeTrains(liveEv());
    expect(v.pass).toBe(true);
    expect(v.never).toEqual([]);
    expect(S2.judgeTrains(liveEv({ parkedToo: false })).never.join("\n")).toContain("Parked (no parked train)");
  });
  it("fails on a missing train id, naming it, and on a name hash that differs", () => {
    const missing = S2.judgeTrains(liveEv({ modTrains: (_, b) => b.slice(0, 1) }));
    expect(missing.pass).toBe(false);
    expect(missing.reasons.join("\n")).toContain("T-parked");
    const name = S2.judgeTrains(liveEv({ modTrains: (i, b) => (i === 7 ? b.map((t) => ({ ...t, name: "hash-x" })) : b) }));
    expect(name.pass).toBe(false);
    expect(name.reasons.join("\n")).toContain("name hash hash-x");
  });
  it("Self-Driving is compared exactly: a mismatch fails", () => {
    const v = S2.judgeTrains(liveEv({ modTrains: (i, b) => (i === 7 ? b.map((t) => (t.id === "T-moving" ? { ...t, status: "Manual" } : t)) : b) }));
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toContain("Self-Driving is compared exactly");
  });
  it("parked, manual and derailed mismatches are reported, not failed", () => {
    const v = S2.judgeTrains(liveEv({ modTrains: (i, b) => (i === 7 ? b.map((t) => (t.id === "T-parked" ? { ...t, status: "Derailed" } : t)) : b) }));
    expect(v.pass).toBe(true);
    expect(v.reasons.join("\n")).toMatch(/reported, not failed \(parked, manual or derailed\): 1 status mismatches/);
  });
  it("a status string that is none of the four known values fails (it is not 'reported')", () => {
    const odd = (status: string) => (i: number, b: Train[]) => (i === 7 ? b.map((t) => (t.id === "T-parked" ? { ...t, status } : t)) : b);
    const v = S2.judgeTrains(liveEv({ modTrains: odd("Docked") }));
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toContain("unknown status Docked");
    expect(S2.judgeTrains(liveEv({ modTrains: odd("Manual") })).pass).toBe(true);
    expect(S2.judgeTrains(liveEv({ frmTrains: (_, b) => b.map((t) => (t.id === "T-parked" ? { ...t, status: "Docked" } : t)), modTrains: odd("Manual") })).pass).toBe(false);
  });
  it("a world with no stationary train says so: the 1 m rule that exposes a position-source bias was not exercised", () => {
    expect(S2.judgeTrains(liveEv({ parkedToo: false })).never.join("\n")).toContain("a stationary train");
    expect(S2.judgeTrains(liveEv()).never.join("\n")).not.toContain("a stationary train");
  });
  it("reports the median signed along-path offset of the moving positions, never failing on it", () => {
    const behind = S2.judgeTrains(liveEv({ modTrains: (_, b) => b.map((t) => (t.id === "T-moving" ? { ...t, x: t.x - 3 } : t)) }));
    expect(behind.pass).toBe(true);
    expect(behind.reasons.join("\n")).toMatch(/median along-path offset of \d+ moving positions: -\d/);
    expect(behind.reasons.join("\n")).toContain("reported, never fails");
    const ahead = S2.judgeTrains(liveEv({ modTrains: (_, b) => b.map((t) => (t.id === "T-moving" ? { ...t, x: t.x + 3 } : t)) }));
    expect(ahead.reasons.join("\n")).toMatch(/median along-path offset of \d+ moving positions: \d/);
  });
  it("a moving train off the FRM track by 8 m passes and by 10 m fails (reads 4 m apart: limit 4 + 5 m from the nearer read)", () => {
    const side = (d: number) => (_: number, b: Train[]) => b.map((t) => (t.id === "T-moving" ? { ...t, y: t.y + d } : t));
    expect(S2.judgeTrains(liveEv({ modTrains: side(8) })).pass).toBe(true); // hypot(2, 8) = 8.2 <= 9
    const v = S2.judgeTrains(liveEv({ modTrains: side(10) })); // hypot(2, 10) = 10.2 > 9
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toContain("beyond what FRM's own reads allow");
  });
  it("a stationary train must be within 1 m: 1 m passes, 2 m fails", () => {
    const shift = (d: number) => (_: number, b: Train[]) => b.map((t) => (t.id === "T-parked" ? { ...t, x: t.x + d } : t));
    expect(S2.judgeTrains(liveEv({ modTrains: shift(1) })).pass).toBe(true);
    const v = S2.judgeTrains(liveEv({ modTrains: shift(2) }));
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toContain("T-parked position");
  });
  it("a long jump along the track fails", () => {
    const ahead = (d: number) => (_: number, b: Train[]) => b.map((t) => (t.id === "T-moving" ? { ...t, x: t.x + d } : t));
    const jump = S2.judgeTrains(liveEv({ modTrains: ahead(60) }));
    expect(jump.pass).toBe(false);
    expect(jump.reasons.join("\n")).toContain("beyond what FRM's own reads allow");
  });
  it("every position missing by about one train length hints at the position source", () => {
    const v = S2.judgeTrains(liveEv({ parkedToo: false, modTrains: (_, b) => b.map((t) => ({ ...t, x: t.x + 30 })) }));
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toContain("position source: try NativeTrainPositionSource LastVehicle");
    const one = S2.judgeTrains(liveEv({ modTrains: (i, b) => (i === 3 ? b.map((t) => ({ ...t, x: t.x + 30 })) : b) }));
    expect(one.reasons.join("\n")).not.toContain("LastVehicle");
  });
  it("a world with no trains passes with empty lists (not unreadable), reporting that nothing was exercised", () => {
    const v = S2.judgeTrains(liveEv({ modTrains: () => [], frmTrains: () => [] }));
    expect(v.pass).toBe(true);
    expect(v.never.join("\n")).toContain("any train");
  });
  it("fewer than the round floor fails; a round whose list is null is reported", () => {
    expect(S2.judgeTrains(liveEv({ rounds: 39 })).reasons.join("\n")).toMatch(/trains compared in only 39 rounds.*need 40/);
    const ev = liveEv();
    ev.rounds[2] = { ...ev.rounds[2]!, trains: null };
    expect(S2.judgeTrains(ev).pass).toBe(false);
  });
});

describe("R3 stations", () => {
  it("passes on equal data; an id, name or position difference fails", () => {
    expect(S2.judgeStations(liveEv()).pass).toBe(true);
    expect(S2.judgeStations(liveEv({ modStations: () => [] })).reasons.join("\n")).toContain("1 in FRM only");
    expect(S2.judgeStations(liveEv({ modStations: () => [{ id: "S1", name: "hash-z", x: 10, y: 20 }] })).reasons.join("\n")).toContain("name hash hash-z");
    expect(S2.judgeStations(liveEv({ modStations: () => [{ id: "S1", name: "hash-s", x: 11, y: 20 }] })).pass).toBe(true);
    expect(S2.judgeStations(liveEv({ modStations: () => [{ id: "S1", name: "hash-s", x: 12, y: 20 }] })).pass).toBe(false);
  });
  it("the same round floor", () => {
    expect(S2.judgeStations(liveEv({ rounds: 39 })).reasons.join("\n")).toMatch(/stations compared in only 39 rounds.*need 40/);
  });
});

// ----------------------------------------------------------------------------------------------------------------- trainRoutes

const layer: RouteRecord[] = [
  { recordType: "station", id: "S1", name: "hash-a", platforms: [{ id: "P1", mode: "load", items: ["Desc_A_C"] }] },
  { recordType: "station", id: "S2", name: "hash-b", platforms: [] },
  { recordType: "train", id: "T1", name: "hash-t", stops: ["hash-a", "hash-b"], items: ["Desc_A_C"] },
];
function routesEv(o: { post?: unknown; frm?: RouteRecord[]; posts?: number } = {}): S2.RoutesEvidence {
  const post = o.post ?? layer;
  const frm = canonRoutes(o.frm ?? layer);
  const n = o.posts ?? 2;
  return {
    posts: Array.from({ length: n }, (_, i) => ({ at: iso(T0 + 200_000 + i * 600_000), status: 200, data: post })),
    frmSamples: Array.from({ length: 31 }, (_, i) => ({ startedAt: iso(T0 + i * 60_000), endedAt: iso(T0 + i * 60_000 + 500), value: "h1" })),
    distinct: { h1: frm }, frm: { reads: 31, failed: 0 },
  };
}
describe("trainRoutes", () => {
  it("passes when the posted layer equals FRM's, whatever the order, and reports unexercised fields", () => {
    const v = S2.judgeRoutes(routesEv({ post: [...layer].reverse() }));
    expect(v.pass).toBe(true);
    expect(v.never).toEqual([]);
    expect(S2.judgeRoutes(routesEv({ post: [layer[1]!, layer[2]!], frm: [layer[1]!, layer[2]!] })).never.join("\n")).toContain("platform id, mode and items");
  });
  it("lists every difference", () => {
    const changed = layer.map((r) => (r.recordType === "train" ? { ...r, stops: ["hash-b", "hash-a"] } : r));
    const v = S2.judgeRoutes(routesEv({ post: changed }));
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toMatch(/1 in FRM and not in the mod, 1 in the mod and not in FRM/);
  });
  it("needs a post, a valid layer, and an FRM sample within 120 s", () => {
    expect(S2.judgeRoutes(routesEv({ posts: 0 })).reasons.join("\n")).toContain("posted no trainRoutes layer");
    expect(S2.judgeRoutes(routesEv({ post: [{ recordType: "station", id: "S1" }] })).reasons.join("\n")).toContain("not valid against the pinned contract");
    const ev = routesEv();
    expect(S2.judgeRoutes({ ...ev, frmSamples: [] }).reasons.join("\n")).toContain("no FRM getTrainStation+getTrains sample within 120 s");
  });
  it("an empty layer equal to FRM's empty layer is a pass, not unreadable", () => {
    const v = S2.judgeRoutes(routesEv({ post: [], frm: [] }));
    expect(v.pass).toBe(true);
    expect(v.never.join("\n")).toContain("any station");
  });
  it("one accepted post is enough (the window always holds one: the first lands 180-198 s after Running, then every 600 s)", () => {
    expect(S2.judgeRoutes(routesEv({ posts: 1 })).pass).toBe(true);
  });
});

// ------------------------------------------------------------------------------------------------------- the run's verdict

const PREFIX = "[2026.10.26-12:00:05:123][  0]LogSatisDash: Display: ";
const NATIVE_LINES = [
  `${PREFIX}Readers: players=native resourceNodes=native factory=native trains=native`,
  `${PREFIX}Resource node scan (game thread only): 180000 actors in 90 steps, 612 nodes, longest step 1.75 ms, total 41.50 ms`,
  `${PREFIX}Factory read (game thread only): 900 buildings in 400 steps, 6 machines, longest step 0.80 ms, total 12.00 ms, expected 6, boosted 0, productivity 0.100..0.900`,
  `${PREFIX}Trains read (game thread only): 2 trains, 1 stations in 3 steps, longest step 0.10 ms, total 0.30 ms`,
  `${PREFIX}Train routes read (game thread only): 2 stations, 1 trains in 3 steps, longest step 0.10 ms, total 0.30 ms`,
];
function s1Capture(): Pass.NativeParity {
  const obs = Array.from({ length: Pass.NEED_ROUNDS + 5 }, (_, i) => iso(T0 + i * 5000));
  const g = { groupId: 0, production: 100, consumption: 80, capacity: 120, maxConsumption: 110, batteryCapacity: 0, batteryPercent: 0, batteryDifferential: 0, fuseTriggered: false };
  const c = { circuitGroupId: 0, productionMW: 100, consumptionMW: 80, capacityMW: 120, maxConsumptionMW: 110, fuseTriggered: false, batteryCapacityMWh: 0, batteryPercent: 0, batteryDifferentialMW: 0 };
  const nodes = [{ type: "Iron Ore", purity: "normal", nodeType: "node", x: 1, y: 0, exploited: true }];
  return {
    windowStartedAt: iso(T0), windowEndedAt: iso(T0 + 31 * 60_000), accepted: obs.length, unrecorded: 0, refused: [],
    state: { codeConsumed: true, agentVersion: "mod/0.1.0", lastSeen: true, revoked: false },
    power: { rounds: obs.map((observedAt) => ({ observedAt, circuits: [c], before: [g], after: [g] })) as never, frm: { reads: 100, failed: 0 }, mergedGroupsMax: 1 },
    players: {
      rounds: obs.map((observedAt) => ({ observedAt, available: true, keys: ["h:true"] })),
      frmSamples: obs.flatMap((o) => [{ startedAt: iso(Date.parse(o) - 2000), endedAt: iso(Date.parse(o) - 1900), value: ["h:true"] }, { startedAt: iso(Date.parse(o) + 1000), endedAt: iso(Date.parse(o) + 1100), value: ["h:true"] }]),
      frm: { reads: 100, failed: 0 },
    },
    nodes: {
      posts: [{ at: iso(T0 + 8 * 60_000), status: 200, data: nodes }],
      frmSamples: [{ startedAt: iso(T0 + 7 * 60_000), endedAt: iso(T0 + 7 * 60_000 + 500), value: "h1" }],
      distinct: { h1: nodes }, frm: { reads: 2, failed: 0 },
    },
  };
}
const full = (): Pass.NativeParity => ({ ...s1Capture(), s2: { factory: factoryEv({ mod: (_, ms) => scaled(1)(ms) }), mapLive: liveEv(), routes: routesEv() } });
const evidence = (c: Pass.NativeParity, sources: unknown = NATIVE_SOURCES_S2, lines: string[] = NATIVE_LINES): CoreEvidence =>
  ({ lines, errorLines: [], leaks: { code: false, secret: false, bearer: false }, captures: { "native-parity.json": c, "mod-config.json": sources } });

describe("native-parity judge with the S2 readers", () => {
  it("judges eight readers, each by name, and passes a clean capture", () => {
    const v = Pass.evaluate(evidence(full()));
    expect(v.reasons.filter((r) => r.includes("FAIL"))).toEqual([]);
    expect(v.pass).toBe(true);
    for (const name of ["R0 power", "R1 players", "R4 resource nodes", "R2a factory", "R2b rates", "R3 trains", "R3 stations", "R3 trainRoutes"]) {
      expect(v.reasons.some((r) => r.startsWith(`${name} PASS`))).toBe(true);
    }
    expect(Object.keys(Pass.judgeReaders(full()))).toEqual(["power", "players", "resourceNodes", "factory", "rates", "trains", "stations", "trainRoutes"]);
  });
  it("an S1 capture and S1 sources are judged as before (no S2 lines asked for)", () => {
    const v = Pass.evaluate(evidence(s1Capture(), NATIVE_SOURCES, NATIVE_LINES.slice(0, 2)));
    expect(v.pass).toBe(true);
    expect(v.reasons.some((r) => r.includes("R2a"))).toBe(false);
    expect(Object.keys(Pass.judgeReaders(s1Capture()))).toEqual(["power", "players", "resourceNodes"]);
  });
  it("a failing reader does not change another's verdict, and the run is red", () => {
    const c = full();
    c.s2!.mapLive = liveEv({ modTrains: (_, b) => b.slice(0, 1) });
    const v = Pass.evaluate(evidence(c));
    expect(v.pass).toBe(false);
    expect(v.reasons.some((r) => r.startsWith("R3 trains FAIL"))).toBe(true);
    for (const name of ["R2a factory", "R2b rates", "R3 stations", "R3 trainRoutes"]) expect(v.reasons.some((r) => r.startsWith(`${name} PASS`))).toBe(true);
  });
  it("rates fails with the fallback text while the factory reader still passes", () => {
    const c = full();
    c.s2!.factory = factoryEv({ mod: (_, ms) => scaled(0.5)(ms) });
    const v = Pass.evaluate(evidence(c));
    expect(v.pass).toBe(false);
    expect(v.reasons.some((r) => r.startsWith("R2b rates FAIL") && r.includes("fallback = set DefaultRatesMode to Instantaneous"))).toBe(true);
    expect(v.reasons.some((r) => r.startsWith("R2a factory PASS"))).toBe(true);
    expect(v.reasons.join("\n")).toMatch(/raw productivity 0\.1\.\.0\.9/);
  });
  it("a raw productivity above 1.0001 in the mod's log fails rates with the scale reason", () => {
    const lines = NATIVE_LINES.map((l) => l.replace("productivity 0.100..0.900", "productivity 0.100..87.000"));
    const v = Pass.evaluate(evidence(full(), NATIVE_SOURCES_S2, lines));
    expect(v.reasons.some((r) => r.startsWith("R2b rates FAIL") && r.includes("scale:"))).toBe(true);
  });
  it("fails closed when the log shows a bridge reader or no completed read, even though the values equal FRM's", () => {
    const bridge = Pass.evaluate(evidence(full(), NATIVE_SOURCES_S2, [NATIVE_LINES[0]!.replace("factory=native trains=native", "factory=bridge trains=bridge"), ...NATIVE_LINES.slice(1)]));
    expect(bridge.pass).toBe(false);
    for (const name of ["R2a factory", "R2b rates", "R3 trains", "R3 stations", "R3 trainRoutes"]) expect(bridge.reasons.some((r) => r.startsWith(`${name} FAIL`))).toBe(true);
    const noRead = Pass.evaluate(evidence(full(), NATIVE_SOURCES_S2, NATIVE_LINES.filter((l) => !l.includes("Factory read") && !l.includes("Train routes read"))));
    expect(noRead.reasons.some((r) => r.startsWith("R2a factory FAIL") && r.includes('"Factory read (game thread only):"'))).toBe(true);
    expect(noRead.reasons.some((r) => r.startsWith("R3 trainRoutes FAIL") && r.includes('"Train routes read (game thread only):"'))).toBe(true);
    expect(noRead.reasons.some((r) => r.startsWith("R3 trains PASS"))).toBe(true);
    const s1Line = Pass.evaluate(evidence(full(), NATIVE_SOURCES_S2, [`${PREFIX}Readers: players=native resourceNodes=native`, ...NATIVE_LINES.slice(1)]));
    expect(s1Line.reasons.join("\n")).toContain("has no factory= field");
  });
  it("is red when the config asks for S2 but the capture has none, or the config lacks the S2 sources", () => {
    const lost = Pass.evaluate(evidence(s1Capture()));
    expect(lost.pass).toBe(false);
    expect(lost.reasons.join("\n")).toContain("S2 evidence (factory, mapLive, trainRoutes) was not captured");
    expect(lost.reasons.some((r) => r.startsWith("R2a factory FAIL"))).toBe(true);
    const cfg = Pass.evaluate(evidence(full(), NATIVE_SOURCES));
    expect(cfg.pass).toBe(false);
    expect(cfg.reasons.join("\n")).toContain("did not write factorySource and trainsSource as native");
  });
  it("lists what the world never exercised without failing (and NOT_EXERCISED_FAILS would flip it)", () => {
    const v = Pass.evaluate(evidence(full()));
    expect(v.reasons.join("\n")).toMatch(/R2b rates PASS.*\n?.*NOT EXERCISED by this world: Somersloop/s);
    expect(Pass.NOT_EXERCISED_FAILS).toBe(false);
  });
  it("readers.json is evaluate's final verdicts: a bridge line, a missing read line and a scale problem fail the matching reader there too", () => {
    const ok = evidence(full());
    Pass.evaluate(ok);
    const readers = ok.captures["readers.json"] as Record<string, { pass: boolean }>;
    expect(Object.keys(readers)).toEqual(["power", "players", "resourceNodes", "factory", "rates", "trains", "stations", "trainRoutes"]);
    expect(Object.values(readers).every((x) => x.pass)).toBe(true);
    const bridge = evidence(full(), NATIVE_SOURCES_S2, [NATIVE_LINES[0]!.replace("factory=native trains=native", "factory=bridge trains=bridge"), ...NATIVE_LINES.slice(1)]);
    Pass.evaluate(bridge);
    const b = bridge.captures["readers.json"] as Record<string, { pass: boolean }>;
    expect([b.factory!.pass, b.rates!.pass, b.trains!.pass, b.stations!.pass, b.trainRoutes!.pass]).toEqual([false, false, false, false, false]);
    expect(b.power!.pass).toBe(true);
    const noRead = evidence(full(), NATIVE_SOURCES_S2, NATIVE_LINES.filter((l) => !l.includes("Trains read")));
    Pass.evaluate(noRead);
    const n = noRead.captures["readers.json"] as Record<string, { pass: boolean }>;
    expect([n.trains!.pass, n.stations!.pass, n.factory!.pass]).toEqual([false, false, true]);
    const scale = evidence(full(), NATIVE_SOURCES_S2, NATIVE_LINES.map((l) => l.replace("productivity 0.100..0.900", "productivity 0.100..87.000")));
    Pass.evaluate(scale);
    const s = scale.captures["readers.json"] as Record<string, { pass: boolean }>;
    expect([s.rates!.pass, s.factory!.pass]).toEqual([false, true]);
    // S1 readers too: a bridge players line fails players in readers.json.
    const s1 = evidence(full(), NATIVE_SOURCES_S2, [NATIVE_LINES[0]!.replace("players=native", "players=bridge"), ...NATIVE_LINES.slice(1)]);
    Pass.evaluate(s1);
    expect((s1.captures["readers.json"] as Record<string, { pass: boolean }>).players!.pass).toBe(false);
  });
  it("lists the S2 readers among the run's requirements", () => {
    expect(Pass.requirements).toEqual(expect.arrayContaining(["native-parity", "R0-power", "R1-players", "R4-resource-nodes", "R2a-factory", "R2b-rates", "R3-trains", "R3-stations", "R3-train-routes"]));
  });
  it("the capture holds hashed names only", () => {
    expect(text(full())).not.toMatch(/Test Train|Test Station/);
  });
});
