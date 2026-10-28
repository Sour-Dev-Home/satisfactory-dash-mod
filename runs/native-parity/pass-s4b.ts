// The S4-B pass conditions of the native-parity run (ADR-0058 Decision 3, sprint S4 part B): the extractors, generators, train
// station rates and storage contents the mod posts, judged against the FRM-derived records (harness/src/frm-s4b.ts) taken on either
// side of each post. Each reader is judged separately. A new file: the S1 to S4-A judges stay as they are (rule 3).
// Every threshold below says where it comes from. NEW = fixed in the plan's Task 18 table before any run, never loosened;
// "controller ruling, 2026-10-27" = the controller's amendment of that table (the oracles cannot supply the value).
// The posted names are already hashes (the run builds that evidence); no name, hashed or raw, is ever put in a reason.
// Pure: evidence in, verdict out.
import { FactoryBuildingSchema } from "../../harness/contract/src/factory";
import { MapTrainStationSchema } from "../../harness/contract/src/map";
import { PowerGeneratorSchema } from "../../harness/contract/src/power";
import { StorageContainerSchema, STORAGE_MAX_ITEMS_PER_CONTAINER } from "../../harness/contract/src/storage";
import type { FrmContainer, FrmExtractor, FrmGenerator, FrmStationRate } from "../../harness/src/frm-s4b";
import { evidenceId } from "../../harness/src/frm-s4";
import type { Bracket, Counts } from "./pass-s2";

export type { Bracket, Counts };

/** NEW (Task 18): a post is compared with an FRM sample taken within this of it (the same 120 s as S4_GAP_MS). */
export const S4B_GAP_MS = 120_000;
/** How many differences a reason lists (display only; it never decides a verdict). */
export const S4B_SHOW = 10;
/** NEW (comparison float slack, not a threshold): added to every band so a value exactly on the edge passes. */
const EPS = 1e-9;

/** NEW (Task 18, extractors): maxPerMinute within max(0.1/min, 1%) of FRM's max in either bracketing sample. Reason: max includes purity and clock, which change slowly. */
export const EXTRACTOR_MAX_ABS = 0.1;
export const EXTRACTOR_MAX_REL = 0.01;
/** NEW (Task 18, extractors): currentPerMinute (as a share of FRM's max) and percent within this many percentage points of either bracket, or inside the closed range between the brackets. Reason: productivity moves between reads. */
export const EXTRACTOR_POINTS = 5;
/** NEW (Task 18, generators): capacityMW within max(0.1 MW, 1%) of either bracket. Reason: capacity is stable (clock and potential). */
export const GENERATOR_CAP_ABS_MW = 0.1;
export const GENERATOR_CAP_REL = 0.01;
/** NEW (Task 18, generators): currentMW within max(0.5 MW, 2%) of the capacity-scaled range between the brackets. Reason: output follows demand. */
export const GENERATOR_CUR_ABS_MW = 0.5;
export const GENERATOR_CUR_REL = 0.02;
/** NEW (Task 18, stations): a rate passes when |m - f| <= max(0.5, 25% of f) against either bracket. Reason: smoothed values taken at different instants. */
export const STATION_ABS = 0.5;
export const STATION_REL = 0.25;
/** NEW (Task 18, stations): the median of m/f over stations with f >= STATION_FACTOR_MIN_F must be inside this band. Reason: it catches a wrong unit (R4). */
export const STATION_FACTOR_MIN_F = 1;
export const STATION_FACTOR_LOW = 0.8;
export const STATION_FACTOR_HIGH = 1.25;
/** NEW (Task 18, storage): a fluid amount in a stable container equals FRM's within 0.5% or 0.01 m3, whichever is larger. Reason: fluid levels carry float noise; a solid amount is exact. */
export const STORAGE_FLUID_REL = 0.005;
export const STORAGE_FLUID_ABS_M3 = 0.01;

/** Controller ruling, 2026-10-27: extractor isBackedUp is report-only (the oracle's backedUp is always null: FRM's output has no such flag). */
const RULING_BACKED_UP = "controller ruling, 2026-10-27: FRM has no backed-up flag";
/** Controller ruling, 2026-10-27: generator isProducing is report-only (the oracle derives it from RegulatedDemandProd > 0: observed, not specified). */
const RULING_GEN_PRODUCING = "controller ruling, 2026-10-27: the oracle derives it from RegulatedDemandProd > 0";
/** Controller ruling, 2026-10-27: a storage fluid capacity VALUE is report-only (the oracle's fluidCapacity is always null: no capture shows FRM's key). */
const RULING_FLUID_CAP = "controller ruling, 2026-10-27: no capture shows FRM's fluid capacity key";
/** Controller ruling, 2026-10-27: the station id rule compares FRM stations with transfer > 0 against the mod's stations with a rate (FRM's fluid-platform marker is unknown). */
const RULING_STATION_IDS = "controller ruling, 2026-10-27: FRM's fluid-platform marker is not in any capture";

