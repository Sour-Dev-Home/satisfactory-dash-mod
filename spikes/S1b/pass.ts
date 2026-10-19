// S1b (claim C3): HTTPS with certificate verification on. A 200 from the real host; the wrong hostname must fail.
import type { Evidence, Verdict } from "../../harness/src/verdict";

export const claim = "C3";

export function evaluate(e: Evidence): Verdict {
  const reasons: string[] = [];
  const good = e.modLines.some((l) => /satis-mod s1b good status=200\b/.test(l));
  const badError = e.modLines.some((l) => /satis-mod s1b bad error=/.test(l));
  const badStatus = e.modLines.some((l) => /satis-mod s1b bad status=/.test(l));
  if (!good) reasons.push("no 'satis-mod s1b good status=200' line: the verified request did not succeed");
  if (!badError) reasons.push("no 'satis-mod s1b bad error=' line: the wrong-hostname request did not fail");
  if (badStatus) reasons.push("the wrong-hostname request got a status: certificate verification is not on");
  return reasons.length ? { pass: false, reasons } : { pass: true, reasons: ["verified HTTPS succeeded and the wrong hostname was refused"] };
}
