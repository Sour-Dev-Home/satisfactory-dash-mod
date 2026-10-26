// FRM's HTTP output as the S2 native-parity run's oracle (ADR-0058 Decision 3, sprint S2): getFactory, getTrains and
// getTrainStation as the harness reads them from the test server, mapped to the contract's forms by the rules the bridge's own
// mappers pin (FrmFactory.cpp, FrmMapLive.cpp: our code, checked against the mod's fixtures), plus the compact form the
// evidence keeps. No FRM source is read: the shapes are the bridge's fixtures and the dash's captured responses (clean room,
// docs/clean-room.md). Player-chosen names (stations, trains, timetable stops) leave this file only as sessionHash values.
import { z } from "zod";
import { sessionHash } from "./status";

const LIMIT_METRES = 1_000_000;
const MAX_STRING = 200;
const MAX_BUILDINGS = 20_000;
const MAX_RATES = 16;
const MAX_ITEM_CLASSES = 64;
const MAX_PLATFORMS = 64;
const MAX_STOPS = 200;
const MAX_ROUTE_RECORDS = 4000;
const cut = (s: string, n = MAX_STRING) => (s.length > n ? s.slice(0, n) : s);

/** FrmMap.cpp JsRound(x / 100): JavaScript's Math.round of a true division, -0 written 0. */
const wholeMetres = (units: number): number => {
  const v = units / 100;
  const floor = Math.floor(v);
  const m = v - floor >= 0.5 ? floor + 1 : floor;
  return m === 0 ? 0 : m;
};
/** Centimetres to metres as a true division (FrmFactoryMetres), -0 written 0 as JSON.stringify does. */
const metres = (units: number): number => {
  const v = units / 100;
  return v === 0 ? 0 : v;
};

// ---------------------------------------------------------------------------------------------------------------- getFactory

/** One production or ingredient row, as the contract has it (factory.ts ProductionRateSchema without `unit`). */
export interface CanonRate { name: string; className: string; currentPerMinute: number; maxPerMinute: number; percent: number }
/** One machine as the mod's `factory` part has it (agent.ts AgentFactoryBuildingSchema). */
export interface CanonMachine {
  id: string; name: string; className: string; recipe: string | null;
  isProducing: boolean; isPaused: boolean; isBackedUp: boolean; inputsFull?: boolean;
  circuitGroupId: number;
  location?: { xM: number; yM: number; zM: number; rotationDeg: number };
  clockSpeedPercent?: number;
  production: CanonRate[];
  ingredients?: CanonRate[];
}

const num = z.number().finite();
const RateRaw = (cur: string, max: string, pct: string) =>
  z.object({ Name: z.string(), ClassName: z.string(), [cur]: num, [max]: num, [pct]: num }).passthrough();
const SlotRaw = z.object({ ClassName: z.string(), Amount: num, MaxAmount: num }).passthrough();
const FrmBuildingSchema = z
  .object({
    ID: z.string().min(1),
    Name: z.string(),
    ClassName: z.string(),
    Recipe: z.string().min(1).optional(),
    IsConfigured: z.boolean().optional(),
    IsProducing: z.boolean(),
    IsPaused: z.boolean(),
    production: z.array(RateRaw("CurrentProd", "MaxProd", "ProdPercent")).optional(),
    ingredients: z.array(RateRaw("CurrentConsumed", "MaxConsumed", "ConsPercent")).optional(),
    InputInventory: z.array(SlotRaw).optional(),
    OutputInventory: z.array(SlotRaw).optional(),
    PowerInfo: z.object({ CircuitGroupID: z.number().int() }).passthrough().optional(),
    location: z.object({ x: num, y: num, z: num, rotation: num.optional() }).passthrough().optional(),
    ManuSpeed: z.unknown().optional(),
  })
  .passthrough();
type FrmBuilding = z.infer<typeof FrmBuildingSchema>;

/** FrmFactory.cpp FrmFactoryYaw (conform.ts normalizeYaw): into [0, 360); float noise that lands on 360 is 0. */
const yaw = (deg: number): number => {
  const y = (((deg % 360) + 360) % 360);
  return y >= 360 ? 0 : y;
};
const nonNeg = (v: number) => Math.max(0, v);

function ratesOf(list: Array<Record<string, unknown>> | undefined, cur: string, max: string, pct: string): CanonRate[] {
  return (list ?? []).slice(0, MAX_RATES).map((r) => ({
    name: cut(String(r.Name)),
    className: cut(String(r.ClassName)),
    currentPerMinute: nonNeg(r[cur] as number),
    maxPerMinute: nonNeg(r[max] as number),
    percent: nonNeg(r[pct] as number),
  }));
}

