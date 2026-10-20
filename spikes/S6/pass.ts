// S6 (claim C8): every status field the mod reads natively matches QueryServerState and HealthCheck read right after,
// both without a world (the entry map) and with one.
import type { Evidence, StatusReading, Verdict } from "../../harness/src/verdict";

export const claim = "C8";
const NEED = 10;
/** One round is 10 s, so a reading taken up to a round later may be that far ahead (spec: "one round's drift"). */
const DURATION_DRIFT = 10;
const tickClose = (a: number, b: number) => Math.abs(a - b) <= Math.max(2, 0.1 * Math.max(a, b));
const EXACT: { key: keyof StatusReading; name: string }[] = [
  { key: "running", name: "running" },
  { key: "paused", name: "paused" },
  { key: "players", name: "players" },
  { key: "limit", name: "limit" },
  { key: "health", name: "health" },
  { key: "session", name: "session" },
];

export function evaluate(e: Evidence): Verdict {
  if (!e.status) return { pass: false, reasons: ["the harness collected no status readings"] };
  const reasons: string[] = [];
  const judged = e.status.pairs.filter((p) => p.api !== null);
  const without = judged.filter((p) => !p.api!.running).length;
  const withWorld = judged.length - without;
  if (without < NEED) reasons.push(`${without} readings without a world, need ${NEED}`);
  if (withWorld < NEED) reasons.push(`${withWorld} readings with a world, need ${NEED}`);
  for (const p of judged) {
    const api = p.api!;
    for (const { key, name } of EXACT) if (p.mod[key] !== api[key]) reasons.push(`${p.at}: ${name} ${p.mod[key]} vs API ${api[key]}`);
    if (Math.abs(p.mod.duration - api.duration) > DURATION_DRIFT) reasons.push(`${p.at}: duration ${p.mod.duration} vs API ${api.duration}`);
    if (!tickClose(p.mod.tickRate, api.tickRate)) reasons.push(`${p.at}: tick rate ${p.mod.tickRate} vs API ${api.tickRate}`);
  }
  return reasons.length
    ? { pass: false, reasons }
    : { pass: true, reasons: [`${without} readings without a world and ${withWorld} with one: every field matches QueryServerState and HealthCheck`] };
}
