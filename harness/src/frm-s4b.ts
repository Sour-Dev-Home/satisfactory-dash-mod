// FRM's HTTP output as the S4-B native-parity oracles (ADR-0058, sprint S4 part B): getExtractor, getGenerators, getTrainStation
// and getStorageInv mapped to plain records for the Task 18 judges. No FRM source is read: the shapes are our captures of its
// output (docs-vault/raw-sources/frm-s4-captures-2026-10-27, clean room, docs/clean-room.md).
//
// Numbers are copied exactly as FRM wrote them: no unit conversion, no rounding. Conversion and tolerance belong to the judges.
// What each number appears to be is noted per field below and is "observed, not specified".
//
// Player text: every Name goes through evidenceHash (the per-run keyed hash S4-A's oracles use) and no raw name is returned.
// Ids are the game's own object ids (class name plus number), as in frm-s4. A record that is not an object, or has no usable
// id, is dropped, as in frm-s4 (S4-A keeps no drop counter).

import { evidenceHash } from "./evidence-hash";
import { MAX_STRING, byCodeUnit } from "./frm-s4";

const isObj = (v: unknown): v is Record<string, unknown> => typeof v === "object" && v !== null && !Array.isArray(v);
const isNum = (v: unknown): v is number => typeof v === "number" && Number.isFinite(v);
const arrayBody = (body: unknown, what: string): unknown[] => {
  if (!Array.isArray(body)) throw new Error(`${what}: the body is not an array`);
  return body;
};
const goodString = (v: unknown): v is string => typeof v === "string" && v.length > 0 && v.length <= MAX_STRING;
const idOf = (r: Record<string, unknown>): string | undefined => (goodString(r.ID) ? r.ID : undefined);
/** PowerInfo.CircuitID: the captures show -1 on an unconnected building (observed, not specified); that and a missing value give undefined. */
const circuitOf = (r: Record<string, unknown>): number | undefined => {
  const c = isObj(r.PowerInfo) ? r.PowerInfo.CircuitID : undefined;
  return isNum(c) && Number.isInteger(c) && c >= 0 ? c : undefined;
};
const byId = <T extends { id: string }>(list: T[]): T[] => list.sort((a, b) => byCodeUnit(a.id, b.id));

// ------------------------------------------------------------------------------------------------------------------ extractors

export interface FrmExtractor {
  id: string; className: string;
  /** IsProducing and IsPaused as FRM wrote them. */
  producing: boolean; paused: boolean;
  /** Derived: FRM has no such flag (see mapFrmExtractors). */
  backedUp: boolean;
  /** production[0].ClassName. */
  resourceClass: string;
  /** production[0].CurrentProd: observed per minute, 18.8 on a Mk.1 miner at 15.7 percent, 150 on an oil pump. Not specified. */
  current: number;
  /** production[0].MaxProd: observed per minute (120 on a Mk.1 miner, 240 on a Mk.2). Not specified. */
  max: number;
  /** production[0].ProdPercent: observed 0..100 (CurrentProd / MaxProd). Not specified. */
  percent: number;
  /** ManuSpeed: observed 100 for a default clock, 250 with three power shards. Not specified. */
  clockPercent: number;
  circuit?: number;
}

/** getExtractor. FRM's output has no backed-up flag (observed keys: IsConfigured, IsPaused, IsProducing, production[]); backedUp is
 *  reported as `IsPaused === false && IsProducing === false`, the closest reading of an extractor that is switched on but not
 *  running (observed, not specified). A record without a production entry with numeric figures is dropped. */
export function mapFrmExtractors(body: unknown): FrmExtractor[] {
  const out: FrmExtractor[] = [];
  for (const r of arrayBody(body, "getExtractor")) {
    if (!isObj(r)) continue;
    const id = idOf(r);
    const p = Array.isArray(r.production) ? r.production[0] : undefined;
    if (id === undefined || !goodString(r.ClassName) || !isObj(p) || !goodString(p.ClassName)) continue;
    if (!isNum(p.CurrentProd) || !isNum(p.MaxProd) || !isNum(p.ProdPercent) || !isNum(r.ManuSpeed)) continue;
    if (typeof r.IsProducing !== "boolean" || typeof r.IsPaused !== "boolean") continue;
    const e: FrmExtractor = {
      id, className: r.ClassName, producing: r.IsProducing, paused: r.IsPaused, backedUp: !r.IsPaused && !r.IsProducing,
      resourceClass: p.ClassName, current: p.CurrentProd, max: p.MaxProd, percent: p.ProdPercent, clockPercent: r.ManuSpeed,
    };
    const circuit = circuitOf(r);
    if (circuit !== undefined) e.circuit = circuit;
    out.push(e);
  }
  return byId(out);
}

// ------------------------------------------------------------------------------------------------------------------ generators

export interface FrmGenerator {
  id: string; className: string; nameHash: string;
  /** RegulatedDemandProd: observed megawatts (75 on a running coal generator, 0 on an idle burner). Not specified. */
  currentMW: number;
  /** ProductionCapacity: observed megawatts (75 coal, 250 fuel, 30 automated biomass). Not specified. */
  capacityMW: number;
  /** CurrentPotential: observed 100 at default clock. Not specified. */
  clockPercent: number;
  /** currentMW > 0 (FRM has no generator running flag in the captures). */
  producing: boolean;
  /** The first FuelInventory entry that is not the Supplement class, and its Amount (items; FuelInventory.Amount, observed). */
  fuelClass: string | null; fuelAmount: number | null;
  /** The FuelInventory entry whose class is Supplement.ClassName (water on a coal generator), and its Amount. */
  supplementClass: string | null; supplementAmount: number | null;
  circuit?: number;
}

