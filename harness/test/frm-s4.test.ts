import { describe, expect, it } from "vitest";
import { canonDrone, canonMarkers, canonWires, mapFrmDroneFull, mapFrmMarkers, mapFrmWires, markerName } from "../src/frm-s4";
import { sessionHash } from "../src/status";

// Synthetic bodies in the shape of FRM's output; every name and id here is made up.
const marker = (o: Record<string, unknown> = {}) => ({
  ID: "M1", Name: "Alpha", location: { x: 1246855.859375, y: 303000, z: 5 }, MapMarkerType: "RT_MapMarker", IconID: 660, ColorSlot: "6EA37BFF", ...o,
});

describe("markerName (the NativeMapMarkers.Name vectors)", () => {
  const pair = "\u{1F600}";
  it("matches the C++ rule", () => {
    expect(markerName("  a ")).toBe("a");
    expect(markerName(" ﻿ a  ")).toBe("a");
    expect(markerName("\t\nb c\r\n")).toBe("b c");
    expect(markerName("a".repeat(61))).toBe("a".repeat(60));
    expect(markerName("a".repeat(59) + pair)).toBe("a".repeat(59));
    const exact = "a".repeat(58) + pair;
    expect(markerName(exact)).toBe(exact);
    expect(markerName("x\uD800y")).toBe("x�y");
    expect(markerName("x\uDC00y")).toBe("x�y");
    expect(markerName("")).toBe("");
    expect(markerName(undefined)).toBe("");
  });
});

describe("mapFrmMarkers", () => {
  it("maps colour, kind and unrounded metres", () => {
    const [m] = mapFrmMarkers([marker()]);
    expect(m).toEqual({ id: "M1", nameHash: sessionHash("Alpha"), kind: "marker", icon: 660, color: "#6ea37b", xM: 12468.56, yM: 3030 });
    expect(mapFrmMarkers([marker({ ColorSlot: undefined })])[0]!.color).toBe("#ffffff");
    expect(mapFrmMarkers([marker({ ColorSlot: "zzz" })])[0]!.color).toBe("#ffffff");
    expect(mapFrmMarkers([marker({ MapMarkerType: "RT_Stamp" })])[0]!.kind).toBe("stamp");
    expect(mapFrmMarkers([marker({ MapMarkerType: "Other" })])[0]!.kind).toBe("marker");
  });
  it("never returns a raw name", () => {
    const out = mapFrmMarkers([marker({ Name: "Secret Outpost Name", ID: "" })]);
    expect(JSON.stringify(out)).not.toContain("Secret");
    expect(JSON.stringify(canonMarkers(out))).not.toContain("Secret");
    expect(out[0]!.id.startsWith("derived-")).toBe(true);
  });
  it("derives a stable id for a missing or over-long id and drops bad icons and positions", () => {
    const a = mapFrmMarkers([marker({ ID: undefined })])[0]!.id;
    expect(mapFrmMarkers([marker({ ID: "x".repeat(201) })])[0]!.id).toBe(a);
    expect(mapFrmMarkers([marker({ IconID: 1.5 }), marker({ IconID: -1 }), marker({ IconID: undefined }), marker({ location: { x: 1e12, y: 0 } })])).toEqual([]);
  });
  it("keeps repeated ids (the native readers keep the first), sorts by id, caps at 1000", () => {
    expect(mapFrmMarkers([marker({ ID: "B" }), marker({ ID: "A" }), marker({ ID: "A", IconID: 1 })]).map((m) => m.id)).toEqual(["A", "A", "B"]);
    const many = Array.from({ length: 1005 }, (_, i) => marker({ ID: `id${String(i).padStart(5, "0")}` }));
    expect(mapFrmMarkers(many)).toHaveLength(1000);
  });
  it("rejects a non-array body", () => {
    expect(() => mapFrmMarkers({})).toThrow();
  });
});

const port = (o: Record<string, unknown> = {}) => ({
  ID: "P1", Name: "Port One", PairedStation: "None", location: { x: 100, y: 250, z: 0 },
  InputInventory: [{ ClassName: "Desc_B_C", Amount: 3 }, { ClassName: "Desc_A_C" }, { ClassName: "Desc_B_C" }, { ClassName: "" }],
  OutputInventory: [], FuelInventory: [{ ClassName: "Desc_Fuel_C" }], ...o,
});

describe("mapFrmDroneFull", () => {
  it("unpaired gives a null hash and no ids; inventories are sorted distinct classes", () => {
    const [p] = mapFrmDroneFull([port()]);
    expect(p).toEqual({ id: "P1", nameHash: sessionHash("Port One"), pairedHash: null, input: ["Desc_A_C", "Desc_B_C"], output: [], fuel: ["Desc_Fuel_C"], xM: 1, yM: 2.5, pairedIds: [] });
  });
  it("resolves the paired name over the whole body", () => {
    const body = [port({ ID: "A", Name: "One", PairedStation: "Two" }), port({ ID: "B", Name: "Two", PairedStation: "One" }), port({ ID: "C", Name: "Two" })];
    const out = mapFrmDroneFull(body);
    expect(out.find((p) => p.id === "A")!.pairedIds).toEqual(["B", "C"]);
    expect(out.find((p) => p.id === "A")!.pairedHash).toBe(sessionHash("Two"));
    expect(out.find((p) => p.id === "B")!.pairedIds).toEqual(["A"]);
    expect(mapFrmDroneFull([port({ PairedStation: "Nobody" })])[0]!.pairedIds).toEqual([]);
  });
  it("returns no raw name and drops unusable records", () => {
    const out = mapFrmDroneFull([port({ Name: "Hidden Label", PairedStation: "Hidden Label2" }), port({ ID: "" }), 5, port({ ID: "x".repeat(201) })]);
    expect(out).toHaveLength(1);
    expect(JSON.stringify(out)).not.toContain("Hidden");
    expect(JSON.stringify(canonDrone(out))).not.toContain("Hidden");
  });
});

describe("mapFrmWires", () => {
  it("divides the ends by 100 and sorts by id", () => {
    const out = mapFrmWires([
      { ID: "W2", location0: { x: 12345, y: -250 }, location1: { x: 0, y: 99 } },
      { ID: "W1", location0: { x: 100, y: 200 }, location1: { x: 300, y: 400 } },
    ]);
    expect(out).toEqual([{ id: "W1", ends: [[1, 2], [3, 4]] }, { id: "W2", ends: [[123.45, -2.5], [0, 0.99]] }]);
    expect(canonWires(out)).toEqual(["W1|1,2|3,4", "W2|123.45,-2.5|0,0.99"]);
  });
  it("derives an id when missing and drops a line without two usable ends", () => {
    const out = mapFrmWires([{ location0: { x: 1, y: 2 }, location1: { x: 3, y: 4 } }, { ID: "W", location0: { x: 1, y: 2 } }]);
    expect(out).toHaveLength(1);
    expect(out[0]!.id.startsWith("derived-")).toBe(true);
  });
});
