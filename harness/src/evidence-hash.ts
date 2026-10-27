// A keyed hash for player text that reaches run evidence (S4 marker and port names, derived ids; ADR-0058 final review I-2).
//
// sessionHash (status.ts) is an unsalted SHA-1, so a short label can be recovered from a capture by a dictionary attack. This is
// HMAC-SHA256 under a random per-process key, cut to the same 12 hex characters the judges expect. The key lives only in this
// module's memory: it is never written, logged, returned or put in a capture. The oracle (frm-s4), the capture path
// (native-parity) and the judge (pass-s4's evidenceId) all call this one module in the run's process, so they share the key and
// the judge's equality is unchanged; a second process would hash differently, which is intended (evidence from two runs is not linkable).

import { createHmac, randomBytes } from "node:crypto";

let key: Buffer | null = null;

/** HMAC-SHA256(run key, text) as 12 hex characters. The key is created on first use. */
export function evidenceHash(text: string): string {
  key ??= randomBytes(32);
  return createHmac("sha256", key).update(text, "utf8").digest("hex").slice(0, 12);
}

/** Tests only: replace the run key (a given one, or a fresh random one when omitted). */
export function resetEvidenceKeyForTests(k?: Buffer): void {
  key = k ?? null;
}
