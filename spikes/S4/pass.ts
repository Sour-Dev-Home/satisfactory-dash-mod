// S4 (claim C6): the mod reads a credential from its SML config on the dedicated server and never logs it.
import type { Evidence, Verdict } from "../../harness/src/verdict";

export const claim = "C6";
const READ = /satis-mod s4 read length=(\d+) sha1=([0-9a-f]{12})/;

export function evaluate(e: Evidence): Verdict {
  if (!e.credential) return { pass: false, reasons: ["the harness wrote no credential"] };
  const m = e.modLines.map((l) => READ.exec(l)).find((x) => x !== null);
  if (!m) return { pass: false, reasons: ['no "satis-mod s4 read" line in this run\'s server log'] };
  if (e.credential.inLog) return { pass: false, reasons: ["the credential appears in the server log"] };
  const { length, sha1Prefix } = e.credential;
  if (Number(m[1]) !== length || m[2] !== sha1Prefix) {
    return { pass: false, reasons: [`the mod read a different value (length ${m[1]}, sha1 ${m[2]}; wrote length ${length}, sha1 ${sha1Prefix})`] };
  }
  return { pass: true, reasons: ["the mod read the credential the harness wrote, and the log never shows it"] };
}
