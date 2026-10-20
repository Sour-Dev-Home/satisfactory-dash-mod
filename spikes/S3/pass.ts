// S3 (claim C5): the mod's power production and capacity per circuit match FRM getPower read right after.
import type { Evidence, Verdict } from "../../harness/src/verdict";

export const claim = "C5";
const NEED = 10;
const within = (a: number, b: number) => Math.abs(a - b) <= 0.01 * Math.max(Math.abs(a), Math.abs(b), 1);

export function evaluate(e: Evidence): Verdict {
  if (!e.power) return { pass: false, reasons: ["the harness collected no power readings"] };
  const reasons: string[] = [];
  for (const c of e.power.frmCircuits) if (!e.power.modCircuits.includes(c)) reasons.push(`FRM has circuit ${c}, but the mod never logged it`);
  for (const c of e.power.modCircuits) if (!e.power.frmCircuits.includes(c)) reasons.push(`the mod logged circuit ${c}, but FRM doesn't have it`);
  const judged = e.power.pairs.filter((p) => p.frm !== null);
  if (judged.length < NEED) reasons.push(`${judged.length} paired readings, need ${NEED}`);
  for (const p of judged) {
    if (!within(p.mod.production, p.frm!.production)) reasons.push(`${p.at} circuit ${p.circuitId}: production ${p.mod.production} vs FRM ${p.frm!.production}`);
    if (!within(p.mod.capacity, p.frm!.capacity)) reasons.push(`${p.at} circuit ${p.circuitId}: capacity ${p.mod.capacity} vs FRM ${p.frm!.capacity}`);
  }
  return reasons.length
    ? { pass: false, reasons }
    : { pass: true, reasons: [`${judged.length} readings: production and capacity within 1% of FRM, the same circuits on both sides`] };
}