/** The shape of a posted station name: evidenceHash (keyed, 12 hex characters) or empty. Anything else is a raw name. */
const HASHED_NAME = /^(?:[0-9a-f]{12})?$/;

type Post = { at: string; status: number; data: unknown };
export interface S4bEvidence<F> { posts: Post[]; frmSamples: Array<Bracket<string>>; distinct: Record<string, F[]>; frm: Counts }
export type ExtractorsEvidence = S4bEvidence<FrmExtractor>;
export type GeneratorsEvidence = S4bEvidence<FrmGenerator>;
export type StationsEvidence = S4bEvidence<FrmStationRate>;
export type StorageEvidence = S4bEvidence<FrmContainer>;
export interface S4bResult { pass: boolean; reasons: string[]; never: string[] }

const ms = (iso: string) => Date.parse(iso);
const failing = (reasons: string[]): S4bResult => ({ pass: false, reasons: reasons.slice(0, 40), never: [] });
const num = (n: number) => String(Math.round(n * 1000) / 1000);
const shown = (xs: readonly string[]) => xs.slice(0, S4B_SHOW).join(" ");
const uniq = (xs: string[]) => [...new Set(xs)];
const bySorted = (xs: Iterable<string>) => [...xs].sort((a, b) => (a < b ? -1 : a > b ? 1 : 0));
const near = (a: number, b: number, abs: number, rel: number) => Math.abs(a - b) <= Math.max(abs, rel * Math.abs(b)) + EPS;
const inRange = (v: number, xs: readonly number[], slack: number) => xs.length > 0 && v >= Math.min(...xs) - slack - EPS && v <= Math.max(...xs) + slack + EPS;

/** The FRM samples before (ended within S4B_GAP_MS) and after (started within it) a post. */
function sidesOf<T>(at: string, samples: readonly Bracket<string>[], distinct: Record<string, T>): { sides: T[]; missing: string | null } {
  const before = samples.filter((s) => ms(s.endedAt) <= ms(at) && ms(at) - ms(s.endedAt) <= S4B_GAP_MS).at(-1) ?? null;
  const after = samples.find((s) => ms(s.startedAt) >= ms(at) && ms(s.startedAt) - ms(at) <= S4B_GAP_MS) ?? null;
  const found = [before, after].filter((s): s is Bracket<string> => s !== null);
  const gone = found.find((s) => !Object.hasOwn(distinct, s.value));
  return { sides: found.map((s) => distinct[s.value]!), missing: gone === undefined ? null : gone.value };
}

interface Accepted<M, F> { at: string; mod: M[]; sides: F[][] }
interface Schema { safeParse: (d: unknown) => { success: true; data: unknown } | { success: false; error: { issues: Array<{ message: string }> } } }

/** The posts loop all four judges share: schema, the FRM brackets, hashed names. Returns the failure reasons, or the accepted posts. */
function collect<M extends { id: string }, F extends { id: string }>(
  e: S4bEvidence<F>, o: { layer: string; endpoint: string; schema: Schema; names?: (m: M) => Array<string | null | undefined> },
): { reasons: string[]; ok: Array<Accepted<M, F>> } {
  const reasons: string[] = [];
  const ok: Array<Accepted<M, F>> = [];
  // Only ACCEPTED posts (status 200) belong in `posts`: `status` is not read here.
  if (e.posts.length === 0) reasons.push(`the mod posted no ${o.layer} in the window`);
  for (const post of e.posts) {
    const parsed = o.schema.safeParse(post.data);
    if (!parsed.success) {
      reasons.push(`${post.at}: the posted ${o.layer} is not valid against the pinned contract (${parsed.error.issues[0]?.message ?? "?"})`);
      continue;
    }
    const { sides, missing } = sidesOf<F[]>(post.at, e.frmSamples, e.distinct);
    if (missing !== null) {
      reasons.push(`${post.at}: FRM sample ${missing} has no recorded ${o.layer}`);
      continue;
    }
    if (sides.length === 0) {
      reasons.push(`${post.at}: no FRM ${o.endpoint} sample within ${S4B_GAP_MS / 1000} s`);
      continue;
    }
    const mod = (parsed.data as M[]).map((m) => ({ ...m, id: evidenceId(m.id) }));
    if (mod.some((m) => (o.names?.(m) ?? []).some((n) => typeof n === "string" && !HASHED_NAME.test(n)))) {
      reasons.push(`${post.at}: a posted name is not a hash (the value is not printed)`);
      continue;
    }
    ok.push({ at: post.at, mod, sides });
  }
  if (e.frm.failed > 0 && e.frmSamples.length === 0) reasons.push(`no FRM ${o.endpoint} sample succeeded`);
  return { reasons, ok };
}

