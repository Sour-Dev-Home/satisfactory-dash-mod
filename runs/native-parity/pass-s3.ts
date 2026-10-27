// The S3 pass conditions of the native-parity run (ADR-0058 Decision 3, sprint S3): the rails and conveyors layers the mod posts,
// judged against the FRM-derived layers taken on either side of each post. Each reader is judged separately. A new file: the S1
// and S2 judges (pass.ts, pass-s2.ts) stay as they are. The thresholds below are the owner's (2026-10-26); the constants marked
// "new" are this file's own and are listed in the plan's Open questions. Pure: evidence in, verdict out.
import { ConveyorsLayerDataSchema, RailsLayerDataSchema } from "../../harness/contract/src/map";
import type { FrmConveyorSample, FrmEdge, FrmRail } from "../../harness/src/frm-s3";
import { round4 } from "../../harness/src/frm-s3";
import type { Bracket, Counts } from "./pass-s2";

export type { Bracket, Counts };

/** Owner, 2026-10-26: rails, a symmetric point-to-polyline distance, each way, against FRM's unrounded values (0.01 m, M3). */
export const S3_RAILS_MAX_M = 1;
/** Owner, 2026-10-26: a conveyor's `at` is within 1 m of FRM's unrounded value. */
export const S3_AT_MAX_M = 1;
/** Reuse (not new) of S2's trainRoutes rule: a post is compared with an FRM sample taken within this of it. */
export const S3_GAP_MS = 120_000;
/** How many differences a reason lists. */
export const S3_SHOW = 10;
/** New (reporting only, never fails): a track is "curved" when a point is this far from its end-to-end chord. */
export const S3_CURVE_M = 0.5;
/** New (reporting only, never fails): the point clamp, a track over this many metres has its points clamped by the mod. */
export const S3_LONG_TRACK_M = 999;
/** New (comparison float slack, not a threshold): added to the owner's metre limits so a distance of exactly 1 passes. */
const EPS = 1e-9;

export interface RailsEvidence { posts: Array<{ at: string; status: number; data: unknown }>; frmSamples: Array<Bracket<string>>; distinct: Record<string, FrmRail[]>; frm: Counts }
export interface ConveyorsEvidence { posts: Array<{ at: string; status: number; data: unknown }>; frmSamples: Array<Bracket<string>>; distinct: Record<string, FrmConveyorSample>; frm: Counts; drone: { ids: string[]; frm: Counts } }
export interface S3Evidence { rails: RailsEvidence; conveyors: ConveyorsEvidence }
export interface S3Result { pass: boolean; reasons: string[]; never: string[] }

const ms = (iso: string) => Date.parse(iso);
const failing = (reasons: string[]): S3Result => ({ pass: false, reasons: reasons.slice(0, 30), never: [] });
const fmt2 = (n: number) => n.toFixed(2);

// ------------------------------------------------------------------------------------------------------------------- geometry

/** The least distance from a point to any segment of a polyline (clamped projection); a zero-length segment is the distance to its point. */
export function distToPolylineM(p: [number, number], poly: Array<[number, number]>): number {
  if (poly.length === 0) return Infinity;
  let best = Infinity;
  for (let i = 0; i < Math.max(1, poly.length - 1); i++) {
    const a = poly[i]!;
    const b = poly[i + 1] ?? a;
    const dx = b[0] - a[0];
    const dy = b[1] - a[1];
    const len2 = dx * dx + dy * dy;
    const t = len2 === 0 ? 0 : Math.min(1, Math.max(0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / len2));
    best = Math.min(best, Math.hypot(p[0] - (a[0] + t * dx), p[1] - (a[1] + t * dy)));
  }
  return best;
}

const trackLengthM = (pts: Array<[number, number]>): number => pts.slice(1).reduce((s, p, i) => s + Math.hypot(p[0] - pts[i]![0], p[1] - pts[i]![1]), 0);
const isCurved = (pts: Array<[number, number]>): boolean => {
  if (pts.length < 3) return false;
  const chord: Array<[number, number]> = [pts[0]!, pts.at(-1)!];
  return pts.some((p) => distToPolylineM(p, chord) > S3_CURVE_M);
};

// ------------------------------------------------------------------------------------------------------------------ shared pairing

