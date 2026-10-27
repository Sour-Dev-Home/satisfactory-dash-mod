// The S4 capture (ADR-0058 Sprint S4 prerequisite): the staged Test3 world is booted with FRM and the mod idle, and FRM's HTTP
// output for the endpoints S4 will replace is saved in s4-capture.json, so the dash repo can design each new contract from a real
// capture (its ground rule 2). It judges no mod data. Pass = every endpoint answered at least once with a JSON array; the counts,
// the record keys and the NOT EXERCISED note are printed in the reasons and none is judged. The captures are FRM output only
// (docs/clean-room.md allows that); FRM's source is never opened.
import type { CoreEvidence } from "../../harness/src/core-run";
import type { Verdict } from "../../harness/src/verdict";

export const requirements = ["S4-capture"];

export const S4_CAPTURE_ENDPOINTS = ["/getGenerators", "/getExtractor", "/getStorageInv", "/getTrains", "/getTrainStation", "/getMapMarkers", "/getDroneStation", "/getCables"] as const;
/** The endpoints whose rate fields are worth a full body each time they are non-zero. */
export const RATE_ENDPOINTS: readonly string[] = ["/getTrains", "/getTrainStation"];
/** A key anywhere in a record that names a transfer rate. */
export const RATE_KEY = /TransferRate|InflowRate|OutflowRate/i;
export const RATE_SAMPLES_MAX = 3;
export const KEYS_SHOWN = 40;

export interface RateSample {
  at: string;
  body: unknown;
}
export interface EndpointCapture {
  /** The first successful response body, unmodified. */
  first: unknown;
  reads: number;
  failed: number;
  /** The sorted union of the top-level keys of the records. */
  keys: string[];
  rateSamples: RateSample[];
  rateHits: number;
}
export interface S4Capture {
  settleMs: number;
  intervalMs: number;
  windowMs: number;
  endpoints: Record<string, EndpointCapture>;
}

/** The top-level keys of an array response's objects. */
export function recordKeys(body: unknown): string[] {
  if (!Array.isArray(body)) return [];
  const keys = new Set<string>();
  for (const r of body) if (typeof r === "object" && r !== null && !Array.isArray(r)) for (const k of Object.keys(r)) keys.add(k);
  return [...keys];
}

/** True if a key matching RATE_KEY holds a finite non-zero number at any depth. */
export function hasNonZeroRate(value: unknown): boolean {
  if (Array.isArray(value)) return value.some(hasNonZeroRate);
  if (typeof value !== "object" || value === null) return false;
  return Object.entries(value).some(([k, v]) => (RATE_KEY.test(k) && typeof v === "number" && Number.isFinite(v) && v !== 0) || hasNonZeroRate(v));
}

export function evaluate(e: CoreEvidence): Verdict {
  const capture = e.captures["s4-capture.json"] as S4Capture | undefined;
  const failures: string[] = [];
  const reasons: string[] = [];
  let rateHits = 0;
  for (const path of S4_CAPTURE_ENDPOINTS) {
    const c = capture?.endpoints?.[path];
    if (!c || !Array.isArray(c.first)) {
      failures.push(`${path}: never answered with a JSON array (${c ? `${c.reads} reads, ${c.failed} failed` : "no capture"})`);
      continue;
    }
    reasons.push(`${path}: ${c.reads} reads, ${c.failed} failed, ${c.first.length} records in the first; keys: ${c.keys.slice(0, KEYS_SHOWN).join(", ")}`);
    if (RATE_ENDPOINTS.includes(path)) {
      reasons.push(`${path}: rate fields non-zero in ${c.rateHits} of ${c.reads} samples`);
      rateHits += c.rateHits;
    }
  }
  if (failures.length) return { pass: false, reasons: failures };
  if (rateHits === 0) reasons.push("NOT EXERCISED: no train or station read showed a non-zero transfer rate in the window");
  return { pass: true, reasons };
}
