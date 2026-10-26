import { readFileSync } from "node:fs";
import { describe, expect, it } from "vitest";
import { TrainRoutesLayerDataSchema } from "../contract/src/map";
import {
  canonRoutes, countSomersloops, createFactoryEncoder, FactoryTable, hashRoutes, inputsFullOf, mapFrmFactory, mapFrmStations, mapFrmTrainRoutes, mapFrmTrains,
  readFrmRoutes, readFrmStations, readFrmTrains,
} from "../src/frm-s2";
import { sessionHash } from "../src/status";

// The mod's own recorded FRM outputs and the bridge's expected outputs for them (the FRM oracle's goldens).
const load = (name: string): unknown => JSON.parse(readFileSync(new URL(`../../mod/SatisDashAgent/Source/SatisDashCoreTests/Fixtures/${name}`, import.meta.url), "utf8"));

describe("mapFrmFactory reproduces the bridge's expected output", () => {
  it("running world", () => {
    expect({ buildings: mapFrmFactory(load("frm-getFactory-running.json")) }).toEqual(load("frm-getFactory-running.expected.json"));
  });
  it("unconfigured machines: null recipe, empty rates, inputsFull false", () => {
    const out = mapFrmFactory(load("frm-getFactory-norecipe.json"));
    expect({ buildings: out }).toEqual(load("frm-getFactory-norecipe.expected.json"));
    expect(out[0]).toMatchObject({ recipe: null, production: [], ingredients: [], inputsFull: false });
  });
  it("counts machines with a Somersloop", () => {
    expect(countSomersloops(load("frm-getFactory-running.json"))).toBe(0);
    expect(countSomersloops([{ Somersloops: 1 }, { Somersloops: 0 }, {}])).toBe(1);
  });
  it("refuses an invalid record rather than guessing", () => {
    expect(() => mapFrmFactory([{ ID: "", Name: "x" }])).toThrow();
  });
});

describe("mapFrmTrains and mapFrmStations", () => {
  const expected = load("frm-mapLive.expected.json") as { trains: unknown[]; stations: unknown[] };
  it("joined equal frm-mapLive.expected.json (whole metres, the out-of-bounds train dropped)", () => {
    expect({ trains: mapFrmTrains(load("frm-getTrains.json")), stations: mapFrmStations(load("frm-getTrainStation.json")) }).toEqual(expected);
  });
  it("equal it after hashing, and the reads the sampler keeps carry hashes only", () => {
    const hash = (xs: unknown[]) => xs.map((x) => ({ ...(x as object), name: sessionHash((x as { name: string }).name) }));
    const read = { trains: readFrmTrains(load("frm-getTrains.json")), stations: readFrmStations(load("frm-getTrainStation.json")) };
    expect(read).toEqual({ trains: hash(expected.trains), stations: hash(expected.stations) });
    expect(JSON.stringify(read)).not.toContain("Test Train");
    expect(JSON.stringify(read)).not.toContain("Test Station");
  });
});

describe("mapFrmTrainRoutes", () => {
  it("the bridge's fixtures: valid against the contract and equal to a hand-written layer", () => {
    const layer = mapFrmTrainRoutes(load("frm-getTrainStation.json"), load("frm-getTrains.json"));
    expect(TrainRoutesLayerDataSchema.safeParse(layer).success).toBe(true);
    expect(layer).toEqual([
      { recordType: "station", id: "Build_TrainStation_C_1", name: "Test Station A", platforms: [] },
      { recordType: "station", id: "Build_TrainStation_C_2", name: "Test Station B", platforms: [] },
      { recordType: "train", id: "BP_Train_C_1", name: "Test Train A", stops: ["Test Station A", "Test Station B"], items: [] },
      { recordType: "train", id: "BP_Train_C_2", name: "Test Train B", stops: [], items: [] },
      { recordType: "train", id: "BP_Train_C_3", name: "Far Away", stops: [], items: [] },
    ]);
  });
  const stations = [
    { ID: "S2", Name: "Out ", CargoInventory: [
      { ID: "P2", LoadingMode: "Unloading", Inventory: [{ ClassName: "Desc_B_C" }, { ClassName: "Desc_A_C" }, { ClassName: "Desc_B_C" }] },
      { ID: "P1", LoadingMode: "Loading", Inventory: [] },
    ] },
    { ID: "S1", Name: "In" },
  ];
  const trains = [{ ID: "T1", Name: "Loop", TimeTable: [{ StationName: "Out " }, { StationName: "In" }], Vehicles: [{ Inventory: [] }, { Inventory: [{ ClassName: "Desc_Z_C" }, { ClassName: "Desc_A_C" }] }] }];
  it("platforms from CargoInventory (load or unload, items distinct and sorted), stops in order, cargo from the vehicles", () => {
    const layer = mapFrmTrainRoutes(stations, trains);
    expect(layer).toEqual([
      { recordType: "station", id: "S1", name: "In", platforms: [] },
      { recordType: "station", id: "S2", name: "Out ", platforms: [{ id: "P1", mode: "load", items: [] }, { id: "P2", mode: "unload", items: ["Desc_A_C", "Desc_B_C"] }] },
      { recordType: "train", id: "T1", name: "Loop", stops: ["Out ", "In"], items: ["Desc_A_C", "Desc_Z_C"] },
    ]);
    expect(TrainRoutesLayerDataSchema.safeParse(layer).success).toBe(true);
  });
  it("hashes names and stops; the canonical list is order-free and the sampler's read carries no raw name", () => {
    const read = readFrmRoutes(stations, trains);
    expect(JSON.stringify(read)).not.toContain("Loop");
    expect(JSON.stringify(read)).not.toContain("Out ");
    const hashedLayer = hashRoutes(mapFrmTrainRoutes(stations, trains));
    expect(canonRoutes([...hashedLayer].reverse())).toEqual(read.canon);
    expect(TrainRoutesLayerDataSchema.safeParse(hashedLayer).success).toBe(true);
  });
  it("an empty world is an empty layer, not an error", () => {
    expect(mapFrmTrainRoutes([], [])).toEqual([]);
  });
});

describe("the factory's compact evidence", () => {
  it("rows name an interned static, delta samples carry only what changed, and flags round-trip", () => {
    const machines = mapFrmFactory(load("frm-getFactory-running.json"));
    const table = new FactoryTable();
    const enc = createFactoryEncoder(table);
    const first = enc.push(machines);
    expect(first.full).toBe(true);
    expect(first.rows).toHaveLength(machines.length);
    const again = enc.push(machines);
    expect(again).toEqual({ rows: [], gone: [], full: false });
    const changed = machines.map((m, i) => (i === 0 ? { ...m, isProducing: true } : m));
    const third = enc.push(changed);
    expect(third.rows).toHaveLength(1);
    expect(third.rows[0]![1]! & 1).toBe(1);
    const dropped = enc.push(changed.slice(1));
    expect(dropped.gone).toEqual([third.rows[0]![0]]);
    expect(table.statics[first.rows[0]![0]!]!.id).toBe(machines[0]!.id);
    expect(inputsFullOf(first.rows[0]![1]!)).toBe(machines[0]!.inputsFull);
  });
});
