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
    // The C++ vector: NBSP, U+FEFF, U+2028, U+2029 and U+3000 around the text.
    expect(markerName(" ﻿  a  　")).toBe("a");
    expect(markerName("　a　")).toBe("a");
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
  it("pins the exact derived id (hand-computed sha256 of [12469,3030,\"marker\",660,\"#6ea37b\",\"Alpha\"], hashed for evidence)", () => {
    // sha256 of that JSON, first 32 hex, is fac35e79d01769edc9207e7ac970989e; only its sessionHash form is returned.
    const pinned = "derived-" + sessionHash("derived-fac35e79d01769edc9207e7ac970989e");
    expect(pinned).toBe("derived-6e68fc3e7a32");
    expect(mapFrmMarkers([marker({ ID: undefined })])[0]!.id).toBe(pinned);
    // Whole-metre rule: 12468.5 m and 12469.4 m are the same metre, 12469.6 m is not.
    expect(mapFrmMarkers([marker({ ID: "", location: { x: 1246850, y: 303049, z: 0 } })])[0]!.id).toBe(pinned);
    expect(mapFrmMarkers([marker({ ID: "", location: { x: 1246940, y: 302951, z: 0 } })])[0]!.id).toBe(pinned);
    expect(mapFrmMarkers([marker({ ID: "", location: { x: 1246960, y: 303000, z: 0 } })])[0]!.id).not.toBe(pinned);
    expect(mapFrmMarkers([marker({ ID: "", Name: "  Alpha " })])[0]!.id).toBe(pinned);
  });
  it("hashes any derived- id before it leaves the oracle, and never returns the raw digest", () => {
    const raw = "derived-fac35e79d01769edc9207e7ac970989e";
    const own = mapFrmMarkers([marker({ ID: raw })])[0]!;
    expect(own.id).toBe("derived-" + sessionHash(raw));
    const body = [marker({ ID: undefined }), marker({ ID: raw, IconID: 1 })];
    const drone = [port({ ID: raw, Name: "A", PairedStation: "B" }), port({ ID: "P2", Name: "B", PairedStation: "A" })];
    const wire = [{ location0: { x: 100, y: 200 }, location1: { x: 300, y: 400 } }, { ID: raw, location0: { x: 1, y: 2 }, location1: { x: 3, y: 4 } }];
    const all = JSON.stringify([mapFrmMarkers(body), mapFrmDroneFull(drone), mapFrmWires(wire), canonMarkers(mapFrmMarkers(body)), canonDrone(mapFrmDroneFull(drone)), canonWires(mapFrmWires(wire))]);
    expect(all).not.toContain("fac35e79d01769edc9207e7ac970989e");
    expect(all).not.toContain("03a4cc702aa6e4f169ff34dd05571414");
    expect(mapFrmDroneFull(drone).find((p) => p.id === "P2")!.pairedIds).toEqual(["derived-" + sessionHash(raw)]);
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
  it("drops a port without a numeric z, a 201-character name, or an out-of-bounds position", () => {
    const out = mapFrmDroneFull([
      port({ ID: "noz", location: { x: 1, y: 2 } }), port({ ID: "strz", location: { x: 1, y: 2, z: "5" } }),
      port({ ID: "longname", Name: "n".repeat(201) }), port({ ID: "farx", location: { x: 1e12, y: 0, z: 0 } }),
      port({ ID: "fary", location: { x: 0, y: -1e12, z: 0 } }), port({ ID: "edge", location: { x: 1e8, y: -1e8, z: 0 } }),
      port({ ID: "over", location: { x: 100_000_100, y: 0, z: 0 } }),
    ]);
    expect(out.map((p) => p.id)).toEqual(["edge"]);
  });
  it("PairedStation empty, over 200 characters, or non-string cannot pair; 200 characters can", () => {
    const body = (paired: unknown) => [port({ ID: "A", Name: "One", PairedStation: paired }), port({ ID: "B", Name: "" , PairedStation: "None" })];
    for (const p of ["", "x".repeat(201), 5, undefined, "None"]) {
      const [a] = mapFrmDroneFull(body(p));
      expect(a!.pairedHash).toBeNull();
      expect(a!.pairedIds).toEqual([]);
    }
    const long = "y".repeat(200);
    const [a, b] = mapFrmDroneFull([port({ ID: "A", PairedStation: long }), port({ ID: "B", Name: long })]);
    expect(a!.pairedHash).toBe(sessionHash(long));
    expect(a!.pairedIds).toEqual(["B"]);
    expect(b!.pairedIds).toEqual([]);
  });
  it("a port paired to its own name lists itself; a repeated id is listed once", () => {
    const [self] = mapFrmDroneFull([port({ ID: "S", Name: "Me", PairedStation: "Me" })]);
    expect(self!.pairedIds).toEqual(["S"]);
    const out = mapFrmDroneFull([port({ ID: "A", Name: "One", PairedStation: "Two" }), port({ ID: "B", Name: "Two" }), port({ ID: "B", Name: "Two", fuel: 1 })]);
    expect(out.find((p) => p.id === "A")!.pairedIds).toEqual(["B"]);
  });
  it("caps item classes at 64 per inventory, skips over-long classes, and caps the layer at 2000", () => {
    const classes = Array.from({ length: 70 }, (_, i) => ({ ClassName: `Desc_${String(i).padStart(3, "0")}_C` }));
    const [p] = mapFrmDroneFull([port({ InputInventory: [...classes, { ClassName: "c".repeat(201) }] })]);
    expect(p!.input).toHaveLength(64);
    expect(p!.input[0]).toBe("Desc_000_C");
    const many = Array.from({ length: 2005 }, (_, i) => port({ ID: `id${String(i).padStart(5, "0")}`, Name: `n${i}`, PairedStation: "None" }));
    const out = mapFrmDroneFull(many);
    expect(out).toHaveLength(2000);
    expect(out[1999]!.id).toBe("id01999");
  });
  it("breaks a repeated-id tie on the mapper's content (whole metres), not the 0.01 m values", () => {
    // Same id; the mapper compares JSON with x:1 vs x:2 whole metres, so the 1 m port sorts first whatever the input order.
    const lo = port({ ID: "T", location: { x: 140, y: 0, z: 0 } });
    const hi = port({ ID: "T", location: { x: 160, y: 0, z: 0 } });
    expect(mapFrmDroneFull([hi, lo]).map((p) => p.xM)).toEqual([1.4, 1.6]);
    expect(mapFrmDroneFull([lo, hi]).map((p) => p.xM)).toEqual([1.4, 1.6]);
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
  it("pins the exact derived id (sha256 of [[1,2],[3,4]] is 03a4cc702aa6e4f169ff34dd05571414, hashed for evidence) and the same-metre rule", () => {
    const pinned = "derived-" + sessionHash("derived-03a4cc702aa6e4f169ff34dd05571414");
    expect(mapFrmWires([{ location0: { x: 100, y: 200 }, location1: { x: 300, y: 400 } }])[0]!.id).toBe(pinned);
    // 149 cm and 151 cm round to 1 m and 2 m; 100 cm and 200 cm are the same metres as 149 and 249.
    expect(mapFrmWires([{ ID: "", location0: { x: 149, y: 249 }, location1: { x: 250, y: 350 } }])[0]!.id).toBe(pinned);
    expect(mapFrmWires([{ ID: "x".repeat(201), location0: { x: 51, y: 150 }, location1: { x: 349, y: 449 } }])[0]!.id).toBe(pinned);
    expect(mapFrmWires([{ location0: { x: 151, y: 200 }, location1: { x: 300, y: 400 } }])[0]!.id).not.toBe(pinned);
  });
  it("drops a line with either end out of bounds, and caps at 20000", () => {
    const ok = { x: 0, y: 0 };
    const far = { x: 1e12, y: 0 };
    const edge = { x: 1e8, y: -1e8 };
    const over = { x: 100_000_100, y: 0 };
    const out = mapFrmWires([
      { ID: "a", location0: far, location1: ok }, { ID: "b", location0: ok, location1: far }, { ID: "c", location0: ok, location1: over },
      { ID: "d", location0: { x: 0, y: -1e12 }, location1: ok }, { ID: "e", location0: edge, location1: ok },
    ]);
    expect(out.map((w) => w.id)).toEqual(["e"]);
    const many = Array.from({ length: 20_005 }, (_, i) => ({ ID: `w${String(i).padStart(6, "0")}`, location0: ok, location1: ok }));
    const capped = mapFrmWires(many);
    expect(capped).toHaveLength(20_000);
    expect(capped[19_999]!.id).toBe("w019999");
  });
  it("breaks a repeated-id tie on whole-metre ends", () => {
    const lo = { ID: "T", location0: { x: 140, y: 0 }, location1: { x: 0, y: 0 } };
    const hi = { ID: "T", location0: { x: 160, y: 0 }, location1: { x: 0, y: 0 } };
    expect(mapFrmWires([hi, lo]).map((w) => w.ends[0][0])).toEqual([1.4, 1.6]);
  });
});
