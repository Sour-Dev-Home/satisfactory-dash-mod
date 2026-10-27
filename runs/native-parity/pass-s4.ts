// The S4 pass conditions of the native-parity run (ADR-0058 Decision 3, sprint S4 part A): the mapMarkers, droneRoutes and
// powerLines layers the mod posts, judged against the FRM-derived layers (harness/src/frm-s4.ts) taken on either side of each
// post. Each layer is judged separately. A new file: the S1 to S3 judges (pass.ts, pass-s2.ts, pass-s3.ts) stay as they are.
// These are the plan's rule R-d and are PROVISIONAL pending the owner: every threshold below says who set it. The posted
// names are already hashes (the run builds that evidence), so names are compared as hashes and no raw name enters a reason.
// Pure: evidence in, verdict out.
import { DroneRoutesLayerDataSchema, MapMarkersLayerDataSchema, PowerLinesLayerDataSchema } from "../../harness/contract/src/map";
import type { FrmDronePort, FrmMarker, FrmWire } from "../../harness/src/frm-s4";
import { DRONE_ROUTES_MAX_ITEMS, MAP_MARKERS_MAX_ITEMS, POWER_LINES_MAX_ITEMS } from "../../harness/src/frm-s4";
import type { Bracket, Counts } from "./pass-s2";

export type { Bracket, Counts };

/** NEW for S4 (reuses S3_GAP_MS and the routes rule; plan Open questions): a post is compared with an FRM sample taken within this of it. */
export const S4_GAP_MS = 120_000;
/** Owner R-d (provisional): drone x/y and each power-line end are within 1 m of FRM's unrounded value. */
export const S4_XY_MAX_M = 1;
/** NEW (controller, provisional; plan Open questions): a marker's whole-metre x/y may differ by up to 1 m from Math.round(FRM) when FRM's value is at a .5 m boundary. */
export const S4_MARKER_ROUND_MAX_M = 1;
/** NEW (controller, provisional): how close to .5 FRM's fraction must be (0.01 m is FRM's own rounding) to count as a boundary. */
export const S4_MARKER_BOUNDARY_TOL_M = 0.01;
/** How many differences a reason lists. */
export const S4_SHOW = 10;
/** NEW (comparison float slack, not a threshold): added to the metre limits so a distance of exactly the limit passes. */
const EPS = 1e-9;
/** The id length where the native readers clamp (the contract's id maximum); reporting only. */
const ID_MAX = 200;
/** The prefix the oracles give an id they derive (empty or over-long FRM id); reporting only. */
const DERIVED_PREFIX = "derived-";

type Post = { at: string; status: number; data: unknown };
export interface MarkersEvidence { posts: Post[]; frmSamples: Array<Bracket<string>>; distinct: Record<string, FrmMarker[]>; frm: Counts }
export interface DroneEvidence { posts: Post[]; frmSamples: Array<Bracket<string>>; distinct: Record<string, FrmDronePort[]>; frm: Counts }
export interface WiresEvidence { posts: Post[]; frmSamples: Array<Bracket<string>>; distinct: Record<string, FrmWire[]>; frm: Counts }
export interface S4Evidence { markers: MarkersEvidence; drone: DroneEvidence; wires: WiresEvidence }
export interface S4Result { pass: boolean; reasons: string[]; never: string[] }

interface ModMarker { id: string; name: string; kind: string; icon: number; color: string; x: number; y: number }
interface ModPort { id: string; name: string; paired: string | null; pairedId?: string | null; input: string[]; output: string[]; fuel: string[]; x: number; y: number }
interface ModWire { id: string; points: [[number, number], [number, number]] }

const ms = (iso: string) => Date.parse(iso);
const failing = (reasons: string[]): S4Result => ({ pass: false, reasons: reasons.slice(0, 30), never: [] });
const fmt2 = (n: number) => n.toFixed(2);
const show = (v: unknown) => (v === null ? "null" : v === undefined ? "absent" : Array.isArray(v) ? `[${v.join(",")}]` : String(v));
const same = (a: unknown, b: unknown) => JSON.stringify(a) === JSON.stringify(b);

/** The FRM samples before (ended within S4_GAP_MS) and after (started within it) a post, as judgeRoutes pairs them. */
function sidesOf<T>(at: string, samples: readonly Bracket<string>[], distinct: Record<string, T>, empty: T): T[] {
  const before = samples.filter((s) => ms(s.endedAt) <= ms(at) && ms(at) - ms(s.endedAt) <= S4_GAP_MS).at(-1) ?? null;
  const after = samples.find((s) => ms(s.startedAt) >= ms(at) && ms(s.startedAt) - ms(at) <= S4_GAP_MS) ?? null;
  return [before, after].filter((s): s is Bracket<string> => s !== null).map((s) => distinct[s.value] ?? empty);
}

