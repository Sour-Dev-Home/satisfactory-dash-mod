import { describe, expect, it } from "vitest";
import { nativeParity, S4_SAMPLER_MS, s4EvidenceFrom, s4LayerPosts, startS4Samplers, takeAfterNewS4Posts, type S4Samplers } from "../src/core-runs/native-parity";
import { NATIVE_SOURCES_S4 } from "../src/core-run";
import type { RecordedCall } from "../src/recorder";
import { evidenceHash } from "../src/evidence-hash";

const T0 = Date.parse("2026-10-27T12:00:00.000Z");
const call = (over: Partial<RecordedCall>): RecordedCall => ({ at: new Date(T0 + 60_000).toISOString(), doneAt: new Date(T0 + 60_100).toISOString(), agent: "mod", method: "POST", path: "/agent/v1/world/mapMarkers", status: 200, authorization: true, ...over });
const post = (layer: string, over: Partial<RecordedCall> = {}) => call({ world: { layer, items: 1, bytes: 10 }, ...over });
const body = (path: string): unknown =>
  path === "/getMapMarkers" ? [{ ID: "m1", Name: "Alpha", MapMarkerType: "RT_MapMarker", IconID: 3, ColorSlot: "6EA37BFF", location: { x: 1000, y: 2000 } }]
  : path === "/getDroneStation" ? [{ ID: "p1", Name: "Beta", PairedStation: "None", location: { x: 100, y: 200, z: 0 }, InputInventory: [], OutputInventory: [], FuelInventory: [] }]
  : path === "/getCables" ? [{ ID: "w1", location0: { x: 0, y: 0 }, location1: { x: 1000, y: 0 } }]
  : [];

const hang = () => new Promise<void>(() => {});
const flush = () => new Promise((r) => setTimeout(r, 0));
const zero = { reads: 0, failed: 0 };

