// S0 (claim C1): the mod loads on the dedicated server and writes a log line. Pass condition from mod-claims.md.
import type { Evidence, Verdict } from "../../harness/src/verdict";

export const claim = "C1";

export function evaluate(e: Evidence): Verdict {
  return e.modLines.some((l) => l.includes("satis-mod loaded"))
    ? { pass: true, reasons: ["this run's server log has 'satis-mod loaded'"] }
    : { pass: false, reasons: ["no 'satis-mod loaded' line in this run's server log"] };
}