/** Ids equal (exact) against the better bracketing side; FRM-only and mod-only ids fail (first S4B_SHOW listed); the mod's repeated ids fail. */
function idDiff(label: string, modIds: readonly string[], sideIds: readonly string[][]): { failures: string[]; side: number } {
  const mine = new Set(modIds);
  const failures: string[] = [];
  if (mine.size < modIds.length) failures.push(`the mod sent ${label} id(s) twice`);
  let best: { total: number; side: number; frmOnly: string[]; modOnly: string[] } | null = null;
  sideIds.forEach((ids, side) => {
    const theirs = new Set(ids);
    const frmOnly = bySorted([...theirs].filter((x) => !mine.has(x)));
    const modOnly = bySorted([...mine].filter((x) => !theirs.has(x)));
    if (best === null || frmOnly.length + modOnly.length < best.total) best = { total: frmOnly.length + modOnly.length, side, frmOnly, modOnly };
  });
  const b = best!;
  if (b.frmOnly.length) failures.push(`${b.frmOnly.length} ${label} ids in FRM only (first ${S4B_SHOW}): ${shown(b.frmOnly)}`);
  if (b.modOnly.length) failures.push(`${b.modOnly.length} ${label} ids in the mod only (first ${S4B_SHOW}): ${shown(b.modOnly)}`);
  return { failures, side: b.side };
}

/** Field differences keyed by field, so a reason reads "field: N (first 10): ...". */
class Diffs {
  private by = new Map<string, string[]>();
  add(field: string, line: string): void { this.by.set(field, [...(this.by.get(field) ?? []), line]); }
  get size(): number { return [...this.by.values()].reduce((n, l) => n + l.length, 0); }
  lines(): string[] { return [...this.by].map(([f, l]) => `${f}: ${l.length} difference(s) (first ${S4B_SHOW}): ${l.slice(0, S4B_SHOW).join(" | ")}`); }
}

const candsOf = <F extends { id: string }>(id: string, sides: readonly F[][]): F[] => sides.flatMap((s) => s.find((f) => f.id === id) ?? []);
const postFail = (at: string, lines: string[]) => [`${at}: the posted data differs from FRM's`, ...lines.map((l) => `  ${l}`)];

// ------------------------------------------------------------------------------------------------------------------ extractors

interface ModExtractor {
  id: string; className: string; isProducing: boolean; isPaused: boolean; isBackedUp: boolean; circuitGroupId?: number;
  production: Array<{ className: string; currentPerMinute: number; maxPerMinute: number; percent: number }>;
}
/** The oracle gives no circuit for an unconnected building (-1); the mod sends -1 or nothing. Both read as "none". */
const circuitKey = (c: number | undefined): number | null => (c === undefined || c < 0 ? null : c);

/**
 * extractors (Task 18 table): valid against the contract; id sets equal; className, resourceClass and circuit exact; maxPerMinute within
 * max(0.1, 1%); currentPerMinute and percent within 5 points of either bracket or between the brackets; isProducing and isPaused equal to
 * either bracket. isBackedUp is REPORT-ONLY (controller ruling). An extractor the mod posts with no production passes when FRM has none.
 */
