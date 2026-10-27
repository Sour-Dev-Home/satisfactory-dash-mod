// The S2 readers' judges for the native-parity run (ADR-0058 Decision 3 and the owner's decisions of 2026-10-26; the table is in
// docs/plans/2026-10-26-native-readers-s2.md, Task 10). Beside S1's three judges in pass.ts, never inside them: S1's conditions
// are untouched. Each judge reads only its own evidence and returns pass, reasons and the fields the world never exercised; pass.ts
// turns that into a Verdict (NOT_EXERCISED_FAILS decides whether "never exercised" fails).
//
// The oracle is FRM's own HTTP output, read by the harness as a black box in the same minutes. A mod snapshot part carries no
// read time, so each accepted part is compared with the FRM samples whose read STARTED in [observedAt - 40 s, observedAt + 5 s].
// That window is NEW (this task): it covers the part's age (the 30 s cadence) and one status pass, and replaces the S1 bracket
// "the read just before and just after", which needs the mod's read time. Owner-set thresholds are marked (owner); everything this
// file adds on top is marked NEW and is listed among the owner questions in the task report.
import { TrainRoutesLayerDataSchema } from "../../harness/contract/src/map";
import {
  canonRoutes, FLAG_BACKED_UP, FLAG_PAUSED, FLAG_PRODUCING, inputsFullOf, type FactoryRow, type FactorySampleRows, type FactoryStatic, type LiveStation,
  type LiveTrain, type RouteRecord,
} from "../../harness/src/frm-s2";
import { inBracket, PARITY_POWER_TOLERANCE } from "../parity/pass";

// -------------------------------------------------------------------------------------------------------------- thresholds

/** NEW: compared rounds a reader needs: 30 min / 30 s cadence = 60 rounds, x 0.8 = 48, less slack for a deferred or skipped round. */
export const S2_NEED_ROUNDS = 40;
/** NEW: the FRM samples a mod part is compared with start this long before, and this long after, the snapshot's observedAt. */
export const S2_WINDOW_BEFORE_MS = 40_000;
export const S2_WINDOW_AFTER_MS = 5_000;
/** NEW: a machine's dynamic flags must equal a value FRM showed in the window for at least this share of machine-rounds (owner, 2026-10-26). */
export const S2_DYNAMIC_SHARE = 0.99;
/** Static fields: location within 1 m (owner), clockSpeedPercent within 0.01 (owner). */
export const S2_LOCATION_M = 1;
export const S2_CLOCK_TOLERANCE = 0.01;
/** maxPerMinute within 0.1% or 0.001 absolute (owner). */
export const S2_MAX_REL = 0.001;
export const S2_MAX_ABS = 0.001;
/** current/percent per item class: the power bracket rule, 3% (owner; the parity run's export). Per building: 5% (owner), for at least 95% of buildings (owner). */
export const S2_CLASS_TOLERANCE = PARITY_POWER_TOLERANCE;
export const S2_BUILDING_TOLERANCE = 0.05;
/**
 * The item-class rule holds for at least this share of class-checks (one class, one of current or percent, one round): the
 * owner, 2026-10-26, after run 40388387353, "an allowance like factory's", the same 1% the dynamic flags have per machine-round.
 */
export const S2_CLASS_SHARE = 0.99;
/**
 * NEW: every percentage tolerance here (the owner's 3% and 5%) is applied to max(|lo|, |hi|, 1), as S1's `inBracket` does, so
 * below 1 item/min (or 1 %) the allowance is an absolute 0.03 or 0.05 rather than a relative one: lenient for slow machines.
 */
export const S2_BUILDING_SHARE = 0.95;
/** A raw productivity above this in the mod's `Factory read` line means the mod's scale is wrong (owner: an explicit "scale" reason). */
export const S2_PRODUCTIVITY_MAX = 1.0001;
/** Positions (owner): a stationary train or any station within 1 m; a moving train within the distance FRM's own two neighbouring reads moved, plus NEW 5 m. */
export const S2_STATIONARY_M = 1;
export const S2_MOVING_EXTRA_M = 5;
/** NEW: a hint (never a verdict) when nearly every train position misses by about one train length. */
const LENGTH_HINT_MIN_M = 5;
const LENGTH_HINT_MAX_M = 60;
/** The trainRoutes layer is compared with the FRM-derived layer taken on at least one side of the post within this (as S1's nodes). */
export const S2_ROUTES_GAP_MS = 120_000;
/** NEW: more than this share of the harness's FRM reads failing fails a reader (a few lost samples only thin the windows, and the round floor guards that). */
export const S2_FRM_FAIL_SHARE = 0.05;
/** The owner's fallback text (2026-10-26) when only the averaged current/percent miss. */
export const S2_RATES_FALLBACK = "R2b: max matches, averaged current does not; fallback = set DefaultRatesMode to Instantaneous (Task 6) and the owner rules on the tolerance";
const SHOW = 10;

// ---------------------------------------------------------------------------------------------------------------- evidence

export interface Counts { reads: number; failed: number }
export interface Bracket<T> { startedAt: string; endedAt: string; value: T }
export interface FactoryFrmSample extends FactorySampleRows { startedAt: string; endedAt: string }
export interface FactoryRound { observedAt: string; rows: FactoryRow[] | null }
/** The factory part: the distinct static lists once, a round's rows per mod snapshot, FRM's reads as the rows that changed. */
export interface FactoryEvidence { statics: FactoryStatic[]; rounds: FactoryRound[]; frmSamples: FactoryFrmSample[]; frm: Counts; somersloopMachines: number }
export interface LiveRound { observedAt: string; trains: LiveTrain[] | null; stations: LiveStation[] | null }
export interface MapLiveEvidence { rounds: LiveRound[]; trains: Array<Bracket<LiveTrain[]>>; stations: Array<Bracket<LiveStation[]>>; trainsFrm: Counts; stationsFrm: Counts }
export interface RoutesEvidence { posts: Array<{ at: string; status: number; data: unknown }>; frmSamples: Array<Bracket<string>>; distinct: Record<string, string[]>; frm: Counts }
export interface S2Evidence { factory: FactoryEvidence; mapLive: MapLiveEvidence; routes: RoutesEvidence }
/** A judge's answer: pass, the reasons (all failures when it fails), and the fields the world never exercised. */
export interface S2Result { pass: boolean; reasons: string[]; never: string[] }

