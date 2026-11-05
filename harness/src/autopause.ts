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

/** GetServerOptions also carries other options (some are secrets): only FG.DSAutoPause is kept, and the response is never stored. */
export async function readAutoPause(api: { call(fn: string): Promise<{ status: number; body: unknown }> }): Promise<boolean> {
  return (await readAutoPauseOptions(api)).applied;
}

/** FG.DSAutoPause applied, and its pending value (null when none, or not "True"/"False", as the mod reads it). */
export async function readAutoPauseOptions(api: { call(fn: string): Promise<{ status: number; body: unknown }> }): Promise<{ applied: boolean; pending: boolean | null }> {
  const r = await api.call("GetServerOptions");
  if (r.status < 200 || r.status > 299) throw new Error(`GetServerOptions returned ${r.status}`);
  const data = (r.body as { data?: { serverOptions?: Record<string, unknown>; pendingServerOptions?: Record<string, unknown> } } | null)?.data;
  const value = data?.serverOptions?.["FG.DSAutoPause"];
  if (value !== "True" && value !== "False") throw new Error("GetServerOptions has no FG.DSAutoPause");
  const pending = data?.pendingServerOptions?.["FG.DSAutoPause"];
  return { applied: value === "True", pending: pending === "True" ? true : pending === "False" ? false : null };
}

/** satisfactory-dash serverOptionsAdapter.ts:12, :133-137: ApplyServerOptions with PascalCase keys and "True"/"False". */
export async function setAutoPause(api: { call(fn: string, data?: Record<string, unknown>): Promise<{ status: number }> }, enabled: boolean): Promise<void> {
  const r = await api.call("ApplyServerOptions", { UpdatedServerOptions: { "FG.DSAutoPause": enabled ? "True" : "False" } });
  if (r.status < 200 || r.status > 299) throw new Error(`ApplyServerOptions returned ${r.status}: insecure local access may not change server options`);
}