/** The ids that occur more than once. */
function dupes(ids: readonly string[]): string[] {
  const seen = new Set<string>();
  const twice = new Set<string>();
  for (const id of ids) (seen.has(id) ? twice : seen).add(id);
  return [...twice];
}

// -------------------------------------------------------------------------------------------------------- shared layer compare

type Stats = Record<string, number>;
interface LayerCmp<M, F> { failures: string[]; notes: string[]; pairs: Array<[M, F]>; stats: Stats }

/**
 * Id sets equal (exact) and per-id field differences from `diff`. The native readers' known divergences are reported, never
 * failed, when they explain an id-set difference: FRM ids the oracle derived (empty or over-long FRM id; the native reader drops
 * or clamps), a mod id clamped to the id maximum, and any id difference when either side is at the layer cap (the mapper cuts the
 * id-sorted list, the native readers send the rest to `Over`). FRM's repeated ids are reported (the native readers keep the first;
 * the best-matching FRM item is the one compared). The mod's own repeated ids fail.
 */
function compareLayer<M extends { id: string }, F extends { id: string }>(
  label: string, mod: M[], frm: F[], cap: number, diff: (m: M, f: F, st: Stats) => string[],
): LayerCmp<M, F> {
  const out: LayerCmp<M, F> = { failures: [], notes: [], pairs: [], stats: {} };
  const modDup = dupes(mod.map((m) => m.id));
  if (modDup.length) out.failures.push(`the mod sent ${label} id(s) twice (first ${S4_SHOW}): ${modDup.slice(0, S4_SHOW).join(" ")}`);
  const frmDup = dupes(frm.map((f) => f.id));
  if (frmDup.length) out.notes.push(`FRM listed ${frmDup.length} ${label} id(s) twice, reported and not failed (the native readers keep the first)`);
  const frmById = new Map<string, F[]>();
  for (const f of frm) frmById.set(f.id, [...(frmById.get(f.id) ?? []), f]);
  const modById = new Map(mod.map((m) => [m.id, m]));
  let frmOnly = [...frmById.keys()].filter((id) => !modById.has(id));
  let modOnly = [...modById.keys()].filter((id) => !frmById.has(id));
  const derived = frmOnly.filter((id) => id.startsWith(DERIVED_PREFIX));
  if (derived.length) {
    out.notes.push(`${derived.length} FRM ${label}(s) have a derived id (empty or over-long FRM id), reported and not failed (the native reader drops or clamps them)`);
    frmOnly = frmOnly.filter((id) => !id.startsWith(DERIVED_PREFIX));
    const clamped = modOnly.filter((id) => id.length >= ID_MAX);
    if (clamped.length) {
      out.notes.push(`${clamped.length} mod ${label}(s) have a clamped id, reported and not failed`);
      modOnly = modOnly.filter((id) => id.length < ID_MAX);
    }
  }
  if (frmOnly.length) out.failures.push(...idSetLine(`${frmOnly.length} ${label} ids in FRM only`, frmOnly, frm.length, mod.length, cap, out.notes));
  if (modOnly.length) out.failures.push(...idSetLine(`${modOnly.length} ${label} ids in the mod only`, modOnly, frm.length, mod.length, cap, out.notes));
  const diffs: string[] = [];
  for (const [id, m] of modById) {
    const cands = frmById.get(id);
    if (cands === undefined) continue;
    let best: { f: F; d: string[]; st: Stats } | null = null;
    for (const f of cands) {
      const st: Stats = {};
      const d = diff(m, f, st);
      if (best === null || d.length < best.d.length) best = { f, d, st };
      if (d.length === 0) break;
    }
    for (const [k, v] of Object.entries(best!.st)) out.stats[k] = (out.stats[k] ?? 0) + v;
    out.pairs.push([m, best!.f]);
    diffs.push(...best!.d);
  }
  if (diffs.length) out.failures.push(`${diffs.length} field differences (first ${S4_SHOW}):`, ...diffs.slice(0, S4_SHOW).map((d) => `    ${d}`));
  return out;
}

function idSetLine(head: string, ids: string[], frmN: number, modN: number, cap: number, notes: string[]): string[] {
  if (frmN >= cap || modN >= cap) {
    notes.push(`${head}, reported and not failed: a side is at the layer cap of ${cap} (the mapper cuts the id-sorted list, the native readers send the rest to Over)`);
    return [];
  }
  return [`${head} (first ${S4_SHOW}): ${ids.slice(0, S4_SHOW).join(" ")}`];
}