const ms = (iso: string) => Date.parse(iso);
const fmt = (n: number) => String(Math.round(n * 1000) / 1000);
const failing = (reasons: string[]): S2Result => ({ pass: false, reasons: reasons.slice(0, 30), never: [] });
const windowOf = <T extends { startedAt: string }>(samples: readonly T[], observedAt: string): T[] => {
  const o = ms(observedAt);
  return samples.filter((s) => ms(s.startedAt) >= o - S2_WINDOW_BEFORE_MS && ms(s.startedAt) <= o + S2_WINDOW_AFTER_MS);
};
function frmReasons(what: string, c: Counts): string[] {
  const attempts = c.reads + c.failed;
  if (c.failed > 0 && c.reads === 0) return [`no FRM ${what} read succeeded in the window (${c.failed} failed)`];
  if (attempts > 0 && c.failed / attempts > S2_FRM_FAIL_SHARE) return [`${c.failed} of ${attempts} FRM ${what} reads in the window failed, over ${S2_FRM_FAIL_SHARE * 100}%`];
  return [];
}
const frmNote = (what: string, c: Counts): string[] => (c.failed > 0 && frmReasons(what, c).length === 0 ? [`${c.failed} of ${c.reads + c.failed} FRM ${what} reads failed (a thinner window, not a mismatch)`] : []);

// ------------------------------------------------------------------------------------------------------------ factory decoding

interface DMachine { s: FactoryStatic; flags: number; vals: number[] }
interface DSample { t: number; machines: Map<string, DMachine> }
interface PRound { observedAt: string; mod: Map<string, DMachine> | null; dups: string[]; win: DSample[] }

function decodeRow(statics: readonly FactoryStatic[], row: FactoryRow): DMachine {
  return { s: statics[row[0]!]!, flags: row[1]!, vals: row.slice(2) };
}

function prepare(f: FactoryEvidence): { rounds: PRound[]; unpaired: number } {
  const samples: DSample[] = [];
  let current = new Map<string, DMachine>();
  for (const s of f.frmSamples) {
    const next = new Map(current);
    if (s.full) next.clear();
    for (const idx of s.gone) next.delete(f.statics[idx]!.id);
    for (const row of s.rows) {
      const m = decodeRow(f.statics, row);
      next.set(m.s.id, m);
    }
    current = next;
    samples.push({ t: ms(s.startedAt), machines: next });
  }
  const rounds: PRound[] = [];
  let unpaired = 0;
  for (const r of f.rounds) {
    const o = ms(r.observedAt);
    const win = samples.filter((s) => s.t >= o - S2_WINDOW_BEFORE_MS && s.t <= o + S2_WINDOW_AFTER_MS);
    let mod: Map<string, DMachine> | null = null;
    const dups: string[] = [];
    if (r.rows) {
      mod = new Map();
      for (const row of r.rows) {
        const m = decodeRow(f.statics, row);
        if (mod.has(m.s.id)) dups.push(m.s.id);
        mod.set(m.s.id, m);
      }
    }
    if (mod && win.length === 0) unpaired++;
    rounds.push({ observedAt: r.observedAt, mod, dups, win });
  }
  return { rounds, unpaired };
}

const itemKeyCache = new WeakMap<FactoryStatic, string[]>();
/** "<kind>:<className>#<n>" per item, n counting repeats of the same class in the machine. */
function itemKeys(s: FactoryStatic): string[] {
  let keys = itemKeyCache.get(s);
  if (!keys) {
    const seen = new Map<string, number>();
    keys = s.items.map(([kind, cls]) => {
      const base = `${kind}:${cls}`;
      const n = seen.get(base) ?? 0;
      seen.set(base, n + 1);
      return `${base}#${n}`;
    });
    itemKeyCache.set(s, keys);
  }
  return keys;
}

const label = (s: FactoryStatic) => `${s.id} (${s.className})`;

// ------------------------------------------------------------------------------------------------------------- R2a factory

/** Static fields differing between the mod's and one FRM read of a machine (the rate columns are R2b's). */
function staticDiff(a: FactoryStatic, b: FactoryStatic): string[] {
  if (a === b) return [];
  const out: string[] = [];
  if (a.name !== b.name) out.push(`name ${a.name} vs ${b.name}`);
  if (a.className !== b.className) out.push(`className ${a.className} vs ${b.className}`);
  if (a.recipe !== b.recipe) out.push(`recipe ${a.recipe ?? "null"} vs ${b.recipe ?? "null"}`);
  if (a.group !== b.group) out.push(`circuitGroupId ${a.group} vs ${b.group}`);
  if ((a.loc === null) !== (b.loc === null) || (a.loc && b.loc && !(Math.hypot(a.loc[0] - b.loc[0], a.loc[1] - b.loc[1], a.loc[2] - b.loc[2]) <= S2_LOCATION_M))) out.push(`location ${JSON.stringify(a.loc)} vs ${JSON.stringify(b.loc)}`);
  if ((a.clock === null) !== (b.clock === null) || (a.clock !== null && b.clock !== null && !(Math.abs(a.clock - b.clock) <= S2_CLOCK_TOLERANCE))) out.push(`clockSpeedPercent ${a.clock} vs ${b.clock}`);
  return out;
}