export function judgeExtractors(e: ExtractorsEvidence): S4bResult {
  const { reasons, ok } = collect<ModExtractor, FrmExtractor>(e, { layer: "extractors", endpoint: "getExtractor", schema: FactoryBuildingSchema.array() });
  let compared = 0, idle = 0, backedUp = 0, modAll = 0, multi = 0;
  const flat: ModExtractor[] = [];
  for (const a of ok) {
    const full = a.mod.filter((m) => m.production.length > 0);
    const empty = a.mod.filter((m) => m.production.length === 0);
    idle += empty.length;
    const lines: string[] = [];
    // FRM drops a record with no production entry, so an id FRM does have contradicts an empty mod record.
    const contradicted = empty.filter((m) => candsOf(m.id, a.sides).length > 0).map((m) => m.id);
    if (contradicted.length) lines.push(`${contradicted.length} extractor(s) with no production in the mod but with one in FRM (first ${S4B_SHOW}): ${shown(contradicted)}`);
    const ids = idDiff("extractor", full.map((m) => m.id), a.sides.map((s) => s.map((f) => f.id)));
    lines.push(...ids.failures);
    const d = new Diffs();
    for (const m of full) {
      const cands = candsOf(m.id, a.sides);
      if (cands.length === 0) continue;
      compared++;
      const p = m.production[0]!;
      if (m.production.length > 1) multi++;
      const any = (ok1: (f: FrmExtractor) => boolean) => cands.some(ok1);
      if (!any((f) => f.className === m.className)) d.add("className", `${m.id} mod=${m.className} frm=${cands[0]!.className}`);
      if (!any((f) => f.resourceClass === p.className)) d.add("resourceClass", `${m.id} mod=${p.className} frm=${cands[0]!.resourceClass}`);
      if (!any((f) => circuitKey(f.circuit) === circuitKey(m.circuitGroupId))) d.add("circuitGroupId", `${m.id} mod=${m.circuitGroupId ?? "absent"} frm=${cands[0]!.circuit ?? "absent"}`);
      if (!any((f) => near(p.maxPerMinute, f.max, EXTRACTOR_MAX_ABS, EXTRACTOR_MAX_REL))) d.add("maxPerMinute", `${m.id} mod=${num(p.maxPerMinute)} frm=${cands.map((f) => num(f.max)).join("/")}`);
      const curOk = any((f) => (f.max > 0 ? (100 * Math.abs(p.currentPerMinute - f.current)) / f.max <= EXTRACTOR_POINTS + EPS : p.currentPerMinute === f.current))
        || inRange(p.currentPerMinute, cands.map((f) => f.current), 0);
      if (!curOk) d.add("currentPerMinute", `${m.id} mod=${num(p.currentPerMinute)} frm=${cands.map((f) => num(f.current)).join("/")} of max ${num(cands[0]!.max)}`);
      const pctOk = any((f) => Math.abs(p.percent - f.percent) <= EXTRACTOR_POINTS + EPS) || inRange(p.percent, cands.map((f) => f.percent), 0);
      if (!pctOk) d.add("percent", `${m.id} mod=${num(p.percent)} frm=${cands.map((f) => num(f.percent)).join("/")}`);
      if (!any((f) => f.producing === m.isProducing)) d.add("isProducing", `${m.id} mod=${m.isProducing}`);
      if (!any((f) => f.paused === m.isPaused)) d.add("isPaused", `${m.id} mod=${m.isPaused}`);
    }
    lines.push(...d.lines());
    modAll += full.length;
    backedUp += full.filter((m) => m.isBackedUp).length;
    flat.push(...full);
    if (lines.length) reasons.push(...postFail(a.at, lines));
  }
  if (reasons.length) return failing(reasons);
  const never = compared === 0
    ? ["any extractor (both sides empty)"]
    : [
        flat.some((m) => /Pump|Fracking/i.test(m.className)) ? null : "a fluid extractor (pump)",
        flat.some((m) => m.isBackedUp) ? null : "an occupied-and-backed-up extractor",
      ].filter((x): x is string => x !== null);
  const out = [
    `${ok.length} posted extractor list(s) valid against the contract; extractor ids equal FRM's; ${compared} extractors compared; className, resource class and circuit exact; max within max(${EXTRACTOR_MAX_ABS}/min, ${EXTRACTOR_MAX_REL * 100}%); current and percent within ${EXTRACTOR_POINTS} points or between the brackets; isProducing and isPaused equal to a bracket`,
    `isBackedUp: report-only (${RULING_BACKED_UP}); the mod sent true on ${backedUp} of ${modAll} extractors`,
  ];
  if (multi > 0) out.push(`${multi} extractor(s) sent more than one production entry; only the first is compared with FRM's (reported, not failed)`);
  if (idle > 0) out.push(`${idle} extractor(s) with no production in the mod and none in FRM, reported and not compared`);
  return { pass: true, reasons: out, never };
}

// ------------------------------------------------------------------------------------------------------------------ generators

interface ModItem { className: string; amount: number }
interface ModGenerator {
  id: string; className: string; circuitGroupId?: number; currentMW: number; capacityMW: number; isProducing: boolean;
  fuel: ModItem | null; supplement?: ModItem | null;
}
const rawLine = (raw: Record<string, number>) => Object.entries(raw).map(([k, v]) => `${k}=${num(v)}`).join(" ");

/**
 * generators (Task 18 table): valid against the contract (a `fuel` key on every record); id sets equal; className, circuit, fuel class and
 * supplement class exact (null = null); capacityMW within max(0.1, 1%) of either bracket; currentMW within max(0.5, 2%) of the
 * capacity-scaled range between the brackets. REPORT-ONLY: isProducing (controller ruling), fuel and supplement amounts, and a
 * geothermal capacity when FRM says 0 (V5). The oracle's currentMW and capacityMW are picks ("observed, not specified"): a failure
 * quotes the oracle's raw map for the first mismatching generators so the run shows which FRM field matches.
 */