interface Accepted<M, F> { mod: M[]; frm: F[]; cmp: LayerCmp<M, F> }

/** The posts loop all three judges share. Returns the failure reasons, or the accepted posts. */
function runPosts<M extends { id: string }, F extends { id: string }>(
  e: { posts: Post[]; frmSamples: Array<Bracket<string>>; distinct: Record<string, F[]>; frm: Counts },
  o: { layer: string; endpoint: string; label: string; schema: { safeParse: (d: unknown) => { success: true; data: unknown } | { success: false; error: { issues: Array<{ message: string }> } } }; cap: number; diff: (mod: M[]) => (m: M, f: F, st: Stats) => string[]; hint?: (st: Stats) => string[] },
): { reasons: string[]; ok: Array<Accepted<M, F>> } {
  const reasons: string[] = [];
  const ok: Array<Accepted<M, F>> = [];
  if (e.posts.length === 0) reasons.push(`the mod posted no ${o.layer} layer in the window`);
  for (const post of e.posts) {
    const parsed = o.schema.safeParse(post.data);
    if (!parsed.success) {
      reasons.push(`${post.at}: the posted layer is not valid against the pinned contract (${parsed.error.issues[0]?.message ?? "?"})`);
      continue;
    }
    const sides = sidesOf<F[]>(post.at, e.frmSamples, e.distinct, []);
    if (sides.length === 0) {
      reasons.push(`${post.at}: no FRM ${o.endpoint} sample within ${S4_GAP_MS / 1000} s`);
      continue;
    }
    const mod = parsed.data as M[];
    const cmps = sides.map((s) => compareLayer(o.label, mod, s, o.cap, o.diff(mod)));
    const i = cmps.findIndex((c) => c.failures.length === 0);
    if (i < 0) {
      reasons.push(`${post.at}: the layer differs from FRM's`, ...cmps[0]!.failures.map((f) => `  ${f}`), ...(o.hint?.(cmps[0]!.stats) ?? []).map((f) => `  ${f}`));
      continue;
    }
    ok.push({ mod, frm: sides[i]!, cmp: cmps[i]! });
  }
  if (e.frm.failed > 0 && e.frmSamples.length === 0) reasons.push(`no FRM ${o.endpoint} sample succeeded`);
  return { reasons, ok };
}

const uniq = (lines: string[]) => [...new Set(lines)];

// -------------------------------------------------------------------------------------------------------------------- markers

const isBoundary = (v: number) => Math.abs(v - Math.floor(v) - 0.5) <= S4_MARKER_BOUNDARY_TOL_M + EPS;

function markerAxis(id: string, k: "x" | "y", mod: number, frm: number, st: Stats): string | null {
  if (mod === Math.round(frm)) return null;
  if (Math.abs(mod - frm) <= S4_MARKER_ROUND_MAX_M + EPS && isBoundary(frm)) {
    st.rounding = (st.rounding ?? 0) + 1;
    return null;
  }
  return `${id} ${k} mod=${mod} frm=${frm}`;
}

/**
 * mapMarkers (plan rule R-d, provisional): per accepted post the layer is valid against the contract; the id set equals FRM's;
 * for every common id kind, icon, color and the name hash are exactly equal and x/y equal Math.round(FRM) (or, at a .5 m
 * boundary, within 1 m, counted and reported).
 */
