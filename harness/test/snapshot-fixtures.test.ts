import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { AgentPowerCircuitSchema, AgentStatusSchema, SnapshotRequestSchema } from "../contract/src/agent";
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
  it("a circuit has exactly the contract's circuit fields", () => {
    expect(keys(load("snapshot-world.json").power.circuits[0])).toEqual(keys(AgentPowerCircuitSchema.shape));
  });
  it("the top level is only what v1 sends: no factory, players or mapLive", () => {
    expect(keys(load("snapshot-world.json"))).toEqual(
      ["agentVersion", "observedAt", "paused", "power", "reachable", "settings", "status"],
    );
  });
});
