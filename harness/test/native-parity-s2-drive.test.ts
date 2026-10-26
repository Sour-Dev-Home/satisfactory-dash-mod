import { describe, expect, it } from "vitest";
import { nativeParity, routePosts, s2RoundsFrom } from "../src/core-runs/native-parity";
import { FactoryTable } from "../src/frm-s2";
import type { RecordedCall } from "../src/recorder";
import { sessionHash } from "../src/status";

const call = (over: Partial<RecordedCall>): RecordedCall => ({ at: "2026-10-26T12:10:00.000Z", doneAt: "2026-10-26T12:10:00.100Z", agent: "mod", method: "POST", path: "/agent/v1/snapshots", status: 200, authorization: true, ...over });
const building = { id: "b1", name: "Constructor", className: "Build_ConstructorMk1_C", recipe: "Iron Plate", isProducing: true, isPaused: false, isBackedUp: false, inputsFull: false, circuitGroupId: 0, production: [{ name: "Iron Plate", className: "Desc_IronPlate_C", currentPerMinute: 5, maxPerMinute: 10, percent: 50 }] };

describe("native-parity drive (S2)", () => {
  it("runs on the S2 world with all four readers native and keeps the snapshot parts and the routes data", () => {
    expect(nativeParity.worldKey).toBe("s2");
    expect(nativeParity.modConfig).toMatchObject({ factorySource: "native", trainsSource: "native" });
  });

  it("builds factory and mapLive rounds from the kept parts; a part that is not valid becomes a round with null rows", () => {
    const table = new FactoryTable();
    const entries = [
      call({ snapshot: { observedAt: "2026-10-26T12:10:00.000Z" }, parts: { factory: { buildings: [building] }, mapLive: { trains: [{ id: "t", name: "h", x: 1, y: 2, status: "Parked" }], stations: [] } } }),
      call({ snapshot: { observedAt: "2026-10-26T12:10:05.000Z" } }), // a snapshot with neither part: no round
      call({ snapshot: { observedAt: "2026-10-26T12:10:30.000Z" }, parts: { factory: { buildings: "x" }, mapLive: { trains: 5 } } }),
    ];
    const r = s2RoundsFrom(entries, table);
    expect(r.factory.map((x) => x.observedAt)).toEqual(["2026-10-26T12:10:00.000Z", "2026-10-26T12:10:30.000Z"]);
    expect(r.factory[0]!.rows).toHaveLength(1);
    expect(r.factory[1]!.rows).toBeNull();
    expect(table.statics[0]).toMatchObject({ id: "b1", className: "Build_ConstructorMk1_C", items: [[0, "Desc_IronPlate_C", 10]] });
    expect(r.mapLive[0]).toEqual({ observedAt: "2026-10-26T12:10:00.000Z", trains: [{ id: "t", name: "h", x: 1, y: 2, status: "Parked" }], stations: [] });
    expect(r.mapLive[1]).toMatchObject({ trains: null, stations: null });
  });

  it("an empty factory part is a round with no rows, not an unreadable one", () => {
    const r = s2RoundsFrom([call({ snapshot: { observedAt: "2026-10-26T12:10:00.000Z" }, parts: { factory: { buildings: [] }, mapLive: { trains: [], stations: [] } } })], new FactoryTable());
    expect(r.factory[0]!.rows).toEqual([]);
    expect(r.mapLive[0]).toMatchObject({ trains: [], stations: [] });
  });

  it("hashes the routes layer's names and stops before they enter evidence, and marks an invalid layer without keeping it", () => {
    const layer = [
      { recordType: "station", id: "S1", name: "Silica Out ", platforms: [] },
      { recordType: "train", id: "T1", name: "Night Loop", stops: ["Silica Out "], items: [] },
    ];
    const posts = routePosts(
      [
        call({ path: "/agent/v1/world/trainRoutes", world: { layer: "trainRoutes", items: 2, bytes: 100 }, worldData: layer }),
        call({ at: "2026-10-26T12:20:00.000Z", path: "/agent/v1/world/trainRoutes", world: { layer: "trainRoutes", items: 1, bytes: 10 }, worldData: [{ recordType: "train", name: "Secret Name" }] }),
        call({ at: "2026-10-26T11:00:00.000Z", path: "/agent/v1/world/trainRoutes", world: { layer: "trainRoutes", items: 0, bytes: 10 }, worldData: [] }),
        call({ path: "/agent/v1/world/rails", world: { layer: "rails", items: 0, bytes: 10 }, worldData: [] }),
      ],
      Date.parse("2026-10-26T12:00:00.000Z"),
    );
    expect(posts).toHaveLength(2); // the one before the window and the rails post are left out
    expect(posts[0]!.data).toEqual([
      { recordType: "station", id: "S1", name: sessionHash("Silica Out "), platforms: [] },
      { recordType: "train", id: "T1", name: sessionHash("Night Loop"), stops: [sessionHash("Silica Out ")], items: [] },
    ]);
    expect(JSON.stringify(posts)).not.toMatch(/Silica|Night Loop|Secret Name/);
  });
});