function mapBuilding(b: FrmBuilding): CanonMachine {
  // satisfactoryServerAdapter: "Unassigned" or a missing recipe, or IsConfigured false, is no recipe.
  const configured = (b.IsConfigured ?? true) && b.Recipe !== undefined && b.Recipe !== "Unassigned";
  const prodRaw = configured ? b.production : undefined;
  const ingRaw = configured ? b.ingredients : undefined;
  const ingredients = (b.ingredients ?? []).filter(() => configured);
  // factoryMapper.ts isBackedUp / areInputsFull, on the whole lists.
  const backedUp = !b.IsPaused && configured && (b.OutputInventory ?? []).some((s) => s.MaxAmount > 0 && s.Amount >= s.MaxAmount);
  const full =
    !b.IsPaused &&
    configured &&
    ingredients.length > 0 &&
    b.InputInventory !== undefined &&
    ingredients.every((i) => b.InputInventory!.some((s) => s.ClassName === i.ClassName && s.MaxAmount > 0 && s.Amount >= s.MaxAmount));
  const m: CanonMachine = {
    id: cut(b.ID),
    name: cut(b.Name),
    className: cut(b.ClassName),
    recipe: configured ? cut(b.Recipe!) : null,
    isProducing: b.IsProducing,
    isPaused: b.IsPaused,
    isBackedUp: backedUp,
    circuitGroupId: b.PowerInfo?.CircuitGroupID ?? -1,
    production: ratesOf(prodRaw as never, "CurrentProd", "MaxProd", "ProdPercent"),
    ingredients: ratesOf(ingRaw as never, "CurrentConsumed", "MaxConsumed", "ConsPercent"),
  };
  if (b.InputInventory !== undefined) m.inputsFull = full;
  if (b.location) m.location = { xM: metres(b.location.x), yM: metres(b.location.y), zM: metres(b.location.z), rotationDeg: yaw(b.location.rotation ?? 0) };
  if (typeof b.ManuSpeed === "number" && Number.isFinite(b.ManuSpeed) && b.ManuSpeed > 0) m.clockSpeedPercent = b.ManuSpeed;
  return m;
}

/** getFactory's body to the contract's machines, in FRM's order (the rules of FrmFactory.cpp). Throws on an invalid record. */
export function mapFrmFactory(body: unknown): CanonMachine[] {
  return z.array(FrmBuildingSchema).parse(body).slice(0, MAX_BUILDINGS).map(mapBuilding);
}

/** How many machines in a getFactory body carry a Somersloop (the world's evidence on whether that case is exercised). */
export function countSomersloops(body: unknown): number {
  return z.array(z.object({ Somersloops: z.number().optional() }).passthrough()).parse(body).filter((b) => (b.Somersloops ?? 0) > 0).length;
}

// --------------------------------------------------------------------------------------------- getTrains / getTrainStation

export interface LiveTrain { id: string; name: string; x: number; y: number; status: string }
export interface LiveStation { id: string; name: string; x: number; y: number }

const Located = z.object({ x: num, y: num }).passthrough();
const FrmTrainSchema = z.object({ ID: z.string().min(1), Name: z.string(), Status: z.string(), location: Located }).passthrough();
const FrmStationSchema = z.object({ ID: z.string().min(1), Name: z.string(), location: Located }).passthrough();
const inBounds = (...v: number[]) => v.every((m) => Number.isFinite(m) && Math.abs(m) <= LIMIT_METRES);

/** getTrains' body to the mapLive trains (FrmMapLive.cpp): whole metres, a record out of bounds dropped. Names are raw here. */
export function mapFrmTrains(body: unknown): LiveTrain[] {
  const out: LiveTrain[] = [];
  for (const t of z.array(FrmTrainSchema).parse(body)) {
    const x = wholeMetres(t.location.x);
    const y = wholeMetres(t.location.y);
    if (inBounds(x, y)) out.push({ id: cut(t.ID), name: cut(t.Name), x, y, status: cut(t.Status) });
  }
  return out;
}

/** getTrainStation's body to the mapLive stations. Names are raw here. */
export function mapFrmStations(body: unknown): LiveStation[] {
  const out: LiveStation[] = [];
  for (const s of z.array(FrmStationSchema).parse(body)) {
    const x = wholeMetres(s.location.x);
    const y = wholeMetres(s.location.y);
    if (inBounds(x, y)) out.push({ id: cut(s.ID), name: cut(s.Name), x, y });
  }
  return out;
}

const hashed = <T extends { name: string }>(list: T[]): T[] => list.map((x) => ({ ...x, name: sessionHash(x.name) }));
/** The forms the sampler stores: names are hashed before anything is kept. */
export const readFrmTrains = (body: unknown): LiveTrain[] => hashed(mapFrmTrains(body));
export const readFrmStations = (body: unknown): LiveStation[] => hashed(mapFrmStations(body));

