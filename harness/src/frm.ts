import { z } from "zod";
import type { PowerPair, PowerReading } from "./verdict";

// Field names from satisfactory-dash raw-sources/captured-responses/frm-getPower-2026-09-22-*.json.
const GroupSchema = z.object({
  CircuitGroupID: z.number().int(),
  PowerProduction: z.number(),
  PowerConsumed: z.number(),
  PowerCapacity: z.number(),
  AssociatedCircuits: z.array(z.number().int()),
});
export interface FrmGroup { groupId: number; circuits: number[]; production: number; consumption: number; capacity: number }

export function parseGetPower(body: unknown): FrmGroup[] {
  return z.array(GroupSchema).parse(body).map((g) => ({
    groupId: g.CircuitGroupID, circuits: g.AssociatedCircuits, production: g.PowerProduction, consumption: g.PowerConsumed, capacity: g.PowerCapacity,
  }));
}

// The full figures, with the field names satisfactory-dash parses (packages/game-adapter/src/rawSchemas.ts:125-136).
const FullGroupSchema = z.object({
  CircuitGroupID: z.number().int(),
  PowerProduction: z.number(),
  PowerConsumed: z.number(),
  PowerCapacity: z.number(),
  PowerMaxConsumed: z.number(),
  BatteryCapacity: z.number(),
  BatteryPercent: z.number(),
  BatteryDifferential: z.number(),
  FuseTriggered: z.boolean(),
});
export interface FrmCircuitGroup {
  groupId: number; production: number; consumption: number; capacity: number; maxConsumption: number;
  batteryCapacity: number; batteryPercent: number; batteryDifferential: number; fuseTriggered: boolean;
}

/** One row per circuit group: rows FRM gives for circuits of one group must carry the same figures, or the body is refused. */
export function parseGetPowerFull(body: unknown): FrmCircuitGroup[] {
  const out: FrmCircuitGroup[] = [];
  for (const g of z.array(FullGroupSchema).parse(body)) {
    const row: FrmCircuitGroup = {
      groupId: g.CircuitGroupID, production: g.PowerProduction, consumption: g.PowerConsumed, capacity: g.PowerCapacity,
      maxConsumption: g.PowerMaxConsumed, batteryCapacity: g.BatteryCapacity, batteryPercent: g.BatteryPercent,
      batteryDifferential: g.BatteryDifferential, fuseTriggered: g.FuseTriggered,
    };
    const seen = out.find((x) => x.groupId === row.groupId);
    if (!seen) out.push(row);
    else if (JSON.stringify(seen) !== JSON.stringify(row)) throw new Error(`FRM's rows for group ${row.groupId} disagree`);
  }
  return out;
}

/** One successful FRM read and when it ran (ms since the epoch, the harness's clock, which is the server's machine). */
export interface FrmRead { startedAt: number; endedAt: number; groups: FrmCircuitGroup[] }

/**
 * The owner's bracket (2026-10-20): the last read that had ended by the sample, and the first that started after it,
 * so the two reads enclose the moment the mod sampled. A read farther than gapMs from the sample doesn't count.
 */
export function pickBracket(reads: FrmRead[], sampleAt: number, gapMs: number): { before: FrmRead | null; after: FrmRead | null } {
  const before = reads.filter((r) => r.endedAt <= sampleAt && sampleAt - r.endedAt <= gapMs).at(-1) ?? null;
  const after = reads.find((r) => r.startedAt >= sampleAt && r.startedAt - sampleAt <= gapMs) ?? null;
  return { before, after };
}

/** Reads FRM back to back (a short sleep between) until stopped; keeps the last 400 successful reads. */
export function startFrmPoller(
  read: () => Promise<FrmCircuitGroup[]>,
  opts: { now: () => number; sleep: (ms: number) => Promise<void>; intervalMs?: number },
): { reads: FrmRead[]; counts(): { reads: number; failed: number }; stop(): void } {
  const reads: FrmRead[] = [];
  let running = true;
  let total = 0;
  let failed = 0;
  void (async () => {
    while (running) {
      const startedAt = opts.now();
      try {
        const groups = await read();
        if (!running) break;
        reads.push({ startedAt, endedAt: opts.now(), groups });
        total++;
        if (reads.length > 400) reads.shift();
      } catch {
        // a failed read brackets nothing; the round then misses a read and fails
        if (running) failed++;
      }
      await opts.sleep(opts.intervalMs ?? 250);
    }
  })();
  return { reads, counts: () => ({ reads: total, failed }), stop: () => void (running = false) };
}

const LINE = /satis-mod s3 circuit=(\d+) production=([\d.]+) consumption=([\d.]+) capacity=([\d.]+)/;
export function parseS3Lines(lines: string[]): { circuitId: number; reading: PowerReading }[] {
  return lines.flatMap((l) => {
    const m = LINE.exec(l);
    return m ? [{ circuitId: Number(m[1]), reading: { production: Number(m[2]), consumption: Number(m[3]), capacity: Number(m[4]) } }] : [];
  });
}

/** A mod circuit pairs with the FRM group holding it (ruling 6, amended by the owner 2026-10-20: in run #11 FRM grouped
 *  circuits 0 and 3, and the mod read the group's figures for both). The verdict checks that a joined group's circuits
 *  read the same values; a circuit FRM doesn't know pairs with null. */
export function pairReadings(at: string, mod: { circuitId: number; reading: PowerReading }[], frm: FrmGroup[] | null): PowerPair[] {
  return mod.map(({ circuitId, reading }) => {
    const g = frm?.find((x) => x.circuits.includes(circuitId));
    return g
      ? { at, circuitId, mod: reading, frm: { production: g.production, consumption: g.consumption, capacity: g.capacity }, group: g.circuits }
      : { at, circuitId, mod: reading, frm: null };
  });
}

/** Polls the log for new s3 lines; for each non-empty batch, reads FRM at once and pairs the two (Review Focus 4: a
 *  failed FRM read pairs with null, never with an older reading). */
export async function collectPowerPairs(opts: {
  readNew: () => string[];
  frm: () => Promise<FrmGroup[]>;
  sleep: (ms: number) => Promise<void>;
  now: () => number;
  durationMs: number;
  intervalMs: number;
}): Promise<{ pairs: PowerPair[]; frmCircuits: number[]; modCircuits: number[] }> {
  const pairs: PowerPair[] = [];
  const frmCircuits = new Set<number>();
  const end = opts.now() + opts.durationMs;
  while (opts.now() < end) {
    const mod = parseS3Lines(opts.readNew());
    if (mod.length) {
      let frm: FrmGroup[] | null = null;
      try { frm = await opts.frm(); } catch { frm = null; }
      // Every circuit FRM knows, joined groups included, so a circuit the mod never logged fails by name (final review,
      // finding 3); only single-circuit groups pair with values (ruling 6).
      for (const g of frm ?? []) for (const c of g.circuits) frmCircuits.add(c);
      pairs.push(...pairReadings(new Date(opts.now()).toISOString(), mod, frm));
    }
    await opts.sleep(opts.intervalMs);
  }
  return { pairs, frmCircuits: [...frmCircuits], modCircuits: [...new Set(pairs.map((p) => p.circuitId))] };
}

/** GetServerOptions (satisfactory-dash raw-sources/dedicated-server-api.md E9) also returns FRM's
 *  uWS.AuthenticationToken in plain text: only FG.DSAutoPause is kept, and the response is never stored. */
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