/** The FRM samples before (ended within S3_GAP_MS) and after (started within it) a post, as judgeRoutes pairs them. */
function sidesOf<T>(at: string, samples: readonly Bracket<string>[], distinct: Record<string, T>, empty: T): T[] {
  const before = samples.filter((s) => ms(s.endedAt) <= ms(at) && ms(at) - ms(s.endedAt) <= S3_GAP_MS).at(-1) ?? null;
  const after = samples.find((s) => ms(s.startedAt) >= ms(at) && ms(s.startedAt) - ms(at) <= S3_GAP_MS) ?? null;
  return [before, after].filter((s): s is Bracket<string> => s !== null).map((s) => distinct[s.value] ?? empty);
}

// ---------------------------------------------------------------------------------------------------------------------- rails

interface RailsCompare { failures: string[]; compared: number; maxModToFrm: number; maxFrmToMod: number; notRepresentable: string[] }

function compareRails(mod: Array<{ id: string; points: Array<[number, number]> }>, frm: FrmRail[]): RailsCompare {
  const out: RailsCompare = { failures: [], compared: 0, maxModToFrm: 0, maxFrmToMod: 0, notRepresentable: frm.filter((t) => !t.representable).map((t) => t.id) };
  const frmById = new Map(frm.filter((t) => t.representable).map((t) => [t.id, t.points]));
  const modById = new Map(mod.map((t) => [t.id, t.points]));
  const frmOnly = [...frmById.keys()].filter((id) => !modById.has(id));
  const modOnly = [...modById.keys()].filter((id) => !frmById.has(id));
  if (frmOnly.length) out.failures.push(`${frmOnly.length} track ids in FRM only (first ${S3_SHOW}): ${frmOnly.slice(0, S3_SHOW).join(" ")}`);
  if (modOnly.length) out.failures.push(`${modOnly.length} track ids in the mod only (first ${S3_SHOW}): ${modOnly.slice(0, S3_SHOW).join(" ")}`);
  const far: string[] = [];
  for (const [id, modPts] of modById) {
    const frmPts = frmById.get(id);
    if (frmPts === undefined) continue;
    out.compared++;
    for (const p of modPts) {
      const d = distToPolylineM(p, frmPts);
      out.maxModToFrm = Math.max(out.maxModToFrm, d);
      if (d > S3_RAILS_MAX_M + EPS) far.push(`${id} mod point (${p[0]}, ${p[1]}) is ${fmt2(d)} m from FRM's track`);
    }
    for (const p of frmPts) {
      const d = distToPolylineM(p, modPts);
      out.maxFrmToMod = Math.max(out.maxFrmToMod, d);
      if (d > S3_RAILS_MAX_M + EPS) far.push(`${id} FRM point (${p[0]}, ${p[1]}) is ${fmt2(d)} m from the mod's track`);
    }
  }
  if (far.length) out.failures.push(`${far.length} points over ${S3_RAILS_MAX_M} m (first ${S3_SHOW}): ${far.slice(0, S3_SHOW).join("; ")}`);
  return out;
}

/**
 * rails (owner's rule, 2026-10-26): the mod's posted layer is valid against the pinned contract; the set of track ids equals FRM's
 * (FRM's taken from the tracks the contract can hold, the others reported); every point of the mod's polyline is within 1 m of FRM's
 * polyline for that track and every FRM point within 1 m of the mod's. The post passes against either FRM sample around it.
 */
export function judgeRails(r: RailsEvidence): S3Result {
  const reasons: string[] = [];
  let posts = 0;
  let tracks = 0;
  let maxA = 0;
  let maxB = 0;
  const notRep = new Set<string>();
  const frmTracks: FrmRail[] = [];
  if (r.posts.length === 0) reasons.push("the mod posted no rails layer in the window");
  for (const post of r.posts) {
    const parsed = RailsLayerDataSchema.safeParse(post.data);
    if (!parsed.success) {
      reasons.push(`${post.at}: the posted layer is not valid against the pinned contract (${parsed.error.issues[0]?.message ?? "?"})`);
      continue;
    }
    const sides = sidesOf<FrmRail[]>(post.at, r.frmSamples, r.distinct, []);
    if (sides.length === 0) {
      reasons.push(`${post.at}: no FRM getTrainRails sample within ${S3_GAP_MS / 1000} s`);
      continue;
    }
    const results = sides.map((s) => compareRails(parsed.data as never, s));
    const ok = results.findIndex((c) => c.failures.length === 0);
    if (ok < 0) {
      reasons.push(`${post.at}: the layer differs from FRM's`, ...results[0]!.failures.map((f) => `  ${f}`));
      continue;
    }
    posts++;
    tracks = results[ok]!.compared;
    maxA = Math.max(maxA, results[ok]!.maxModToFrm);
    maxB = Math.max(maxB, results[ok]!.maxFrmToMod);
    for (const id of results[ok]!.notRepresentable) notRep.add(id);
    frmTracks.push(...sides[ok]!);
  }
  if (r.frm.failed > 0 && r.frmSamples.length === 0) reasons.push("no FRM getTrainRails sample succeeded");
  if (reasons.length) return failing(reasons);
  const never = [
    frmTracks.some((t) => isCurved(t.points)) ? null : "a curved track (no FRM track has a point over 0.5 m from its chord)",
    frmTracks.some((t) => trackLengthM(t.points) > S3_LONG_TRACK_M) ? null : "a track over 999 m (the point clamp)",
    notRep.size > 0 ? null : "a track the contract cannot hold",
  ].filter((x): x is string => x !== null);
  const out = [
    `${posts} posted layer(s) valid against the contract; track ids equal FRM's; ${tracks} tracks compared; farthest point ${fmt2(maxA)} m (mod to FRM) and ${fmt2(maxB)} m (FRM to mod), limit ${S3_RAILS_MAX_M} m`,
  ];
  if (notRep.size > 0) out.push(`${notRep.size} FRM tracks the contract cannot hold, reported and not failed (first ${S3_SHOW}): ${[...notRep].slice(0, S3_SHOW).join(" ")}`);
  return { pass: true, reasons: out, never };
}