export function judgeGenerators(e: GeneratorsEvidence): S4bResult {
  const { reasons, ok } = collect<ModGenerator, FrmGenerator>(e, { layer: "generators", endpoint: "getGenerators", schema: PowerGeneratorSchema.array() });
  let compared = 0, prodDiff = 0, geoZero = 0;
  const amounts: Record<"fuel" | "supplement", { n: number; seen: number; largest: number; at: string }> = {
    fuel: { n: 0, seen: 0, largest: 0, at: "" }, supplement: { n: 0, seen: 0, largest: 0, at: "" },
  };
  const flat: ModGenerator[] = [];
  const frmFlat: FrmGenerator[] = [];
  for (const a of ok) {
    const lines: string[] = [];
    lines.push(...idDiff("generator", a.mod.map((m) => m.id), a.sides.map((s) => s.map((f) => f.id))).failures);
    const d = new Diffs();
    const bad: Array<{ m: ModGenerator; f: FrmGenerator }> = [];
    for (const m of a.mod) {
      const cands = candsOf(m.id, a.sides);
      if (cands.length === 0) continue;
      compared++;
      flat.push(m);
      frmFlat.push(...cands);
      const any = (ok1: (f: FrmGenerator) => boolean) => cands.some(ok1);
      if (!any((f) => f.className === m.className)) d.add("className", `${m.id} mod=${m.className} frm=${cands[0]!.className}`);
      if (!any((f) => circuitKey(f.circuit) === circuitKey(m.circuitGroupId))) d.add("circuitGroupId", `${m.id} mod=${m.circuitGroupId ?? "absent"} frm=${cands[0]!.circuit ?? "absent"}`);
      if (!any((f) => f.fuelClass === (m.fuel?.className ?? null))) d.add("fuel class", `${m.id} mod=${m.fuel?.className ?? "null"} frm=${cands[0]!.fuelClass ?? "null"}`);
      if (!any((f) => f.supplementClass === (m.supplement?.className ?? null))) d.add("supplement class", `${m.id} mod=${m.supplement?.className ?? "null"} frm=${cands[0]!.supplementClass ?? "null"}`);
      let wrong = false;
      if (/Geothermal/i.test(m.className) && cands.every((f) => f.capacityMW === 0)) geoZero++;
      else if (!any((f) => near(m.capacityMW, f.capacityMW, GENERATOR_CAP_ABS_MW, GENERATOR_CAP_REL))) {
        d.add("capacityMW", `${m.id} mod=${num(m.capacityMW)} frm=${cands.map((f) => num(f.capacityMW)).join("/")}`);
        wrong = true;
      }
      // The brackets' output scaled to the mod's capacity (output follows demand and clock), widened by max(0.5 MW, 2% of the range's top).
      const scaled = cands.map((f) => (f.capacityMW > 0 ? (f.currentMW * m.capacityMW) / f.capacityMW : f.currentMW));
      const slack = Math.max(GENERATOR_CUR_ABS_MW, GENERATOR_CUR_REL * Math.max(...scaled));
      if (!inRange(m.currentMW, scaled, slack)) {
        d.add("currentMW", `${m.id} mod=${num(m.currentMW)} frm(scaled)=${scaled.map(num).join("/")}`);
        wrong = true;
      }
      if (wrong) bad.push({ m, f: cands[0]! });
      if (!any((f) => f.producing === m.isProducing)) prodDiff++;
      for (const [k, mine, theirs] of [
        ["fuel", m.fuel, (f: FrmGenerator) => [f.fuelClass, f.fuelAmount] as const],
        ["supplement", m.supplement ?? null, (f: FrmGenerator) => [f.supplementClass, f.supplementAmount] as const],
      ] as const) {
        const pairs = cands.map(theirs).filter(([c, amt]) => mine !== null && c === mine.className && amt !== null);
        if (mine === null || pairs.length === 0) continue;
        const diff = Math.min(...pairs.map(([, amt]) => Math.abs(mine.amount - amt!)));
        amounts[k].seen++;
        if (diff > EPS) {
          amounts[k].n++;
          if (diff > amounts[k].largest) { amounts[k].largest = diff; amounts[k].at = m.id; }
        }
      }
    }
    lines.push(...d.lines());
    // Every bracket that has the id is quoted, so both sides are visible.
    for (const { m } of bad.slice(0, 3)) {
      candsOf(m.id, a.sides).forEach((f, i) => lines.push(`raw FRM numbers for ${m.id}, bracket ${i + 1} (mod current=${num(m.currentMW)} capacity=${num(m.capacityMW)}): ${rawLine(f.raw)}`));
    }
    if (lines.length) reasons.push(...postFail(a.at, lines));
  }
  if (reasons.length) return failing(reasons);
  const never = compared === 0
    ? ["any generator (both sides empty)"]
    : [
        flat.some((m) => /Nuclear/i.test(m.className)) ? null : "a nuclear generator",
        flat.some((m) => /Geothermal/i.test(m.className)) ? null : "a geothermal generator",
        flat.some((m) => m.supplement != null) || frmFlat.some((f) => f.supplementClass !== null) ? null : "a generator with a supplement",
      ].filter((x): x is string => x !== null);
  const out = [
    `${ok.length} posted generator list(s) valid against the contract; generator ids equal FRM's; ${compared} generators compared; className, circuit, fuel class and supplement class exact; capacity within max(${GENERATOR_CAP_ABS_MW} MW, ${GENERATOR_CAP_REL * 100}%); current within max(${GENERATOR_CUR_ABS_MW} MW, ${GENERATOR_CUR_REL * 100}%) of the capacity-scaled range between the brackets`,
    `isProducing: report-only (${RULING_GEN_PRODUCING}); ${prodDiff} generator(s) differ from both brackets`,
  ];
  for (const k of ["fuel", "supplement"] as const) {
    const a = amounts[k];
    out.push(`${k} amounts: report-only (they change every second); ${a.n} of ${a.seen} differ${a.n ? `, the largest ${num(a.largest)} on ${a.at}` : ""}`);
  }
  if (geoZero > 0) out.push(`${geoZero} geothermal generator(s) with an FRM capacity of 0: capacity report-only (V5)`);
  return { pass: true, reasons: out, never };
}

