import { describe, expect, it } from "vitest";
import type { FrmContainer, FrmExtractor, FrmGenerator, FrmStationRate } from "../src/frm-s4b";
import * as B from "../../runs/native-parity/pass-s4b";

const T0 = Date.parse("2026-10-28T12:00:00.000Z");
const iso = (ms: number) => new Date(ms).toISOString();
const text = (v: B.S4bResult) => v.reasons.join("\n");

/** Evidence with one FRM sample before the post (and one after, when `after` is given). */
function ev<F>(o: { post?: unknown; posts?: B.StorageEvidence["posts"]; frm?: F[]; after?: F[]; frmSamples?: B.StorageEvidence["frmSamples"]; failed?: number }, defFrm: F[], defPost: unknown): B.S4bEvidence<F> {
  return {
    posts: o.posts ?? [{ at: iso(T0), status: 200, data: o.post ?? defPost }],
    frmSamples: o.frmSamples ?? [
      { startedAt: iso(T0 - 20_000), endedAt: iso(T0 - 10_000), value: "before" },
      ...(o.after ? [{ startedAt: iso(T0 + 10_000), endedAt: iso(T0 + 20_000), value: "after" }] : []),
    ],
    distinct: { before: o.frm ?? defFrm, after: o.after ?? [] },
    frm: { reads: 5, failed: o.failed ?? 0 },
  };
}
type Opts<F> = { post?: unknown; posts?: B.StorageEvidence["posts"]; frm?: F[]; after?: F[]; frmSamples?: B.StorageEvidence["frmSamples"]; failed?: number };

/** The cases every judge shares. */
function common<F>(name: string, judge: (e: B.S4bEvidence<F>) => B.S4bResult, mk: (o: Opts<F>) => B.S4bEvidence<F>, layer: string, endpoint: string, extraId: unknown, frmExtra: F, valid: unknown, o: { ids: boolean; empty: string } = { ids: true, empty: "both sides empty" }) {
  describe(`${name} (shared rules)`, () => {
    it("identical data passes", () => expect(judge(mk({})).pass).toBe(true));
    // Station ids follow the controller's own rule (tested below), not the equal-id-set rule.
    (o.ids ? it : it.skip)("an id only in FRM fails", () => {
      const v = judge(mk({ frm: [...(mk({}).distinct.before!), frmExtra] }));
      expect(v.pass).toBe(false);
      expect(text(v)).toContain("in FRM only");
    });
    (o.ids ? it : it.skip)("an id only in the mod fails", () => {
      const v = judge(mk({ post: [...(valid as unknown[]), extraId] }));
      expect(v.pass).toBe(false);
      expect(text(v)).toContain("in the mod only");
    });
    it("an invalid post fails", () => {
      const v = judge(mk({ post: [{ id: 5 }] }));
      expect(v.pass).toBe(false);
      expect(text(v)).toContain("not valid against the pinned contract");
    });
    it("no post fails", () => {
      const v = judge(mk({ posts: [] }));
      expect(v.pass).toBe(false);
      expect(text(v)).toContain(`posted no ${layer}`);
    });
    it("no FRM sample within 120 s fails", () => {
      const v = judge(mk({ frmSamples: [{ startedAt: iso(T0 - 300_000), endedAt: iso(T0 - 290_000), value: "before" }] }));
      expect(v.pass).toBe(false);
      expect(text(v)).toContain(`no FRM ${endpoint} sample within 120 s`);
    });
    it("failed FRM reads with no sample fail", () => {
      const v = judge({ ...mk({}), frm: { reads: 3, failed: 3 }, frmSamples: [] });
      expect(v.pass).toBe(false);
      expect(text(v)).toContain(`no FRM ${endpoint} sample succeeded`);
    });
    it("zero items on both sides passes with NOT EXERCISED", () => {
      const v = judge(mk({ post: [], frm: [] }));
      expect(v.pass).toBe(true);
      expect(v.never).toEqual([expect.stringContaining(o.empty)]);
    });
  });
}

