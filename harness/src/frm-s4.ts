// FRM's HTTP output as the S4-A native-parity oracles (ADR-0058, sprint S4 part A): getMapMarkers, getDroneStation and getCables
// mapped to the contract's mapMarkers, droneRoutes and powerLines forms by the rules the dash adapter's own mappers pin
// (mapMarkersMapper.ts, droneRoutesMapper.ts, powerLinesMapper.ts: our code). No FRM source is read: the shapes are the
// dash's captured responses (clean room, docs/clean-room.md).
//
// Names are player text. Every name becomes sessionHash(name) here and no raw name is returned or put in an id; the pairing of
// drone ports is resolved inside mapFrmDroneFull and only ids and hashes leave it.
//
// Where the native readers deliberately differ, these oracles follow the dash mapper and the parity judge reports the
// difference (it is not an oracle bug):
//  - repeated ids: the mapper keeps every item, the native readers keep the first;
//  - empty ids: markers and wires get a derived id here, the native readers drop the item;
//  - ids over 200 characters: markers and wires get a derived id here, the native readers clamp;
//  - items over the layer cap: the mapper cuts the id-sorted list, the native readers send the rest to `Over`.

import { createHash } from "node:crypto";
import { sessionHash } from "./status";

const LIMIT_METRES = 1_000_000;
const MAX_STRING = 200;
const MAX_ITEM_CLASSES = 64;
const MAX_ICON = 1_000_000;
const MAP_MARKER_NAME_MAX_CHARS = 60;
/** The contract's layer caps (packages/shared/src/map.ts). */
export const MAP_MARKERS_MAX_ITEMS = 1_000;
export const DRONE_ROUTES_MAX_ITEMS = 2_000;
export const POWER_LINES_MAX_ITEMS = 20_000;
const DEFAULT_MARKER_COLOR = "#ffffff";
const DERIVED_ID_PREFIX = "derived-";
const UNPAIRED = "None";

const isObj = (v: unknown): v is Record<string, unknown> => typeof v === "object" && v !== null && !Array.isArray(v);
const isNum = (v: unknown): v is number => typeof v === "number" && Number.isFinite(v);
const byCodePoint = (a: string, b: string) => (a < b ? -1 : a > b ? 1 : 0);
/** Centimetres to metres, rounded to 0.01 m, -0 written 0 (the same form as frm-s3). */
const metres2 = (cm: number): number => {
  const v = Math.round(cm) / 100;
  return v === 0 ? 0 : v;
};
/** The mapper's whole-metre bound check, applied to the cm value. */
const inBounds = (cm: number): boolean => {
  const m = Math.round(cm / 100);
  return Number.isFinite(m) && Math.abs(m) <= LIMIT_METRES;
};
const wholeMetres = (cm: number): number => {
  const v = Math.round(cm / 100);
  return v === 0 ? 0 : v;
};
const xy = (v: unknown): { x: number; y: number } | undefined => (isObj(v) && isNum(v.x) && isNum(v.y) ? { x: v.x, y: v.y } : undefined);
const arrayBody = (body: unknown, what: string): unknown[] => {
  if (!Array.isArray(body)) throw new Error(`${what}: the body is not an array`);
  return body;
};
const sha = (v: unknown): string => createHash("sha256").update(JSON.stringify(v)).digest("hex").slice(0, 32);

// -------------------------------------------------------------------------------------------------------------------- markers

export interface FrmMarker { id: string; nameHash: string; kind: "stamp" | "marker"; icon: number; color: string; xM: number; yM: number }

/** A surrogate half with no partner, anywhere in the string. */
const LONE_SURROGATE = /[\uD800-\uDBFF](?![\uDC00-\uDFFF])|(?<![\uD800-\uDBFF])[\uDC00-\uDFFF]/g;

/** The dash markerName, ported: lone surrogates to U+FFFD, trim (JavaScript whitespace), cut to 60 UTF-16 units without
 *  splitting a pair. NativeMapMarkers.Name pins the C++ to the same vectors. */
export function markerName(raw: string | undefined): string {
  const trimmed = (raw ?? "").replace(LONE_SURROGATE, "�").trim();
  if (trimmed.length <= MAP_MARKER_NAME_MAX_CHARS) return trimmed;
  const cut = trimmed.slice(0, MAP_MARKER_NAME_MAX_CHARS);
  const last = cut.charCodeAt(cut.length - 1);
  return last >= 0xd800 && last <= 0xdbff ? cut.slice(0, -1) : cut;
}

/** ColorSlot read as RRGGBBAA, alpha dropped, lower-cased; anything else is white. */
export const markerColor = (raw: unknown): string =>
  typeof raw === "string" && /^[0-9a-fA-F]{8}$/.test(raw) ? `#${raw.slice(0, 6).toLowerCase()}` : DEFAULT_MARKER_COLOR;

export function mapFrmMarkers(body: unknown): FrmMarker[] {
  const out: FrmMarker[] = [];
  for (const item of arrayBody(body, "getMapMarkers")) {
    if (!isObj(item)) continue;
    const loc = xy(item.location);
    const icon = item.IconID;
    if (!loc || !inBounds(loc.x) || !inBounds(loc.y) || !isNum(icon) || !Number.isInteger(icon) || icon < 0 || icon > MAX_ICON) continue;
    const kind = item.MapMarkerType === "RT_Stamp" ? "stamp" : "marker";
    const color = markerColor(item.ColorSlot);
    const name = markerName(typeof item.Name === "string" ? item.Name : undefined);
    const rawId = typeof item.ID === "string" ? item.ID : undefined;
    // Derived from the mapped content in whole metres, exactly as the mapper does (the name is hashed into the digest only).
    const id = rawId !== undefined && rawId.length > 0 && rawId.length <= MAX_STRING
      ? rawId
      : DERIVED_ID_PREFIX + sha([wholeMetres(loc.x), wholeMetres(loc.y), kind, icon, color, name]);
    out.push({ id, nameHash: sessionHash(name), kind, icon, color, xM: metres2(loc.x), yM: metres2(loc.y) });
  }
  out.sort((a, b) => byCodePoint(a.id, b.id) || byCodePoint(JSON.stringify(a), JSON.stringify(b)));
  return out.slice(0, MAP_MARKERS_MAX_ITEMS);
}