// -------------------------------------------------------------------------------------------------------------------- stations

interface ModStation { id: string; name: string; transferPerMinute?: number; inflowPerMinute?: number; outflowPerMinute?: number }

const median = (xs: number[]): number => {
  const s = [...xs].sort((a, b) => a - b);
  const h = s.length >> 1;
  return s.length % 2 ? s[h]! : (s[h - 1]! + s[h]!) / 2;
};

/**
 * station rates (Task 18 table + controller rulings): valid against the contract; a station FRM rates above 0 that the mod leaves without a
 * rate fails; a mod rate where FRM's is 0 or absent is reported (count, ids), not failed (RULING_STATION_IDS); each pair passes when
 * |m - f| <= max(0.5, 25% of f) against either bracket; the median of m/f over stations with f >= 1 must be in [0.8, 1.25] or the verdict fails
 * with the unit hint; inflowPerMinute and outflowPerMinute must be absent (R3). No FRM station with f >= 1 is NOT EXERCISED: rate factor.
 */
export function judgeStations(e: StationsEvidence): S4bResult {
  const { reasons, ok } = collect<ModStation, FrmStationRate>(e, { layer: "station rates", endpoint: "getTrainStation", schema: MapTrainStationSchema.array(), names: (m) => [m.name] });
  const ratios: Array<{ id: string; m: number; f: number }> = [];
  let compared = 0, extra = 0;
  const extraIds: string[] = [];
  for (const a of ok) {
    const rated = new Map(a.mod.filter((m) => m.transferPerMinute !== undefined).map((m) => [m.id, m.transferPerMinute!]));
    // Per side: the FRM stations with transfer > 0 the mod left without a rate. The better side decides.
    let side = 0, missing: string[] = [];
    a.sides.forEach((s, i) => {
      const gone = bySorted(s.filter((f) => f.transfer > 0 && !rated.has(f.id)).map((f) => f.id));
      if (i === 0 || gone.length < missing.length) { side = i; missing = gone; }
    });
    const lines: string[] = [];
    const dup = a.mod.length - new Set(a.mod.map((m) => m.id)).size;
    if (dup > 0) lines.push(`the mod sent station id(s) twice`);
    if (missing.length) lines.push(`${missing.length} station ids FRM rates above 0 with no rate in the mod (first ${S4B_SHOW}): ${shown(missing)}`);
    const flows = a.mod.filter((m) => m.inflowPerMinute !== undefined || m.outflowPerMinute !== undefined).map((m) => m.id);
    if (flows.length) lines.push(`${flows.length} station(s) with inflowPerMinute or outflowPerMinute, which must be absent (R3) (first ${S4B_SHOW}): ${shown(flows)}`);
    const d = new Diffs();
    for (const [id, rate] of rated) {
      const cands = candsOf(id, a.sides);
      // Controller ruling, 2026-10-27 (review I1): "extra (reported)" only when NO bracketing record has transfer > 0; otherwise the rate is judged against either bracket.
      if (!cands.some((f) => f.transfer > 0)) {
        extra++;
        extraIds.push(id);
        continue;
      }
      compared++;
      if (!cands.some((f) => Math.abs(rate - f.transfer) <= Math.max(STATION_ABS, STATION_REL * f.transfer) + EPS)) d.add("rate", `${id} mod=${num(rate)} frm=${cands.map((f) => num(f.transfer)).join("/")}`);
      // Controller ruling, 2026-10-27: the factor uses the first bracket with f >= 1 (not the one nearest the mod's rate, which would bias the median toward 1).
      const big = cands.find((f) => f.transfer >= STATION_FACTOR_MIN_F);
      if (big !== undefined) ratios.push({ id, m: rate, f: big.transfer });
    }
    lines.push(...d.lines());
    if (lines.length) reasons.push(...postFail(a.at, lines));
  }
  const med = ratios.length ? median(ratios.map((r) => r.m / r.f)) : null;
  if (ok.length && med !== null && (med < STATION_FACTOR_LOW - EPS || med > STATION_FACTOR_HIGH + EPS)) {
    reasons.push(
      `unit factor suspect: median ratio ${num(med)} (NativeStationRateToPerMinute in NativeTrains.h); band [${STATION_FACTOR_LOW}, ${STATION_FACTOR_HIGH}] over ${ratios.length} stations with an FRM rate of ${STATION_FACTOR_MIN_F}/min or more`,
      `  raw pairs (first ${S4B_SHOW}): ${ratios.slice(0, S4B_SHOW).map((r) => `${r.id} mod=${num(r.m)} frm=${num(r.f)}`).join(" | ")}`,
    );
  }
  if (reasons.length) return failing(reasons);
  const never = ratios.length === 0 ? ["a rate over 1/min"] : [];
  const out = [
    `${ok.length} posted station list(s) valid against the contract; every station FRM rates above 0 has a mod rate (${RULING_STATION_IDS}); ${compared} stations compared; rate within max(${STATION_ABS}, ${STATION_REL * 100}%) of a bracket; inflow and outflow absent`,
  ];
  if (med === null) out.push(`NOT EXERCISED: rate factor (no FRM station with a rate of ${STATION_FACTOR_MIN_F}/min or more); only the id and absent rules were judged`);
  else out.push(`rate factor: median m/f ${num(med)} over ${ratios.length} stations, inside [${STATION_FACTOR_LOW}, ${STATION_FACTOR_HIGH}]`);
  if (extra > 0) out.push(`reported, not failed (${RULING_STATION_IDS}): ${extra} mod rate(s) where FRM's rate is 0 or absent (first ${S4B_SHOW}): ${shown(uniq(extraIds))}`);
  return { pass: true, reasons: out, never };
}

