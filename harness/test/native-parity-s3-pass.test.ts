import { describe, expect, it } from "vitest";
import type { FrmConveyorSample, FrmEdge, FrmRail } from "../src/frm-s3";
import * as S3 from "../../runs/native-parity/pass-s3";

const T0 = Date.parse("2026-10-26T12:00:00.000Z");
const iso = (ms: number) => new Date(ms).toISOString();
const text = (v: S3.S3Result) => v.reasons.join("\n");
const none: S3.Counts = { reads: 5, failed: 0 };

describe("distToPolylineM", () => {
  const line: Array<[number, number]> = [[0, 0], [10, 0]];
  it("is the perpendicular distance inside the segment and the end distance outside it", () => {
    expect(S3.distToPolylineM([5, 1], line)).toBe(1);
    expect(S3.distToPolylineM([12, 0], line)).toBe(2);
  });
  it("a zero-length segment is the distance to its point; a vertex is 0", () => {
    expect(S3.distToPolylineM([3, 4], [[0, 0], [0, 0]])).toBe(5);
    expect(S3.distToPolylineM([3, 4], [[0, 0]])).toBe(5);
    expect(S3.distToPolylineM([10, 0], [[0, 0], [10, 0], [10, 10]])).toBe(0);
  });
});

// --------------------------------------------------------------------------------------------------------------------- rails

type Pts = Array<[number, number]>;
const track = (id: string, points: Pts, representable = true): FrmRail => ({ id, points, representable });
const modTrack = (id: string, points: Pts) => ({ id, points });
function railsEv(o: { post?: unknown; frm?: FrmRail[]; after?: FrmRail[]; posts?: S3.RailsEvidence["posts"]; frmSamples?: S3.RailsEvidence["frmSamples"]; failed?: number }): S3.RailsEvidence {
  const distinct: Record<string, FrmRail[]> = { before: o.frm ?? [track("T1", [[0, 0], [10, 0]])], after: o.after ?? o.frm ?? [] };
  const frmSamples = o.frmSamples ?? [
    { startedAt: iso(T0 - 20_000), endedAt: iso(T0 - 10_000), value: "before" },
    ...(o.after ? [{ startedAt: iso(T0 + 10_000), endedAt: iso(T0 + 20_000), value: "after" }] : []),
  ];
  return {
    posts: o.posts ?? [{ at: iso(T0), status: 200, data: o.post ?? [modTrack("T1", [[0, 0], [10, 0]])] }],
    frmSamples, distinct, frm: { reads: 5, failed: o.failed ?? 0 },
  };
}

