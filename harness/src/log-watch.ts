import { closeSync, existsSync, openSync, readSync, rmSync, statSync } from "node:fs";

/** Every line a spike mod writes starts with this (the spike contract). */
export const MOD_PREFIX = "satis-mod";

export function logOffset(path: string): number {
  return existsSync(path) ? statSync(path).size : 0;
}

/**
 * Deletes the previous run's log, so this run reads its own from byte 0. The harness owns the test server, and a size
 * offset into a log the game rotates at startup can skip this run's first lines once the new file outgrows the old.
 */
export function resetLog(path: string): number {
  rmSync(path, { force: true });
  return 0;
}

/** The text written after offset. A file smaller than offset was rotated, so it is read from the start. */
export function readLogSince(path: string, offset: number): string {
  if (!existsSync(path)) return "";
  const size = statSync(path).size;
  const from = size < offset ? 0 : offset;
  const length = size - from;
  if (length <= 0) return "";
  const buf = Buffer.alloc(length);
  const fd = openSync(path, "r");
  try {
    readSync(fd, buf, 0, length, from);
  } finally {
    closeSync(fd);
  }
  return buf.toString("utf8");
}

export interface WaitOptions {
  fromOffset: number;
  timeoutMs: number;
  pollMs?: number;
  sleep?: (ms: number) => Promise<void>;
  now?: () => number;
}

/** Resolves with the first line written after fromOffset that matches pattern, or rejects at the timeout. */
export async function waitForLine(path: string, pattern: RegExp, opts: WaitOptions): Promise<{ line: string }> {
  if (pattern.global || pattern.sticky) throw new Error("waitForLine needs a non-global, non-sticky pattern");
  const sleep = opts.sleep ?? ((ms: number) => new Promise<void>((r) => setTimeout(r, ms)));
  const now = opts.now ?? Date.now;
  const deadline = now() + opts.timeoutMs;
  for (;;) {
    const line = readLogSince(path, opts.fromOffset).split(/\r?\n/).find((l) => pattern.test(l));
    if (line !== undefined) return { line };
    if (now() >= deadline) throw new Error(`timed out after ${opts.timeoutMs} ms waiting for ${pattern} in ${path}`);
    await sleep(opts.pollMs ?? 1000);
  }
}

export function modLines(text: string): string[] {
  return text.split(/\r?\n/).filter((l) => l.includes(MOD_PREFIX));
}

/** Lines that look like version information, kept verbatim. Their real format is first seen in S0's evidence. */
export function versionLines(text: string, max = 20): string[] {
  return text.split(/\r?\n/).filter((l) => /version|changelist|\bCL\b/i.test(l)).slice(0, max);
}