const FLAGS: Array<[string, (f: number) => unknown]> = [
  ["isProducing", (f) => (f & FLAG_PRODUCING) !== 0],
  ["isPaused", (f) => (f & FLAG_PAUSED) !== 0],
  ["isBackedUp", (f) => (f & FLAG_BACKED_UP) !== 0],
  ["inputsFull", (f) => inputsFullOf(f)],
];

/**
 * R2a: at least S2_NEED_ROUNDS compared rounds; id sets equal to the union of FRM's ids in the window; static fields equal for
 * every machine in every round; dynamic flags equal to a value FRM showed in the window for at least S2_DYNAMIC_SHARE of machine-rounds.
 */
export function judgeFactory(f: FactoryEvidence): S2Result {
  const reasons: string[] = [];
  const { rounds, unpaired } = prepare(f);
  let compared = 0;
  let machineRounds = 0;
  let flagMisses = 0;
  const flagLines: string[] = [];
  let staticMisses = 0;
  for (const r of rounds) {
    if (!r.mod) {
      reasons.push(`${r.observedAt}: the mod's factory part is not valid against the pinned contract`);
      continue;
    }
    if (r.dups.length) reasons.push(`${r.observedAt}: the mod sent machine id(s) twice: ${r.dups.slice(0, 3).join(" ")}`);
    if (r.win.length === 0) continue;
    compared++;
    const union = new Map<string, FactoryStatic>();
    for (const s of r.win) for (const [id, m] of s.machines) union.set(id, m.s);
    const missing = [...union].filter(([id]) => !r.mod!.has(id)).map(([, s]) => label(s));
    const extra = [...r.mod].filter(([id]) => !union.has(id)).map(([, m]) => label(m.s));
    if (missing.length || extra.length) {
      reasons.push(`${r.observedAt}: machine ids differ from the union of FRM's in the window: ${missing.length} in FRM only, ${extra.length} in the mod only`);
      if (missing.length) reasons.push(`  in FRM only (first ${SHOW}): ${missing.slice(0, SHOW).join(" ")}`);
      if (extra.length) reasons.push(`  in the mod only (first ${SHOW}): ${extra.slice(0, SHOW).join(" ")}`);
    }
    for (const [id, m] of r.mod) {
      const cands = r.win.map((s) => s.machines.get(id)).filter((x): x is DMachine => x !== undefined);
      if (cands.length === 0) continue;
      if (!cands.some((c) => staticDiff(m.s, c.s).length === 0)) {
        staticMisses++;
        if (staticMisses <= SHOW) reasons.push(`${r.observedAt}: ${label(m.s)} static fields differ from every FRM read in the window: ${staticDiff(m.s, cands[0]!.s).join("; ")}`);
      }
      machineRounds++;
      const bad = FLAGS.filter(([, get]) => !cands.some((c) => get(c.flags) === get(m.flags))).map(([name, get]) => `${name} mod ${String(get(m.flags))} vs FRM ${[...new Set(cands.map((c) => String(get(c.flags))))].join("/")}`);
      if (bad.length) {
        flagMisses++;
        if (flagLines.length < SHOW) flagLines.push(`${r.observedAt}: ${label(m.s)} ${bad.join("; ")}`);
      }
    }
  }
  if (staticMisses > SHOW) reasons.push(`... ${staticMisses - SHOW} more static mismatches`);
  if (compared < S2_NEED_ROUNDS) reasons.push(`factory compared in only ${compared} rounds (${unpaired} had no FRM read in the window), need ${S2_NEED_ROUNDS}`);
  const share = machineRounds === 0 ? 1 : 1 - flagMisses / machineRounds;
  if (share < S2_DYNAMIC_SHARE) reasons.push(`dynamic flags matched a value FRM showed in the window for ${(share * 100).toFixed(2)}% of ${machineRounds} machine-rounds, need ${S2_DYNAMIC_SHARE * 100}%`, ...flagLines);
  reasons.push(...frmReasons("getFactory", f.frm));
  if (reasons.length) return failing(reasons);
  const noteLines = [`${compared} rounds; machine ids equal to FRM's in the window; static fields equal; dynamic flags matched in ${(share * 100).toFixed(2)}% of ${machineRounds} machine-rounds${flagMisses ? ` (${flagMisses} within the ${((1 - S2_DYNAMIC_SHARE) * 100).toFixed(0)}% allowance, first ${Math.min(flagLines.length, SHOW)}: ${flagLines.join(" | ")})` : ""}`, ...frmNote("getFactory", f.frm)];
  return { pass: true, reasons: noteLines, never: factoryNever(rounds) };
}

/**
 * Fluid rule (reporting only): the canon rows carry no fluid flag, so a fluid item is one whose class is a vanilla fluid descriptor,
 * the liquids and gases the fixtures and FRM's own outputs show (Desc_Water_C, Desc_LiquidOil_C, Desc_LiquidFuel_C, ...), listed by
 * exact class name; a fluid machine is any machine with such an item among its rows.
 */
const FLUID_CLASSES: ReadonlySet<string> = new Set([
  "Desc_Water_C", "Desc_LiquidOil_C", "Desc_HeavyOilResidue_C", "Desc_LiquidFuel_C", "Desc_LiquidTurboFuel_C", "Desc_LiquidBiofuel_C",
  "Desc_AluminaSolution_C", "Desc_SulfuricAcid_C", "Desc_NitricAcid_C", "Desc_NitrogenGas_C", "Desc_DissolvedSilica_C",
  "Desc_RocketFuel_C", "Desc_IonizedFuel_C", "Desc_DarkMatter_C",
]);
const hasFluid = (s: FactoryStatic) => s.items.some(([, cls]) => FLUID_CLASSES.has(cls));

