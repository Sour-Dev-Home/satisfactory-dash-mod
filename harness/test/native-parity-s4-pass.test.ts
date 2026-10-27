import { describe, expect, it } from "vitest";
import type { FrmDronePort, FrmMarker, FrmWire } from "../src/frm-s4";
import * as S4 from "../../runs/native-parity/pass-s4";

const T0 = Date.parse("2026-10-28T12:00:00.000Z");
const iso = (ms: number) => new Date(ms).toISOString();
const text = (v: S4.S4Result) => v.reasons.join("\n");

/** Builds evidence with one FRM sample before the post (and one after, when `after` is given). */
function ev<F>(o: { post?: unknown; posts?: S4.MarkersEvidence["posts"]; frm: F[]; after?: F[]; frmSamples?: S4.MarkersEvidence["frmSamples"]; failed?: number; defPost: unknown }) {
  const distinct: Record<string, F[]> = { before: o.frm, after: o.after ?? [] };
  return {
    posts: o.posts ?? [{ at: iso(T0), status: 200, data: o.post ?? o.defPost }],
    frmSamples: o.frmSamples ?? [
      { startedAt: iso(T0 - 20_000), endedAt: iso(T0 - 10_000), value: "before" },
      ...(o.after ? [{ startedAt: iso(T0 + 10_000), endedAt: iso(T0 + 20_000), value: "after" }] : []),
    ],
    distinct, frm: { reads: 5, failed: o.failed ?? 0 },
  };
}

// ------------------------------------------------------------------------------------------------------------------ markers

const fm = (o: Partial<FrmMarker> = {}): FrmMarker => ({ id: "M1", nameHash: "h1", kind: "marker", icon: 660, color: "#6ea37b", xM: 100, yM: 200, ...o });
const mm = (o: Record<string, unknown> = {}) => ({ id: "M1", name: "h1", kind: "marker", icon: 660, color: "#6ea37b", x: 100, y: 200, ...o });
const mev = (o: { post?: unknown; frm?: FrmMarker[]; after?: FrmMarker[]; posts?: S4.MarkersEvidence["posts"]; frmSamples?: S4.MarkersEvidence["frmSamples"]; failed?: number } = {}): S4.MarkersEvidence =>
  ev({ ...o, frm: o.frm ?? [fm()], defPost: [mm()] });

