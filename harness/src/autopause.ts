// S7 (claim C9): each auto-pause step the mod logs, paired with GetServerOptions read right after it.
import type { AutoPauseStep, AutoPauseStepName } from "./verdict";

const S7 = /satis-mod s7 step=(read|set|check|restore|final) value=(True|False)(?: pending=([01]))?/;

export function parseS7Line(line: string): { step: AutoPauseStepName; value: boolean; pending: boolean | null } | null {
  const m = S7.exec(line);
  if (!m) return null;
  return { step: m[1] as AutoPauseStepName, value: m[2] === "True", pending: m[3] === undefined ? null : m[3] === "1" };
}

export async function collectAutoPauseSteps(opts: {
  readNew: () => string[];
  api: () => Promise<boolean>;
  sleep: (ms: number) => Promise<void>;
  now: () => number;
  durationMs: number;
  intervalMs: number;
}): Promise<AutoPauseStep[]> {
  const steps: AutoPauseStep[] = [];
  const end = opts.now() + opts.durationMs;
  while (opts.now() < end) {
    for (const l of opts.readNew()) {
      if (l.includes("satis-mod s7 done")) return steps;
      const s = parseS7Line(l);
      if (!s) continue;
      let api: boolean | null = null;
      try { api = await opts.api(); } catch { api = null; }
      steps.push({ at: new Date(opts.now()).toISOString(), step: s.step, mod: s.value, pending: s.pending, api });
    }
    await opts.sleep(opts.intervalMs);
  }
  return steps;
}
