import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { AgentPowerCircuitSchema, AgentStatusSchema, SnapshotRequestSchema } from "../contract/src/agent";
import { FactorySchema } from "../contract/src/factory";
import { MapLiveSchema, RailsLayerDataSchema, RailsWorldIngestRequestSchema, ResourceNodesLayerDataSchema } from "../contract/src/map";

const FIXTURES = new URL("../../mod/SatisDashAgent/Source/SatisDashCoreTests/Fixtures/", import.meta.url);
const load = (name: string) => JSON.parse(readFileSync(fileURLToPath(new URL(name, FIXTURES)), "utf8"));
const keys = (o: object) => Object.keys(o).sort();

describe("the core mod's snapshot fixtures, against the pinned contract (spec S13)", () => {
  for (const name of ["snapshot-world.json", "snapshot-no-world.json"]) {
    it(`${name} is a valid snapshot (strict: no field the contract doesn't define)`, () => {
      const r = SnapshotRequestSchema.safeParse(load(name));
      expect(r.success ? [] : r.error.issues).toEqual([]);
    });
    it(`${name}'s status has exactly the contract's status fields`, () => {
      expect(keys(load(name).status)).toEqual(keys(AgentStatusSchema.shape));
    });
  }
  it("snapshot-frm.json, with players and factory, is a valid snapshot", () => {
    const r = SnapshotRequestSchema.safeParse(load("snapshot-frm.json"));
    expect(r.success ? [] : r.error.issues).toEqual([]);
  });
  it("a snapshot with gameReachable, lastGameReadAt and modVersion at the top level is valid (S4-A, R7)", () => {
    const body = { ...load("snapshot-world.json"), gameReachable: true, lastGameReadAt: "2026-10-27T12:00:00.000Z", modVersion: "mod/0.1.0" };
    const r = SnapshotRequestSchema.safeParse(body);
    expect(r.success ? [] : r.error.issues).toEqual([]);
    expect(SnapshotRequestSchema.safeParse({ ...body, modVersion: "" }).success).toBe(false);
    expect(SnapshotRequestSchema.safeParse({ ...body, modVersion: "v".repeat(65) }).success).toBe(false);
    // The unreachable shape: the game never answered, so there is no lastGameReadAt (omitted, never null).
    const { lastGameReadAt: _omit, ...rest } = body;
    const unreachable = SnapshotRequestSchema.safeParse({ ...rest, gameReachable: false });
    expect(unreachable.success ? [] : unreachable.error.issues).toEqual([]);
  });
  it("frm-mapLive.expected.json is a valid mapLive part (the contract's caps and shapes)", () => {
    const r = MapLiveSchema.safeParse(load("frm-mapLive.expected.json"));
    expect(r.success ? [] : r.error.issues).toEqual([]);
  });
  it("the world fixtures' expected layers are valid against the contract", () => {
    expect(RailsLayerDataSchema.safeParse(load("frm-getTrainRails.expected.json")).success).toBe(true);
    expect(ResourceNodesLayerDataSchema.safeParse(load("frm-getResourceNode.expected.json")).success).toBe(true);
    const body = { observedAt: "2026-10-24T12:00:00.000Z", data: load("frm-getTrainRails.expected.json") };
    expect(RailsWorldIngestRequestSchema.safeParse(body).success).toBe(true); // strict: exactly observedAt and data
  });
  it("native-extractors.expected.json is valid extractors: the building shape, recipe null, no ingredients (S4-B)", () => {
    const list = load("native-extractors.expected.json");
    const r = FactorySchema.shape.extractors.safeParse(list);
    expect(r.success ? [] : r.error.issues).toEqual([]);
    expect(list).toHaveLength(3);
    for (const e of list) {
      expect(e.recipe).toBeNull();
      expect(e.ingredients).toEqual([]);
      expect(typeof e.isBackedUp).toBe("boolean");
    }
    // Strict where the schema is: the same fixture inside a factory part parses too.
    expect(FactorySchema.safeParse({ buildings: [], extractors: list, backedUpCount: 0 }).success).toBe(true);
  });
  it("a circuit has exactly the contract's circuit fields", () => {
    expect(keys(load("snapshot-world.json").power.circuits[0])).toEqual(keys(AgentPowerCircuitSchema.shape));
  });
  it("the top level is only what v1 sends: no factory, players or mapLive", () => {
    expect(keys(load("snapshot-world.json"))).toEqual(
      ["agentVersion", "observedAt", "paused", "power", "reachable", "settings", "status"],
    );
  });
});
