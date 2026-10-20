// S7 (claim C9): the mod reads auto-pause natively, changes it, and puts it back; GetServerOptions, read after each
// step, agrees every time, and no change is left pending.
import type { AutoPauseStep, Evidence, Verdict } from "../../harness/src/verdict";

export const claim = "C9";
const ORDER: AutoPauseStep["step"][] = ["read", "set", "check", "restore", "final"];
const word = (b: boolean) => (b ? "True" : "False");

export function evaluate(e: Evidence): Verdict {
  if (!e.autoPause) return { pass: false, reasons: ["the harness collected no auto-pause steps"] };
  const reasons: string[] = [];
  const by = new Map(e.autoPause.steps.map((s) => [s.step, s]));
  for (const name of ORDER) {
    const n = e.autoPause.steps.filter((s) => s.step === name).length;
    if (n === 0) reasons.push(`the mod never logged step=${name}`);
    // A second run of the steps (e.g. from the entry map's world) could have changed the value before the harness
    // recorded it (final review, finding 1).
    if (n > 1) reasons.push(`the mod logged step=${name} ${n} times`);
  }
  for (const s of e.autoPause.steps) {
    if (s.api === null) reasons.push(`${s.step}: GetServerOptions failed`);
    else if (s.api !== s.mod) reasons.push(`${s.step}: mod ${word(s.mod)} vs GetServerOptions ${word(s.api)}`);
    if (s.pending === true) reasons.push(`${s.step}: the option is still pending`);
  }
  const read = by.get("read");
  if (read && e.autoPause.before !== null && read.mod !== e.autoPause.before) {
    reasons.push(`read: the mod read ${word(read.mod)}, but the harness recorded ${word(e.autoPause.before)} before the mod acted`);
  }
  if (read) {
    const expect = { set: !read.mod, check: !read.mod, restore: read.mod, final: read.mod } as const;
    for (const [name, want] of Object.entries(expect) as [keyof typeof expect, boolean][]) {
      const s = by.get(name);
      if (s && s.mod !== want) reasons.push(`${name}: ${word(s.mod)}, but the mod read ${word(read.mod)} first`);
    }
  }
  return reasons.length || !read
    ? { pass: false, reasons }
    : { pass: true, reasons: [`auto-pause read ${word(read.mod)}, changed to ${word(!read.mod)} and back natively; GetServerOptions agreed at all five steps`] };
}