describe("native-parity drive (S4)", () => {
  it("runs with all nine readers native", () => {
    expect(nativeParity.modConfig).toMatchObject(NATIVE_SOURCES_S4); // S4-B adds its four on top
  });

  it("starts the three samplers, takes an extra sample on demand, and stops them", async () => {
    const paths: string[] = [];
    const s = startS4Samplers(async (p) => (paths.push(p), body(p)), { now: () => T0 + paths.length, sleep: hang });
    await flush();
    expect(paths).toEqual(expect.arrayContaining(["/getMapMarkers", "/getDroneStation", "/getCables"]));
    const before = paths.filter((p) => p === "/getCables").length;
    await s.wires.take();
    expect(paths.filter((p) => p === "/getCables").length).toBe(before + 1);
    s.stop();
  });

  it("samples every S4 layer faster than the judge's gap (the real period is the interval plus the read)", async () => {
    const sleeps: number[] = [];
    const s = startS4Samplers(async (p) => body(p), { now: Date.now, sleep: (ms) => (sleeps.push(ms), hang()) });
    await flush();
    s.stop();
    expect(sleeps.length).toBeGreaterThanOrEqual(3);
    for (const ms of sleeps) expect(ms).toBeLessThan(120_000);
    expect(S4_SAMPLER_MS).toBeLessThan(120_000);
  });

  it("maps a non-object item to null and a non-array data to [], so raw data never passes through", () => {
    const secret = "SecretName";
    const entries = [
      post("mapMarkers", { worldData: [secret, 7, null, ["x"], { id: "m1", name: secret }] }),
      post("droneRoutes", { worldData: { name: secret } as unknown as unknown[] }),
      post("powerLines", { worldData: secret as unknown as unknown[] }),
    ];
    expect(s4LayerPosts(entries, "mapMarkers", T0)[0]!.data).toEqual([null, null, null, null, expect.objectContaining({ id: "m1", name: evidenceHash(secret) })]);
    expect(s4LayerPosts(entries, "droneRoutes", T0)[0]!.data).toEqual([]);
    expect(s4LayerPosts(entries, "powerLines", T0)[0]!.data).toEqual([]);
    expect(JSON.stringify(s4LayerPosts(entries, "mapMarkers", T0))).not.toContain(secret);
  });

  it("keeps each distinct body once by hash and leaves no name anywhere in the evidence", async () => {
    const s = startS4Samplers(async (p) => body(p), { now: Date.now, sleep: hang });
    await flush();
    await s.markers.take();
    s.stop();
    const entries = [
      post("mapMarkers", { worldData: [{ id: "m1", name: "Alpha", kind: "marker", icon: 3, color: "#6ea37b", x: 10, y: 20, note: "Alpha" }] }),
      post("droneRoutes", { worldData: [{ id: "p1", name: "Beta", paired: "Alpha", pairedId: "p2", input: [], output: [], fuel: [], x: 1, y: 2 }] }),
      post("powerLines", { worldData: [{ id: "w1", points: [[0, 0], [10, 0]] }] }),
      post("mapMarkers", { at: new Date(T0 - 1000).toISOString(), worldData: [] }), // before enrolment
    ];
    const ev = s4EvidenceFrom(s, entries, T0, { markers: zero, drone: zero, wires: zero });
    expect(ev.markers.posts).toHaveLength(1);
    expect(ev.markers.posts[0]!.data).toEqual([{ id: "m1", name: evidenceHash("Alpha"), kind: "marker", icon: 3, color: "#6ea37b", x: 10, y: 20 }]);
    expect(ev.drone.posts[0]!.data).toEqual([{ id: "p1", name: evidenceHash("Beta"), paired: evidenceHash("Alpha"), pairedId: "p2", input: [], output: [], fuel: [], x: 1, y: 2 }]);
    expect(s.markers.samples.length).toBeGreaterThan(1);
    expect(Object.keys(ev.markers.distinct)).toHaveLength(1); // equal bodies, one hash, kept once
    expect(ev.markers.frmSamples).toHaveLength(s.markers.samples.length);
    expect(JSON.stringify(ev)).not.toMatch(/Alpha|Beta/);
  });

  it("only accepted posts enter the evidence: a refused or non-mod post never reaches the judge", () => {
    const entries = [
      post("mapMarkers", { status: 400, worldData: [{ id: "m1", name: "x" }] }),
      post("mapMarkers", { status: 503, worldData: [] }),
      post("droneRoutes", { agent: "dash" as RecordedCall["agent"], worldData: [] }),
      post("powerLines", { worldData: [] }),
    ];
    expect(s4LayerPosts(entries, "mapMarkers", T0)).toHaveLength(0);
    expect(s4LayerPosts(entries, "droneRoutes", T0)).toHaveLength(0);
    expect(s4LayerPosts(entries, "powerLines", T0)).toHaveLength(1);
  });

  describe("takeAfterNewS4Posts", () => {
    const fake = () => {
      const calls: string[] = [];
      const t = (n: string) => ({ take: async () => { await flush(); calls.push(n); } });
      return { calls, s: { markers: t("markers"), drone: t("drone"), wires: t("wires") } as unknown as Pick<S4Samplers, "markers" | "drone" | "wires"> };
    };
    const fresh = () => ({ mapMarkers: 0, droneRoutes: 0, powerLines: 0 });

    it("one accepted post per layer gives exactly one take of its sampler, and none for a repeat check", async () => {
      const { calls, s } = fake();
      const seen = fresh();
      const entries = [post("mapMarkers"), post("droneRoutes"), post("powerLines")];
      expect(await takeAfterNewS4Posts(s, entries, T0, seen)).toEqual(["mapMarkers", "droneRoutes", "powerLines"]);
      expect(await takeAfterNewS4Posts(s, entries, T0, seen)).toEqual([]);
      expect(calls).toEqual(["markers", "drone", "wires"]);
      await takeAfterNewS4Posts(s, [...entries, post("powerLines")], T0, seen);
      expect(calls).toEqual(["markers", "drone", "wires", "wires"]);
    });

    it("a 400, a non-mod post, or a post before enrolment gives nothing", async () => {
      const { calls, s } = fake();
      const entries = [
        post("mapMarkers", { status: 400 }),
        post("droneRoutes", { agent: "dash" as RecordedCall["agent"] }),
        post("powerLines", { at: new Date(T0 - 1000).toISOString() }),
        post("rails"),
      ];
      expect(await takeAfterNewS4Posts(s, entries, T0, fresh())).toEqual([]);
      expect(calls).toEqual([]);
    });
  });
});
