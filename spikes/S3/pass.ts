// S3 (claim C5): the mod's power production and capacity per circuit match FRM getPower read right after.
import type { Evidence, PowerPair, Verdict } from "../../harness/src/verdict";

export const claim = "C5";
const NEED = 10;
const within = (a: number, b: number) => Math.abs(a - b) <= 0.01 * Math.max(Math.abs(a), Math.abs(b), 1);

export function evaluate(e: Evidence): Verdict {
  if (!e.power) return { pass: false, reasons: ["the harness collected no power readings"] };
  const reasons: string[] = [];
  for (const c of e.power.frmCircuits) if (!e.power.modCircuits.includes(c)) reasons.push(`FRM has circuit ${c}, but the mod never logged it`);
  for (const c of e.power.modCircuits) if (!e.power.frmCircuits.includes(c)) reasons.push(`the mod logged circuit ${c}, but FRM doesn't have it`);
  // A joined FRM group is judged only if the mod read one set of figures for all its circuits in that round
  // (ruling 6, amended by the owner 2026-10-20).
  const rounds = new Map<string, PowerPair[]>();
  for (const p of e.power.pairs) {
    if (!p.group || p.group.length < 2) continue;
    const key = `${p.at}|${p.group.join(",")}`;
    rounds.set(key, [...(rounds.get(key) ?? []), p]);
  }
  for (const [key, ps] of rounds) {
    const [a] = ps;
    const same = ps.every((p) => p.mod.production === a!.mod.production && p.mod.capacity === a!.mod.capacity);
    if (!same) reasons.push(`${key.split("|")[0]}: circuits ${ps.map((p) => p.circuitId).join(", ")} share an FRM group but the mod read different values`);
  }
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
