import { describe, expect, it } from "vitest";
import {
  nativeParity, S4B_SAMPLER_MS, S4_SAMPLER_MS, s4bEvidenceFrom, s4bLayerPosts, startS4bSamplers, stationPairsFrom, takeAfterNewS4bPosts, type S4bSamplers,
} from "../src/core-runs/native-parity";
import { NATIVE_SOURCES_S4B } from "../src/core-run";
import type { RecordedCall } from "../src/recorder";
import { evidenceHash } from "../src/evidence-hash";
import * as Pass from "../../runs/native-parity/pass";

const T0 = Date.parse("2026-10-27T12:00:00.000Z");
const iso = (ms: number) => new Date(ms).toISOString();
const snap = (parts: NonNullable<RecordedCall["parts"]>, over: Partial<RecordedCall> = {}): RecordedCall => ({
  at: iso(T0 + 60_000), doneAt: iso(T0 + 60_100), agent: "mod", method: "POST", path: "/agent/v1/snapshots", status: 200, authorization: true, parts, ...over,
});
const extractor = { id: "E1", name: "Alpha", className: "Build_MinerMk1_C", recipe: null, isProducing: true, isPaused: false, isBackedUp: false, production: [] };
const generator = { id: "G1", name: "Beta", className: "Build_GeneratorCoal_C", currentMW: 75, capacityMW: 75, isProducing: true, fuel: null };
const station = { id: "S1", name: "Alpha", x: 1, y: 2, transferPerMinute: 10 };
const container = { id: "C1", name: "Beta", className: "Build_StorageContainerMk1_C", slots: 24, slotsUsed: 0, items: [] };
const full = snap({ factory: { buildings: [], extractors: [extractor] }, power: { circuits: [], generators: [generator] }, mapLive: { trains: [], stations: [station] }, storage: { containers: [container] } });

const body = (path: string): unknown =>
  path === "/getExtractor" ? [{ ID: "E1", ClassName: "Build_MinerMk1_C", Name: "Alpha", IsProducing: true, IsPaused: false, production: [{ ClassName: "Desc_OreIron_C", CurrentProd: 60, MaxProd: 120, ProdPercent: 50 }] }]
  : path === "/getTrainStation" ? [{ ID: "S1", Name: "Alpha", TransferRate: 10, CargoInventory: [] }]
  : [];

const hang = () => new Promise<void>(() => {});
const flush = () => new Promise((r) => setTimeout(r, 0));
const zero = { reads: 0, failed: 0 };
const zeros = { extractors: zero, generators: zero, stationRates: zero, storage: zero };

