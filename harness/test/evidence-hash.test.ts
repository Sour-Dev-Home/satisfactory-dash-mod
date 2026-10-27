import { randomBytes } from "node:crypto";
import { afterEach, describe, expect, it } from "vitest";
import { s4LayerPosts } from "../src/core-runs/native-parity";
import { evidenceHash, resetEvidenceKeyForTests } from "../src/evidence-hash";
import { evidenceId, mapFrmMarkers } from "../src/frm-s4";
import { sessionHash } from "../src/status";

afterEach(() => resetEvidenceKeyForTests());

describe("evidenceHash (final review I-2)", () => {
  it("is 12 lower-case hex characters, equal for the same name within a run, and not the unsalted sessionHash", () => {
    const h = evidenceHash("Alpha");
    expect(h).toMatch(/^[0-9a-f]{12}$/);
    expect(evidenceHash("Alpha")).toBe(h);
    expect(evidenceHash("Other")).not.toBe(h);
    expect(h).not.toBe(sessionHash("Alpha"));
  });
  it("a different key gives a different hash for the same name", () => {
    resetEvidenceKeyForTests(Buffer.alloc(32, 1));
    const a = evidenceHash("Alpha");
    resetEvidenceKeyForTests(Buffer.alloc(32, 2));
    expect(evidenceHash("Alpha")).not.toBe(a);
    resetEvidenceKeyForTests(Buffer.alloc(32, 1));
    expect(evidenceHash("Alpha")).toBe(a);
  });
  it("the oracle, the capture and the judge's id hash share the run key", () => {
    const frm = mapFrmMarkers([{ ID: "m1", Name: "Alpha", location: { x: 100, y: 200, z: 0 }, IconID: 1, MapMarkerType: "RT_Marker", ColorSlot: "ffffffff" }]);
    const entries = [{ agent: "mod", status: 200, at: new Date(1000).toISOString(), world: { layer: "mapMarkers" }, worldData: [{ id: "m1", name: "Alpha", kind: "marker", icon: 1, color: "#ffffff", x: 1, y: 2 }] }];
    const posted = s4LayerPosts(entries as never, "mapMarkers", 0)[0]!.data as Array<{ name: string }>;
    expect(posted[0]!.name).toBe(frm[0]!.nameHash);
    expect(evidenceId("derived-abc")).toBe("derived-" + evidenceHash("derived-abc"));
  });
  it("the key never appears in a capture", () => {
    const key = randomBytes(32);
    resetEvidenceKeyForTests(key);
    const entries = [{ agent: "mod", status: 200, at: new Date(1000).toISOString(), world: { layer: "droneRoutes" }, worldData: [{ id: "p1", name: "Beta", paired: "Alpha", input: [], output: [], fuel: [], x: 1, y: 2 }] }];
    const text = JSON.stringify(s4LayerPosts(entries as never, "droneRoutes", 0)) + JSON.stringify(mapFrmMarkers([]));
    for (const form of [key.toString("hex"), key.toString("base64"), key.toString("base64url")]) expect(text).not.toContain(form);
    expect(text).not.toContain("Beta");
  });
});
