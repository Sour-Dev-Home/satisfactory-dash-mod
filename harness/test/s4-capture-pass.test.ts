import { afterEach, describe, expect, it, vi } from "vitest";
import { CORE_RUN_IDS, type CoreContext, type CoreDeps, type CoreEvidence } from "../src/core-run";
import { CORE_RUNS } from "../src/core-runs";
import { CAPTURE_INTERVAL_MS, CAPTURE_SETTLE_MS, CAPTURE_WINDOW_MS, s4Capture } from "../src/core-runs/s4-capture";
import * as Pass from "../../runs/s4-capture/pass";

const ev = (capture: unknown): CoreEvidence => ({ lines: [], errorLines: [], leaks: { code: false, secret: null, bearer: false }, captures: { "s4-capture.json": capture } });
const endpoint = (over: Partial<Pass.EndpointCapture> = {}): Pass.EndpointCapture => ({ first: [{ ID: "a", Name: "x" }], reads: 30, failed: 0, keys: ["ID", "Name"], rateSamples: [], rateHits: 0, ...over });
const capture = (over: Record<string, Partial<Pass.EndpointCapture>> = {}): Pass.S4Capture => ({
  settleMs: CAPTURE_SETTLE_MS, intervalMs: CAPTURE_INTERVAL_MS, windowMs: CAPTURE_WINDOW_MS,
  endpoints: Object.fromEntries(Pass.S4_CAPTURE_ENDPOINTS.map((p) => [p, endpoint(over[p])])),
});

describe("s4-capture judge", () => {
  it("passes and prints the counts and keys per endpoint", () => {
    const v = Pass.evaluate(ev(capture({ "/getTrains": { rateHits: 4 } })));
    expect(v.pass).toBe(true);
    expect(v.reasons).toContain("/getGenerators: 30 reads, 0 failed, 1 records in the first; keys: ID, Name");
    expect(v.reasons).toContain("/getTrains: rate fields non-zero in 4 of 30 samples");
    expect(v.reasons.join("\n")).not.toContain("NOT EXERCISED");
  });
  it("fails naming each endpoint that never answered with an array", () => {
    const v = Pass.evaluate(ev(capture({ "/getCables": { first: undefined, reads: 0, failed: 30 }, "/getTrains": { first: { error: "x" } } })));
    expect(v.pass).toBe(false);
    expect(v.reasons).toHaveLength(2);
    expect(v.reasons.join("\n")).toMatch(/\/getTrains.*\n\/getCables|\/getCables.*\n\/getTrains/);
  });
  it("fails with no capture at all", () => {
    expect(Pass.evaluate(ev(undefined)).pass).toBe(false);
  });
  it("prints NOT EXERCISED when neither trains nor stations showed a rate", () => {
    const v = Pass.evaluate(ev(capture()));
    expect(v.pass).toBe(true);
    expect(v.reasons.join("\n")).toContain("NOT EXERCISED: no train or station read showed a non-zero transfer rate in the window");
  });
  it("shows at most 40 keys", () => {
    const keys = Array.from({ length: 50 }, (_, i) => `K${String(i).padStart(2, "0")}`);
    const line = Pass.evaluate(ev(capture({ "/getCables": { keys } }))).reasons.find((r) => r.startsWith("/getCables:"))!;
    expect(line).toContain("K39");
    expect(line).not.toContain("K40");
  });
  it("finds a non-zero rate key at any depth, case-insensitively, and ignores zero and non-numbers", () => {
    expect(Pass.hasNonZeroRate([{ a: [{ b: { inflowRate: 2.5 } }] }])).toBe(true);
    expect(Pass.hasNonZeroRate([{ TransferRate: 0 }, { OutflowRate: "5" }, { Other: 9 }, { InflowRate: null }])).toBe(false);
  });
});

describe("s4-capture spec", () => {
  it("is registered, has no backend and no mod config, and loads sprint3", () => {
    expect(CORE_RUN_IDS).toContain("s4-capture");
    expect(CORE_RUNS["s4-capture"]).toBe(s4Capture);
    expect(s4Capture).toMatchObject({ backend: false, world: true, worldKey: "sprint3", requirements: ["S4-capture"] });
    expect(s4Capture.modConfig).toBeUndefined();
    expect(Pass.S4_CAPTURE_ENDPOINTS).toEqual(["/getGenerators", "/getExtractor", "/getStorageInv", "/getTrains", "/getTrainStation", "/getMapMarkers", "/getDroneStation", "/getCables"]);
    expect([CAPTURE_SETTLE_MS, CAPTURE_INTERVAL_MS, CAPTURE_WINDOW_MS]).toEqual([120_000, 30_000, 900_000]);
  });
  it("refuses without frmBaseUrl", () => {
    expect(() => s4Capture.preflight!({ config: {} } as unknown as CoreDeps)).toThrow(/frmBaseUrl/);
  });
});