// ---------------------------------------------------------------------------------------------------------------------- storage

interface ModContainer {
  id: string; className: string; slots: number; slotsUsed: number; fluidCapacityM3?: number;
  items: Array<{ className: string; amount: number; stackMax?: number | null }>;
}
const sameJson = (a: unknown, b: unknown) => JSON.stringify(a) === JSON.stringify(b);

/**
 * storage (Task 18 table + controller rulings): valid against the contract; id sets equal; className and slots exact (FRM's missing Slots
 * reads as 0, as a fluid buffer's does); stackMax equal to FRM's for solids and null on every fluid item. slotsUsed and the item class set and
 * amounts are EXACT only for a container whose two bracketing FRM records are identical (a fluid amount within 0.5% or 0.01 m3); a
 * container that changed between the brackets is counted, not judged. fluidCapacityM3 is REPORT-ONLY against FRM (controller ruling): its
 * presence is FAILED when FRM shows a fluid (an item with stackMax null) or the mod container has no slots but items.
 */
export function judgeStorage(e: StorageEvidence): S4bResult {
  const { reasons, ok } = collect<ModContainer, FrmContainer>(e, { layer: "storage", endpoint: "getStorageInv", schema: StorageContainerSchema.array() });
  let compared = 0, stable = 0, changed = 0, unverifiable = 0, fluids = 0, fluidSeen = 0;
  let over64 = false, depot = 0;
  for (const a of ok) {
    const lines = idDiff("container", a.mod.map((m) => m.id), a.sides.map((s) => s.map((f) => f.id))).failures;
    const d = new Diffs();
    for (const m of a.mod) {
      const cands = candsOf(m.id, a.sides);
      if (cands.length === 0) continue;
      compared++;
      const fluid = m.fluidCapacityM3 !== undefined;
      // Controller ruling 2026-10-27 (review I3); no capture shows a fluid buffer yet: if FRM's stackMax for a fluid is not null, the first
      // parity run shows it and fails, never loosened. An FRM container with an item whose stackMax is null is a fluid buffer; the mod's
      // matching container must carry fluidCapacityM3 (its value stays report-only). A mod container with no slots but items needs it too.
      const frmFluid = cands.some((f) => f.items.some((i) => i.stackMax === null));
      if (frmFluid && !fluid) d.add("fluidCapacityM3", `${m.id} FRM shows a fluid (an item with no stack size) but the mod sent no fluidCapacityM3`);
      if (m.slots === 0 && m.items.length > 0 && !fluid) d.add("fluidCapacityM3", `${m.id} has no slots and holds items but the mod sent no fluidCapacityM3`);
      if (fluid) fluids++;
      if (fluid && frmFluid) fluidSeen++;
      if (/CentralStorage|Depot|Uploader/i.test(m.className)) depot++;
      if (new Set(m.items.map((x) => x.className)).size < m.items.length) d.add("item classes", `${m.id} repeats an item class (items are merged per class)`);
      if (!cands.some((f) => f.className === m.className)) d.add("className", `${m.id} mod=${m.className} frm=${cands[0]!.className}`);
      if (!cands.some((f) => (f.slots ?? 0) === m.slots)) d.add("slots", `${m.id} mod=${m.slots} frm=${cands.map((f) => f.slots ?? 0).join("/")}`);
      for (const it of m.items) {
        if (fluid) {
          if (it.stackMax !== null) d.add("stackMax", `${m.id} ${it.className} is a fluid and must be null, mod=${it.stackMax ?? "absent"}`);
          continue;
        }
        const theirs = cands.flatMap((f) => f.items.filter((x) => x.class === it.className));
        if (theirs.length > 0 && !theirs.some((x) => x.stackMax === (it.stackMax ?? null))) d.add("stackMax", `${m.id} ${it.className} mod=${it.stackMax ?? "absent"} frm=${theirs.map((x) => x.stackMax ?? "null").join("/")}`);
      }
      const first = cands[0]!;
      if (a.sides.length < 2 || cands.length < a.sides.length) { unverifiable++; continue; }
      if (!cands.every((f) => f.slotsUsed === first.slotsUsed && sameJson(f.items, first.items))) { changed++; continue; }
      stable++;
      if (first.slotsUsed !== null && m.slotsUsed !== first.slotsUsed) d.add("slotsUsed", `${m.id} mod=${m.slotsUsed} frm=${first.slotsUsed}`);
      const mine = new Map(m.items.map((x) => [x.className, x.amount]));
      const capped = first.items.length > STORAGE_MAX_ITEMS_PER_CONTAINER;
      // Controller ruling 2026-10-27 (review M1): over the 64-class cap the mod must hold exactly the first 64 classes in FRM's sorted order
      // (the oracle sorts by code unit like Core); shared amounts are compared as for any container.
      const want = new Map((capped ? first.items.slice(0, STORAGE_MAX_ITEMS_PER_CONTAINER) : first.items).map((x) => [x.class, x.amount]));
      if (capped) over64 = true;
      const missing = [...want.keys()].filter((c) => !mine.has(c));
      const extra = [...mine.keys()].filter((c) => !want.has(c));
      if (missing.length || extra.length) d.add("item classes", `${m.id} mod-only=[${shown(extra)}] frm-only=[${shown(missing)}]`);
      for (const [c, amt] of mine) {
        const f = want.get(c);
        if (f === undefined) continue;
        const good = fluid ? Math.abs(amt - f) <= Math.max(STORAGE_FLUID_ABS_M3, STORAGE_FLUID_REL * Math.abs(f)) + EPS : amt === f;
        if (!good) d.add("amount", `${m.id} ${c} mod=${num(amt)} frm=${num(f)}`);
      }
    }
    lines.push(...d.lines());
    if (lines.length) reasons.push(...postFail(a.at, lines));
  }
  if (reasons.length) return failing(reasons);
  const never = compared === 0
    ? ["any container (both sides empty)"]
    : [
        fluidSeen > 0 ? null : "a fluid buffer",
        over64 ? null : "a container over 64 classes",
        stable > 0 ? null : "a container unchanged between the brackets (exact contents rule)",
      ].filter((x): x is string => x !== null);
  const out = [
    `${ok.length} posted storage list(s) valid against the contract; container ids equal FRM's; ${compared} containers compared; className and slots exact; stackMax equal to FRM's (null on fluids)`,
    `${stable} container(s) unchanged between the brackets: slotsUsed, item classes and amounts exact (fluid within ${STORAGE_FLUID_REL * 100}% or ${STORAGE_FLUID_ABS_M3} m3)`,
    `${changed} container(s) changed between the brackets and ${unverifiable} had one bracket only: contents report-only`,
    `fluidCapacityM3: value report-only (${RULING_FLUID_CAP}); ${fluids} mod fluid buffer(s), ${fluidSeen} matched to an FRM fluid`,
  ];
  if (depot > 0) out.push(`${depot} depot uploader(s) compared like any container`);
  return { pass: true, reasons: out, never };
}