export function mapFrmGenerators(body: unknown): FrmGenerator[] {
  const out: FrmGenerator[] = [];
  for (const r of arrayBody(body, "getGenerators")) {
    if (!isObj(r)) continue;
    const id = idOf(r);
    if (id === undefined || !goodString(r.ClassName) || typeof r.Name !== "string") continue;
    if (!isNum(r.RegulatedDemandProd) || !isNum(r.ProductionCapacity) || !isNum(r.CurrentPotential)) continue;
    const supplementClass = isObj(r.Supplement) && goodString(r.Supplement.ClassName) ? r.Supplement.ClassName : null;
    let fuelClass: string | null = null;
    let fuelAmount: number | null = null;
    let supplementAmount: number | null = null;
    if (Array.isArray(r.FuelInventory)) {
      for (const e of r.FuelInventory) {
        if (!isObj(e) || !goodString(e.ClassName) || !isNum(e.Amount)) continue;
        if (e.ClassName === supplementClass) supplementAmount ??= e.Amount;
        else if (fuelClass === null) { fuelClass = e.ClassName; fuelAmount = e.Amount; }
      }
    }
    const g: FrmGenerator = {
      id, className: r.ClassName, nameHash: evidenceHash(r.Name),
      currentMW: r.RegulatedDemandProd, capacityMW: r.ProductionCapacity, clockPercent: r.CurrentPotential,
      producing: r.RegulatedDemandProd > 0,
      fuelClass, fuelAmount,
      supplementClass: supplementAmount === null ? null : supplementClass, supplementAmount,
    };
    const circuit = circuitOf(r);
    if (circuit !== undefined) g.circuit = circuit;
    out.push(g);
  }
  return byId(out);
}

// --------------------------------------------------------------------------------------------------------------- station rates

export interface FrmStationRate {
  id: string; nameHash: string;
  /** The station's TransferRate: observed 2.06 on a station whose two platforms read 1.50 and 0.56 (items per minute is not
   *  specified; the unit is not stated). Copied as given. */
  transfer: number;
  /** The sum of the platforms' TransferRate (CargoInventory entries), copied as given and added in order. */
  platformSum: number;
  platformCount: number;
}

export function mapFrmStationRates(body: unknown): FrmStationRate[] {
  const out: FrmStationRate[] = [];
  for (const r of arrayBody(body, "getTrainStation")) {
    if (!isObj(r)) continue;
    const id = idOf(r);
    if (id === undefined || typeof r.Name !== "string" || !isNum(r.TransferRate)) continue;
    let platformSum = 0;
    let platformCount = 0;
    if (Array.isArray(r.CargoInventory)) {
      for (const p of r.CargoInventory) {
        if (!isObj(p) || !isNum(p.TransferRate)) continue;
        platformSum += p.TransferRate;
        platformCount++;
      }
    }
    out.push({ id, nameHash: evidenceHash(r.Name), transfer: r.TransferRate, platformSum, platformCount });
  }
  return byId(out);
}

// ------------------------------------------------------------------------------------------------------------------------ storage

export interface FrmContainerItem { class: string; amount: number; stackMax: number | null }
export interface FrmContainer {
  id: string; className: string; nameHash: string;
  /** Slots and SlotsUsed as FRM wrote them; a missing or non-numeric value is null. */
  slots: number | null; slotsUsed: number | null;
  /** FluidCapacity, for a fluid buffer. The four captures hold no fluid buffer, so this key is NOT observed: it is read when
   *  present and numeric, else null. Unverified until a fluid buffer is captured. */
  fluidCapacity: number | null;
  /** Inventory entries: ClassName, Amount, MaxAmount (the stack size, observed; null when absent). */
  items: FrmContainerItem[];
}

export function mapFrmStorage(body: unknown): FrmContainer[] {
  const out: FrmContainer[] = [];
  for (const r of arrayBody(body, "getStorageInv")) {
    if (!isObj(r)) continue;
    const id = idOf(r);
    if (id === undefined || !goodString(r.ClassName) || typeof r.Name !== "string") continue;
    const items: FrmContainerItem[] = [];
    if (Array.isArray(r.Inventory)) {
      for (const e of r.Inventory) {
        if (!isObj(e) || !goodString(e.ClassName) || !isNum(e.Amount)) continue;
        items.push({ class: e.ClassName, amount: e.Amount, stackMax: isNum(e.MaxAmount) ? e.MaxAmount : null });
      }
    }
    items.sort((a, b) => byCodeUnit(a.class, b.class));
    out.push({
      id, className: r.ClassName, nameHash: evidenceHash(r.Name),
      slots: isNum(r.Slots) ? r.Slots : null, slotsUsed: isNum(r.SlotsUsed) ? r.SlotsUsed : null,
      fluidCapacity: isNum(r.FluidCapacity) ? r.FluidCapacity : null, items,
    });
  }
  return byId(out);
}