describe("s4-capture drive", () => {
  afterEach(() => vi.unstubAllGlobals());

  function setup(handler: (path: string, call: number) => unknown, exitAtMs?: number) {
    let t = 0;
    let exit: () => void = () => undefined;
    const steps: string[] = [];
    const calls: Record<string, number> = {};
    const deps = {
      config: { frmBaseUrl: "http://127.0.0.1:9" },
      now: () => new Date(t),
      sleep: async (ms: number) => {
        t += ms;
        if (exitAtMs !== undefined && t >= exitAtMs) exit();
      },
      loadWorld: async (key?: string) => void steps.push(`loadWorld:${key}`),
      waitHealthy: async () => void steps.push("waitHealthy"),
      readAutoPause: async () => true,
      setAutoPause: async () => void steps.push("autoPauseOff"),
    } as unknown as CoreDeps;
    const times: Array<[string, number]> = [];
    vi.stubGlobal("fetch", async (url: URL) => {
      const path = url.pathname;
      calls[path] = (calls[path] ?? 0) + 1;
      times.push([path, t]);
      const out = handler(path, calls[path]);
      if (out instanceof Error) throw out;
      return { ok: true, status: 200, json: async () => out };
    });
    const exited = new Promise<void>((resolve) => void (exit = resolve));
    const captures: Record<string, unknown> = {};
    const ctx = { deps, captures, proc: { exited }, rememberAutoPause: () => undefined } as unknown as CoreContext;
    return { ctx, captures, steps, times, calls, get t() { return t; } };
  }
  const get = (c: Record<string, unknown>) => c["s4-capture.json"] as Pass.S4Capture;

  it("settles, then reads every endpoint 30 times at 30 s over the 15 minute window, keeping the first body", async () => {
    const s = setup((path, call) => [{ ID: `${path}-${call}`, Name: "n", Extra: 1 }]);
    await s4Capture.drive(s.ctx);
    expect(s.steps).toEqual(["loadWorld:sprint3", "waitHealthy", "autoPauseOff"]);
    expect(s.times[0]![1]).toBe(CAPTURE_SETTLE_MS);
    expect(s.times.at(-1)![1]).toBe(CAPTURE_SETTLE_MS + CAPTURE_WINDOW_MS - CAPTURE_INTERVAL_MS);
    for (const p of Pass.S4_CAPTURE_ENDPOINTS) {
      const c = get(s.captures).endpoints[p]!;
      expect(c.reads).toBe(30);
      expect(c.failed).toBe(0);
      expect(c.first).toEqual([{ ID: `${p}-1`, Name: "n", Extra: 1 }]);
      expect(c.keys).toEqual(["Extra", "ID", "Name"]);
    }
    expect(Pass.evaluate(ev(get(s.captures))).pass).toBe(true);
  });
  it("counts a failed GET and never throws on it", async () => {
    const s = setup((path, call) => (path === "/getCables" && call <= 2 ? new Error("down") : []));
    await expect(s4Capture.drive(s.ctx)).resolves.toBeUndefined();
    const c = get(s.captures).endpoints["/getCables"]!;
    expect(c.failed).toBe(2);
    expect(c.reads).toBe(28);
    expect(c.first).toEqual([]);
  });
  it("keeps at most 3 rate samples, only for a non-zero nested rate key on trains and stations, and counts every hit", async () => {
    const s = setup((path, call) => {
      if (path === "/getTrains") return [{ ID: "t", Timetable: [{ Station: { InflowRate: call % 2 ? 0 : 5 } }] }];
      if (path === "/getTrainStation") return [{ ID: "s", TransferRate: 0 }];
      return [{ ID: "g", InflowRate: 9 }];
    });
    await s4Capture.drive(s.ctx);
    const trains = get(s.captures).endpoints["/getTrains"]!;
    expect(trains.rateHits).toBe(15);
    expect(trains.rateSamples).toHaveLength(3);
    expect(trains.rateSamples[0]!.body).toEqual([{ ID: "t", Timetable: [{ Station: { InflowRate: 5 } }] }]);
    expect(get(s.captures).endpoints["/getTrainStation"]).toMatchObject({ rateHits: 0, rateSamples: [], reads: 30 });
    expect(get(s.captures).endpoints["/getGenerators"]).toMatchObject({ rateHits: 0, rateSamples: [] });
  });
  it("throws when the server process exits, leaving what was read", async () => {
    const s = setup(() => [{ ID: "a" }], CAPTURE_SETTLE_MS + 5 * CAPTURE_INTERVAL_MS);
    await expect(s4Capture.drive(s.ctx)).rejects.toThrow(/server exited/);
    const reads = get(s.captures).endpoints["/getTrains"]!.reads;
    expect(reads).toBeGreaterThan(0);
    expect(reads).toBeLessThan(30);
  });
});