// ----------------------------------------------------------------------------------------------------------------- trainRoutes

export interface RoutePlatform { id: string; mode: string; items: string[] }
export interface RouteStation { recordType: "station"; id: string; name: string; platforms: RoutePlatform[] }
export interface RouteTrain { recordType: "train"; id: string; name: string; stops: string[]; items: string[] }
export type RouteRecord = RouteStation | RouteTrain;

const InvItem = z.object({ ClassName: z.string().optional() }).passthrough();
const FrmPlatformSchema = z.object({ ID: z.string(), LoadingMode: z.string().optional(), Inventory: z.array(InvItem).optional() }).passthrough();
const FrmRouteStationSchema = z.object({ ID: z.string(), Name: z.string(), CargoInventory: z.array(FrmPlatformSchema).optional() }).passthrough();
const FrmRouteTrainSchema = z
  .object({
    ID: z.string(),
    Name: z.string(),
    TimeTable: z.array(z.object({ StationName: z.string() }).passthrough()).optional(),
    // The dash's captures say Vehicles; the bridge's older fixture says Cars: both are read.
    Vehicles: z.array(z.object({ Inventory: z.array(InvItem).optional() }).passthrough()).optional(),
    Cars: z.array(z.object({ Inventory: z.array(InvItem).optional() }).passthrough()).optional(),
  })
  .passthrough();

const byText = (a: string, b: string) => (a < b ? -1 : a > b ? 1 : 0);
/** Clamped, empty ones left out, sorted, distinct, at most 64 (NativeRoutesItemClasses): the form both sides are compared in. */
function itemClasses(list: Array<{ ClassName?: string | undefined }>): string[] {
  const names = list.map((i) => cut(i.ClassName ?? "")).filter((s) => s.length > 0).sort(byText);
  return names.filter((s, i) => i === 0 || s !== names[i - 1]).slice(0, MAX_ITEM_CLASSES);
}

/**
 * The trainRoutes layer FRM's two endpoints give, in the contract's form (map.ts TrainRoutesLayerDataSchema; the bridge never
 * sent this layer, so nothing here is a bridge rule): a station's platforms are its CargoInventory entries ("Loading" is load,
 * anything else unload; items are the classes of the platform's Inventory), a train's stops are its TimeTable station names in
 * order, its items the classes in its vehicles' inventories. Stations first by id, then trains by id. Names are raw here.
 */
export function mapFrmTrainRoutes(stationBody: unknown, trainsBody: unknown): RouteRecord[] {
  const stations: RouteStation[] = [];
  for (const s of z.array(FrmRouteStationSchema).parse(stationBody)) {
    const id = cut(s.ID);
    if (!id) continue;
    const platforms: RoutePlatform[] = [];
    for (const p of s.CargoInventory ?? []) {
      const pid = cut(p.ID);
      if (pid) platforms.push({ id: pid, mode: p.LoadingMode === "Loading" ? "load" : "unload", items: itemClasses(p.Inventory ?? []) });
    }
    platforms.sort((a, b) => byText(a.id, b.id));
    stations.push({ recordType: "station", id, name: cut(s.Name), platforms: platforms.slice(0, MAX_PLATFORMS) });
  }
  const trains: RouteTrain[] = [];
  for (const t of z.array(FrmRouteTrainSchema).parse(trainsBody)) {
    const id = cut(t.ID);
    if (!id) continue;
    const cars = t.Vehicles ?? t.Cars ?? [];
    trains.push({
      recordType: "train", id, name: cut(t.Name),
      stops: (t.TimeTable ?? []).slice(0, MAX_STOPS).map((x) => cut(x.StationName)),
      items: itemClasses(cars.flatMap((c) => c.Inventory ?? [])),
    });
  }
  stations.sort((a, b) => byText(a.id, b.id));
  trains.sort((a, b) => byText(a.id, b.id));
  return [...stations, ...trains].slice(0, MAX_ROUTE_RECORDS);
}

/** Station, train and stop names replaced by their hash (player text never enters evidence). */
export function hashRoutes(records: RouteRecord[]): RouteRecord[] {
  return records.map((r) =>
    r.recordType === "station" ? { ...r, name: sessionHash(r.name) } : { ...r, name: sessionHash(r.name), stops: r.stops.map(sessionHash) },
  );
}