/** The cases the compared rounds never showed, from both sides' statics and flags (reported, never failing: NOT_EXERCISED_FAILS decides). */
function factoryNever(rounds: PRound[]): string[] {
  const seen = { any: false, unconfigured: false, paused: false, clock: false, backedUp: false, inputsFull: false, fluid: false };
  const take = (m: DMachine) => {
    seen.any = true;
    if (m.s.recipe === null) seen.unconfigured = true;
    if ((m.flags & FLAG_PAUSED) !== 0) seen.paused = true;
    if (m.s.clock !== null && Math.abs(m.s.clock - 100) > S2_CLOCK_TOLERANCE) seen.clock = true;
    if ((m.flags & FLAG_BACKED_UP) !== 0) seen.backedUp = true;
    if (inputsFullOf(m.flags) === true) seen.inputsFull = true;
    if (hasFluid(m.s)) seen.fluid = true;
  };
  for (const r of rounds) {
    if (!r.mod || r.win.length === 0) continue;
    for (const m of r.mod.values()) take(m);
    for (const s of r.win) for (const m of s.machines.values()) take(m);
  }
  const lines: Array<[boolean, string]> = [
    [seen.any, "any machine (neither side listed one)"],
    [seen.unconfigured, "an unconfigured machine (recipe null)"],
    [seen.paused, "a paused machine"],
    [seen.clock, "a clock other than 100%"],
    [seen.backedUp, "a machine backed up (isBackedUp true)"],
    [seen.inputsFull, "a machine with inputs full (inputsFull true)"],
    [seen.fluid, "a fluid machine (no item of a fluid class)"],
  ];
  return lines.filter(([shown]) => !shown).map(([, line]) => line);
}

// --------------------------------------------------------------------------------------------------------------- R2b rates

const within = (mod: number, lo: number, hi: number, tol: number) => lo - tol * Math.max(Math.abs(lo), Math.abs(hi), 1) <= mod && mod <= hi + tol * Math.max(Math.abs(lo), Math.abs(hi), 1);
const maxClose = (a: number, b: number) => Math.abs(a - b) <= Math.max(S2_MAX_ABS, S2_MAX_REL * Math.max(Math.abs(a), Math.abs(b)));
const median = (xs: number[]) => (xs.length === 0 ? NaN : [...xs].sort((a, b) => a - b)[Math.floor(xs.length / 2)]!);
const classOf = (key: string) => key.slice(0, key.lastIndexOf("#"));

/** The mod's raw productivity range from its `Factory read` lines (the scale check), or null when no line carries it. */
export function productivityRange(lines: readonly string[]): { min: number; max: number } | null {
  let min = Infinity;
  let max = -Infinity;
  for (const line of lines) {
    const m = /Factory read \(game thread only\):.*productivity (-?\d+(?:\.\d+)?)\.\.(-?\d+(?:\.\d+)?)/.exec(line);
    if (m) {
      min = Math.min(min, Number(m[1]));
      max = Math.max(max, Number(m[2]));
    }
  }
  return Number.isFinite(min) ? { min, max } : null;
}

/**
 * R2b: maxPerMinute within 0.1% or 0.001 absolute for every item of every machine in every round; current and percent per item
 * class (factory total) within 3% of FRM's range over the window, and per building within 5% for at least 95% of building-rounds.
 * When only current/percent miss, the verdict fails with the owner's fallback text.
 */
