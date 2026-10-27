import { readFileSync } from "node:fs";
import { describe, expect, it } from "vitest";
import { canonEdges, canonRails, classifyConveyorEnd, mapFrmConveyors, mapFrmDronePorts, mapFrmRails, round4 } from "../src/frm-s3";

// The mod's own recorded FRM outputs (the FRM oracle's goldens).
const load = (name: string): unknown => JSON.parse(readFileSync(new URL(`../../mod/SatisDashAgent/Source/SatisDashCoreTests/Fixtures/${name}`, import.meta.url), "utf8"));

describe("mapFrmRails", () => {
  const rails = mapFrmRails(load("frm-getTrainRails.json"));
  it("track 1: centimetres to metres at 0.01, and representable", () => {
    expect(rails[0]).toEqual({ id: "Build_RailroadTrack_C_1", points: [[0, 0], [0.1, 0.1], [2.5, 0], [2.5, 0], [5, 10000000000], [10.5, -0.5]], representable: true });
  });
  it("track 2 (one distinct metre) and track 3 (no points) are not representable", () => {
    expect(rails.map((r) => [r.id, r.representable])).toEqual([["Build_RailroadTrack_C_1", true], ["Build_RailroadTrack_C_2", false], ["Build_RailroadTrack_C_3", false]]);
    expect(rails[1]!.points).toEqual([[1, 1], [1.2, 1.2]]);
  });
  it("canonRails sorts and prints ids and points only", () => {
    expect(canonRails(rails)).toEqual([
      "Build_RailroadTrack_C_1|0,0;0.1,0.1;2.5,0;2.5,0;5,10000000000;10.5,-0.5",
      "Build_RailroadTrack_C_2|1,1;1.2,1.2",
      "Build_RailroadTrack_C_3|",
    ]);
  });
  it("a non-array body throws", () => {
    expect(() => mapFrmRails({})).toThrow();
  });
});

describe("mapFrmConveyors", () => {
  const sample = mapFrmConveyors(load("frm-getBelts.json"), load("frm-getLifts.json"), load("frm-getPipes.json"));
  const byId = (id: string) => sample.edges.find((e) => e.id === id)!;
  it("the miner-fed belt: extractor end, the capture's rate and name", () => {
    expect(byId("Build_ConveyorBeltMk1_C_2146994301")).toEqual({
      id: "Build_ConveyorBeltMk1_C_2146994301", kind: "belt",
      from: "Build_MinerMk1_C_2147011597", fromKind: "extractor",
      to: "Build_ConveyorAttachmentSplitter_C_2146996403", toKind: "splitter",
      capacityPerMinute: 60, tier: "Conveyor Belt Mk.1", atX: -555.55, atY: -1286.06,
    });
  });
  it("the open-end belt has to: null and toKind: null", () => {
    expect(byId("Build_ConveyorBeltMk1_C_2146953393")).toMatchObject({ from: "Build_ConveyorAttachmentSplitter_C_2146953396", fromKind: "splitter", to: null, toKind: null });
  });
  it("a pipe: capacity is Speed * 60, rounded to four decimals", () => {
    expect(byId("Build_Pipeline_C_2147376300")).toMatchObject({ kind: "pipe", capacityPerMinute: round4(5 * 60), tier: "Pipeline Mk.1", fromKind: "extractor", toKind: "junction" });
  });
  it("a lift keeps its ItemsPerMinute", () => {
    expect(byId("Build_ConveyorLiftMk3_C_2147447582")).toMatchObject({ kind: "lift", capacityPerMinute: 270, fromKind: "storage", toKind: "storage" });
  });
  it("a hypertube pipe record is filtered, not an edge; unreadable records are counted", () => {
    const s = mapFrmConveyors([{ ID: "b" }], [], [
      { ID: "Build_PipeHyperExample_C_1", Name: "Hyper", ClassName: "Build_PipeHyperExample_C", location0: { x: 0, y: 0 }, ConnectedTo0: "", ConnectedTo1: "" },
    ]);
    expect(s.filtered).toEqual([{ id: "Build_PipeHyperExample_C_1", className: "Build_PipeHyperExample_C" }]);
    expect(s.edges).toEqual([]);
    expect(s.dropped).toBe(1);
  });
  it("drops a record with an over-long id or a missing ConnectedTo field; capacity is null when absent or negative", () => {
    const rec = (over: object) => ({ ID: "Build_ConveyorBeltMk1_C_1", Name: "Conveyor Belt Mk.1", location0: { x: 100, y: 200 }, ConnectedTo0: "", ConnectedTo1: "", ItemsPerMinute: 60, ...over });
    const s = mapFrmConveyors([rec({ ID: "x".repeat(129) }), rec({ ConnectedTo1: undefined }), rec({ ID: "a", ItemsPerMinute: -1 }), rec({ ID: "b", ItemsPerMinute: undefined })], [], []);
    expect(s.dropped).toBe(2);
    expect(s.edges.map((e) => [e.id, e.capacityPerMinute])).toEqual([["a", null], ["b", null]]);
  });
  it("canonEdges is deterministic and sorted", () => {
    const lines = canonEdges(sample);
    expect(lines).toHaveLength(sample.edges.length + 1);
    expect(lines[0]!.split("|")).toHaveLength(9);
    expect(canonEdges({ ...sample, edges: [...sample.edges].reverse() })).toEqual(lines);
  });
});