/** One string per record, sorted: two layers are equal when these lists are. Platforms and items are put in a fixed order first. */
export function canonRoutes(records: RouteRecord[]): string[] {
  return records
    .map((r) =>
      r.recordType === "station"
        ? JSON.stringify(["station", r.id, r.name, [...r.platforms].sort((a, b) => byText(a.id, b.id)).map((p) => [p.id, p.mode, [...p.items].sort(byText)])])
        : JSON.stringify(["train", r.id, r.name, r.stops, [...r.items].sort(byText)]),
    )
    .sort(byText);
}

/** What the routes sampler keeps of one pair of reads: the canonical list (hashed names) and its short hash. */
export function readFrmRoutes(stationBody: unknown, trainsBody: unknown): { hash: string; canon: string[] } {
  const canon = canonRoutes(hashRoutes(mapFrmTrainRoutes(stationBody, trainsBody)));
  return { hash: sessionHash(canon.join("\n")), canon };
}

// ------------------------------------------------------------------------------------------- the factory's compact evidence

/**
 * The part of a machine that does not move between reads, interned once per distinct value (the capture's `statics`). `items`
 * are the rate rows in order: [0 production | 1 ingredient, className, maxPerMinute]. Values here are also what the judge compares
 * for static fields and for maxPerMinute.
 */
export interface FactoryStatic {
  id: string; name: string; className: string; recipe: string | null; group: number;
  loc: [number, number, number] | null; clock: number | null;
  items: Array<[0 | 1, string, number]>;
}
/** [static index, flags, current0, percent0, current1, percent1, ...]: the dynamic part of one machine in one read. */
export type FactoryRow = number[];
/** Flags: 1 producing, 2 paused, 4 backed up, bits 3-4 inputsFull (0 absent, 1 false, 2 true). */
export const FLAG_PRODUCING = 1;
export const FLAG_PAUSED = 2;
export const FLAG_BACKED_UP = 4;
export const inputsFullOf = (flags: number): boolean | undefined => ((flags >> 3) & 3) === 0 ? undefined : ((flags >> 3) & 3) === 2;
/** The rates are kept to four decimals in the rows (evidence size); every tolerance the judge applies is far wider. */
const r4 = (v: number) => Math.round(v * 10_000) / 10_000;

export const staticOf = (m: CanonMachine): FactoryStatic => ({
  id: m.id, name: m.name, className: m.className, recipe: m.recipe, group: m.circuitGroupId,
  loc: m.location ? [m.location.xM, m.location.yM, m.location.zM] : null,
  clock: m.clockSpeedPercent ?? null,
  items: [...m.production.map((r) => [0, r.className, r4(r.maxPerMinute)] as [0, string, number]), ...(m.ingredients ?? []).map((r) => [1, r.className, r4(r.maxPerMinute)] as [1, string, number])],
});

export const flagsOf = (m: CanonMachine): number =>
  (m.isProducing ? FLAG_PRODUCING : 0) | (m.isPaused ? FLAG_PAUSED : 0) | (m.isBackedUp ? FLAG_BACKED_UP : 0) | ((m.inputsFull === undefined ? 0 : m.inputsFull ? 2 : 1) << 3);

/** The distinct static lists, once each; a row names its static by index. */
export class FactoryTable {
  readonly statics: FactoryStatic[] = [];
  private readonly index = new Map<string, number>();
  intern(s: FactoryStatic): number {
    const key = JSON.stringify(s);
    let i = this.index.get(key);
    if (i === undefined) {
      i = this.statics.push(s) - 1;
      this.index.set(key, i);
    }
    return i;
  }
  row(m: CanonMachine): FactoryRow {
    const out: number[] = [this.intern(staticOf(m)), flagsOf(m)];
    for (const r of [...m.production, ...(m.ingredients ?? [])]) out.push(r4(r.currentPerMinute), r4(r.percent));
    return out;
  }
}

export interface FactorySampleRows { rows: FactoryRow[]; gone: number[]; full: boolean }

/** Turns each FRM read into the rows that changed since the last one (the first is whole); `gone` lists machines no longer present. */
export function createFactoryEncoder(table: FactoryTable): { push(machines: CanonMachine[]): FactorySampleRows } {
  const last = new Map<string, { key: string; sidx: number }>();
  let first = true;
  return {
    push(machines) {
      const rows: FactoryRow[] = [];
      const seen = new Set<string>();
      for (const m of machines) {
        const row = table.row(m);
        const key = row.join(",");
        seen.add(m.id);
        if (last.get(m.id)?.key !== key) rows.push(row);
        last.set(m.id, { key, sidx: row[0]! });
      }
      const gone: number[] = [];
      for (const [id, v] of last) if (!seen.has(id)) (gone.push(v.sidx), last.delete(id));
      const out = { rows, gone, full: first };
      first = false;
      return out;
    },
  };
}
