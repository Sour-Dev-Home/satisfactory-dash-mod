import { describe, expect, it } from "vitest";
import { nativeParity, s3EvidenceFrom, s3LayerPosts, startS3Samplers, takeAfterNewS3Posts, type S3Samplers } from "../src/core-runs/native-parity";
import { NATIVE_SOURCES_S3 } from "../src/core-run";
import { kindCheck, logisticsLayers } from "../src/reader-lines";
import type { RecordedCall } from "../src/recorder";

const T0 = Date.parse("2026-10-27T12:00:00.000Z");
const call = (over: Partial<RecordedCall>): RecordedCall => ({ at: new Date(T0 + 60_000).toISOString(), doneAt: new Date(T0 + 60_100).toISOString(), agent: "mod", method: "POST", path: "/agent/v1/world/rails", status: 200, authorization: true, ...over });
const body = (path: string): unknown => (path === "/getDroneStation" ? [{ ID: "port-1", Name: "FUEL 5" }, { ID: "port-2", Name: "Secret Label" }] : []);

/** A sleep that never returns: each sampler reads once on start, and the test drives the rest with take(). */
const hang = () => new Promise<void>(() => {});
const flush = () => new Promise((r) => setTimeout(r, 0));
const zero = { reads: 0, failed: 0 };

describe("native-parity drive (S3)", () => {
  it("runs on the sprint3 world with all six S1 to S3 readers native (S4 adds three keys on top)", () => {
    expect(nativeParity.worldKey).toBe("sprint3");
    expect(nativeParity.modConfig).toMatchObject(NATIVE_SOURCES_S3);
  });

  it("starts the three samplers (one read each at once), takes an extra rails sample on demand, and stops them", async () => {
    const paths: string[] = [];
    const s = startS3Samplers(async (p) => (paths.push(p), body(p)), { now: () => T0 + paths.length, sleep: hang });
    await flush();
    expect(paths).toEqual(expect.arrayContaining(["/getTrainRails", "/getBelts", "/getLifts", "/getPipes", "/getDroneStation"]));
    const before = paths.filter((p) => p === "/getTrainRails").length;
    await s.rails.take();
    expect(paths.filter((p) => p === "/getTrainRails").length).toBe(before + 1);
    // Conveyors are three reads back to back per sample.
    expect(paths.filter((p) => p === "/getBelts").length).toBe(paths.filter((p) => p === "/getLifts").length);
    s.stop();
  });

  it("keeps each distinct FRM body once by hash, the drone ids only, and the posts since enrolment", async () => {
    const s = startS3Samplers(async (p) => body(p), { now: Date.now, sleep: hang });
    await flush();
    await s.rails.take();
    await s.drone.take();
    s.stop();
    const entries = [
      call({ world: { layer: "rails", items: 1, bytes: 10 }, worldData: [{ id: "T1", points: [[0, 0]] }] }),
      call({ at: new Date(T0 - 1000).toISOString(), world: { layer: "rails", items: 0, bytes: 1 }, worldData: [] }), // before enrolment
      call({ world: { layer: "conveyors", items: 0, bytes: 1 }, worldData: [] }),
      call({ status: 400, world: { layer: "rails", items: 0, bytes: 1 }, worldData: [] }),
    ];
    const ev = s3EvidenceFrom(s, entries, T0, { rails: zero, conveyors: zero, drone: zero });
    expect(ev.rails.posts).toHaveLength(1);
    expect(ev.conveyors.posts).toHaveLength(1);
    expect(s3LayerPosts(entries, "rails", T0)).toHaveLength(1);
    expect(s.rails.samples.length).toBeGreaterThan(1);
    expect(Object.keys(ev.rails.distinct)).toHaveLength(1); // equal bodies, one hash, kept once
    expect(ev.rails.frmSamples).toHaveLength(s.rails.samples.length);
    expect(ev.conveyors.drone.ids).toEqual(["port-1", "port-2"]);
    expect(JSON.stringify(ev)).not.toMatch(/FUEL 5|Secret Label/);
  });

  describe("takeAfterNewS3Posts", () => {
    const fake = () => {
      const calls: string[] = [];
      return { calls, s: { rails: { take: async () => { await flush(); calls.push("rails"); } }, conveyors: { take: async () => { await flush(); calls.push("conveyors"); } } } as unknown as Pick<S3Samplers, "rails" | "conveyors"> };
    };
    const post = (layer: string, over: Partial<RecordedCall> = {}) => call({ world: { layer, items: 0, bytes: 1 }, ...over });

    it("one accepted rails post gives exactly one rails take, and none for a repeat check over the same posts", async () => {
      const { calls, s } = fake();
      const seen = { rails: 0, conveyors: 0 };
      const entries = [post("rails")];
      expect(await takeAfterNewS3Posts(s, entries, T0, seen)).toEqual(["rails"]);
      expect(await takeAfterNewS3Posts(s, entries, T0, seen)).toEqual([]);
      expect(calls).toEqual(["rails"]);
      // a second post is a new take
      await takeAfterNewS3Posts(s, [...entries, post("rails")], T0, seen);
      expect(calls).toEqual(["rails", "rails"]);
    });

    it("a conveyors post gives a conveyors take only", async () => {
      const { calls, s } = fake();
      await takeAfterNewS3Posts(s, [post("conveyors")], T0, { rails: 0, conveyors: 0 });
      expect(calls).toEqual(["conveyors"]);
    });

    it("a 400, a non-mod post, or a post before enrolment gives nothing", async () => {
      const { calls, s } = fake();
      const entries = [
        post("rails", { status: 400 }),
        post("rails", { agent: "dash" as RecordedCall["agent"] }),
        post("conveyors", { at: new Date(T0 - 1000).toISOString() }),
        post("trainRoutes"),
      ];
      expect(await takeAfterNewS3Posts(s, entries, T0, { rails: 0, conveyors: 0 })).toEqual([]);
      expect(calls).toEqual([]);
    });
  });

  it("parses the shared pass's layers and the kinds line from the mod's log", () => {
    const P = "LogSatisDash: Display: ";
    const lines = [`${P}Logistics read (game thread only): layers rails=1 conveyors=0, 9 buildables`, `${P}Conveyor kinds: 5 ends checked, 1 differ from the class table (first classes: A_C)`];
    expect(logisticsLayers(lines)).toEqual({ rails: 1, conveyors: 0 });
    expect(logisticsLayers([])).toBeNull();
    expect(kindCheck(lines)).toEqual({ checked: 5, differ: 1, classes: ["A_C"] });
  });
});
