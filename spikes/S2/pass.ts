// S2 (claim C4): a 30 s timer posts 40 times (20 min) with no tick-rate drop. Window: owner, 2026-10-19.
import type { Evidence, Verdict } from "../../harness/src/verdict";

export const claim = "C4";
export const S2_PATH = "/s2";
export const S2_EXPECTED_POSTS = 40;
/** "No drop" means the with-mod median is at least 95% of the baseline median. */
export const S2_TICK_TOLERANCE = 0.05;

export function evaluate(e: Evidence): Verdict {
  const reasons: string[] = [];
  const ns = e.echo
    .filter((r) => r.method === "POST" && r.path === S2_PATH)
    .map((r) => {
      try {
        return (JSON.parse(r.body) as { n?: unknown }).n;
      } catch {
        return undefined;
      }
    });
  if (ns.length !== S2_EXPECTED_POSTS) reasons.push(`expected ${S2_EXPECTED_POSTS} posts to ${S2_PATH}, got ${ns.length}`);
  else if (!ns.every((n, i) => n === i + 1)) reasons.push(`the posts' n values are not 1..${S2_EXPECTED_POSTS} in order`);
  const base = e.ticks?.baseline.median ?? null;
  const withMod = e.ticks?.withMod.median ?? null;
  if (base === null || withMod === null) reasons.push("a tick-rate window has no samples");
  else if (withMod < base * (1 - S2_TICK_TOLERANCE)) {
    reasons.push(`tick median dropped from ${base} to ${withMod}, more than ${S2_TICK_TOLERANCE * 100}%`);
  }
  return reasons.length
    ? { pass: false, reasons }
    : { pass: true, reasons: [`${S2_EXPECTED_POSTS} posts in order; tick median ${base} without the mod, ${withMod} with it`] };
}
