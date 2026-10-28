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
    if (o.ids) {
      it("an id only in FRM fails", () => {
        const v = judge(mk({ frm: [...(mk({}).distinct.before!), frmExtra] }));
        expect(v.pass).toBe(false);
        expect(text(v)).toContain("in FRM only");
      });
      it("an id only in the mod fails", () => {
        const v = judge(mk({ post: [...(valid as unknown[]), extraId] }));
        expect(v.pass).toBe(false);
        expect(text(v)).toContain("in the mod only");
      });
    }
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
    ["maxPerMinute inside the 1% band (120 max: 1.2)", { maxPerMinute: 121.2 }, true],
    ["maxPerMinute past the 1% band", { maxPerMinute: 121.3 }, false],
    ["percent at 5 points", { percent: 55 }, true],
    ["percent past 5 points", { percent: 55.1 }, false],
    ["current at 5 points of max (6/min of 120)", { currentPerMinute: 66 }, true],
    ["current past 5 points of max", { currentPerMinute: 66.2 }, false],
  ])("%s", (_n, patch, pass) => {
    expect(B.judgeExtractors(eev({ post: [me({}, patch)] })).pass).toBe(pass);
  });
  it("max floor of 0.1/min applies when 1% is smaller (max 5: 5.1 passes, 5.11 fails)", () => {
    const frm = [fe({ max: 5, current: 2.5 })];
    expect(B.judgeExtractors(eev({ frm, post: [me({}, { maxPerMinute: 5.1, currentPerMinute: 2.5, percent: 50 })] })).pass).toBe(true);
    expect(B.judgeExtractors(eev({ frm, post: [me({}, { maxPerMinute: 5.11, currentPerMinute: 2.5, percent: 50 })] })).pass).toBe(false);
  });
  it("extra production entries are reported, not failed", () => {
    const m = me();
    const v = B.judgeExtractors(eev({ post: [{ ...m, production: [...m.production, ...m.production] }] }));
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("sent more than one production entry");
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
    ["circuit", { circuitGroupId: 2 }, "circuitGroupId"],
  ])("a %s mismatch against both brackets fails, counted per field", (_n, patch, field) => {
    const v = B.judgeExtractors(eev({ post: [me(patch)], after: [fe()] }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain(`${field}: 1 difference(s)`);
  });
  // Owner ruling 2026-10-28 after run 40623235820: isProducing and isPaused are judged as S2's dynamic flags (share >= S2_DYNAMIC_SHARE).
  it.each([
    ["isProducing", { isProducing: false }],
    ["isPaused", { isPaused: true }],
  ])("a %s mismatch on the only extractor fails (0% matched)", (field, patch) => {
    const v = B.judgeExtractors(eev({ post: [me(patch)], after: [fe()] }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain(`${field} mod=`);
    expect(text(v)).toContain("need 99%");
  });
  it("flags matching for exactly the S2 share pass, just below fail", () => {
    const many = (n: number) => Array.from({ length: n }, (_x, i) => fe({ id: `E${i}` }));
    const post = (misses: number) => many(100).map((f, i) => me({ id: f.id, ...(i < misses ? { isProducing: false } : {}) }));
    const run = (misses: number) => B.judgeExtractors(eev({ frm: many(100), after: many(100), post: post(misses) }));
    expect(run(0).pass).toBe(true);
    expect(run(1).pass).toBe(true);
    expect(text(run(1))).toContain("99.00% of 100");
    const bad = run(2);
    expect(bad.pass).toBe(false);
    expect(text(bad)).toContain("98.00% of 100");
  });
  it("isPaused is held to the same share: 1 of 100 passes, 2 of 100 fails", () => {
    const many = Array.from({ length: 100 }, (_x, i) => fe({ id: `E${i}` }));
    const run = (misses: number) => B.judgeExtractors(eev({ frm: many, after: many, post: many.map((f, i) => me({ id: f.id, ...(i < misses ? { isPaused: true } : {}) })) }));
    expect(run(1).pass).toBe(true);
    expect(run(2).pass).toBe(false);
  });
  it("an extractor missing both flags counts once", () => {
    const many = Array.from({ length: 100 }, (_x, i) => fe({ id: `E${i}` }));
    const v = B.judgeExtractors(eev({ frm: many, after: many, post: many.map((f, i) => me({ id: f.id, ...(i === 0 ? { isProducing: false, isPaused: true } : {}) })) }));
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("99.00% of 100");
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
    ["capacity at the 1% edge (75.75 of 75)", { capacityMW: 75.75, currentMW: 75 }, true],
    ["capacity just past 1% (75.76)", { capacityMW: 75.76, currentMW: 75 }, false],
    ["current at the 2% edge (1.5 MW of 75)", { currentMW: 76.5 }, true],
    ["current just past 2%", { currentMW: 76.51 }, false],
  ])("%s", (_n, patch, pass) => {
    expect(B.judgeGenerators(gev({ post: [mg(patch)] })).pass).toBe(pass);
  });
  it.each([
    ["capacity at the 0.1 MW floor (5 MW: 1% is 0.05)", { capacityMW: 5.1 }, true],
    ["capacity just past the 0.1 MW floor", { capacityMW: 5.11 }, false],
    ["current at the 0.5 MW floor (10 MW: 2% is 0.2)", { currentMW: 10.5 }, true],
    ["current just past the 0.5 MW floor", { currentMW: 10.51 }, false],
  ])("%s", (_n, patch, pass) => {
    const small = /capacity/.test(_n) ? { capacityMW: 5, currentMW: 0 } : { capacityMW: 10, currentMW: 10 };
    const body = mg({ capacityMW: small.capacityMW, currentMW: small.currentMW, ...patch });
    expect(B.judgeGenerators(gev({ frm: [fg(small)], post: [body] })).pass).toBe(pass);
  });
  it("current is judged against the capacity-scaled range between the brackets", () => {
    // FRM's capacity is 100 and its output 0 then 100; the mod's capacity is 101 (the 1% edge), so FRM's range scales to 0 to 101
    // (slack max(0.5, 2% of 101) = 2.02). Unscaled, the top of the band would be 102.
    const frm = [fg({ currentMW: 0, capacityMW: 100 })];
    const after = [fg({ currentMW: 100, capacityMW: 100 })];
    const mod = (currentMW: number) => gev({ frm, after, post: [mg({ capacityMW: 101, currentMW })] });
    expect(B.judgeGenerators(mod(40)).pass).toBe(true);
    // 103 passes only because of the scaling.
    expect(B.judgeGenerators(mod(103)).pass).toBe(true);
    expect(B.judgeGenerators(mod(103.03)).pass).toBe(false);
    expect(B.judgeGenerators(mod(110)).pass).toBe(false);
  });
  it("geothermal: only the capacity is excused when FRM says 0; a mod current above the band still fails", () => {
    const frm = [fg({ className: "Build_GeneratorGeoThermal_C", capacityMW: 0, currentMW: 0, fuelClass: null, supplementClass: null, supplementAmount: null })];
    const post = (currentMW: number) => [mg({ className: "Build_GeneratorGeoThermal_C", capacityMW: 200, currentMW, fuel: null, supplement: null })];
    expect(B.judgeGenerators(gev({ frm, post: post(0.5) })).pass).toBe(true);
    const v = B.judgeGenerators(gev({ frm, post: post(150) }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("currentMW");
  });
  it("a failed MW run quotes every bracket's raw map", () => {
    const v = B.judgeGenerators(gev({ post: [mg({ currentMW: 10 })], after: [fg({ raw: { RegulatedDemandProd: 74 } })] }));
    expect(text(v)).toContain("bracket 1");
    expect(text(v)).toContain("bracket 2");
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
  it("brackets of 0 and 10 against a mod rate of 50 fail; against 10 they pass", () => {
    expect(anchored([fs({ transfer: 0 })], [ms({ transferPerMinute: 50 })], [fs()]).pass).toBe(false);
    expect(anchored([fs({ transfer: 0 })], [ms()], [fs()]).pass).toBe(true);
    // A bracket at 0 passes only a rate of 0.5 or less.
    expect(anchored([fs({ transfer: 0 })], [ms({ transferPerMinute: 0.5 })], [fs({ transfer: 0.4 })]).pass).toBe(true);
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

/** Three posts (30 s apart) with an FRM sample between each; `mods[i]` is post i's container list, `frm` the same record in every sample. */
const threePosts = (mods: unknown[][], frm: FrmContainer[] = [fc()]): B.StorageEvidence => ({
  posts: mods.map((data, i) => ({ at: iso(T0 + i * 30_000), status: 200, data })),
  frmSamples: [-1, 0, 1, 2].map((k) => ({ startedAt: iso(T0 + k * 30_000 + 10_000), endedAt: iso(T0 + k * 30_000 + 20_000), value: "s" })).map((s, i) => ({ ...s, value: `s${i}` })),
  distinct: { s0: frm, s1: frm, s2: frm, s3: frm },
  frm: { reads: 5, failed: 0 },
});

// Both brackets carry the same FRM records, so a container can be stable (the owner ruling 2026-10-28 fails a run with none).
common("judgeStorage", B.judgeStorage, (o: Opts<FrmContainer>) => cev({ ...o, after: o.after ?? o.frm ?? [fc()] }), "storage", "getStorageInv", mc({ id: "C9" }), fc({ id: "C9" }), [mc()]);

describe("judgeStorage (the mod's own neighbouring posts, owner ruling 2026-10-28 after run 40623235820)", () => {
  const moved = mc({ slotsUsed: 1, items: [{ className: "Desc_Coal_C", name: "Coal", amount: 149, stackMax: 100 }] });
  const off = mc({ items: [{ className: "Desc_Coal_C", name: "Coal", amount: 149, stackMax: 100 }, { className: "Desc_OreIron_C", name: "Iron Ore", amount: 30, stackMax: 100 }] });
  // A second container C2 that never changes, so a run always has a stable container (the none-stable rule is tested apart).
  const c2 = mc({ id: "C2" });
  const run = (c1s: unknown[][]) => B.judgeStorage(threePosts(c1s.map((c) => [...c, c2]), [fc(), fc({ id: "C2" })]));
  it("FRM brackets equal but the mod's neighbours differ: contents are report-only", () => {
    // The middle post differs from FRM and from both neighbours; the first and third posts have a differing neighbour too: no amount check ran.
    const v = run([[mc()], [moved], [mc()]]);
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("3 changed between the mod's own neighbouring posts");
  });
  it("a post whose neighbours all agree with it is judged exactly: it fails when it differs from FRM", () => {
    const v = run([[off], [off], [off]]);
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("amount: ");
  });
  it("all agree with each other and with FRM: passes", () => {
    const v = run([[mc()], [mc()], [mc()]]);
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("6 container(s) unchanged between the brackets and across the mod's neighbouring posts");
  });
  it("a changing mod container that differs from FRM passes as report-only", () => {
    const v = run([[moved], [mc({ slotsUsed: 2 })], [moved]]);
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("3 changed between the mod's own neighbouring posts");
  });
  it("the first and last post check their one neighbour (the first differs from the second, the last agrees with FRM and the second)", () => {
    const v = run([[off], [mc()], [mc()]]);
    // Post 1 differs from its only neighbour: report-only. Post 2 has a differing neighbour: report-only. Post 3 agrees with post 2 and FRM: judged, passes.
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("2 changed between the mod's own neighbouring posts");
    // The last post alone differing from FRM and agreeing with its neighbour fails.
    expect(run([[mc()], [off], [off]]).pass).toBe(false);
  });
  it("a container in one post only has no neighbour holding it and is judged on the FRM brackets", () => {
    const v = run([[], [off], []]);
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("amount: ");
  });
  it("no stable container among those compared fails", () => {
    const v = B.judgeStorage(threePosts([[moved], [mc({ slotsUsed: 2 })], [moved]]));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("no stable container among 3 compared: contents never judged");
  });
});

describe("judgeStorage", () => {
  it("passes and lists NOT EXERCISED", () => {
    const v = B.judgeStorage(stable());
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("1 container(s) unchanged between the brackets");
    expect(v.never).toEqual(["a fluid buffer", "a container over 64 classes"]);
  });
  it("one bracket only is report-only for contents (and says so in NOT EXERCISED)", () => {
    const v = B.judgeStorage(cev({ post: [mc({ slotsUsed: 9, items: [] })] }));
    // Owner ruling 2026-10-28: nothing was stable, so the run fails, but the contents were not judged.
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("no stable container among 1 compared");
    expect(text(v)).not.toContain("slotsUsed:");
  });
  it("a container that changed between the brackets is report-only for contents (another container is stable)", () => {
    const c2 = fc({ id: "C2" });
    const v = B.judgeStorage(cev({ frm: [fc(), c2], after: [fc({ slotsUsed: 3, items: [{ class: "Desc_Coal_C", amount: 1, stackMax: 100 }] }), c2], post: [mc({ slotsUsed: 9, items: [] }), mc({ id: "C2" })] }));
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("1 container(s) changed between the brackets");
  });
  it("owner ruling 2026-10-28: containers compared but none stable fails; one stable passes as before", () => {
    const v = B.judgeStorage(cev({ after: [fc({ slotsUsed: 3 })] }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("no stable container among 1 compared: contents never judged");
    expect(B.judgeStorage(stable()).pass).toBe(true);
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
  const many = Array.from({ length: 70 }, (_, i) => ({ class: `Desc_I${String(i).padStart(2, "0")}_C`, amount: 1, stackMax: 100 }));
  const modItems = (xs: typeof many) => xs.map((x) => ({ className: x.class, name: "n", amount: x.amount, stackMax: 100 }));
  it("a container over 64 classes: the mod must hold exactly the first 64 in FRM's sorted order", () => {
    const frm = [fc({ items: many, slotsUsed: 70 })];
    const v = B.judgeStorage(stable(frm, [mc({ slotsUsed: 70, items: modItems(many.slice(0, 64)) })]));
    expect(v.pass).toBe(true);
    expect(v.never).not.toContain("a container over 64 classes");
    expect(B.judgeStorage(stable(frm, [mc({ slotsUsed: 70, items: modItems(many.slice(0, 60)) })])).pass).toBe(false);
    // Any other 64 of the 70 is no longer accepted.
    expect(text(B.judgeStorage(stable(frm, [mc({ slotsUsed: 70, items: modItems(many.slice(6, 70)) })])))).toContain("item classes");
    // A shared amount is still compared.
    expect(B.judgeStorage(stable(frm, [mc({ slotsUsed: 70, items: modItems(many.slice(0, 64)).map((x, i) => (i === 3 ? { ...x, amount: 2 } : x)) })])).pass).toBe(false);
  });
  it("the over-64 line stays NOT EXERCISED when the capped container was not judged exactly", () => {
    const frm = [fc({ items: many, slotsUsed: 70 })];
    const c2 = fc({ id: "C2" });
    const v = B.judgeStorage(cev({ frm: [...frm, c2], after: [fc({ items: many.slice(0, 5), slotsUsed: 5 }), c2], post: [mc({ slotsUsed: 70, items: modItems(many.slice(0, 64)) }), mc({ id: "C2" })] }));
    expect(v.pass).toBe(true);
    expect(v.never).toContain("a container over 64 classes");
  });
  it("FRM-side fluid signal: a matched mod container without fluidCapacityM3 fails; the line clears only with it", () => {
    const frm = [ftank(100)];
    const v = B.judgeStorage(stable(frm, [tank(100)]));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("fluidCapacityM3");
    expect(B.judgeStorage(stable(frm, [tank(100, 400)])).never).not.toContain("a fluid buffer");
    // A mod fluid buffer with no FRM fluid evidence does not clear the line.
    expect(B.judgeStorage(stable([fc({ slots: 0, slotsUsed: 0, items: [{ class: "Desc_Water_C", amount: 100, stackMax: 100 }] })], [{ ...tank(100, 400), items: [{ className: "Desc_Water_C", name: "w", amount: 100, stackMax: null }] }])).never).toContain("a fluid buffer");
  });
  it("a mod container with no slots but items and no fluidCapacityM3 fails", () => {
    const v = B.judgeStorage(stable([fc({ slots: null, slotsUsed: null })], [mc({ slots: 0, slotsUsed: 0 })]));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("no slots and holds items");
  });
  it("a repeated item class in a mod container fails", () => {
    const dup = mc({ items: [...mc().items, mc().items[0]!] });
    const v = B.judgeStorage(cev({ post: [dup] }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("repeats an item class");
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