export function judgeRates(f: FactoryEvidence, raw: { min: number; max: number } | null = null): S2Result {
  const { rounds, unpaired } = prepare(f);
  const maxLines: string[] = [];
  const curLines: string[] = [];
  let maxMisses = 0;
  let compared = 0;
  let buildingRounds = 0;
  let buildingMisses = 0;
  let classMisses = 0;
  let classChecks = 0;
  let sawCurrent = false;
  const ratios: number[] = [];
  for (const r of rounds) {
    if (!r.mod || r.win.length === 0) continue;
    compared++;
    const modTotals = new Map<string, [number, number]>();
    for (const [id, m] of r.mod) {
      const keys = itemKeys(m.s);
      keys.forEach((k, i) => {
        const t = modTotals.get(classOf(k)) ?? [0, 0];
        modTotals.set(classOf(k), [t[0] + (m.vals[2 * i] ?? 0), t[1] + (m.vals[2 * i + 1] ?? 0)]);
      });
      const cands = r.win.map((s) => s.machines.get(id)).filter((x): x is DMachine => x !== undefined);
      if (cands.length === 0) continue;
      // maxPerMinute: some FRM read in the window has the same set of items (matched by class key, not by position: the order a
      // reader lists them in is not a finding) with the same maxima.
      const maxOf = (s: FactoryStatic) => new Map(itemKeys(s).map((k, i) => [k, s.items[i]![2]] as const));
      const modMax = maxOf(m.s);
      const sameItems = (c: FactoryStatic) => {
        const cm = maxOf(c);
        return cm.size === modMax.size && [...modMax].every(([k, v]) => cm.has(k) && maxClose(v, cm.get(k)!));
      };
      if (!cands.some((c) => sameItems(c.s))) {
        maxMisses++;
        if (maxLines.length < SHOW) {
          const cm = maxOf(cands[0]!.s);
          const onlyMod = [...modMax.keys()].filter((k) => !cm.has(k));
          const onlyFrm = [...cm.keys()].filter((k) => !modMax.has(k));
          const d = onlyMod.length || onlyFrm.length
            ? `items differ: in the mod only [${onlyMod.join(",")}], in FRM only [${onlyFrm.join(",")}]`
            : [...modMax].filter(([k, v]) => !maxClose(v, cm.get(k)!)).map(([k, v]) => `${k} maxPerMinute mod ${fmt(v)} vs FRM ${fmt(cm.get(k)!)}`).join("; ");
          maxLines.push(`${r.observedAt}: ${label(m.s)} ${d}`);
        }
      }
      // current and percent per building, against the machine's own range over the window.
      buildingRounds++;
      let ok = true;
      const why: string[] = [];
      keys.forEach((k, i) => {
        for (const [col, name] of [[0, "currentPerMinute"], [1, "percent"]] as const) {
          const vs = cands.map((c) => itemKeys(c.s).indexOf(k)).map((j, ci) => (j < 0 ? null : cands[ci]!.vals[2 * j + col]!)).filter((v): v is number => v !== null);
          if (vs.length === 0) continue; // a missing item is the max check's finding
          if (col === 0 && vs.some((v) => v > 0)) sawCurrent = true;
          const v = m.vals[2 * i + col]!;
          if (!within(v, Math.min(...vs), Math.max(...vs), S2_BUILDING_TOLERANCE)) {
            ok = false;
            why.push(`${k} ${name} mod ${fmt(v)} vs FRM ${fmt(Math.min(...vs))}..${fmt(Math.max(...vs))}`);
          }
        }
      });
      if (!ok) {
        buildingMisses++;
        if (curLines.length < SHOW) curLines.push(`${r.observedAt}: ${label(m.s)} ${why.join("; ")}`);
      }
    }
    // Per item class, the factory total against FRM's totals over the window.
    const frmTotals = r.win.map((s) => {
      const t = new Map<string, [number, number]>();
      for (const m of s.machines.values()) itemKeys(m.s).forEach((k, i) => {
        const c = t.get(classOf(k)) ?? [0, 0];
        t.set(classOf(k), [c[0] + (m.vals[2 * i] ?? 0), c[1] + (m.vals[2 * i + 1] ?? 0)]);
      });
      return t;
    });
    const classes = new Set([...modTotals.keys(), ...frmTotals.flatMap((t) => [...t.keys()])]);
    for (const cls of classes) {
      for (const [col, name] of [[0, "currentPerMinute"], [1, "percent"]] as const) {
        const mod = modTotals.get(cls)?.[col] ?? 0;
        const vs = frmTotals.map((t) => t.get(cls)?.[col] ?? 0);
        const lo = Math.min(...vs);
        const hi = Math.max(...vs);
        if (hi > 0 && col === 0) ratios.push(mod / ((lo + hi) / 2 || 1));
        classChecks++;
        if (!inBracket(mod, lo, hi)) {
          classMisses++;
          if (curLines.length < SHOW + 5 && classMisses <= SHOW) curLines.push(`${r.observedAt}: item class ${cls} total ${name} mod ${fmt(mod)} vs FRM ${fmt(lo)}..${fmt(hi)}`);
        }
      }
    }
  }
  const reasons: string[] = [];
  if (compared < S2_NEED_ROUNDS) reasons.push(`rates compared in only ${compared} rounds (${unpaired} had no FRM read in the window), need ${S2_NEED_ROUNDS}`);
  if (maxMisses > 0) reasons.push(`maxPerMinute differs from FRM's beyond ${S2_MAX_REL * 100}% or ${S2_MAX_ABS} absolute in ${maxMisses} machine-rounds (first ${maxLines.length}):`, ...maxLines.map((l) => `  ${l}`));
  const buildingShare = buildingRounds === 0 ? 1 : 1 - buildingMisses / buildingRounds;
  const classShare = classChecks === 0 ? 1 : 1 - classMisses / classChecks;
  const currentMiss = classShare < S2_CLASS_SHARE || buildingShare < S2_BUILDING_SHARE;
  const classNote = `${classMisses} of ${classChecks} item-class checks outside FRM's range +/-${S2_CLASS_TOLERANCE * 100}% over the window (${(classShare * 100).toFixed(2)}% within, need ${S2_CLASS_SHARE * 100}%)`;
  if (currentMiss) {
    if (maxMisses === 0) reasons.push(S2_RATES_FALLBACK);
    reasons.push(`current/percent: ${classNote}; ${(buildingShare * 100).toFixed(1)}% of ${buildingRounds} building-rounds within ${S2_BUILDING_TOLERANCE * 100}% (need ${S2_BUILDING_SHARE * 100}%)`);
    reasons.push(`median mod/FRM current ratio ${Number.isNaN(median(ratios)) ? "n/a" : fmt(median(ratios))}; the mod's raw productivity ${raw ? `${raw.min}..${raw.max}` : "range is not in its log"}`);
    reasons.push(...curLines.map((l) => `  ${l}`));
  }
  if (raw && raw.max > S2_PRODUCTIVITY_MAX) reasons.push(`scale: the mod logged a raw productivity of ${raw.max}, above ${S2_PRODUCTIVITY_MAX} (a 0..1 fraction was expected), so its current/percent are on the wrong scale`);
  reasons.push(...frmReasons("getFactory", f.frm));
  if (reasons.length) return failing(reasons);
  const never = [
    f.somersloopMachines === 0 ? "Somersloop (no machine in any FRM read carried one, so the boosted maxPerMinute is unchecked)" : null,
    !sawCurrent ? "currentPerMinute above zero (every FRM read of every machine was idle)" : null,
    // The fluid rule is the one documented at FLUID_CLASSES.
    !rounds.some((r) => r.mod && r.win.length > 0 && ([...r.mod.values()].some((m) => hasFluid(m.s)) || r.win.some((s) => [...s.machines.values()].some((m) => hasFluid(m.s))))) ? "a fluid item (no item of a fluid class in any compared machine)" : null,
  ].filter((x): x is string => x !== null);
  return { pass: true, reasons: [`${compared} rounds; maxPerMinute within ${S2_MAX_REL * 100}% or ${S2_MAX_ABS}; current and percent: ${classNote}${classMisses ? ` (first ${Math.min(classMisses, SHOW)}: ${curLines.filter((l) => l.includes(" item class ")).join(" | ")})` : ""}; within ${S2_BUILDING_TOLERANCE * 100}% for ${(buildingShare * 100).toFixed(1)}% of ${buildingRounds} building-rounds; median mod/FRM current ratio ${fmt(median(ratios))}`, ...frmNote("getFactory", f.frm)], never };
}