// ------------------------------------------------------------------------------------------------------------------- conveyors

type ModEdge = {
  id: string; kind: string; from: string | null; fromKind: string | null; to: string | null; toKind: string | null;
  capacityPerMinute: number | null; tier: string; at: { x: number; y: number };
};

const classPart = (id: string) => id.replace(/_\d+$/, "");
const isDronePort = (id: string) => classPart(id).startsWith("Build_DroneStation");
const END_KINDS_SEEN = ["splitter", "merger", "junction", "station", "storage", "extractor", "consumer", "machine"] as const;
const show = (v: unknown) => (v === null ? "null" : String(v));

function compareEdges(mod: ModEdge[], frm: FrmEdge[]): string[] {
  const failures: string[] = [];
  const frmById = new Map(frm.map((e) => [e.id, e]));
  const modById = new Map(mod.map((e) => [e.id, e]));
  const frmOnly = [...frmById.keys()].filter((id) => !modById.has(id));
  const modOnly = [...modById.keys()].filter((id) => !frmById.has(id));
  if (frmOnly.length) failures.push(`${frmOnly.length} edge ids in FRM only (first ${S3_SHOW}): ${frmOnly.slice(0, S3_SHOW).join(" ")}`);
  if (modOnly.length) failures.push(`${modOnly.length} edge ids in the mod only (first ${S3_SHOW}): ${modOnly.slice(0, S3_SHOW).join(" ")}`);
  const diffs: string[] = [];
  for (const [id, m] of modById) {
    const f = frmById.get(id);
    if (f === undefined) continue;
    for (const k of ["kind", "from", "to", "fromKind", "toKind", "tier"] as const) {
      if (m[k] !== f[k]) diffs.push(`${id} ${k} mod=${show(m[k])} frm=${show(f[k])}`);
    }
    // capacityPerMinute: equal after round4 on both sides (new: round4 is the shared "exact" for numbers, M4); null equals null.
    const mc = m.capacityPerMinute === null ? null : round4(m.capacityPerMinute);
    const fc = f.capacityPerMinute === null ? null : round4(f.capacityPerMinute);
    if (mc !== fc) diffs.push(`${id} capacityPerMinute mod=${show(mc)} frm=${show(fc)}`);
    const d = Math.hypot(m.at.x - f.atX, m.at.y - f.atY);
    if (!(d <= S3_AT_MAX_M + EPS)) diffs.push(`${id} at mod=(${m.at.x}, ${m.at.y}) frm=(${f.atX}, ${f.atY}) ${fmt2(d)} m`);
  }
  if (diffs.length) failures.push(`${diffs.length} field differences (first ${S3_SHOW}):`, ...diffs.slice(0, S3_SHOW).map((d) => `    ${d}`));
  return failures;
}

/**
 * conveyors (owner's rule, 2026-10-26): per accepted post, valid against the pinned contract; the set of edge ids equals FRM's
 * (hypertubes FRM put in `filtered` are reported, not compared); for every common id kind, from, to, fromKind, toKind, tier and
 * capacityPerMinute (after round4) are equal and `at` is within 1 m. Open ends both sides agree on are reported and never fail; a
 * disagreement is a from/to mismatch. Every drone-port end must be a port FRM lists. NOT EXERCISED lines are reported only.
 */
