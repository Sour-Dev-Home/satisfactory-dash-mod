import { describe, expect, it } from "vitest";
import { mapFrmExtractors, mapFrmGenerators, mapFrmStationRates, mapFrmStorage } from "../src/frm-s4b";
import { evidenceHash } from "../src/evidence-hash";

// Synthetic bodies in the shape of FRM's output; every name and id here is made up.
const extractor = (o: Record<string, unknown> = {}) => ({
  ID: "Build_MinerMk1_C_1", Name: "Secret Miner", ClassName: "Build_MinerMk1_C", Recipe: "Iron Ore", RecipeClassName: "Desc_OreIron_C",
  production: [{ Name: "Iron Ore", ClassName: "Desc_OreIron_C", Amount: 100, MaxAmount: 100, CurrentProd: 18.5, MaxProd: 120, ProdPercent: 15.4 }],
  ManuSpeed: 100, IsConfigured: true, IsProducing: true, IsPaused: false, PowerInfo: { CircuitGroupID: 6, CircuitID: 0 }, ...o,
});
const generator = (o: Record<string, unknown> = {}) => ({
  ID: "Build_GeneratorCoal_C_1", Name: "Secret Plant", ClassName: "Build_GeneratorCoal_C", RegulatedDemandProd: 75, ProductionCapacity: 75,
  CurrentPotential: 100, FuelAmount: 0.9, Supplement: { Name: "Water", ClassName: "Desc_Water_C" },
  FuelInventory: [{ Name: "Coal", ClassName: "Desc_Coal_C", Amount: 26, MaxAmount: 100 }, { Name: "Water", ClassName: "Desc_Water_C", Amount: 50, MaxAmount: 50 }],
  PowerInfo: { CircuitGroupID: 6 }, ...o,
});
const station = (o: Record<string, unknown> = {}) => ({
  ID: "Build_TrainStation_C_1", Name: "Secret Station", ClassName: "Build_TrainStation_C", TransferRate: 2, InflowRate: 0, OutflowRate: 0,
  CargoInventory: [{ ID: "p1", Name: "Freight Platform", TransferRate: 1.5 }, { ID: "p2", Name: "Freight Platform", TransferRate: 0.5 }], ...o,
});
const box = (o: Record<string, unknown> = {}) => ({
  ID: "Build_StorageContainerMk1_C_1", Name: "Secret Box", ClassName: "Build_StorageContainerMk1_C", Slots: 24, SlotsUsed: 2,
  Inventory: [{ Name: "Wire", ClassName: "Desc_Wire_C", Amount: 600, MaxAmount: 500 }, { Name: "Cable", ClassName: "Desc_Cable_C", Amount: 3, MaxAmount: 200 }], ...o,
});

describe("mapFrmExtractors", () => {
  it("copies current, max, percent and the clock", () => {
    expect(mapFrmExtractors([extractor()])).toEqual([{
      id: "Build_MinerMk1_C_1", className: "Build_MinerMk1_C", producing: true, paused: false, backedUp: null,
      resourceClass: "Desc_OreIron_C", current: 18.5, max: 120, percent: 15.4, clockPercent: 100, circuit: 6,
      raw: { Amount: 100, MaxAmount: 100, CurrentProd: 18.5, MaxProd: 120, ProdPercent: 15.4, ManuSpeed: 100 },
    }]);
  });
  it("omits an unconnected circuit and drops malformed records", () => {
    const out = mapFrmExtractors([extractor({ PowerInfo: { CircuitGroupID: -1, CircuitID: 3 } }), extractor({ ID: "" }), extractor({ production: [] }), 7, null]);
    expect(out).toHaveLength(1);
    expect("circuit" in out[0]!).toBe(false);
  });
  it("reads PowerInfo.CircuitGroupID, not CircuitID, and treats a missing one as unconnected", () => {
    const [a, b] = mapFrmExtractors([extractor(), extractor({ ID: "Build_MinerMk1_C_2", PowerInfo: { CircuitID: 5 } })]);
    expect(a!.circuit).toBe(6);
    expect("circuit" in b!).toBe(false);
  });
  it("throws on a non-array body and never returns a raw name", () => {
    expect(() => mapFrmExtractors({})).toThrow();
    expect(JSON.stringify(mapFrmExtractors([extractor()]))).not.toContain("Secret");
  });
});