// ----------------------------------------------------------------------------------------------------------- R3 trains, stations

const dist = (a: { x: number; y: number }, b: { x: number; y: number }) => Math.hypot(a.x - b.x, a.y - b.y);
const idsOfList = (xs: Array<{ id: string }>) => xs.map((x) => x.id);

/** Does the mod's position fit FRM's reads in the window? Each consecutive pair of reads of the train is a candidate neighbour pair. */
function positionFits(mod: { x: number; y: number }, reads: Array<{ x: number; y: number }>): { ok: boolean; excess: number; moving: boolean; offset: number | null } {
  let best = Infinity;
  let moving = false;
  let offset: number | null = null;
  const pairs = reads.length === 1 ? [[reads[0]!, reads[0]!] as const] : reads.slice(1).map((r, i) => [reads[i]!, r] as const);
  for (const [a, b] of pairs) {
    const gap = dist(a, b);
    const allowed = gap <= S2_STATIONARY_M ? S2_STATIONARY_M : gap + S2_MOVING_EXTRA_M;
    if (gap > S2_STATIONARY_M) moving = true;
    const near = dist(mod, a) <= dist(mod, b) ? a : b;
    const excess = dist(mod, near) - allowed;
    if (excess < best) {
      best = excess;
      // Report only: the mod's offset from the nearer read along the direction FRM's pair moved (negative: behind the FRM track).
      offset = gap > S2_STATIONARY_M ? ((mod.x - near.x) * (b.x - a.x) + (mod.y - near.y) * (b.y - a.y)) / gap : null;
    }
  }
  return { ok: best <= 0, excess: best, moving, offset };
}

/** The statuses FRM gives a train (owner, 2026-10-26: Self-Driving exact; the other three are reported). Any other string is a finding. */
const KNOWN_STATUSES = ["Self-Driving", "Parked", "Manual", "Derailed"];

export function judgeTrains(m: MapLiveEvidence): S2Result {
  const reasons: string[] = [];
  let compared = 0;
  let unpaired = 0;
  let posChecks = 0;
  let posMisses = 0;
  const misses: number[] = [];
  let reported = 0;
  const reportedLines: string[] = [];
  let selfDriving = 0;
  let parked = 0;
  let moved = 0;
  let stationary = 0;
  const offsets: number[] = [];
  let trainsSeen = 0;
  for (const r of m.rounds) {
    if (r.trains === null) {
      reasons.push(`${r.observedAt}: the mod's mapLive part has no valid trains list`);
      continue;
    }
    const win = windowOf(m.trains, r.observedAt);
    if (win.length === 0) {
      unpaired++;
      continue;
    }
    compared++;
    const union = new Set(win.flatMap((s) => idsOfList(s.value)));
    const modIds = new Set(idsOfList(r.trains));
    const missing = [...union].filter((id) => !modIds.has(id));
    const extra = [...modIds].filter((id) => !union.has(id));
    if (missing.length || extra.length) reasons.push(`${r.observedAt}: train ids differ from the union of FRM's in the window: ${missing.length} in FRM only (${missing.slice(0, 3).join(" ")}), ${extra.length} in the mod only (${extra.slice(0, 3).join(" ")})`);
    for (const t of r.trains) {
      const reads = win.map((s) => s.value.find((x) => x.id === t.id)).filter((x): x is LiveTrain => x !== undefined);
      if (reads.length === 0) continue;
      trainsSeen++;
      if (!reads.some((x) => x.name === t.name)) reasons.push(`${r.observedAt}: train ${t.id} name hash ${t.name} vs FRM ${reads[0]!.name}`);
      const statuses = new Set(reads.map((x) => x.status));
      if (statuses.has("Self-Driving") || t.status === "Self-Driving") selfDriving++;
      if (statuses.has("Parked") || t.status === "Parked") parked++;
      if (!statuses.has(t.status)) {
        const unknown = [t.status, ...statuses].filter((s) => !KNOWN_STATUSES.includes(s));
        if (unknown.length) reasons.push(`${r.observedAt}: train ${t.id} unknown status ${unknown.join("/")} (mod ${t.status} vs FRM ${[...statuses].join("/")}); the known ones are ${KNOWN_STATUSES.join(", ")}`);
        else if (statuses.has("Self-Driving") || t.status === "Self-Driving") reasons.push(`${r.observedAt}: train ${t.id} status mod ${t.status} vs FRM ${[...statuses].join("/")} (Self-Driving is compared exactly)`);
        else {
          reported++;
          if (reportedLines.length < SHOW) reportedLines.push(`${r.observedAt}: train ${t.id} status mod ${t.status} vs FRM ${[...statuses].join("/")}`);
        }
      }
      posChecks++;
      const fit = positionFits(t, reads);
      if (fit.moving) moved++;
      else stationary++;
      if (fit.offset !== null) offsets.push(fit.offset);
      if (!fit.ok) {
        posMisses++;
        misses.push(fit.excess);
        if (posMisses <= SHOW) reasons.push(`${r.observedAt}: train ${t.id} position (${t.x}, ${t.y}) is ${fmt(fit.excess)} m beyond what FRM's own reads allow (stationary ${S2_STATIONARY_M} m; moving the distance they moved + ${S2_MOVING_EXTRA_M} m)`);
      }
    }
  }
  if (posMisses > SHOW) reasons.push(`... ${posMisses - SHOW} more position misses`);
  if (posChecks > 0 && posMisses === posChecks && median(misses) >= LENGTH_HINT_MIN_M && median(misses) <= LENGTH_HINT_MAX_M) reasons.push("position source: try NativeTrainPositionSource LastVehicle (every position missed by about one train length)");
  if (compared < S2_NEED_ROUNDS) reasons.push(`trains compared in only ${compared} rounds (${unpaired} had no FRM getTrains read in the window), need ${S2_NEED_ROUNDS}`);
  reasons.push(...frmReasons("getTrains", m.trainsFrm));
  // Reported, never judged: a negative median means the mod's moving positions sit behind FRM's track (a position-source or staleness bias).
  const offsetNote = offsets.length > 0 ? [`median along-path offset of ${offsets.length} moving positions: ${fmt(median(offsets))} m (negative: behind the FRM track; reported, never fails)`] : [];
  if (reasons.length) return failing([...reasons, ...offsetNote]);
  const never = [
    trainsSeen === 0 ? "any train (neither side listed one)" : null,
    trainsSeen > 0 && selfDriving === 0 ? "status Self-Driving (no train was self-driving, so the exact status match is unchecked)" : null,
    trainsSeen > 0 && parked === 0 ? "status Parked (no parked train)" : null,
    trainsSeen > 0 && moved === 0 ? "a moving train (every train was stationary, so the moving-distance rule is unchecked)" : null,
    trainsSeen > 0 && stationary === 0 ? "a stationary train (every train moved, so the 1 m check that exposes a position-source bias, such as LastVehicle or a stale position, never ran)" : null,
  ].filter((x): x is string => x !== null);
  const notes = reported > 0 ? [`reported, not failed (parked, manual or derailed): ${reported} status mismatches (first ${reportedLines.length}: ${reportedLines.join(" | ")})`] : [];
  return { pass: true, reasons: [`${compared} rounds; train ids and name hashes equal FRM's; self-driving status exact; positions within FRM's own reads`, ...notes, ...offsetNote,...frmNote("getTrains", m.trainsFrm)], never };
}