describe("classifyConveyorEnd", () => {
  // The vectors of SatisDash.Core.NativeConveyors.Kind (NativeConveyorsTest.cpp): pins this port to the C++ table.
  const vectors: Array<[string, string]> = [
    ["Build_ConveyorAttachmentSplitterSmart_C_12", "splitter"], ["Build_ConveyorAttachmentSplitterSmartLift_C_12", "splitter"],
    ["Build_ConveyorAttachmentMerger_C_5", "merger"], ["Build_ConveyorBeltMk3_C_77", "conveyor"], ["Build_ConveyorLiftMk2_C_3", "conveyor"],
    ["Build_Pipeline_C_9", "conveyor"], ["Build_PipelineMK2_C_9", "conveyor"], ["Build_PipelineJunction_Cross_C_4", "junction"],
    ["Build_PipelinePump_C_8", "junction"], ["Build_MinerMk2_C_6", "extractor"], ["Build_WaterPump_C_1", "extractor"],
    ["Build_GeneratorCoal_C_2", "consumer"], ["Build_ResourceSink_C_1", "consumer"], ["Build_StorageContainerMk1_C_3", "storage"],
    ["Build_IndustrialTank_C_1", "storage"], ["Build_DroneStation_C_2146572476", "station"], ["Build_TrainDockingStation_C_4", "station"],
    ["Build_ConstructorMk1_C_10", "machine"], ["Build_OilRefinery_C_11", "machine"], ["Build_Foo_C_1", "other"], ["Build_Assembler_C", "machine"],
    ["build_conveyorbeltmk1_c_1", "conveyor"], ["build_miner_c_1", "other"], ["", "other"],
    ["Build_ConveyorBeltMk1_C_1", "conveyor"], ["Build_ConveyorLiftMk1_C_1", "conveyor"], ["Build_Pipeline_C_1", "conveyor"], ["Build_PipelineMK2_C_1", "conveyor"],
    ["Build_PipelineJunction_Cross_C_1", "junction"], ["Build_PipelinePump_C_1", "junction"], ["Build_MinerMk1_C_1", "extractor"],
    ["Build_WaterPump_C_1", "extractor"], ["Build_OilPump_C_1", "extractor"], ["Build_FrackingExtractor_C_1", "extractor"],
    ["Build_GeneratorBiomass_Automated_C_1", "consumer"], ["Build_ResourceSink_C_1", "consumer"], ["Build_SpaceElevator_C_1", "consumer"],
    ["Build_StorageContainerMk2_C_1", "storage"], ["Build_CentralStorage_C_1", "storage"], ["Build_IndustrialTank_C_1", "storage"],
    ["Build_PipeStorageTank_C_1", "storage"], ["Build_FluidBuffer_C_1", "storage"], ["Build_TrainDockingStation_C_1", "station"],
    ["Build_TrainStation_C_1", "station"], ["Build_DroneStation_C_2146572476", "station"], ["Build_TruckStation_C_1", "station"],
    ["Build_ConstructorMk1_C_1", "machine"], ["Build_AssemblerMk1_C_1", "machine"], ["Build_ManufacturerMk1_C_1", "machine"],
    ["Build_SmelterMk1_C_1", "machine"], ["Build_FoundryMk1_C_1", "machine"], ["Build_Packager_C_1", "machine"], ["Build_OilRefinery_C_1", "machine"],
    ["Build_Blender_C_1", "machine"], ["Build_HadronCollider_C_1", "machine"], ["Build_Converter_C_1", "machine"], ["Build_QuantumEncoder_C_1", "machine"],
    ["Build_ConveyorAttachmentSplitter_C_1", "splitter"], ["Build_ConveyorAttachmentMerger_C_1", "merger"],
  ];
  it.each(vectors)("%s is %s", (id, kind) => {
    expect(classifyConveyorEnd(id)).toBe(kind);
  });
});

describe("mapFrmDronePorts", () => {
  it("returns ids only, never a label", () => {
    const out = mapFrmDronePorts([{ ID: "Build_DroneStation_C_2146572476", Name: "FUEL 5", ClassName: "Build_DroneStation_C" }, { Name: "no id" }]);
    expect(out).toEqual(["Build_DroneStation_C_2146572476"]);
    expect(JSON.stringify(out)).not.toContain("FUEL");
  });
});