export function judgeConveyors(c: ConveyorsEvidence, kindCheck: { checked: number; differ: number; classes: string[] } | null = null): S3Result {
  const reasons: string[] = [];
  let posts = 0;
  let edges = 0;
  let openEdges = 0;
  let intoPort = 0;
  let outOfPort = 0;
  let filtered = 0;
  const kinds = new Set<string>();
  const endKinds = new Set<string>();
  const beltTiers = new Set<string>();
  const portIds = new Set(c.drone.ids);
  const missingPorts = new Set<string>();
  if (c.posts.length === 0) reasons.push("the mod posted no conveyors layer in the window");
  for (const post of c.posts) {
    const parsed = ConveyorsLayerDataSchema.safeParse(post.data);
    if (!parsed.success) {
      reasons.push(`${post.at}: the posted layer is not valid against the pinned contract (${parsed.error.issues[0]?.message ?? "?"})`);
      continue;
    }
    const sides = sidesOf<FrmConveyorSample | null>(post.at, c.frmSamples, c.distinct, null).filter((s): s is FrmConveyorSample => s !== null);
    if (sides.length === 0) {
      reasons.push(`${post.at}: no FRM getBelts+getLifts+getPipes sample within ${S3_GAP_MS / 1000} s`);
      continue;
    }
    const mod = parsed.data as ModEdge[];
    const results = sides.map((s) => compareEdges(mod, s.edges));
    const ok = results.findIndex((f) => f.length === 0);
    if (ok < 0) {
      reasons.push(`${post.at}: the layer differs from FRM's`, ...results[0]!.map((f) => `  ${f}`));
      continue;
    }
    posts++;
    edges = mod.length;
    openEdges = mod.filter((e) => e.from === null || e.to === null).length;
    filtered = Math.max(filtered, sides[ok]!.filtered.length);
    intoPort = 0;
    outOfPort = 0;
    for (const e of mod) {
      kinds.add(e.kind);
      if (e.kind === "belt") beltTiers.add(e.tier);
      for (const k of [e.fromKind, e.toKind]) if (k !== null) endKinds.add(k);
      for (const end of [e.from, e.to]) if (end !== null && isDronePort(end) && !portIds.has(end)) missingPorts.add(end);
      if (e.kind !== "pipe" && e.to !== null && isDronePort(e.to)) intoPort++;
      if (e.kind !== "pipe" && e.from !== null && isDronePort(e.from)) outOfPort++;
    }
  }
  for (const id of missingPorts) reasons.push(`drone port end ${id} is not among the ports FRM lists (getDroneStation)`);
  if (c.frm.failed > 0 && c.frmSamples.length === 0) reasons.push("no FRM getBelts+getLifts+getPipes sample succeeded");
  if (c.drone.frm.failed > 0 && c.drone.frm.reads === 0) reasons.push("no FRM getDroneStation read succeeded");
  if (reasons.length) return failing(reasons);
  const never = [
    ...END_KINDS_SEEN.filter((k) => !endKinds.has(k)).map((k) => `an end of kind ${k}`),
    kinds.has("lift") ? null : "a lift edge",
    kinds.has("pipe") ? null : "a pipe edge",
    openEdges > 0 ? null : "an open end",
    beltTiers.size >= 2 ? null : "two belt tiers",
    intoPort > 0 ? null : "a belt into a drone port",
    outOfPort > 0 ? null : "a belt out of a drone port",
    filtered > 0 ? null : "a hypertube filtered by FRM",
  ].filter((x): x is string => x !== null);
  const out = [
    `${posts} posted layer(s) valid against the contract; edge ids equal FRM's; ${edges} edges; kind, from, to, fromKind, toKind, tier, capacity (4 decimals) equal; at within ${S3_AT_MAX_M} m`,
    `${openEdges} edges with an open end, agreed with FRM`,
    `${intoPort} edges into a drone port and ${outOfPort} out of one; all drone-port ends are ports FRM lists`,
  ];
  if (filtered > 0) out.push(`${filtered} hypertube record(s) filtered by FRM, reported and not compared`);
  if (kindCheck !== null) {
    out.push(`kind table agrees with casts on ${kindCheck.checked - kindCheck.differ} of ${kindCheck.checked} ends; differing classes: ${kindCheck.classes.length ? kindCheck.classes.join(" ") : "none"}`);
  }
  return { pass: true, reasons: out, never };
}