export function judgeStations(m: MapLiveEvidence): S2Result {
  const reasons: string[] = [];
  let compared = 0;
  let unpaired = 0;
  let seen = 0;
  for (const r of m.rounds) {
    if (r.stations === null) {
      reasons.push(`${r.observedAt}: the mod's mapLive part has no valid stations list`);
      continue;
    }
    const win = windowOf(m.stations, r.observedAt);
    if (win.length === 0) {
      unpaired++;
      continue;
    }
    compared++;
    const union = new Set(win.flatMap((s) => idsOfList(s.value)));
    const modIds = new Set(idsOfList(r.stations));
    const missing = [...union].filter((id) => !modIds.has(id));
    const extra = [...modIds].filter((id) => !union.has(id));
    if (missing.length || extra.length) reasons.push(`${r.observedAt}: station ids differ from the union of FRM's in the window: ${missing.length} in FRM only (${missing.slice(0, 3).join(" ")}), ${extra.length} in the mod only (${extra.slice(0, 3).join(" ")})`);
    for (const s of r.stations) {
      const reads = win.map((w) => w.value.find((x) => x.id === s.id)).filter((x): x is LiveStation => x !== undefined);
      if (reads.length === 0) continue;
      seen++;
      if (!reads.some((x) => x.name === s.name)) reasons.push(`${r.observedAt}: station ${s.id} name hash ${s.name} vs FRM ${reads[0]!.name}`);
      if (!reads.some((x) => dist(x, s) <= S2_STATIONARY_M)) reasons.push(`${r.observedAt}: station ${s.id} position (${s.x}, ${s.y}) vs FRM (${reads[0]!.x}, ${reads[0]!.y}), over ${S2_STATIONARY_M} m`);
    }
  }
  if (compared < S2_NEED_ROUNDS) reasons.push(`stations compared in only ${compared} rounds (${unpaired} had no FRM getTrainStation read in the window), need ${S2_NEED_ROUNDS}`);
  reasons.push(...frmReasons("getTrainStation", m.stationsFrm));
  if (reasons.length) return failing(reasons);
  return { pass: true, reasons: [`${compared} rounds; station ids and name hashes equal FRM's; positions within ${S2_STATIONARY_M} m`, ...frmNote("getTrainStation", m.stationsFrm)], never: seen === 0 ? ["any station (neither side listed one)"] : [] };
}

// ------------------------------------------------------------------------------------------------------------- trainRoutes

const diffLists = (mod: string[], frm: string[]) => {
  const count = (xs: string[]) => xs.reduce((m, x) => m.set(x, (m.get(x) ?? 0) + 1), new Map<string, number>());
  const a = count(mod);
  const b = count(frm);
  const missing: string[] = [];
  const extra: string[] = [];
  for (const [k, n] of b) for (let i = a.get(k) ?? 0; i < n; i++) missing.push(k);
  for (const [k, n] of a) for (let i = b.get(k) ?? 0; i < n; i++) extra.push(k);
  return { missing, extra };
};

