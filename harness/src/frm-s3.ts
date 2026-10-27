// FRM's HTTP output as the S3 native-parity run's oracle (ADR-0058 Decision 3, sprint S3): getTrainRails, getBelts, getLifts,
// getPipes and getDroneStation as the harness reads them from the test server, mapped to the contract's forms by the rules the
// dash adapter's own mappers pin (conveyorsMapper.ts: our code) and the mod's fixtures. No FRM source is read: the shapes are
// the mod's recorded fixtures and the dash's captured responses (clean room, docs/clean-room.md). Nothing a player typed
// (a drone port's label, a name) leaves this file: ports are reduced to ids.

const LIMIT_METRES = 1_000_000;
const MAX_ID_LENGTH = 128;
const MAX_TIER_LENGTH = 64;

/** The shared "exact" for numbers (M4): four decimals. */
export const round4 = (n: number): number => Math.round(n * 1e4) / 1e4;
/** Centimetres to metres, rounded to 0.01 m (M3), -0 written 0. FRM's unrounded value is cm; the judge compares at this precision. */
const metres2 = (cm: number): number => {
  const v = Math.round(cm) / 100;
  return v === 0 ? 0 : v;
};
/** JavaScript's Math.round of a true division, -0 written 0 (the whole-metre form the contract has). */
const wholeMetres = (cm: number): number => {
  const v = Math.round(cm / 100);
  return v === 0 ? 0 : v;
};
const inBounds = (m: number): boolean => Number.isFinite(m) && m >= -LIMIT_METRES && m <= LIMIT_METRES;
const isObj = (v: unknown): v is Record<string, unknown> => typeof v === "object" && v !== null && !Array.isArray(v);
const isNum = (v: unknown): v is number => typeof v === "number" && Number.isFinite(v);
const byCodePoint = (a: string, b: string) => (a < b ? -1 : a > b ? 1 : 0);

// ---------------------------------------------------------------------------------------------------------------------- rails

export interface FrmRail { id: string; points: Array<[number, number]>; representable: boolean }

/**
 * getTrainRails' body to tracks: ID and SplineData x/y (centimetres) as metres rounded to 0.01 (M3). `representable` is whether the
 * contract can hold the track: at least two distinct consecutive whole-metre points (JS Math.round) inside +/-1,000,000 m.
 * A record without a string ID or a SplineData array is skipped; a non-array body throws.
 */
export function mapFrmRails(body: unknown): FrmRail[] {
  if (!Array.isArray(body)) throw new Error("getTrainRails: the body is not an array");
  const out: FrmRail[] = [];
  for (const rec of body) {
    if (!isObj(rec) || typeof rec.ID !== "string" || rec.ID === "" || !Array.isArray(rec.SplineData)) continue;
    const points: Array<[number, number]> = [];
    const whole: Array<[number, number]> = [];
    for (const p of rec.SplineData) {
      if (!isObj(p) || !isNum(p.x) || !isNum(p.y)) continue;
      points.push([metres2(p.x), metres2(p.y)]);
      const w: [number, number] = [wholeMetres(p.x), wholeMetres(p.y)];
      if (!inBounds(w[0]) || !inBounds(w[1])) continue;
      const last = whole.at(-1);
      if (last === undefined || last[0] !== w[0] || last[1] !== w[1]) whole.push(w);
    }
    out.push({ id: rec.ID, points, representable: whole.length >= 2 });
  }
  return out;
}

/** Sorted `id|x,y;x,y` lines: the form the evidence keeps and hashes. */
export const canonRails = (rails: FrmRail[]): string[] =>
  rails.map((r) => `${r.id}|${r.points.map((p) => `${p[0]},${p[1]}`).join(";")}`).sort(byCodePoint);

// ------------------------------------------------------------------------------------------------------------------ conveyors

export type FrmKind = "belt" | "lift" | "pipe";
export type NodeKind = "machine" | "splitter" | "merger" | "storage" | "station" | "extractor" | "consumer" | "junction" | "conveyor" | "other";
export interface FrmEdge {
  id: string; kind: FrmKind;
  from: string | null; fromKind: NodeKind | null; to: string | null; toKind: NodeKind | null;
  capacityPerMinute: number | null; tier: string;
  /** End 0's position in metres, rounded to 0.01, NOT whole metres (M3). */
  atX: number; atY: number;
}
export interface FrmConveyorSample { edges: FrmEdge[]; filtered: Array<{ id: string; className: string }>; dropped: number }