describe("judgeMarkers", () => {
  it("identical layers pass", () => {
    const v = S4.judgeMarkers(mev());
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("1 markers compared");
    expect(v.never).toContain("a stamp");
    expect(v.never).toContain("a name the dash rule changes (trim or cut)");
  });
  it("a one-metre difference at a .5 boundary passes and is counted", () => {
    const v = S4.judgeMarkers(mev({ frm: [fm({ xM: 100.5 })], post: [mm({ x: 100 })] }));
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("1 one-metre rounding differences at .5 m");
    expect(v.never).not.toContain("a marker at a .5 m boundary");
  });
  it("Math.round of the FRM value passes without counting", () => {
    const v = S4.judgeMarkers(mev({ frm: [fm({ xM: 100.5 })], post: [mm({ x: 101 })] }));
    expect(v.pass).toBe(true);
    expect(text(v)).not.toContain("rounding differences");
  });
  it("a one-metre difference away from a boundary fails", () => {
    const v = S4.judgeMarkers(mev({ frm: [fm({ xM: 100.3 })], post: [mm({ x: 99 })] }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("M1 x mod=99 frm=100.3");
  });
  it("a boundary difference over one metre fails", () => {
    expect(S4.judgeMarkers(mev({ frm: [fm({ yM: 200.5 })], post: [mm({ y: 202 })] })).pass).toBe(false);
  });
  it.each([
    ["color", { color: "#000000" }, "color mod=#000000"],
    ["icon", { icon: 7 }, "icon mod=7"],
    ["kind", { kind: "stamp" }, "kind mod=stamp"],
    ["name hash", { name: "other" }, "nameHash mod=other"],
  ])("a %s difference fails with the field named", (_n, patch, expected) => {
    const v = S4.judgeMarkers(mev({ post: [mm(patch)] }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain(expected);
  });
  it("a colour mismatch points at Task 4", () => {
    expect(text(S4.judgeMarkers(mev({ post: [mm({ color: "#000000" })] })))).toContain("NativeMarkerColorSRGB decides this: Task 4");
  });
  it("an id only in FRM fails; an id only in the mod fails", () => {
    const a = S4.judgeMarkers(mev({ frm: [fm(), fm({ id: "M2" })] }));
    expect(a.pass).toBe(false);
    expect(text(a)).toContain("1 marker ids in FRM only");
    const b = S4.judgeMarkers(mev({ post: [mm(), mm({ id: "M3" })] }));
    expect(b.pass).toBe(false);
    expect(text(b)).toContain("1 marker ids in the mod only");
  });
  it("zero items on both sides passes with NOT EXERCISED", () => {
    const v = S4.judgeMarkers(mev({ frm: [], post: [] }));
    expect(v.pass).toBe(true);
    expect(v.never).toEqual(["any marker (both sides empty)"]);
  });
  it("an invalid post (icon as a string) fails", () => {
    const v = S4.judgeMarkers(mev({ post: [mm({ icon: "660" })] }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("not valid against the pinned contract");
  });
  it("no post fails", () => {
    const v = S4.judgeMarkers(mev({ posts: [] }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("posted no mapMarkers layer");
  });
  it("no FRM sample within 120 s fails", () => {
    const v = S4.judgeMarkers(mev({ frmSamples: [{ startedAt: iso(T0 - 300_000), endedAt: iso(T0 - 290_000), value: "before" }] }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("no FRM getMapMarkers sample within 120 s");
  });
  it("failed FRM reads with no sample fail", () => {
    const v = S4.judgeMarkers({ ...mev(), frm: { reads: 3, failed: 3 }, frmSamples: [] });
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("no FRM getMapMarkers sample succeeded");
  });
  it("the other side of the pair rescues", () => {
    const v = S4.judgeMarkers(mev({ frm: [fm({ icon: 1 })], after: [fm()] }));
    expect(v.pass).toBe(true);
  });
  it("derived FRM ids, FRM repeats and a capped layer are reported, not failed", () => {
    const v = S4.judgeMarkers(mev({ frm: [fm(), fm({ id: "derived-abc" }), fm({ id: "M1", icon: 5 })] }));
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("derived id");
    expect(text(v)).toContain("listed 1 marker id(s) twice");
  });
  it("an id difference with a side at the 1,000 cap is reported, not failed", () => {
    const frm = Array.from({ length: 1000 }, (_, i) => fm({ id: `C${String(i).padStart(4, "0")}` }));
    const v = S4.judgeMarkers(mev({ frm, post: frm.slice(0, 999).map((f) => mm({ id: f.id })) }));
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("at the layer cap of 1000");
  });
  it("the mod repeating an id fails", () => {
    expect(S4.judgeMarkers(mev({ post: [mm(), mm()] })).pass).toBe(false);
  });
});

// -------------------------------------------------------------------------------------------------------------------- drones

const fd = (o: Partial<FrmDronePort> = {}): FrmDronePort => ({ id: "D1", nameHash: "n1", pairedHash: "n2", input: ["Desc_A_C"], output: [], fuel: ["Desc_F_C"], xM: 10, yM: 20, pairedIds: ["D2"], ...o });
const dm = (o: Record<string, unknown> = {}) => ({ id: "D1", name: "n1", paired: "n2", pairedId: "D2", input: ["Desc_A_C"], output: [], fuel: ["Desc_F_C"], x: 10, y: 20, ...o });
const d2f = fd({ id: "D2", nameHash: "n2", pairedHash: "n1", input: [], output: ["Desc_A_C"], fuel: [], pairedIds: ["D1"] });
const d2m = dm({ id: "D2", name: "n2", paired: "n1", pairedId: "D1", input: [], output: ["Desc_A_C"], fuel: [] });
const dev = (o: { post?: unknown[]; frm?: FrmDronePort[]; after?: FrmDronePort[]; frmSamples?: S4.DroneEvidence["frmSamples"] } = {}): S4.DroneEvidence =>
  ev({ ...o, frm: o.frm ?? [fd(), d2f], defPost: [dm(), d2m] });
/** The default pair with the first port's mod side patched. */
const dpost = (patch: Record<string, unknown>) => [dm(patch), d2m];

describe("judgeDrone", () => {
  it("identical layers pass and report what was not exercised", () => {
    const v = S4.judgeDrone(dev());
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("2 ports compared");
    expect(v.never).toContain("an unpaired port");
    expect(v.never).not.toContain("a paired port");
  });
  it("x off by (1,0) passes; (1,1) passes per axis; 1.5 fails", () => {
    expect(S4.judgeDrone(dev({ post: dpost({ x: 11 }) })).pass).toBe(true);
    expect(S4.judgeDrone(dev({ post: dpost({ x: 11, y: 21 }) })).pass).toBe(true);
    const v = S4.judgeDrone(dev({ frm: [fd({ xM: 10.5 }), d2f], post: dpost({ x: 12 }) }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("D1 x mod=12 frm=10.5");
  });
  it("(1,1) off diagonally in the metres sense fails when the y axis is over", () => {
    expect(S4.judgeDrone(dev({ post: dpost({ x: 11, y: 22 }) })).pass).toBe(false);
  });
  it("an input set that differs fails", () => {
    const v = S4.judgeDrone(dev({ post: dpost({ input: ["Desc_B_C"] }) }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("D1 input mod=[Desc_B_C] frm=[Desc_A_C]");
    expect(text(v)).not.toContain("swapped");
  });
  it("swapped input and output fails with the V9 hint", () => {
    const v = S4.judgeDrone(dev({ post: [dm({ input: [], output: ["Desc_A_C"] }), dm({ id: "D2", name: "n2", paired: "n1", pairedId: "D1", input: ["Desc_A_C"], output: [], fuel: [] })] }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("input/output look swapped (V9: swap the getters in Task 9)");
  });
  it("a name hash or paired hash difference fails", () => {
    expect(text(S4.judgeDrone(dev({ post: dpost({ name: "zz" }) })))).toContain("nameHash mod=zz");
    expect(text(S4.judgeDrone(dev({ post: dpost({ paired: null }) })))).toContain("paired mod=null");
  });
  it("a fuel difference fails", () => {
    expect(text(S4.judgeDrone(dev({ post: dpost({ fuel: [] }) })))).toContain("D1 fuel");
  });
  it("an id only on one side fails", () => {
    expect(S4.judgeDrone(dev({ frm: [fd(), d2f, fd({ id: "D3", pairedIds: [] })] })).pass).toBe(false);
    expect(S4.judgeDrone(dev({ post: [dm(), d2m, dm({ id: "D3", paired: null, pairedId: null })] })).pass).toBe(false);
  });
  it("pairedId equal to the unique FRM match passes; a different id fails", () => {
    expect(text(S4.judgeDrone(dev()))).toContain("2 ports: pairedId judged");
    const v = S4.judgeDrone(dev({ post: dpost({ pairedId: "D1" }) }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("D1 pairedId mod=D1 frm=D2");
  });
  it("pairedId null with a unique FRM match in the layer fails", () => {
    const v = S4.judgeDrone(dev({ post: dpost({ pairedId: null }) }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("pairedId mod=null frm=D2");
  });
  it("an ambiguous FRM name reports only", () => {
    const frm = [fd({ pairedIds: ["D2", "D3"] }), d2f];
    const v = S4.judgeDrone(dev({ frm, post: dpost({ pairedId: null }) }));
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("pairedId reported, not judged (ambiguous or unpaired in FRM)");
    expect(v.never).not.toContain("two ports sharing a name (pairing ambiguous)");
  });
  it("an unpaired port with pairedId null passes; an absent pairedId is reported only", () => {
    const un = fd({ id: "D9", pairedHash: null, pairedIds: [] });
    expect(S4.judgeDrone(dev({ frm: [un], post: [dm({ id: "D9", paired: null, pairedId: null })] })).pass).toBe(true);
    const absent = dm();
    delete (absent as { pairedId?: unknown }).pairedId;
    expect(S4.judgeDrone(dev({ post: [absent, d2m] })).pass).toBe(true);
  });
  it("a pairedId outside the layer fails", () => {
    const v = S4.judgeDrone(dev({ frm: [fd({ pairedIds: [] }), d2f], post: dpost({ pairedId: "Nope" }) }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("not in the layer");
  });
  it("both sides empty passes with NOT EXERCISED", () => {
    const v = S4.judgeDrone(dev({ frm: [], post: [] }));
    expect(v.pass).toBe(true);
    expect(v.never).toEqual(["any drone port (both sides empty)"]);
  });
  it("an invalid post, no post and no sample fail", () => {
    expect(S4.judgeDrone(dev({ post: [dm({ x: 1.5 })] })).pass).toBe(false);
    expect(S4.judgeDrone({ ...dev(), posts: [] }).pass).toBe(false);
    expect(S4.judgeDrone({ ...dev(), frmSamples: [] }).pass).toBe(false);
  });
});

// --------------------------------------------------------------------------------------------------------------------- wires

const fw = (id: string, a: [number, number], b: [number, number]): FrmWire => ({ id, ends: [a, b] });
const wm = (id: string, a: [number, number], b: [number, number]) => ({ id, points: [a, b] });
const wev = (o: { post?: unknown[]; frm?: FrmWire[]; after?: FrmWire[] } = {}): S4.WiresEvidence =>
  ev({ ...o, frm: o.frm ?? [fw("W1", [0, 0], [10, 10])], defPost: [wm("W1", [0, 0], [10, 10])] });

describe("judgeWires", () => {
  it("identical wires pass", () => {
    const v = S4.judgeWires(wev());
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("1 wires compared");
    expect(v.never).toContain("a wire with swapped ends");
  });
  it("ends within 1 m of FRM's unrounded value pass", () => {
    expect(S4.judgeWires(wev({ frm: [fw("W1", [0.4, 0.4], [10.4, 9.6])] })).pass).toBe(true);
  });
  it("swapped ends pass and are counted", () => {
    const v = S4.judgeWires(wev({ post: [wm("W1", [10, 10], [0, 0])] }));
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("1 wires with swapped ends");
    expect(v.never).not.toContain("a wire with swapped ends");
  });
  it("one end 1.5 m off fails with the larger sum", () => {
    const v = S4.judgeWires(wev({ post: [wm("W1", [0, 0], [10, 12])] }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("W1 ends are over 1 m");
    // in order: 0 + 2 = 2; swapped: hypot(10,10)+hypot(10,12) is the larger sum
    expect(text(v)).toContain(`${(Math.hypot(10, 10) + Math.hypot(10, 12)).toFixed(2)} m total`);
  });
  it("a wire id only in FRM fails and the dropped count is quoted", () => {
    const v = S4.judgeWires(wev({ frm: [fw("W1", [0, 0], [10, 10]), fw("W2", [5, 5], [6, 6])] }), { dropped: 3 });
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("1 wire ids in FRM only");
    expect(text(v)).toContain("3 dropped wire(s)");
  });
  it("a wire id only in the mod fails", () => {
    expect(S4.judgeWires(wev({ post: [wm("W1", [0, 0], [10, 10]), wm("W9", [1, 1], [2, 2])] })).pass).toBe(false);
  });
  it("both sides empty passes with NOT EXERCISED; no post fails", () => {
    const v = S4.judgeWires(wev({ frm: [], post: [] }));
    expect(v.pass).toBe(true);
    expect(v.never).toEqual(["any wire (both sides empty)"]);
    expect(S4.judgeWires({ ...wev(), posts: [] }).pass).toBe(false);
  });
  it("an invalid post fails; the other side rescues", () => {
    expect(S4.judgeWires(wev({ post: [{ id: "W1", points: [[0, 0]] }] })).pass).toBe(false);
    expect(S4.judgeWires(wev({ frm: [fw("W1", [50, 50], [60, 60])], after: [fw("W1", [0, 0], [10, 10])] })).pass).toBe(true);
  });
  it("derived FRM ids are reported, not failed", () => {
    const v = S4.judgeWires(wev({ frm: [fw("W1", [0, 0], [10, 10]), fw("derived-x", [1, 1], [2, 2])] }));
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("derived id");
  });
});