describe("mapFrmGenerators", () => {
  it("splits fuel from the supplement and copies the power figures", () => {
    const [g] = mapFrmGenerators([generator()]);
    expect(g).toEqual({
      id: "Build_GeneratorCoal_C_1", className: "Build_GeneratorCoal_C", nameHash: evidenceHash("Secret Plant"), currentMW: 75, capacityMW: 75,
      clockPercent: 100, producing: true, fuelClass: "Desc_Coal_C", fuelAmount: 26, supplementClass: "Desc_Water_C", supplementAmount: 50, circuit: 6,
      raw: { RegulatedDemandProd: 75, CurrentPotential: 100, ProductionCapacity: 75, FuelAmount: 0.9 },
    });
  });
  it("adds BaseProd to RegulatedDemandProd (a geothermal shape: base above 0, regulated 0)", () => {
    const [g] = mapFrmGenerators([generator({ ClassName: "Build_GeneratorGeoThermal_C", BaseProd: 304.5, RegulatedDemandProd: 0, FuelInventory: [], Supplement: {} })]);
    expect(g).toMatchObject({ currentMW: 304.5, producing: true });
    expect(g!.raw).toMatchObject({ BaseProd: 304.5, RegulatedDemandProd: 0 });
    const [h] = mapFrmGenerators([generator({ BaseProd: 10, RegulatedDemandProd: 65 })]);
    expect(h!.currentMW).toBe(75);
  });
  it("gives null for a missing slot", () => {
    const [g] = mapFrmGenerators([generator({ FuelInventory: [], Supplement: {}, RegulatedDemandProd: 0 })]);
    expect(g).toMatchObject({ fuelClass: null, fuelAmount: null, supplementClass: null, supplementAmount: null, producing: false });
  });
  it("drops a malformed or over-long-named record", () => {
    expect(mapFrmGenerators([generator({ ID: "" }), generator({ Name: "n".repeat(201) }), generator({ ProductionCapacity: "x" }), 1])).toEqual([]);
  });
  it("never returns a raw name", () => {
    expect(JSON.stringify(mapFrmGenerators([generator()]))).not.toContain("Secret");
  });
});

describe("mapFrmStationRates", () => {
  it("has a station transfer equal to the sum of its platforms in the fixture", () => {
    const [s] = mapFrmStationRates([station()]);
    expect(s).toEqual({ id: "Build_TrainStation_C_1", nameHash: evidenceHash("Secret Station"), transfer: 2, platformSum: 2, platformCount: 2,
      platforms: [{ className: "", rate: 1.5 }, { className: "", rate: 0.5 }],
    });
    expect(s!.transfer).toBe(s!.platformSum);
  });
  it("keeps each platform's class, an empty platform included", () => {
    const [s] = mapFrmStationRates([station({ CargoInventory: [
      { ClassName: "Build_TrainDockingStation_C", TransferRate: 2 }, { ClassName: "Build_TrainPlatformEmpty_02_C", TransferRate: 0 },
    ] })]);
    expect(s).toMatchObject({ platformCount: 2, platformSum: 2, platforms: [
      { className: "Build_TrainDockingStation_C", rate: 2 }, { className: "Build_TrainPlatformEmpty_02_C", rate: 0 },
    ] });
  });
  it("drops a malformed or over-long-named record", () => {
    expect(mapFrmStationRates([station({ ID: "" }), station({ Name: "n".repeat(201) }), station({ TransferRate: "x" }), 3])).toEqual([]);
  });
  it("counts only platforms with a numeric rate and hides the name", () => {
    const out = mapFrmStationRates([station({ CargoInventory: [{ TransferRate: "x" }, { TransferRate: 1 }] })]);
    expect(out[0]).toMatchObject({ platformSum: 1, platformCount: 1 });
    expect(JSON.stringify(out)).not.toContain("Secret");
  });
});

describe("mapFrmStorage", () => {
  it("maps slots, items and stack size, and a box has no fluid capacity", () => {
    const [c] = mapFrmStorage([box()]);
    expect(c).toEqual({
      id: "Build_StorageContainerMk1_C_1", className: "Build_StorageContainerMk1_C", nameHash: evidenceHash("Secret Box"), slots: 24, slotsUsed: 2,
      fluidCapacity: null, items: [{ class: "Desc_Cable_C", amount: 3, stackMax: 200 }, { class: "Desc_Wire_C", amount: 600, stackMax: 500 }],
    });
  });
  it("gives null for a missing slot count or stack size, and fluidCapacity is always null", () => {
    const [c] = mapFrmStorage([box({ Slots: undefined, FluidCapacity: 400, Inventory: [{ ClassName: "Desc_Water_C", Amount: 5 }] })]);
    expect(c).toMatchObject({ slots: null, slotsUsed: 2, fluidCapacity: null, items: [{ class: "Desc_Water_C", amount: 5, stackMax: null }] });
  });
  it("merges stacks of one class (sum, first stack max) and sorts by class", () => {
    const [c] = mapFrmStorage([box({ Inventory: [
      { ClassName: "Desc_Wire_C", Amount: 100, MaxAmount: 500 }, { ClassName: "Desc_Cable_C", Amount: 3 },
      { ClassName: "Desc_Wire_C", Amount: 50, MaxAmount: 999 }, { ClassName: "Desc_Cable_C", Amount: 4, MaxAmount: 200 },
    ] })]);
    expect(c!.items).toEqual([{ class: "Desc_Cable_C", amount: 7, stackMax: null }, { class: "Desc_Wire_C", amount: 150, stackMax: 500 }]);
  });
  it("drops a malformed or over-long-named record", () => {
    expect(mapFrmStorage([box({ ID: "" }), box({ Name: "n".repeat(201) }), box({ ClassName: 5 }), null])).toEqual([]);
  });
  it("never returns a raw name", () => {
    expect(JSON.stringify(mapFrmStorage([box()]))).not.toContain("Secret");
  });
});