export function judgeMarkers(e: MarkersEvidence): S4Result {
  const { reasons, ok } = runPosts<ModMarker, FrmMarker>(e, {
    layer: "mapMarkers", endpoint: "getMapMarkers", label: "marker", schema: MapMarkersLayerDataSchema, cap: MAP_MARKERS_MAX_ITEMS,
    diff: () => (m, f, st) => {
      const d: string[] = [];
      if (m.kind !== f.kind) d.push(`${m.id} kind mod=${m.kind} frm=${f.kind}`);
      if (m.icon !== f.icon) d.push(`${m.id} icon mod=${m.icon} frm=${f.icon}`);
      if (m.color !== f.color) d.push(`${m.id} color mod=${m.color} frm=${f.color} (NativeMarkerColorSRGB decides this: Task 4)`);
      if (m.name !== f.nameHash) d.push(`${m.id} nameHash mod=${m.name} frm=${f.nameHash}`);
      for (const r of [markerAxis(m.id, "x", m.x, f.xM, st), markerAxis(m.id, "y", m.y, f.yM, st)]) if (r !== null) d.push(r);
      return d;
    },
  });
  if (reasons.length) return failing(reasons);
  const pairs = ok.flatMap((a) => a.cmp.pairs);
  const modAll = ok.flatMap((a) => a.mod);
  const bothEmpty = ok.every((a) => a.mod.length === 0 && a.frm.length === 0);
  const rounding = Math.max(0, ...ok.map((a) => a.cmp.stats.rounding ?? 0));
  const never = bothEmpty
    ? ["any marker (both sides empty)"]
    : [
        modAll.some((m) => m.kind === "stamp") ? null : "a stamp",
        modAll.some((m) => m.kind === "marker") ? null : "a marker",
        modAll.some((m) => m.color !== "#ffffff") ? null : "a non-white colour",
        // Names are hashed in the evidence, so the trim or cut cannot be seen from here: always listed.
        "a name the dash rule changes (trim or cut)",
        pairs.some(([, f]) => isBoundary(f.xM) || isBoundary(f.yM)) ? null : "a marker at a .5 m boundary",
      ].filter((x): x is string => x !== null);
  const out = [
    `${ok.length} posted layer(s) valid against the contract; marker ids equal FRM's; ${pairs.length} markers compared; kind, icon, color and name hash equal; x/y equal Math.round(FRM)`,
  ];
  if (rounding > 0) out.push(`${rounding} one-metre rounding differences at .5 m`);
  out.push(...uniq(ok.flatMap((a) => a.cmp.notes)));
  return { pass: true, reasons: out, never };
}

// ---------------------------------------------------------------------------------------------------------------------- drones

/**
 * droneRoutes (plan rule R-d, provisional): valid against the contract; id set equal; name hash, paired hash and the input,
 * output and fuel sets exactly equal; x/y each within 1 m. pairedId (controller rule): when FRM's paired name matches exactly one
 * port the mod's pairedId must be that id (null fails when that port is in the layer); otherwise it is reported, not judged.
 * A pairedId outside the layer fails.
 */
export function judgeDrone(e: DroneEvidence): S4Result {
  const { reasons, ok } = runPosts<ModPort, FrmDronePort>(e, {
    layer: "droneRoutes", endpoint: "getDroneStation", label: "port", schema: DroneRoutesLayerDataSchema, cap: DRONE_ROUTES_MAX_ITEMS,
    // Controller (V9): input and output equal only when swapped, on every port where they differ, means the getters are swapped.
    hint: (st) => ((st.ioDiff ?? 0) > 0 && st.ioDiff === st.ioSwapped ? ["input/output look swapped (V9: swap the getters in Task 9)"] : []),
    diff: (mod) => {
      const modIds = new Set(mod.map((p) => p.id));
      return (m, f, st) => {
        const d: string[] = [];
        if (m.name !== f.nameHash) d.push(`${m.id} nameHash mod=${m.name} frm=${f.nameHash}`);
        if (m.paired !== f.pairedHash) d.push(`${m.id} paired mod=${show(m.paired)} frm=${show(f.pairedHash)}`);
        for (const k of ["input", "output", "fuel"] as const) if (!same(m[k], f[k])) d.push(`${m.id} ${k} mod=${show(m[k])} frm=${show(f[k])}`);
        if (!same(m.input, f.input) || !same(m.output, f.output)) {
          st.ioDiff = (st.ioDiff ?? 0) + 1;
          if (same(m.input, f.output) && same(m.output, f.input)) st.ioSwapped = (st.ioSwapped ?? 0) + 1;
        }
        for (const [k, mv, fv] of [["x", m.x, f.xM], ["y", m.y, f.yM]] as const) {
          if (!(Math.abs(mv - fv) <= S4_XY_MAX_M + EPS)) d.push(`${m.id} ${k} mod=${mv} frm=${fv}`);
        }
        const only = f.pairedIds.length === 1 ? f.pairedIds[0]! : null;
        if (typeof m.pairedId === "string" && !modIds.has(m.pairedId)) d.push(`${m.id} pairedId names an id that is not in the layer`);
        else if (only !== null && m.pairedId !== undefined) {
          st.pairedJudged = (st.pairedJudged ?? 0) + 1;
          if (m.pairedId === null && modIds.has(only)) d.push(`${m.id} pairedId mod=null frm=${only} (FRM resolves exactly one port in the layer)`);
          else if (typeof m.pairedId === "string" && m.pairedId !== only) d.push(`${m.id} pairedId mod=${m.pairedId} frm=${only}`);
        } else st.pairedReported = (st.pairedReported ?? 0) + 1;
        return d;
      };
    },
  });
  if (reasons.length) return failing(reasons);
  return finishDrone(ok);
}