// ------------------------------------------------------------------------------------------------------------------ extractors

const fe = (o: Partial<FrmExtractor> = {}): FrmExtractor => ({
  id: "E1", className: "Build_MinerMk1_C", producing: true, paused: false, backedUp: null, raw: {}, resourceClass: "Desc_OreIron_C",
  current: 60, max: 120, percent: 50, clockPercent: 100, circuit: 1, ...o,
});
const me = (o: Record<string, unknown> = {}, p: Record<string, unknown> = {}) => ({
  id: "E1", name: "Miner Mk.1", className: "Build_MinerMk1_C", recipe: null, isProducing: true, isPaused: false, isBackedUp: false, circuitGroupId: 1,
  production: [{ name: "Iron Ore", className: "Desc_OreIron_C", currentPerMinute: 60, maxPerMinute: 120, percent: 50, ...p }], ...o,
});
const eev = (o: Opts<FrmExtractor> = {}) => ev(o, [fe()], [me()]);

common("judgeExtractors", B.judgeExtractors, eev, "extractors", "getExtractor", me({ id: "E9" }), fe({ id: "E9" }), [me()]);

describe("judgeExtractors", () => {
  it("passes and lists NOT EXERCISED", () => {
    const v = B.judgeExtractors(eev());
    expect(v.pass).toBe(true);
    expect(v.never).toEqual(["a fluid extractor (pump)", "an occupied-and-backed-up extractor"]);
  });
  it("isBackedUp is report-only: a true flag never fails", () => {
    const v = B.judgeExtractors(eev({ post: [me({ isBackedUp: true })] }));
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("isBackedUp: report-only (controller ruling, 2026-10-27");
    expect(v.never).toEqual(["a fluid extractor (pump)"]);
  });
  it("a pump is exercised", () => {
    const v = B.judgeExtractors(eev({ frm: [fe({ className: "Build_OilPump_C" })], post: [me({ className: "Build_OilPump_C" })] }));
    expect(v.never).not.toContain("a fluid extractor (pump)");
  });
  it.each([
    ["maxPerMinute at the edge (0.1)", { maxPerMinute: 120.1 }, true],
    ["maxPerMinute past the edge", { maxPerMinute: 121.3 }, false],
    ["percent at 5 points", { percent: 55 }, true],
    ["percent past 5 points", { percent: 55.1 }, false],
    ["current at 5 points of max (6/min of 120)", { currentPerMinute: 66 }, true],
    ["current past 5 points of max", { currentPerMinute: 66.2 }, false],
  ])("%s", (_n, patch, pass) => {
    expect(B.judgeExtractors(eev({ post: [me({}, patch)] })).pass).toBe(pass);
  });
  it("max uses 1% when that is larger (2400/min: 24 passes, 24.1 fails)", () => {
    const frm = [fe({ max: 2400 })];
    expect(B.judgeExtractors(eev({ frm, post: [me({}, { maxPerMinute: 2424, currentPerMinute: 60 })] })).pass).toBe(true);
    expect(B.judgeExtractors(eev({ frm, post: [me({}, { maxPerMinute: 2424.1, currentPerMinute: 60 })] })).pass).toBe(false);
  });
  it("current and percent between the two brackets pass", () => {
    const frm = [fe({ current: 0, percent: 0 })];
    const after = [fe({ current: 120, percent: 100 })];
    expect(B.judgeExtractors(eev({ frm, after, post: [me({}, { currentPerMinute: 60, percent: 50 })] })).pass).toBe(true);
    // With one bracket only the same values are out of range.
    expect(B.judgeExtractors(eev({ frm, post: [me({}, { currentPerMinute: 60, percent: 50 })] })).pass).toBe(false);
  });
  it.each([
    ["className", { className: "Build_MinerMk2_C" }, "className"],
    ["isProducing", { isProducing: false }, "isProducing"],
    ["isPaused", { isPaused: true }, "isPaused"],
    ["circuit", { circuitGroupId: 2 }, "circuitGroupId"],
  ])("a %s mismatch against both brackets fails, counted per field", (_n, patch, field) => {
    const v = B.judgeExtractors(eev({ post: [me(patch)], after: [fe()] }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain(`${field}: 1 difference(s)`);
  });
  it("a resource class mismatch fails", () => {
    expect(text(B.judgeExtractors(eev({ post: [me({}, { className: "Desc_Coal_C" })] })))).toContain("resourceClass");
  });
  it("a state that matches the other bracket passes", () => {
    expect(B.judgeExtractors(eev({ frm: [fe({ producing: false })], after: [fe()] })).pass).toBe(true);
  });
  it("an unoccupied extractor with no production passes when FRM has none", () => {
    const v = B.judgeExtractors(eev({ frm: [], post: [me({ production: [] })] }));
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("no production in the mod and none in FRM");
  });
  it("an extractor with no production in the mod fails when FRM has one", () => {
    expect(B.judgeExtractors(eev({ post: [me({ production: [] })] })).pass).toBe(false);
  });
});

// ------------------------------------------------------------------------------------------------------------------ generators

const fg = (o: Partial<FrmGenerator> = {}): FrmGenerator => ({
  id: "G1", className: "Build_GeneratorCoal_C", nameHash: "a1a1a1a1a1a1", currentMW: 75, capacityMW: 75, clockPercent: 100, producing: true,
  fuelClass: "Desc_Coal_C", fuelAmount: 20, supplementClass: "Desc_Water_C", supplementAmount: 100, raw: { RegulatedDemandProd: 75, ProductionCapacity: 75 }, circuit: 1, ...o,
});
const mg = (o: Record<string, unknown> = {}) => ({
  id: "G1", name: "Coal Generator", className: "Build_GeneratorCoal_C", circuitGroupId: 1, currentMW: 75, capacityMW: 75, isProducing: true,
  fuel: { className: "Desc_Coal_C", name: "Coal", amount: 20 }, supplement: { className: "Desc_Water_C", name: "Water", amount: 100 }, ...o,
});
const gev = (o: Opts<FrmGenerator> = {}) => ev(o, [fg()], [mg()]);

common("judgeGenerators", B.judgeGenerators, gev, "generators", "getGenerators", mg({ id: "G9" }), fg({ id: "G9" }), [mg()]);

describe("judgeGenerators", () => {
  it("passes; a supplement and an FRM-side check are exercised, nuclear and geothermal are not", () => {
    const v = B.judgeGenerators(gev());
    expect(v.pass).toBe(true);
    expect(v.never).toEqual(["a nuclear generator", "a geothermal generator"]);
  });
  it("a fuel key missing from the post is invalid", () => {
    const { fuel: _f, ...noFuel } = mg();
    expect(B.judgeGenerators(gev({ post: [noFuel] })).pass).toBe(false);
  });
  it.each([
    ["capacity at 0.1 MW", { capacityMW: 75.1, currentMW: 75.1 }, true],
    ["capacity past 0.1 MW and 1%", { capacityMW: 76, currentMW: 76 }, false],
    ["current at 1.5 MW (2% of 75)", { currentMW: 76.5 }, true],
    ["current past 2%", { currentMW: 76.6 }, false],
  ])("%s", (_n, patch, pass) => {
    expect(B.judgeGenerators(gev({ post: [mg(patch)] })).pass).toBe(pass);
  });
  it("current is judged against the capacity-scaled range between the brackets", () => {
    const frm = [fg({ currentMW: 0, capacityMW: 150 })];
    const after = [fg({ currentMW: 150, capacityMW: 150 })];
    // The mod's capacity 75 halves FRM's 150: the scaled range is 0 to 75.
    expect(B.judgeGenerators(gev({ frm, after, post: [mg({ capacityMW: 150, currentMW: 100 })] })).pass).toBe(true);
    expect(B.judgeGenerators(gev({ frm, after, post: [mg({ capacityMW: 150, currentMW: 150 })] })).pass).toBe(true);
  });
  it("a failed MW run quotes the oracle's raw map (ids and numbers only)", () => {
    const v = B.judgeGenerators(gev({ post: [mg({ currentMW: 10 })] }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("raw FRM numbers for G1");
    expect(text(v)).toContain("RegulatedDemandProd=75");
  });
  it.each([
    ["className", { className: "Build_GeneratorFuel_C" }, "className"],
    ["fuel class", { fuel: { className: "Desc_Wood_C", name: "x", amount: 1 } }, "fuel class"],
    ["a missing fuel", { fuel: null }, "fuel class"],
    ["supplement class", { supplement: null }, "supplement class"],
    ["circuit", { circuitGroupId: 7 }, "circuitGroupId"],
  ])("a %s mismatch fails", (_n, patch, field) => {
    const v = B.judgeGenerators(gev({ post: [mg(patch)] }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain(field);
  });
  it("null equals null for fuel and supplement", () => {
    const v = B.judgeGenerators(gev({ frm: [fg({ fuelClass: null, fuelAmount: null, supplementClass: null, supplementAmount: null })], post: [mg({ fuel: null, supplement: null })] }));
    expect(v.pass).toBe(true);
  });
  it("isProducing and the amounts are report-only", () => {
    const v = B.judgeGenerators(gev({ post: [mg({ isProducing: false, fuel: { className: "Desc_Coal_C", name: "Coal", amount: 15 }, supplement: { className: "Desc_Water_C", name: "Water", amount: 90 } })] }));
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("isProducing: report-only (controller ruling, 2026-10-27");
    expect(text(v)).toContain("1 generator(s) differ from both brackets");
    expect(text(v)).toContain("fuel amounts: report-only");
    expect(text(v)).toContain("the largest 5 on G1");
    expect(text(v)).toContain("the largest 10 on G1");
  });
  it("a geothermal capacity is report-only when FRM says 0; nuclear is exercised by class", () => {
    const frm = [fg({ className: "Build_GeneratorGeoThermal_C", capacityMW: 0, currentMW: 0, fuelClass: null, supplementClass: null, supplementAmount: null })];
    const v = B.judgeGenerators(gev({ frm, post: [mg({ className: "Build_GeneratorGeoThermal_C", capacityMW: 200, currentMW: 0, fuel: null, supplement: null })] }));
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("capacity report-only (V5)");
    expect(v.never).not.toContain("a geothermal generator");
  });
});

// -------------------------------------------------------------------------------------------------------------------- stations

const H1 = "a1a1a1a1a1a1", H2 = "b2b2b2b2b2b2";
const fs = (o: Partial<FrmStationRate> = {}): FrmStationRate => ({ id: "S1", nameHash: H1, transfer: 10, platformSum: 10, platformCount: 1, platforms: [{ className: "Build_TrainDockingStation_C", rate: 10 }], ...o });
const ms = (o: Record<string, unknown> = {}) => ({ id: "S1", name: H1, x: 100, y: 200, transferPerMinute: 10, ...o });
const sev = (o: Opts<FrmStationRate> = {}) => ev(o, [fs()], [ms()]);

common("judgeStations", B.judgeStations, sev, "station rates", "getTrainStation", ms({ id: "S9" }), fs({ id: "S9" }), [ms()], { ids: false, empty: "a rate over 1/min" });

describe("judgeStations", () => {
  it("identical rates pass with the factor line and no NOT EXERCISED", () => {
    const v = B.judgeStations(sev());
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("rate factor: median m/f 1");
    expect(v.never).toEqual([]);
  });
  // Four exact stations anchor the factor median at 1, so these cases test the per-station band alone.
  const anchors = Array.from({ length: 4 }, (_, i) => fs({ id: `A${i}`, transfer: 50 }));
  const anchored = (frm: FrmStationRate[], post: unknown[], after?: FrmStationRate[]) =>
    B.judgeStations(sev({ frm: [...anchors, ...frm], after: after && [...anchors, ...after], post: [...anchors.map((a) => ms({ id: a.id, transferPerMinute: 50 })), ...post] }));
  it.each([
    ["at the edge (25% of 10 = 2.5)", 12.5, true],
    ["past the edge", 12.6, false],
    ["at the low edge", 7.5, true],
    ["below the low edge", 7.4, false],
  ])("a rate %s", (_n, rate, pass) => {
    expect(anchored([fs()], [ms({ transferPerMinute: rate })]).pass).toBe(pass);
  });
  it("the 0.5 floor applies to a small rate (f = 1: 1.5 passes, 1.6 fails)", () => {
    expect(anchored([fs({ transfer: 1 })], [ms({ transferPerMinute: 1.5 })]).pass).toBe(true);
    expect(anchored([fs({ transfer: 1 })], [ms({ transferPerMinute: 1.6 })]).pass).toBe(false);
  });
  it("either bracket may match", () => {
    expect(anchored([fs({ transfer: 30 })], [ms()], [fs()]).pass).toBe(true);
  });
  it("a wrong factor (ratio 2) fails with the hint and the raw pairs", () => {
    const frm = [fs({ id: "S1", transfer: 10 }), fs({ id: "S2", transfer: 20 })];
    const v = B.judgeStations(sev({ frm, post: [ms({ transferPerMinute: 20 }), ms({ id: "S2", transferPerMinute: 40 })] }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("unit factor suspect: median ratio 2 (NativeStationRateToPerMinute in NativeTrains.h)");
    expect(text(v)).toContain("S1 mod=20 frm=10");
  });
  it("the median at 0.8 and 1.25 passes, just past fails the factor", () => {
    const frm = Array.from({ length: 5 }, (_, i) => fs({ id: `S${i}`, transfer: 100 }));
    const at = (r: number) => B.judgeStations(sev({ frm, post: frm.map((f) => ms({ id: f.id, transferPerMinute: 100 * r })) }));
    expect(at(1.25).pass).toBe(true);
    expect(at(0.8).pass).toBe(true);
    expect(text(at(1.26))).toContain("unit factor suspect");
    expect(text(at(0.79))).toContain("unit factor suspect");
  });
  it("a station FRM rates above 0 without a mod rate fails", () => {
    const v = B.judgeStations(sev({ post: [ms({ transferPerMinute: undefined })] }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("1 station ids FRM rates above 0 with no rate in the mod");
  });
  it("a mod rate where FRM's is 0 or absent is reported, not failed", () => {
    const v = B.judgeStations(sev({ frm: [fs({ transfer: 0 })], post: [ms({ transferPerMinute: 3 })] }));
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("1 mod rate(s) where FRM's rate is 0 or absent");
    expect(text(v)).toContain("NOT EXERCISED: rate factor");
    expect(v.never).toEqual(["a rate over 1/min"]);
    const w = B.judgeStations(sev({ frm: [], post: [ms({ transferPerMinute: 3 })] }));
    expect(w.pass).toBe(true);
  });
  it("zero on both sides with FRM at 0 passes NOT EXERCISED", () => {
    const v = B.judgeStations(sev({ frm: [fs({ transfer: 0 })], post: [ms({ transferPerMinute: 0 })] }));
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("NOT EXERCISED: rate factor");
  });
  it("inflow and outflow must be absent", () => {
    expect(text(B.judgeStations(sev({ post: [ms({ inflowPerMinute: 1 })] })))).toContain("must be absent (R3)");
    expect(B.judgeStations(sev({ post: [ms({ outflowPerMinute: 0 })] })).pass).toBe(false);
  });
  it("a raw name fails and is never printed", () => {
    const v = B.judgeStations(sev({ post: [ms({ name: "Secret Depot Name" })] }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("not a hash");
    expect(text(v)).not.toContain("Secret");
  });
});

// ---------------------------------------------------------------------------------------------------------------------- storage

const fc = (o: Partial<FrmContainer> = {}): FrmContainer => ({
  id: "C1", className: "Build_StorageContainerMk1_C", nameHash: H2, slots: 24, slotsUsed: 2, fluidCapacity: null,
  items: [{ class: "Desc_Coal_C", amount: 150, stackMax: 100 }, { class: "Desc_OreIron_C", amount: 30, stackMax: 100 }], ...o,
});
const mc = (o: Record<string, unknown> = {}) => ({
  id: "C1", name: "Storage Container", className: "Build_StorageContainerMk1_C", slots: 24, slotsUsed: 2,
  items: [{ className: "Desc_Coal_C", name: "Coal", amount: 150, stackMax: 100 }, { className: "Desc_OreIron_C", name: "Iron Ore", amount: 30, stackMax: 100 }], ...o,
});
const cev = (o: Opts<FrmContainer> = {}) => ev(o, [fc()], [mc()]);
/** Both brackets hold the same container: the exact rule applies. */
const stable = (frm: FrmContainer[] = [fc()], post: unknown[] = [mc()]) => cev({ frm, after: frm, post });

common("judgeStorage", B.judgeStorage, cev, "storage", "getStorageInv", mc({ id: "C9" }), fc({ id: "C9" }), [mc()]);

describe("judgeStorage", () => {
  it("passes and lists NOT EXERCISED", () => {
    const v = B.judgeStorage(stable());
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("1 container(s) unchanged between the brackets");
    expect(v.never).toEqual(["a fluid buffer", "a container over 64 classes"]);
  });
  it("one bracket only is report-only for contents (and says so in NOT EXERCISED)", () => {
    const v = B.judgeStorage(cev({ post: [mc({ slotsUsed: 9, items: [] })] }));
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("1 had one bracket only");
    expect(v.never).toContain("a container unchanged between the brackets (exact contents rule)");
  });
  it("a container that changed between the brackets is report-only for contents", () => {
    const v = B.judgeStorage(cev({ after: [fc({ slotsUsed: 3, items: [{ class: "Desc_Coal_C", amount: 1, stackMax: 100 }] })], post: [mc({ slotsUsed: 9, items: [] })] }));
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("1 container(s) changed between the brackets");
  });
  it.each([
    ["className", { className: "Build_Other_C" }, "className"],
    ["slots", { slots: 25 }, "slots"],
    ["slotsUsed", { slotsUsed: 3 }, "slotsUsed"],
    ["a solid amount one off", { items: [{ className: "Desc_Coal_C", name: "Coal", amount: 151, stackMax: 100 }, { className: "Desc_OreIron_C", name: "x", amount: 30, stackMax: 100 }] }, "amount"],
    ["a missing class", { items: [{ className: "Desc_Coal_C", name: "Coal", amount: 150, stackMax: 100 }] }, "item classes"],
    ["an extra class", { items: [...mc().items, { className: "Desc_Wood_C", name: "x", amount: 1, stackMax: 100 }] }, "item classes"],
    ["stackMax", { items: [{ className: "Desc_Coal_C", name: "Coal", amount: 150, stackMax: 50 }, { className: "Desc_OreIron_C", name: "x", amount: 30, stackMax: 100 }] }, "stackMax"],
  ])("a stable container with %s fails", (_n, patch, field) => {
    const v = B.judgeStorage(stable([fc()], [mc(patch)]));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain(field);
  });
  it("stackMax and className are exact even for a changed container", () => {
    const v = B.judgeStorage(cev({ after: [fc({ slotsUsed: 5 })], post: [mc({ className: "Build_Other_C" })] }));
    expect(v.pass).toBe(false);
  });
  const tank = (amount: number, cap?: number) => mc({ slots: 0, slotsUsed: 0, ...(cap === undefined ? {} : { fluidCapacityM3: cap }), items: [{ className: "Desc_Water_C", name: "Water", amount, stackMax: null }] });
  const ftank = (amount: number) => fc({ slots: null, slotsUsed: null, items: [{ class: "Desc_Water_C", amount, stackMax: null }] });
  it.each([
    ["within 0.5%", 100.5, true],
    ["past 0.5%", 100.6, false],
  ])("a stable fluid amount %s", (_n, amount, pass) => {
    expect(B.judgeStorage(stable([ftank(100)], [tank(amount, 400)])).pass).toBe(pass);
  });
  it("a stable fluid amount within 0.01 m3 passes when that is larger than 0.5%", () => {
    expect(B.judgeStorage(stable([ftank(1)], [tank(1.01, 400)])).pass).toBe(true);
    expect(B.judgeStorage(stable([ftank(1)], [tank(1.02, 400)])).pass).toBe(false);
  });
  it("fluidCapacityM3 is report-only against FRM and a fluid buffer is exercised", () => {
    const v = B.judgeStorage(stable([ftank(100)], [tank(100, 400)]));
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("fluidCapacityM3: value report-only (controller ruling, 2026-10-27");
    expect(v.never).not.toContain("a fluid buffer");
  });
  it("a fluid item must have stackMax null", () => {
    const bad = mc({ slots: 0, slotsUsed: 0, fluidCapacityM3: 400, items: [{ className: "Desc_Water_C", name: "Water", amount: 100, stackMax: 100 }] });
    const v = B.judgeStorage(stable([ftank(100)], [bad]));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("is a fluid and must be null");
  });
  it("a depot uploader is compared like any container", () => {
    const u = fc({ id: "U1", className: "Build_CentralStorage_C", slots: null, slotsUsed: null, items: [] });
    const m = mc({ id: "U1", className: "Build_CentralStorage_C", slots: 0, slotsUsed: 0, items: [] });
    const v = B.judgeStorage(stable([u], [m]));
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("1 depot uploader(s)");
  });
  it("a container over 64 classes: the mod must hold 64 of FRM's classes", () => {
    const many = Array.from({ length: 70 }, (_, i) => ({ class: `Desc_I${String(i).padStart(2, "0")}_C`, amount: 1, stackMax: 100 }));
    const frm = [fc({ items: many, slotsUsed: 70 })];
    const post = [mc({ slotsUsed: 70, items: many.slice(0, 64).map((x) => ({ className: x.class, name: "n", amount: 1, stackMax: 100 })) })];
    const v = B.judgeStorage(stable(frm, post));
    expect(v.pass).toBe(true);
    expect(v.never).not.toContain("a container over 64 classes");
    expect(B.judgeStorage(stable(frm, [mc({ slotsUsed: 70, items: post[0]!.items.slice(0, 60) })])).pass).toBe(false);
  });
});

// ----------------------------------------------------------------------------------------------------------------------- names

describe("no raw name enters a reason", () => {
  it("every failing and passing reason is free of the posted names", () => {
    const secret = "Zq Private Name";
    const texts = [
      B.judgeExtractors(eev({ post: [me({ name: secret, className: "Build_Other_C" })] })),
      B.judgeGenerators(gev({ post: [mg({ name: secret, currentMW: 1 })] })),
      B.judgeStations(sev({ post: [ms({ name: secret })] })),
      B.judgeStorage(stable([fc()], [mc({ name: secret, slots: 1 })])),
    ].map(text);
    for (const t of texts) expect(t).not.toContain(secret);
  });
});