/** One canonical record (canonRoutes) split into its exact part (`key`) and its cargo item sets (`cargo`, one entry per platform, or the train's). */
function splitCanon(c: string): { key: string; id: string; cargo: string[][] } {
  const a = JSON.parse(c) as unknown[];
  if (a[0] === "station") {
    const platforms = a[3] as Array<[string, string, string[]]>;
    return { key: JSON.stringify([a[0], a[1], a[2], platforms.map((p) => [p[0], p[1]])]), id: `station ${String(a[1])}`, cargo: platforms.map((p) => p[2]) };
  }
  return { key: JSON.stringify(a.slice(0, 4)), id: `train ${String(a[1])}`, cargo: [a[4] as string[]] };
}

/**
 * NEW (owner ruling, 2026-10-27, after run 40439238142 on Test3, where one unloading platform read empty in the mod while both FRM
 * samples around it showed Desc_Sulfur_C: cargo changes between reads): stations, platform ids, modes, train ids, names and
 * timetable stops stay exact; a difference only in a platform's or a train's cargo item set is reported, never a failure.
 * Returns null when the two layers differ in anything but cargo, else the cargo differences (empty when equal).
 */
function cargoOnlyDiffs(mod: string[], frm: string[]): string[] | null {
  const a = mod.map(splitCanon).sort((x, y) => (x.key < y.key ? -1 : x.key > y.key ? 1 : 0));
  const b = frm.map(splitCanon).sort((x, y) => (x.key < y.key ? -1 : x.key > y.key ? 1 : 0));
  if (a.length !== b.length || a.some((x, i) => x.key !== b[i]!.key)) return null;
  const out: string[] = [];
  a.forEach((x, i) => x.cargo.forEach((items, j) => {
    const other = b[i]!.cargo[j]!;
    if (items.join(",") !== other.join(",")) out.push(`${x.id}${x.id.startsWith("station") ? ` platform ${j + 1}` : ""}: mod [${items.join(" ")}] vs FRM [${other.join(" ")}]`);
  }));
  return out;
}

/**
 * trainRoutes: the mod's posted layer (native only: the bridge never sent it) is valid against the pinned contract and equals, as a
 * sorted canonical list, the FRM-derived layer taken on at least one side of the post within 120 s; at least one accepted post.
 * A difference only in cargo item sets is reported, never fails (owner ruling 2026-10-27, cargoOnlyDiffs).
 * An empty layer is a real answer (no stations, no trains), never "unreadable".
 */
export function judgeRoutes(r: RoutesEvidence): S2Result {
  const reasons: string[] = [];
  let compared = 0;
  let stationsWithPlatforms = 0;
  let stations = 0;
  let trainsWithItems = 0;
  let records = 0;
  const cargoDiffs: string[] = [];
  if (r.posts.length === 0) reasons.push("the mod posted no trainRoutes layer in the window");
  for (const post of r.posts) {
    const parsed = TrainRoutesLayerDataSchema.safeParse(post.data);
    if (!parsed.success) {
      reasons.push(`${post.at}: the posted layer is not valid against the pinned contract (${parsed.error.issues[0]?.message ?? "?"})`);
      continue;
    }
    const before = r.frmSamples.filter((s) => ms(s.endedAt) <= ms(post.at) && ms(post.at) - ms(s.endedAt) <= S2_ROUTES_GAP_MS).at(-1) ?? null;
    const after = r.frmSamples.find((s) => ms(s.startedAt) >= ms(post.at) && ms(s.startedAt) - ms(post.at) <= S2_ROUTES_GAP_MS) ?? null;
    if (!before && !after) {
      reasons.push(`${post.at}: no FRM getTrainStation+getTrains sample within ${S2_ROUTES_GAP_MS / 1000} s`);
      continue;
    }
    compared++;
    const layer = parsed.data as RouteRecord[];
    records = layer.length;
    for (const x of layer) {
      if (x.recordType === "station") {
        stations++;
        if (x.platforms.length > 0) stationsWithPlatforms++;
      } else if (x.items.length > 0) trainsWithItems++;
    }
    const mod = canonRoutes(layer);
    const sides = [before, after].filter((s): s is Bracket<string> => s !== null).map((s) => r.distinct[s.value] ?? []);
    if (sides.some((l) => l.join("\n") === mod.join("\n"))) continue;
    const cargo = sides.map((l) => cargoOnlyDiffs(mod, l)).filter((x): x is string[] => x !== null).sort((x, y) => x.length - y.length)[0];
    if (cargo !== undefined) {
      cargoDiffs.push(...cargo.map((c) => `${post.at} ${c}`));
      continue;
    }
    const d = diffLists(mod, sides[0]!);
    reasons.push(`${post.at}: the layer differs from FRM's: ${d.missing.length} in FRM and not in the mod, ${d.extra.length} in the mod and not in FRM`);
    if (d.missing.length) reasons.push(`  in FRM only (first ${SHOW}): ${d.missing.slice(0, SHOW).join(" ")}`);
    if (d.extra.length) reasons.push(`  in the mod only (first ${SHOW}): ${d.extra.slice(0, SHOW).join(" ")}`);
  }
  if (r.frm.failed > 0 && r.frmSamples.length === 0) reasons.push("no FRM getTrainStation+getTrains sample succeeded");
  if (reasons.length) return failing(reasons);
  const never = [
    stations === 0 ? "any station (the layer has none)" : stationsWithPlatforms === 0 ? "platform id, mode and items (no station in the layer has a platform)" : null,
    records > 0 && trainsWithItems === 0 ? "train cargo items (no train carried any)" : null,
  ].filter((x): x is string => x !== null);
  const cargoNote = cargoDiffs.length > 0 ? [`reported, not failed (cargo changes between reads): ${cargoDiffs.length} cargo item-set difference(s) (first 3: ${cargoDiffs.slice(0, 3).join(" | ")})`] : [];
  return { pass: true, reasons: [`${compared} posted layer(s) valid against the contract and equal to FRM's apart from reported cargo (${records} records)`, ...cargoNote], never };
}