function finishDrone(ok: Array<Accepted<ModPort, FrmDronePort>>): S4Result {
  const pairs = ok.flatMap((a) => a.cmp.pairs);
  const frmAll = pairs.map(([, f]) => f);
  const bothEmpty = ok.every((a) => a.mod.length === 0 && a.frm.length === 0);
  const never = bothEmpty
    ? ["any drone port (both sides empty)"]
    : [
        frmAll.some((f) => f.pairedHash !== null) ? null : "a paired port",
        frmAll.some((f) => f.pairedHash === null) ? null : "an unpaired port",
        frmAll.some((f) => f.input.length > 0) ? null : "cargo in an input inventory",
        frmAll.some((f) => f.output.length > 0) ? null : "cargo in an output inventory",
        frmAll.some((f) => f.fuel.length > 0) ? null : "fuel",
        frmAll.some((f) => f.pairedIds.length >= 2) ? null : "two ports sharing a name (pairing ambiguous)",
      ].filter((x): x is string => x !== null);
  const last = ok.at(-1)!.cmp.stats;
  const out = [
    `${ok.length} posted layer(s) valid against the contract; port ids equal FRM's; ${pairs.length} ports compared; name hash, paired, input, output and fuel equal; x/y within ${S4_XY_MAX_M} m`,
    `${last.pairedJudged ?? 0} ports: pairedId judged against the one port FRM's paired name matches`,
    `${last.pairedReported ?? 0} ports: pairedId reported, not judged (ambiguous or unpaired in FRM)`,
  ];
  out.push(...uniq(ok.flatMap((a) => a.cmp.notes)));
  return { pass: true, reasons: out, never };
}

// ----------------------------------------------------------------------------------------------------------------------- wires

const endDist = (a: [number, number], b: [number, number]) => Math.hypot(a[0] - b[0], a[1] - b[1]);

/**
 * powerLines (plan rule R-d, provisional): valid against the contract; id set equal (a FRM-only id fails, and the log's dropped
 * count, when given, is quoted so a null-end wire is visible); each end within 1 m of FRM's, in index order or both swapped
 * (swapped is allowed, counted and reported).
 */
export function judgeWires(e: WiresEvidence, log: { dropped: number } | null = null): S4Result {
  const { reasons, ok } = runPosts<ModWire, FrmWire>(e, {
    layer: "powerLines", endpoint: "getCables", label: "wire", schema: PowerLinesLayerDataSchema, cap: POWER_LINES_MAX_ITEMS,
    diff: () => (m, f, st) => {
      const [a, b] = m.points;
      const [f0, f1] = f.ends;
      const straight = [endDist(a, f0), endDist(b, f1)];
      const swapped = [endDist(a, f1), endDist(b, f0)];
      const within = (d: number[]) => d.every((x) => x <= S4_XY_MAX_M + EPS);
      if (within(straight)) return [];
      if (within(swapped)) {
        st.swapped = (st.swapped ?? 0) + 1;
        return [];
      }
      const sum = (d: number[]) => d[0]! + d[1]!;
      return [`${m.id} ends are over ${S4_XY_MAX_M} m from FRM's in either order (${fmt2(Math.max(sum(straight), sum(swapped)))} m total)`];
    },
  });
  const droppedNote = log !== null && log.dropped > 0 ? [`the mod's log counted ${log.dropped} dropped wire(s) (an end that was null)`] : [];
  if (reasons.length) return failing([...reasons, ...droppedNote]);
  const pairs = ok.flatMap((a) => a.cmp.pairs);
  const bothEmpty = ok.every((a) => a.mod.length === 0 && a.frm.length === 0);
  const swapped = Math.max(0, ...ok.map((a) => a.cmp.stats.swapped ?? 0));
  const never = bothEmpty
    ? ["any wire (both sides empty)"]
    : [swapped > 0 ? null : "a wire with swapped ends"].filter((x): x is string => x !== null);
  const out = [
    `${ok.length} posted layer(s) valid against the contract; wire ids equal FRM's; ${pairs.length} wires compared; each end within ${S4_XY_MAX_M} m, in index order or swapped`,
  ];
  if (swapped > 0) out.push(`${swapped} wires with swapped ends`);
  out.push(...droppedNote, ...uniq(ok.flatMap((a) => a.cmp.notes)));
  return { pass: true, reasons: out, never };
}