/** The adapter's classifyConveyorEnd (conveyorsMapper.ts), ported rule for rule: pinned to the C++ table by the Kind vectors in the test. */
export function classifyConveyorEnd(id: string): NodeKind {
  const cls = id.replace(/_\d+$/, "");
  if (/^Build_ConveyorAttachmentSplitter/.test(cls)) return "splitter";
  if (/^Build_ConveyorAttachmentMerger/.test(cls)) return "merger";
  if (/^Build_(ConveyorBelt|ConveyorLift|Pipeline_C$|PipelineMK2_C$)/i.test(cls)) return "conveyor";
  if (/^Build_Pipeline(Junction|Pump)/.test(cls)) return "junction";
  if (/^Build_(Miner|WaterPump|OilPump|Frack)/.test(cls)) return "extractor";
  if (/^Build_(Generator|ResourceSink|SpaceElevator)/.test(cls)) return "consumer";
  if (/^Build_(StorageContainer|CentralStorage|IndustrialTank|PipeStorage|FluidBuffer)/.test(cls)) return "storage";
  if (/^Build_(TrainDockingStation|TrainStation|DroneStation|TruckStation)/.test(cls)) return "station";
  if (/^Build_(Constructor|Assembler|Manufacturer|Smelter|Foundry|Packager|OilRefinery|Blender|HadronCollider|Converter|QuantumEncoder)/.test(cls)) return "machine";
  return "other";
}

const endOf = (connectedTo: string): { id: string | null; kind: NodeKind | null } =>
  connectedTo === "" ? { id: null, kind: null } : { id: connectedTo, kind: classifyConveyorEnd(connectedTo) };

/** One raw record to an edge, or undefined when the adapter would drop it (RawFrmConveyorSchema, MAX_ID_LENGTH, bounds). */
function mapEdge(rec: unknown, kind: FrmKind): FrmEdge | undefined {
  if (!isObj(rec) || typeof rec.ID !== "string" || rec.ID === "" || typeof rec.Name !== "string" || rec.Name === "") return undefined;
  const loc = rec.location0;
  if (!isObj(loc) || !isNum(loc.x) || !isNum(loc.y)) return undefined;
  if (typeof rec.ConnectedTo0 !== "string" || typeof rec.ConnectedTo1 !== "string") return undefined;
  if (rec.ID.length > MAX_ID_LENGTH || rec.ConnectedTo0.length > MAX_ID_LENGTH || rec.ConnectedTo1.length > MAX_ID_LENGTH) return undefined;
  // The adapter drops a record whose whole-metre position is out of bounds (inWholeMetreBounds).
  if (!inBounds(wholeMetres(loc.x)) || !inBounds(wholeMetres(loc.y))) return undefined;
  const raw = kind === "pipe" ? (isNum(rec.Speed) ? rec.Speed * 60 : undefined) : isNum(rec.ItemsPerMinute) ? rec.ItemsPerMinute : undefined;
  const from = endOf(rec.ConnectedTo0);
  const to = endOf(rec.ConnectedTo1);
  return {
    id: rec.ID, kind,
    from: from.id, fromKind: from.kind, to: to.id, toKind: to.kind,
    capacityPerMinute: raw === undefined || !Number.isFinite(raw) || raw < 0 ? null : round4(raw),
    tier: rec.Name.slice(0, MAX_TIER_LENGTH),
    atX: metres2(loc.x), atY: metres2(loc.y),
  };
}

/**
 * getBelts, getLifts and getPipes to the contract's edges by the adapter's rules (mapConveyors). A pipe record whose ClassName
 * matches /hyper/i is a hypertube: `filtered` (reported), not an edge. An unreadable record is counted in `dropped`.
 */
export function mapFrmConveyors(belts: unknown, lifts: unknown, pipes: unknown): FrmConveyorSample {
  const edges: FrmEdge[] = [];
  const filtered: Array<{ id: string; className: string }> = [];
  let dropped = 0;
  const groups: Array<[unknown, FrmKind]> = [[belts, "belt"], [lifts, "lift"], [pipes, "pipe"]];
  for (const [body, kind] of groups) {
    if (!Array.isArray(body)) throw new Error(`the ${kind} body is not an array`);
    for (const rec of body) {
      if (kind === "pipe" && isObj(rec) && typeof rec.ClassName === "string" && /hyper/i.test(rec.ClassName)) {
        filtered.push({ id: typeof rec.ID === "string" ? rec.ID : "", className: rec.ClassName });
        continue;
      }
      const e = mapEdge(rec, kind);
      if (e === undefined) dropped++;
      else edges.push(e);
    }
  }
  edges.sort((a, b) => byCodePoint(a.id, b.id));
  filtered.sort((a, b) => byCodePoint(a.id, b.id));
  return { edges, filtered, dropped };
}

/** Sorted lines, edges then filtered records: the form the evidence keeps and hashes. */
export const canonEdges = (s: FrmConveyorSample): string[] => [
  ...s.edges
    .map((e) => [e.id, e.kind, e.from, e.fromKind, e.to, e.toKind, e.capacityPerMinute, e.tier, `${e.atX},${e.atY}`].map((v) => String(v)).join("|"))
    .sort(byCodePoint),
  ...s.filtered.map((f) => `~${f.id}|${f.className}`).sort(byCodePoint),
  `dropped=${s.dropped}`,
];

// -------------------------------------------------------------------------------------------------------------------- drones

/** getDroneStation's body to the ID of every record: never the label or name (player-typed text leaves nothing here). */
export function mapFrmDronePorts(body: unknown): string[] {
  if (!Array.isArray(body)) throw new Error("getDroneStation: the body is not an array");
  return body.flatMap((r) => (isObj(r) && typeof r.ID === "string" && r.ID !== "" ? [r.ID] : [])).sort(byCodePoint);
}
