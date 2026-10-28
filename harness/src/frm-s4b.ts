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
/** A Name is bounded before hashing exactly as frm-s4 does: a non-string or over-long name drops the record. */
const nameOk = (v: unknown): v is string => typeof v === "string" && v.length <= MAX_STRING;
const idOf =(r: Record<string, unknown>): string | undefined => (goodString(r.ID) ? r.ID : undefined);
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
  /** FRM output has no backed-up flag; not an oracle for ours. Always null. */
  backedUp: null;
  /** Every finite numeric rate field FRM wrote, by FRM's own key (production[0] Amount, MaxAmount, CurrentProd, MaxProd,
   *  ProdPercent, and ManuSpeed). Evidence for the first parity run to show which field matches ours. */
  raw: Record<string, number>;
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

const RAW_PRODUCTION_KEYS = ["Amount", "MaxAmount", "CurrentProd", "MaxProd", "ProdPercent"];
const RAW_GENERATOR_KEYS = [
  "BaseProd", "DynamicProdCapacity", "DynamicProdDemandFactor", "RegulatedDemandProd", "ProdPowerConsumption", "CurrentPotential",
  "ProductionCapacity", "DefaultProductionCapacity", "PowerProductionPotential", "LoadPercentage", "FuelAmount", "GeoMinPower", "GeoMaxPower",
];
const pickNumbers = (from: Record<string, unknown>, keys: string[], into: Record<string, number> = {}): Record<string, number> => {
  for (const k of keys) { const v = from[k]; if (isNum(v)) into[k] = v; }
  return into;
};

/** getExtractor. FRM's output has no backed-up flag (observed keys: IsConfigured, IsPaused, IsProducing, production[]), so backedUp
 *  is null. A record without a production entry with numeric figures is dropped. */
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
      id, className: r.ClassName, producing: r.IsProducing, paused: r.IsPaused, backedUp: null,
      resourceClass: p.ClassName, current: p.CurrentProd, max: p.MaxProd, percent: p.ProdPercent, clockPercent: r.ManuSpeed,
      raw: pickNumbers(r, ["ManuSpeed"], pickNumbers(p, RAW_PRODUCTION_KEYS)),
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
  /** currentMW > 0 (FRM has no generator running flag in the captures). Observed, not specified. Note an IsProducing flag and a
   *  rate can disagree: a Mk.1 miner in the capture reads IsProducing false with CurrentProd above 0. A geothermal generator reads 0
   *  here (GeoMinPower and GeoMaxPower are in raw). */
  producing: boolean;
  /** The first FuelInventory entry that is not the Supplement class, and its Amount (items; FuelInventory.Amount, observed). */
  fuelClass: string | null; fuelAmount: number | null;
  /** The FuelInventory entry whose class is Supplement.ClassName (water on a coal generator), and its Amount. Both are null when
   *  FRM gives no Supplement class (empty, "N/A" record or `{}`) or FuelInventory has no entry of that class. */
  supplementClass: string | null; supplementAmount: number | null;
  /** Every finite numeric power field FRM wrote, by FRM's own key (BaseProd, RegulatedDemandProd, DynamicProdCapacity,
   *  ProductionCapacity, PowerProductionPotential, DefaultProductionCapacity, CurrentPotential, LoadPercentage, ...). Evidence for
   *  the first parity run to show which FRM field matches ours. */
  raw: Record<string, number>;
  circuit?: number;
}

export function mapFrmGenerators(body: unknown): FrmGenerator[] {
  const out: FrmGenerator[] = [];
  for (const r of arrayBody(body, "getGenerators")) {
    if (!isObj(r)) continue;
    const id = idOf(r);
    if (id === undefined || !goodString(r.ClassName) || !nameOk(r.Name)) continue;
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
      raw: pickNumbers(r, RAW_GENERATOR_KEYS),
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
  /** The judge compares `transfer`; `platforms` is evidence (no capture yet shows FRM's marker for a fluid platform). One entry per
   *  CargoInventory record with a numeric TransferRate, empty platforms (rate 0) included: the class as FRM wrote it, the rate as given. */
  platforms: Array<{ className: string; rate: number }>;
}

export function mapFrmStationRates(body: unknown): FrmStationRate[] {
  const out: FrmStationRate[] = [];
  for (const r of arrayBody(body, "getTrainStation")) {
    if (!isObj(r)) continue;
    const id = idOf(r);
    if (id === undefined || !nameOk(r.Name) || !isNum(r.TransferRate)) continue;
    let platformSum = 0;
    const platforms: Array<{ className: string; rate: number }> = [];
    if (Array.isArray(r.CargoInventory)) {
      for (const p of r.CargoInventory) {
        if (!isObj(p) || !isNum(p.TransferRate)) continue;
        platformSum += p.TransferRate;
        platforms.push({ className: typeof p.ClassName === "string" ? p.ClassName : "", rate: p.TransferRate });
      }
    }
    out.push({ id, nameHash: evidenceHash(r.Name), transfer: r.TransferRate, platformSum, platformCount: platforms.length, platforms });
  }
  return byId(out);
}

// ------------------------------------------------------------------------------------------------------------------------ storage

export interface FrmContainerItem { class: string; amount: number; stackMax: number | null }
export interface FrmContainer {
  id: string; className: string; nameHash: string;
  /** Slots and SlotsUsed as FRM wrote them; a missing or non-numeric value is null. */
  slots: number | null; slotsUsed: number | null;
  /** No capture shows FRM's key for a fluid buffer's capacity; the judge reports it, never judges it, until a capture does. Always null. */
  fluidCapacity: null;
  /** Inventory merged per item class, sorted by class (the rule our Core uses): amount is the sum over the class's entries and
   *  is a total, not a stack (it can exceed stackMax times slots); stackMax is MaxAmount of the first entry of the class, null when absent. */
  items: FrmContainerItem[];
}

export function mapFrmStorage(body: unknown): FrmContainer[] {
  const out: FrmContainer[] = [];
  for (const r of arrayBody(body, "getStorageInv")) {
    if (!isObj(r)) continue;
    const id = idOf(r);
    if (id === undefined || !goodString(r.ClassName) || !nameOk(r.Name)) continue;
    const merged = new Map<string, FrmContainerItem>();
    if (Array.isArray(r.Inventory)) {
      for (const e of r.Inventory) {
        if (!isObj(e) || !goodString(e.ClassName) || !isNum(e.Amount)) continue;
        const have = merged.get(e.ClassName);
        if (have) have.amount += e.Amount;
        else merged.set(e.ClassName, { class: e.ClassName, amount: e.Amount, stackMax: isNum(e.MaxAmount) ? e.MaxAmount : null });
      }
    }
    const items = [...merged.values()].sort((a, b) => byCodeUnit(a.class, b.class));
    out.push({
      id, className: r.ClassName, nameHash: evidenceHash(r.Name),
      slots: isNum(r.Slots) ? r.Slots : null, slotsUsed: isNum(r.SlotsUsed) ? r.SlotsUsed : null,
      fluidCapacity: null, items,
    });
  }
  return byId(out);
}