describe("judgeRails", () => {
  it("identical polylines pass", () => {
    const v = S3.judgeRails(railsEv({}));
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("1 tracks compared");
  });
  it("a mod polyline exactly 1 m away passes; 2 m away fails", () => {
    expect(S3.judgeRails(railsEv({ post: [modTrack("T1", [[0, 1], [10, 1]])] })).pass).toBe(true);
    const v = S3.judgeRails(railsEv({ post: [modTrack("T1", [[0, 2], [10, 2]])] }));
    expect(v.pass).toBe(false);
    // FRM's only segment is the whole track, so the endpoint allowance covers the mod side; the FRM to mod direction still fails it.
    expect(text(v)).toContain("T1 FRM point (0, 0) is 2.00 m");
  });
  // Owner ruling 2026-10-27 (run 40439238142, Test3): the mod's first and last point get 1 m plus FRM's own end segment.
  // (The contract holds integer points, so the mod's side uses whole metres; FRM's side carries the fractions.)
  it("passes a mod endpoint past FRM's short polyline end (the run's case: FRM stops short of the true end)", () => {
    const frm = [track("T1", [[-1400, 1585.67], [-1409.6, 1585.67], [-1410.85, 1585.67]])];
    expect(S3.judgeRails(railsEv({ frm, post: [modTrack("T1", [[-1400, 1586], [-1412, 1586]])] })).pass).toBe(true);
  });
  it("fails a mod endpoint beyond 1 m plus FRM's end segment, at either end", () => {
    const frm = [track("T1", [[0, 0], [9, 0], [10, 0]])];
    expect(S3.judgeRails(railsEv({ frm, post: [modTrack("T1", [[0, 0], [12, 0]])] })).pass).toBe(true);
    const v = S3.judgeRails(railsEv({ frm, post: [modTrack("T1", [[0, 0], [13, 0]])] }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("mod point (13, 0)");
    const start = [track("T1", [[0, 0], [1, 0], [10, 0]])];
    expect(S3.judgeRails(railsEv({ frm: start, post: [modTrack("T1", [[-2, 0], [10, 0]])] })).pass).toBe(true);
    expect(S3.judgeRails(railsEv({ frm: start, post: [modTrack("T1", [[-3, 0], [10, 0]])] })).pass).toBe(false);
  });
  it("the end allowance is capped: a two-point FRM track cannot wave through a far endpoint", () => {
    // FRM's only segment is 10 m; uncapped, the mod's end at 13 m (3 m off) would pass. Capped at 1 m extra, the limit is 2 m.
    const frm = [track("T1", [[0, 0], [10, 0]])];
    const v = S3.judgeRails(railsEv({ frm, post: [modTrack("T1", [[0, 0], [13, 0]])] }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("mod point (13, 0) is 3.00 m");
    expect(S3.judgeRails(railsEv({ frm, post: [modTrack("T1", [[0, 0], [12, 0]])] })).pass).toBe(true);
  });
  it("an interior mod point over 1 m still fails", () => {
    const frm = [track("T1", [[0, 0], [5, 0], [10, 0]])];
    expect(S3.judgeRails(railsEv({ frm, post: [modTrack("T1", [[0, 0], [5, 2], [10, 0]])] })).pass).toBe(false);
  });
  it("the FRM to mod direction is unchanged: an FRM endpoint 2 m past the mod's line fails", () => {
    const frm = [track("T1", [[0, 0], [9, 0], [10, 0], [12, 0]])];
    const v = S3.judgeRails(railsEv({ frm, post: [modTrack("T1", [[0, 0], [10, 0]])] }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("FRM point (12, 0)");
  });
  it("fails on an FRM point the mod's polyline does not reach (the symmetric direction)", () => {
    const v = S3.judgeRails(railsEv({ post: [modTrack("T1", [[0, 0], [5, 0]])] }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("T1 FRM point (10, 0) is 5.00 m from the mod's track");
  });
  it("a track id only in FRM fails, and only in the mod fails", () => {
    const a = S3.judgeRails(railsEv({ frm: [track("T1", [[0, 0], [10, 0]]), track("T2", [[0, 5], [10, 5]])] }));
    expect(a.pass).toBe(false);
    expect(text(a)).toContain("in FRM only");
    expect(text(a)).toContain("T2");
    const b = S3.judgeRails(railsEv({ post: [modTrack("T1", [[0, 0], [10, 0]]), modTrack("T9", [[0, 5], [10, 5]])] }));
    expect(b.pass).toBe(false);
    expect(text(b)).toContain("in the mod only");
  });
  it("an FRM track the contract cannot hold is reported, not failed", () => {
    const v = S3.judgeRails(railsEv({ frm: [track("T1", [[0, 0], [10, 0]]), track("T2", [[1.0, 1.0], [1.2, 1.2]], false)] }));
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("1 FRM tracks the contract cannot hold, reported and not failed");
    expect(v.never.join("\n")).not.toContain("a track the contract cannot hold");
  });
  it("an invalid post (a point as a string) fails; no post fails; no FRM sample within 120 s fails", () => {
    expect(S3.judgeRails(railsEv({ post: [{ id: "T1", points: [["0", 0], [10, 0]] }] })).pass).toBe(false);
    expect(S3.judgeRails(railsEv({ posts: [] })).pass).toBe(false);
    const late = S3.judgeRails(railsEv({ frmSamples: [{ startedAt: iso(T0 - 400_000), endedAt: iso(T0 - 300_000), value: "before" }] }));
    expect(late.pass).toBe(false);
    expect(text(late)).toContain("no FRM getTrainRails sample within 120 s");
  });
  it("the other side of the pair rescues a post that differs from the first", () => {
    const moved = [modTrack("T1", [[0, 5], [10, 5]])];
    expect(S3.judgeRails(railsEv({ post: moved })).pass).toBe(false);
    expect(S3.judgeRails(railsEv({ post: moved, after: [track("T1", [[0, 5], [10, 5]])] })).pass).toBe(true);
  });
  it("fails when FRM reads failed and no sample succeeded", () => {
    const v = S3.judgeRails(railsEv({ frmSamples: [], failed: 3 }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("no FRM getTrainRails sample succeeded");
  });
  it("a duplicate track id in the mod's layer fails; one in FRM's is reported and does not fail", () => {
    const m = S3.judgeRails(railsEv({ post: [modTrack("T1", [[0, 0], [10, 0]]), modTrack("T1", [[0, 0], [10, 0]])] }));
    expect(m.pass).toBe(false);
    expect(text(m)).toContain("the mod sent track id(s) twice");
    const f = S3.judgeRails(railsEv({ frm: [track("T1", [[0, 0], [10, 0]]), track("T1", [[0, 0], [10, 0]])] }));
    expect(f.pass).toBe(true);
    expect(text(f)).toContain("FRM listed 1 track id(s) twice, reported and not failed");
  });
  it("an empty layer on both sides passes and reports NOT EXERCISED any track", () => {
    const v = S3.judgeRails(railsEv({ post: [], frm: [] }));
    expect(v.pass).toBe(true);
    expect(v.never.join("\n")).toContain("any track");
    expect(S3.judgeRails(railsEv({})).never.join("\n")).not.toContain("any track");
  });
  it("a mod track whose id FRM marks not representable is in neither set (reported, not failed)", () => {
    const v = S3.judgeRails(railsEv({ frm: [track("T1", [[0, 0], [10, 0]]), track("T2", [[1, 1], [1.2, 1.2]], false)], post: [modTrack("T1", [[0, 0], [10, 0]]), modTrack("T2", [[1, 1], [2, 2]])] }));
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("1 FRM tracks the contract cannot hold");
  });
  it("reports NOT EXERCISED for a curved track and a track over 999 m, and a bent FRM track removes the first line", () => {
    const flat = S3.judgeRails(railsEv({}));
    expect(flat.never.join("\n")).toContain("a curved track");
    expect(flat.never.join("\n")).toContain("a track over 999 m");
    expect(flat.never.join("\n")).toContain("a track the contract cannot hold");
    const bent = S3.judgeRails(railsEv({ frm: [track("T1", [[0, 0], [5, 0.6], [10, 0]])], post: [modTrack("T1", [[0, 0], [5, 1], [10, 0]])] }));
    expect(bent.pass).toBe(true);
    expect(bent.never.join("\n")).not.toContain("a curved track");
    const long = S3.judgeRails(railsEv({ frm: [track("T1", [[0, 0], [1000, 0]])], post: [modTrack("T1", [[0, 0], [1000, 0]])] }));
    expect(long.never.join("\n")).not.toContain("a track over 999 m");
  });
});

// ------------------------------------------------------------------------------------------------------------------ conveyors

const frmEdge = (o: Partial<FrmEdge> = {}): FrmEdge => ({
  id: "B1", kind: "belt", from: "Build_MinerMk1_C_1", fromKind: "extractor", to: "Build_ConstructorMk1_C_2", toKind: "machine",
  capacityPerMinute: 300, tier: "Conveyor Belt Mk.2", atX: 10, atY: 20, ...o,
});
const modEdge = (o: Record<string, unknown> = {}) => {
  const e = frmEdge();
  return { id: e.id, kind: e.kind, from: e.from, fromKind: e.fromKind, to: e.to, toKind: e.toKind, capacityPerMinute: e.capacityPerMinute, tier: e.tier, at: { x: 10, y: 20 }, ...o };
};
function convEv(o: { post?: unknown[]; frm?: FrmEdge[]; filtered?: FrmConveyorSample["filtered"]; ports?: string[]; posts?: S3.ConveyorsEvidence["posts"]; failed?: number } = {}): S3.ConveyorsEvidence {
  const sample: FrmConveyorSample = { edges: o.frm ?? [frmEdge()], filtered: o.filtered ?? [], dropped: 0 };
  return {
    posts: o.posts ?? [{ at: iso(T0), status: 200, data: o.post ?? [modEdge()] }],
    frmSamples: [{ startedAt: iso(T0 - 20_000), endedAt: iso(T0 - 10_000), value: "s" }],
    distinct: { s: sample }, frm: { reads: 5, failed: o.failed ?? 0 }, drone: { ids: o.ports ?? [], frm: none },
  };
}

describe("judgeConveyors", () => {
  it("identical edges pass", () => {
    const v = S3.judgeConveyors(convEv());
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("1 edges");
  });
  it("capacityPerMinute is compared at four decimals", () => {
    expect(S3.judgeConveyors(convEv({ frm: [frmEdge({ capacityPerMinute: 300.0001 })] })).pass).toBe(false);
    expect(S3.judgeConveyors(convEv({ frm: [frmEdge({ capacityPerMinute: 300.00001 })] })).pass).toBe(true);
    expect(S3.judgeConveyors(convEv({ frm: [frmEdge({ capacityPerMinute: null })] })).pass).toBe(false);
    expect(S3.judgeConveyors(convEv({ post: [modEdge({ capacityPerMinute: null })], frm: [frmEdge({ capacityPerMinute: null })] })).pass).toBe(true);
  });
  it.each([
    ["kind", { kind: "lift" }, "kind mod=lift frm=belt"],
    ["tier", { tier: "Conveyor Belt Mk.3" }, "tier mod=Conveyor Belt Mk.3 frm=Conveyor Belt Mk.2"],
    ["from", { from: "Build_MinerMk1_C_9" }, "from mod=Build_MinerMk1_C_9 frm=Build_MinerMk1_C_1"],
    ["toKind", { toKind: "other" }, "toKind mod=other frm=machine"],
    ["fromKind", { fromKind: "other" }, "fromKind mod=other frm=extractor"],
    ["to", { to: "Build_ConstructorMk1_C_3" }, "to mod=Build_ConstructorMk1_C_3"],
  ])("a differing %s fails and names the field", (_f, over, line) => {
    const v = S3.judgeConveyors(convEv({ post: [modEdge(over)] }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain(line);
  });
  it("`at` off by (1, 0) passes and by (1, 1) fails", () => {
    expect(S3.judgeConveyors(convEv({ post: [modEdge({ at: { x: 11, y: 20 } })] })).pass).toBe(true);
    const v = S3.judgeConveyors(convEv({ post: [modEdge({ at: { x: 11, y: 21 } })] }));
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("B1 at mod=(11, 21)");
  });
  it("an FRM-only pipe in `filtered` is reported, not failed; an FRM-only belt fails", () => {
    const hyper = S3.judgeConveyors(convEv({ filtered: [{ id: "H1", className: "Build_PipeHyperExample_C" }] }));
    expect(hyper.pass).toBe(true);
    expect(text(hyper)).toContain("1 hypertube record(s) filtered by FRM");
    expect(hyper.never.join("\n")).not.toContain("a hypertube filtered by FRM");
    const belt = S3.judgeConveyors(convEv({ frm: [frmEdge(), frmEdge({ id: "B2" })] }));
    expect(belt.pass).toBe(false);
    expect(text(belt)).toContain("in FRM only");
  });
  it("an open end on both sides passes and is counted; open in the mod only fails", () => {
    const open = { to: null, toKind: null };
    const both = S3.judgeConveyors(convEv({ post: [modEdge(open)], frm: [frmEdge(open)] }));
    expect(both.pass).toBe(true);
    expect(text(both)).toContain("1 edges with an open end, agreed with FRM");
    expect(both.never.join("\n")).not.toContain("an open end");
    const one = S3.judgeConveyors(convEv({ post: [modEdge(open)] }));
    expect(one.pass).toBe(false);
    expect(text(one)).toContain("to mod=null");
  });
  it("a drone-port end must be a port FRM lists", () => {
    const port = { to: "Build_DroneStation_C_1", toKind: "station" as const };
    const mod = [modEdge(port)];
    const frm = [frmEdge(port)];
    const missing = S3.judgeConveyors(convEv({ post: mod, frm }));
    expect(missing.pass).toBe(false);
    expect(text(missing)).toContain("Build_DroneStation_C_1");
    const ok = S3.judgeConveyors(convEv({ post: mod, frm, ports: ["Build_DroneStation_C_1"] }));
    expect(ok.pass).toBe(true);
    expect(ok.never.join("\n")).not.toContain("a belt into a drone port");
    expect(ok.never.join("\n")).toContain("a belt out of a drone port");
  });
  it("NOT EXERCISED lines: no port edge, one tier, kinds not seen", () => {
    const never = S3.judgeConveyors(convEv()).never.join("\n");
    for (const line of ["a belt into a drone port", "a belt out of a drone port", "two belt tiers", "a lift edge", "a pipe edge", "an open end", "a hypertube filtered by FRM", "an end of kind splitter", "an end of kind station"]) {
      expect(never).toContain(line);
    }
    expect(never).not.toContain("an end of kind extractor");
    expect(never).not.toContain("an end of kind machine");
    const two = S3.judgeConveyors(convEv({ post: [modEdge(), modEdge({ id: "B2", tier: "Conveyor Belt Mk.3" })], frm: [frmEdge(), frmEdge({ id: "B2", tier: "Conveyor Belt Mk.3" })] }));
    expect(two.never.join("\n")).not.toContain("two belt tiers");
  });
  it("prints the kind-check line when given, and never fails on it", () => {
    const v = S3.judgeConveyors(convEv(), { checked: 20, differ: 2, classes: ["Build_Foo_C"] });
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("kind table agrees with casts on 18 of 20 ends; differing classes: Build_Foo_C");
    expect(text(S3.judgeConveyors(convEv()))).not.toContain("kind table agrees");
  });
  it("an invalid post, no post, no sample within 120 s and failed FRM reads fail", () => {
    expect(S3.judgeConveyors(convEv({ post: [modEdge({ kind: "tube" })] })).pass).toBe(false);
    expect(S3.judgeConveyors(convEv({ posts: [] })).pass).toBe(false);
    const late = convEv();
    late.frmSamples = [{ startedAt: iso(T0 - 400_000), endedAt: iso(T0 - 300_000), value: "s" }];
    expect(S3.judgeConveyors(late).pass).toBe(false);
    const dead = convEv({ failed: 2 });
    dead.frmSamples = [];
    const v = S3.judgeConveyors(dead);
    expect(v.pass).toBe(false);
    expect(text(v)).toContain("no FRM getBelts+getLifts+getPipes sample succeeded");
  });
  it("a duplicate edge id in the mod's layer fails; one in FRM's is reported and does not fail", () => {
    const m = S3.judgeConveyors(convEv({ post: [modEdge(), modEdge()] }));
    expect(m.pass).toBe(false);
    expect(text(m)).toContain("the mod sent edge id(s) twice");
    const f = S3.judgeConveyors(convEv({ frm: [frmEdge(), frmEdge()] }));
    expect(f.pass).toBe(true);
    expect(text(f)).toContain("FRM listed 1 edge id(s) twice, reported and not failed");
  });
  it("an empty layer on both sides passes and reports NOT EXERCISED any edge", () => {
    const v = S3.judgeConveyors(convEv({ post: [], frm: [] }));
    expect(v.pass).toBe(true);
    expect(v.never.join("\n")).toContain("any edge");
    expect(S3.judgeConveyors(convEv()).never.join("\n")).not.toContain("any edge");
  });
  it("reports FRM records the oracle dropped, and does not fail on them", () => {
    const ev = convEv();
    ev.distinct.s!.dropped = 2;
    const v = S3.judgeConveyors(ev);
    expect(v.pass).toBe(true);
    expect(text(v)).toContain("2 FRM records dropped by the adapter's rules");
    expect(text(S3.judgeConveyors(convEv()))).not.toContain("FRM records dropped");
  });
  it("a failed drone read alone does not fail a layer with no port end", () => {
    const ev = convEv();
    ev.drone = { ids: [], frm: { reads: 0, failed: 3 } };
    expect(S3.judgeConveyors(ev).pass).toBe(true);
  });
});