export const canonMarkers = (list: FrmMarker[]): string[] =>
  list.map((m) => [m.id, m.nameHash, m.kind, m.icon, m.color, m.xM, m.yM].map(String).join("|")).sort(byCodePoint);

// ---------------------------------------------------------------------------------------------------------------------- drones

export interface FrmDronePort {
  id: string; nameHash: string; pairedHash: string | null; input: string[]; output: string[]; fuel: string[]; xM: number; yM: number;
  /** Ids of the FRM ports whose Name equals PairedStation (R-d): one entry is a resolved pair, none is unresolved, two or more is ambiguous. */
  pairedIds: string[];
}

/** Distinct, sorted item classes of one inventory; classes the contract cannot hold are skipped, amounts are ignored. */
function classesOf(inv: unknown): string[] {
  const set = new Set<string>();
  if (Array.isArray(inv)) {
    for (const e of inv) {
      const c = isObj(e) ? e.ClassName : undefined;
      if (typeof c === "string" && c.length > 0 && c.length <= MAX_STRING) set.add(c);
    }
  }
  return [...set].sort(byCodePoint).slice(0, MAX_ITEM_CLASSES);
}

/** "None", empty or over-long names cannot pair; otherwise the name is kept exactly, trailing spaces included. */
const pairedName = (raw: unknown): string | null =>
  typeof raw !== "string" || raw === UNPAIRED || raw.length === 0 || raw.length > MAX_STRING ? null : raw;

export function mapFrmDroneFull(body: unknown): FrmDronePort[] {
  const kept: Array<{ id: string; name: string; paired: string | null; input: string[]; output: string[]; fuel: string[]; xM: number; yM: number }> = [];
  for (const r of arrayBody(body, "getDroneStation")) {
    if (!isObj(r) || typeof r.ID !== "string" || r.ID === "" || typeof r.Name !== "string") continue;
    const loc = xy(r.location);
    if (!loc || r.ID.length > MAX_STRING || r.Name.length > MAX_STRING || !inBounds(loc.x) || !inBounds(loc.y)) continue;
    kept.push({
      id: r.ID, name: r.Name, paired: pairedName(r.PairedStation),
      input: classesOf(r.InputInventory), output: classesOf(r.OutputInventory), fuel: classesOf(r.FuelInventory),
      xM: metres2(loc.x), yM: metres2(loc.y),
    });
  }
  const out = kept.map((p): FrmDronePort => ({
    id: p.id,
    nameHash: sessionHash(p.name),
    pairedHash: p.paired === null ? null : sessionHash(p.paired),
    input: p.input, output: p.output, fuel: p.fuel, xM: p.xM, yM: p.yM,
    pairedIds: p.paired === null ? [] : kept.filter((q) => q.name === p.paired).map((q) => q.id).sort(byCodePoint),
  }));
  // The mapper sorts by id, then by content; a repeated id is kept (the native readers keep the first).
  out.sort((a, b) => byCodePoint(a.id, b.id) || byCodePoint(JSON.stringify(a), JSON.stringify(b)));
  return out.slice(0, DRONE_ROUTES_MAX_ITEMS);
}

export const canonDrone = (list: FrmDronePort[]): string[] =>
  list
    .map((p) => [p.id, p.nameHash, p.pairedHash ?? "-", p.input.join(","), p.output.join(","), p.fuel.join(","), p.xM, p.yM, p.pairedIds.join(",")].map(String).join("|"))
    .sort(byCodePoint);

// ----------------------------------------------------------------------------------------------------------------------- wires

export interface FrmWire { id: string; ends: [[number, number], [number, number]] }

export function mapFrmWires(body: unknown): FrmWire[] {
  const out: FrmWire[] = [];
  for (const item of arrayBody(body, "getCables")) {
    if (!isObj(item)) continue;
    const a = xy(item.location0);
    const b = xy(item.location1);
    if (!a || !b || ![a.x, a.y, b.x, b.y].every(inBounds)) continue;
    const rawId = typeof item.ID === "string" ? item.ID : undefined;
    // Derived from the two ends in whole metres, exactly as the mapper does.
    const id = rawId !== undefined && rawId.length > 0 && rawId.length <= MAX_STRING
      ? rawId
      : DERIVED_ID_PREFIX + sha([[wholeMetres(a.x), wholeMetres(a.y)], [wholeMetres(b.x), wholeMetres(b.y)]]);
    out.push({ id, ends: [[metres2(a.x), metres2(a.y)], [metres2(b.x), metres2(b.y)]] });
  }
  out.sort((x, y) => byCodePoint(x.id, y.id) || byCodePoint(JSON.stringify(x.ends), JSON.stringify(y.ends)));
  return out.slice(0, POWER_LINES_MAX_ITEMS);
}

export const canonWires = (list: FrmWire[]): string[] => list.map((w) => `${w.id}|${w.ends[0].join(",")}|${w.ends[1].join(",")}`).sort(byCodePoint);