describe("native-parity drive (S4-B)", () => {
  it("runs with all thirteen readers native", () => {
    expect(nativeParity.modConfig).toEqual(NATIVE_SOURCES_S4B);
  });

  it("starts the four samplers with 20 s read timeouts, takes an extra sample on demand, and stops them", async () => {
    const calls: Array<[string, number]> = [];
    const s = startS4bSamplers(async (p, t) => (calls.push([p, t]), body(p)), { now: () => T0 + calls.length, sleep: hang });
    await flush();
    for (const p of ["/getExtractor", "/getGenerators", "/getTrainStation", "/getStorageInv"]) expect(calls).toContainEqual([p, 20_000]);
    const before = calls.filter(([p]) => p === "/getStorageInv").length;
    await s.storage.take();
    expect(calls.filter(([p]) => p === "/getStorageInv").length).toBe(before + 1);
    s.stop();
  });

  it("samples faster than the judge's 120 s gap (the real period is the interval plus the read)", async () => {
    const sleeps: number[] = [];
    const s = startS4bSamplers(async (p) => body(p), { now: Date.now, sleep: (ms) => (sleeps.push(ms), hang()) });
    await flush();
    s.stop();
    expect(sleeps.length).toBeGreaterThanOrEqual(4);
    for (const ms of sleeps) expect(ms).toBeLessThan(Pass.S4B_GAP_MS);
    expect(S4B_SAMPLER_MS).toBe(S4_SAMPLER_MS);
  });

  it("recorder keeps the power and storage parts the S4-B posts come from", () => {
    expect(s4bLayerPosts([full], "generators", T0)[0]!.data).toHaveLength(1);
    expect(s4bLayerPosts([full], "storage", T0)[0]!.data).toHaveLength(1);
  });

  describe("takeAfterNewS4bPosts", () => {
    const fake = () => {
      const calls: string[] = [];
      const t = (n: string) => ({ take: async () => { await flush(); calls.push(n); } });
      return { calls, s: { extractors: t("extractors"), generators: t("generators"), stationRates: t("stationRates"), storage: t("storage") } as unknown as Pick<S4bSamplers, "extractors" | "generators" | "stationRates" | "storage"> };
    };
    const fresh = () => ({ extractors: 0, generators: 0, stationRates: 0, storage: 0 });

    it("a snapshot with every fragment gives exactly one take per sampler, and none on a repeat check", async () => {
      const { calls, s } = fake();
      const seen = fresh();
      expect(await takeAfterNewS4bPosts(s, [full], T0, seen)).toEqual(["extractors", "generators", "stationRates", "storage"]);
      expect(calls.sort()).toEqual(["extractors", "generators", "stationRates", "storage"]);
      expect(await takeAfterNewS4bPosts(s, [full], T0, seen)).toEqual([]);
      expect(calls).toHaveLength(4);
    });

    it("each fragment alone triggers only its own sampler, and a second snapshot another take", async () => {
      const { calls, s } = fake();
      const seen = fresh();
      await takeAfterNewS4bPosts(s, [snap({ power: { circuits: [], generators: [] } })], T0, seen);
      expect(calls).toEqual(["generators"]);
      await takeAfterNewS4bPosts(s, [snap({ power: { circuits: [], generators: [] } }), snap({ power: { circuits: [], generators: [] } }, { at: iso(T0 + 70_000) })], T0, seen);
      expect(calls).toEqual(["generators", "generators"]);
    });

    it("a refused, non-mod, pre-enrolment or fragment-less snapshot takes nothing", async () => {
      const { calls, s } = fake();
      const entries = [
        { ...full, status: 400 }, { ...full, agent: "node" as const }, { ...full, at: iso(T0 - 1000) },
        snap({ power: { circuits: [] } }), snap({ factory: { buildings: [] } }), snap({}),
      ];
      expect(await takeAfterNewS4bPosts(s, entries, T0, fresh())).toEqual([]);
      expect(calls).toEqual([]);
    });
  });

  it("a fake snapshot with the names Alpha and Beta leaves no trace in the stringified capture", async () => {
    const s = startS4bSamplers(async (p) => body(p), { now: Date.now, sleep: hang });
    await flush();
    s.stop();
    const ev = s4bEvidenceFrom(s, [full], T0, zeros);
    const text = JSON.stringify(ev);
    expect(text).not.toMatch(/Alpha|Beta/);
    expect(ev.extractors.posts[0]!.data).toEqual([expect.objectContaining({ id: "E1", name: evidenceHash("Alpha") })]);
    expect(ev.stations.posts[0]!.data).toEqual([expect.objectContaining({ id: "S1", name: evidenceHash("Alpha"), transferPerMinute: 10 })]);
    expect(ev.storage.posts[0]!.data).toEqual([expect.objectContaining({ id: "C1", name: evidenceHash("Beta") })]);
    expect(ev.generators.posts[0]!.data).toEqual([expect.objectContaining({ id: "G1", name: evidenceHash("Beta") })]);
  });

  it("an unexpected field or a record the contract rejects carries no text into the capture", () => {
    const entries = [snap({ factory: { buildings: [], extractors: [{ ...extractor, note: "SecretNote" }, { id: 5, name: "SecretName" }, "SecretRaw"] } })];
    const data = s4bLayerPosts(entries, "extractors", T0)[0]!.data as unknown[];
    expect(data[0]).not.toHaveProperty("note");
    expect(data.slice(1)).toEqual([null, null]);
    expect(JSON.stringify(data)).not.toMatch(/Secret/);
  });

  it("only accepted mod snapshots since enrolment enter the evidence, and a non-array fragment is no post", () => {
    const entries = [{ ...full, status: 503 }, { ...full, agent: "other" as const }, { ...full, at: iso(T0 - 1) }, snap({ storage: { containers: "x" } as never }), full];
    expect(s4bLayerPosts(entries, "storage", T0)).toHaveLength(1);
    expect(s4bLayerPosts(entries, "extractors", T0)).toHaveLength(1);
  });

  it("keeps each distinct FRM body once by hash and the raw station pairs: the mod's rate beside FRM's, ids and numbers only", async () => {
    const s = startS4bSamplers(async (p) => body(p), { now: Date.now, sleep: hang });
    await flush();
    await s.stationRates.take();
    s.stop();
    const at = Date.now() + 1000;
    const ev = s4bEvidenceFrom(s, [snap({ mapLive: { trains: [], stations: [{ ...station, transferPerMinute: 9.5 }] } }, { at: iso(at) })], T0, zeros);
    expect(Object.keys(ev.stations.distinct)).toHaveLength(1); // equal bodies, one hash, kept once
    expect(ev.stations.frmSamples).toHaveLength(s.stationRates.samples.length);
    expect(ev.stations.frmSamples.length).toBeGreaterThan(1);
    // The post is 1 s after the last sample ended at best: the pair keeps the rate the mod posted (9.5) beside FRM's 10, numbers only.
    expect(ev.stationPairs.every((p) => p.id === "S1" && p.mod === 9.5 && p.frm.every((f) => f.transfer === 10))).toBe(true);
    expect(JSON.stringify(ev.stationPairs)).not.toMatch(/Alpha/);
  });

  it("stationPairsFrom lists the posted value next to FRM's on each bracketing side", () => {
    const at = iso(T0 + 100_000);
    const frm = { id: "S1", nameHash: "0123456789ab", transfer: 10.5, platformSum: 10.5, platformCount: 1, platforms: [] };
    const pairs = stationPairsFrom({
      posts: [{ at, status: 200, data: [{ id: "S1", name: "0123456789ab", x: 0, y: 0, transferPerMinute: 9.5 }, { id: "S2", name: "", x: 0, y: 0 }] }],
      frmSamples: [{ startedAt: iso(T0 + 90_000), endedAt: iso(T0 + 90_500), value: "b" }, { startedAt: iso(T0 + 110_000), endedAt: iso(T0 + 110_500), value: "a" }],
      distinct: { b: [frm], a: [{ ...frm, transfer: 11, platformSum: 11 }] },
      frm: { reads: 2, failed: 0 },
    });
    expect(pairs).toEqual([{ at, id: "S1", mod: 9.5, frm: [{ side: "before", transfer: 10.5, platformSum: 10.5 }, { side: "after", transfer: 11, platformSum: 11 }] }]);
  });
});
