import { readFileSync } from "node:fs";
import { createServer, request, type Server } from "node:http";
import type { AddressInfo } from "node:net";
import { fileURLToPath } from "node:url";
import { afterEach, describe, expect, it } from "vitest";
import { parse } from "yaml";
import { CORE_RUN_IDS, NATIVE_SOURCES, NATIVE_SOURCES_S4B } from "../src/core-run";
import type { CoreContext, CoreDeps, CoreEvidence } from "../src/core-run";
import { CORE_RUNS } from "../src/core-runs";
import { SOAK_STALL_MS, SOAK_WINDOW_MS, snapshotWatchdog, soakNative } from "../src/core-runs/soak-native";
import { TICK_COST_WINDOW_MS, measureWindow } from "../src/core-runs/tick-cost";
import { tickCostNative } from "../src/core-runs/tick-cost-native";
import { startRecorder, type Recorder } from "../src/recorder";
import { TICK_WARMUP_MS } from "../src/ticks";
import * as Soak from "../../runs/soak-native/pass";
import * as Native from "../../runs/tick-cost-native/pass";
import type { TickCost } from "../../runs/tick-cost/pass";

const START = Date.parse("2026-11-03T08:00:00.000Z");
const MIN = 60_000;
const iso = (ms: number) => new Date(ms).toISOString();
const MB = 1024 * 1024;

describe("measureWindow: the window parameter", () => {
  function fakeDeps(onWindow: (o: { durationMs: number; stopped?: () => boolean }) => void, clock: { t: number }) {
    return {
      now: () => new Date(clock.t),
      sleep: async (ms: number) => {
        clock.t += ms;
        await new Promise<void>((r) => setImmediate(r)); // a real sleep yields to other work; without this the sampler spins in microtasks
      },
      sampleTickWindow: async (o: { warmupMs: number; durationMs: number; stopped?: () => boolean }) => {
        onWindow(o);
        // Stands in for the real sampler: yields each second of fake time, so the 30 s sampler loop gets its turns.
        while (!o.stopped?.() && clock.t < START + o.warmupMs + Math.min(o.durationMs, 3 * MIN)) {
          clock.t += 1000;
          await new Promise<void>((r) => setImmediate(r));
        }
        return { warmup: [], samples: [], errors: 0, median: null };
      },
    } as unknown as CoreDeps;
  }

  it("measures TICK_COST_WINDOW_MS (10 minutes) when no window is given: tick-cost and tick-cost-native are unchanged", async () => {
    let asked = 0;
    await measureWindow(fakeDeps((o) => void (asked = o.durationMs), { t: START }), 1);
    expect(TICK_COST_WINDOW_MS).toBe(10 * MIN);
    expect(asked).toBe(TICK_COST_WINDOW_MS);
  });
  it("measures the window it is given", async () => {
    let asked = 0;
    await measureWindow(fakeDeps((o) => void (asked = o.durationMs), { t: START }), 1, undefined, { windowMs: SOAK_WINDOW_MS });
    expect(asked).toBe(24 * 60 * MIN);
  });
  it("still rejects when the server exits (the stopped callback), with a window of any length", async () => {
    const clock = { t: START };
    const exited = Promise.resolve();
    await expect(measureWindow(fakeDeps(() => undefined, clock), 1, exited, { windowMs: SOAK_WINDOW_MS })).rejects.toThrow(/server exited/);
  });
  it("rejects with the watchdog's reason when stall returns one, and stops sampling", async () => {
    const clock = { t: START };
    let calls = 0;
    const stall = () => (++calls >= 2 ? "soak watchdog: down" : null);
    await expect(measureWindow(fakeDeps(() => undefined, clock), 1, undefined, { windowMs: SOAK_WINDOW_MS, stall })).rejects.toThrow("soak watchdog: down");
    expect(calls).toBe(2);
  });
});

describe("soak-native: spec and drive", () => {
  it("is registered and a run id", () => {
    expect(CORE_RUN_IDS).toContain("soak-native");
    expect(CORE_RUNS["soak-native"]).toBe(soakNative);
  });
  it("has tick-cost-native's mod config, world, baseline and order; only the judge and the window differ", () => {
    expect(soakNative.modConfig).toEqual({ ...NATIVE_SOURCES_S4B, stepDiagnostics: true });
    expect(soakNative.modConfig).toEqual(tickCostNative.modConfig);
    expect(soakNative.worldKey).toBe("sprint3");
    expect(soakNative.baseline).toBe(true);
    expect(soakNative.measureBaseline).toBe(tickCostNative.measureBaseline);
    expect(soakNative.evaluate).toBe(Soak.evaluate);
    expect(soakNative.requirements).toEqual([...Native.requirements, "soak-24h"]);
    expect(SOAK_WINDOW_MS).toBe(24 * 60 * MIN);
  });

  async function drive(spec: typeof soakNative) {
    const windows: number[] = [];
    let t = START;
    const deps = {
      now: () => new Date(t),
      sleep: () => new Promise<void>((r) => setImmediate(r)),
      config: {},
      backendHost: async () => ({ relayUrl: "http://127.0.0.1:9", seedCode: async () => undefined }),
      loadWorld: async () => undefined,
      waitHealthy: async () => undefined,
      readAutoPause: async () => true,
      setAutoPause: async () => undefined,
      sampleTickWindow: async (o: { warmupMs: number; durationMs: number }) => {
        windows.push(o.durationMs);
        t += o.warmupMs + o.durationMs;
        return { warmup: [], samples: [{ at: iso(t), value: 22 }], errors: 0, median: 22 };
      },
    } as unknown as CoreDeps;
    const captures: Record<string, unknown> = {};
    const ctx = { deps, captures, proc: { pid: 1, exited: new Promise(() => undefined) }, writeConfig: () => undefined, waitFor: async () => true, noteSecret: () => undefined, rememberAutoPause: () => undefined } as unknown as CoreContext;
    await spec.drive(ctx);
    return { windows, captures };
  }
  it("measures 24 hours with the mod, and tick-cost-native still 10 minutes", async () => {
    expect((await drive(soakNative)).windows).toEqual([SOAK_WINDOW_MS]);
    expect((await drive(tickCostNative)).windows).toEqual([TICK_COST_WINDOW_MS]);
  });
  it("records the measured window so the judge can find it", async () => {
    const parts = (await drive(soakNative)).captures["snapshot-parts.json"] as { measured: { from: string; to: string } };
    expect(Date.parse(parts.measured.to) - Date.parse(parts.measured.from)).toBe(TICK_WARMUP_MS + SOAK_WINDOW_MS);
  });
});

describe("snapshot watchdog (PROPOSAL: fail loudly when the backend host or server dies)", () => {
  const rec = (...ats: number[]) => ({ entries: ats.map((a) => ({ at: iso(a), agent: "mod", path: "/agent/v1/snapshots", status: 200 })) }) as unknown as Recorder;
  it("is quiet while snapshots keep being accepted", () => {
    expect(snapshotWatchdog(rec(START))(START + SOAK_STALL_MS)).toBeNull();
  });
  it("reports once none has been accepted for longer than the limit", () => {
    expect(snapshotWatchdog(rec(START))(START + SOAK_STALL_MS + 1000)).toMatch(/no snapshot was accepted for 15 minutes/);
  });
  it("ignores snapshots the backend refused", () => {
    const r = { entries: [{ at: iso(START), agent: "mod", path: "/agent/v1/snapshots", status: 503 }] } as unknown as Recorder;
    const w = snapshotWatchdog(r);
    expect(w(START)).toBeNull(); // counts from its first look when nothing was accepted
    expect(w(START + SOAK_STALL_MS + 1000)).not.toBeNull();
  });
});

describe("recorder: thinOlderSnapshots", () => {
  let servers: Server[] = [];
  afterEach(() => { for (const s of servers) s.close(); servers = []; });
  async function post(base: string, body: unknown) {
    await new Promise<void>((resolve, reject) => {
      const req = request(new URL("/agent/v1/snapshots", base), { method: "POST", headers: { "user-agent": "satisfactory-dash-mod/0.1.0", "content-type": "application/json" } }, (res) => (res.resume(), res.on("end", resolve)));
      req.on("error", reject);
      req.end(JSON.stringify(body));
    });
  }
  async function run(thin: boolean) {
    const up = createServer((_q, res) => void res.writeHead(200, { "content-type": "application/json" }).end("{}"));
    servers.push(up);
    await new Promise<void>((r) => up.listen(0, "127.0.0.1", () => r()));
    const rec = await startRecorder(`http://127.0.0.1:${(up.address() as AddressInfo).port}`, thin ? { thinOlderSnapshots: true } : undefined);
    for (const n of [1, 2, 3]) await post(rec.url, { observedAt: "x", reachable: true, players: { available: n !== 2, players: [] }, factory: { big: "y".repeat(1000) }, n });
    await rec.close();
    return rec.entries.map((e) => e.snapshot as Record<string, unknown>);
  }
  it("keeps only the newest body whole and the players.available flag of the others", async () => {
    const [a, b, c] = await run(true);
    expect(a).toEqual({ players: { available: true } });
    expect(b).toEqual({ players: { available: false } });
    expect(c).toMatchObject({ n: 3, factory: expect.anything() });
  });
  it("keeps every body whole when off (every other run)", async () => {
    const all = await run(false);
    expect(all.every((s) => s.factory !== undefined)).toBe(true);
  });
});

// ---- the judge ----
const PREFIX = "LogSatisDash: Display: ";
const stamp = (ms: number) => { const d = new Date(ms); const p = (n: number, w = 2) => String(n).padStart(w, "0"); return `[${d.getUTCFullYear()}.${p(d.getUTCMonth() + 1)}.${p(d.getUTCDate())}-${p(d.getUTCHours())}.${p(d.getUTCMinutes())}.${p(d.getUTCSeconds())}:${p(d.getUTCMilliseconds(), 3)}][  0]`; };
const scanLine = (ms: number) => `${stamp(ms)}${PREFIX}Resource node scan (game thread only): 180000 actors in 90 steps, 612 nodes, longest step 1.75 ms, total 41.50 ms`;
const FROM = START + TICK_WARMUP_MS;
const TO = START + TICK_WARMUP_MS + SOAK_WINDOW_MS;

const mem = (mbs: number[], stepMs = 30_000): TickCost["memoryWithMod"] => mbs.map((m, i) => ({ at: iso(TO - (mbs.length - 1 - i) * stepMs), workingSetBytes: m * MB }));
const series = (v: number[]) => ({ warmup: [], samples: v.map((value, i) => ({ at: iso(START + i * 10_000), value })), errors: 0, median: 22 });
const RECORDER = "127.0.0.1:13105";

function capture(over: Partial<TickCost> = {}): TickCost {
  return {
    baseline: series([22, 22, 22, 22]), withMod: series([22, 22, 22, 22]),
    outboundBaseline: ["203.0.113.7:443"], outboundWithMod: ["203.0.113.7:443", RECORDER],
    outboundSamples: { baseline: { ok: 21, failed: 0, dnsFailed: 0 }, withMod: { ok: 2880, failed: 0, dnsFailed: 0 } },
    memoryBaseline: [6000, 6005, 6010].map((m, i) => ({ at: iso(START + i * 30_000), workingSetBytes: m * MB })),
    memoryWithMod: mem([6040, 6042, 6044]),
    recorder: RECORDER, circuitGroups: 3, sessionBaseline: "s1", sessionWithMod: "s1",
    outboundNamesBaseline: { "203.0.113.7": ["api.example.com"] }, outboundNamesWithMod: { "203.0.113.7": ["api.example.com"] },
    ...over,
  };
}

/** Step lines every 30 min (the node scan's cadence) and a status snapshot every 5 s, across the whole window. */
function evidence(over: { lines?: string[]; posts?: unknown[]; errorLines?: string[]; cap?: TickCost; parts?: unknown } = {}): CoreEvidence {
  const scans: string[] = [];
  for (let t = START + 8 * MIN; t <= TO; t += 30 * MIN) scans.push(scanLine(t));
  const posts: unknown[] = [];
  for (let t = FROM; t <= TO; t += 5000) posts.push({ at: iso(t), status: 200, agent: "mod", path: "/agent/v1/snapshots" });
  posts.push({ at: iso(FROM + 20 * MIN), status: 200, agent: "mod", path: "/agent/v1/world/resourceNodes", world: { layer: "resourceNodes", items: 600, bytes: 60000 } });
  return {
    lines: over.lines ?? [`${stamp(START + 9000)}${PREFIX}Readers: players=native resourceNodes=native`, ...scans],
    errorLines: over.errorLines ?? [], leaks: { code: false, secret: false, bearer: false },
    captures: {
      "tick-cost.json": over.cap ?? capture(),
      "mod-config.json": NATIVE_SOURCES,
      "snapshot-parts.json": over.parts ?? { measured: { from: iso(START), to: iso(TO) }, snapshots: [{ at: iso(FROM + MIN), playersAvailable: true }] },
      "recorder.json": over.posts ?? posts,
    },
  };
}

describe("soak-native judge", () => {
  it("a clean day passes, with the proposed conditions labelled and every figure reported", () => {
    const base = Native.evaluate(evidence());
    expect(base.pass, base.reasons.join("\n")).toBe(true); // the fixture is a tick-cost-native pass
    const v = Soak.evaluate(evidence());
    expect(v.pass, v.reasons.join("\n")).toBe(true);
    const text = v.reasons.join("\n");
    expect(text).toMatch(/proposed, owner to confirm/);
    expect(text).toMatch(/Resource node scan 48 \(/); // 8 min, then every 30 min to the end: 48 reads
    expect(text).toMatch(/status snapshots 17\d{3} /);
    expect(text).toMatch(/resourceNodes 1/);
    expect(text).toMatch(/memory over the day.*slope/);
    expect(text).toMatch(/0 Error line\(s\)/);
  });

  it("keeps every tick-cost-native failure: a leak in the last 5 minutes still fails, unchanged", () => {
    const leak = capture({ memoryWithMod: mem([6040, 6060, 6080], 150_000) }); // +40 MB over the last 5 minutes, baseline +10 -> 30 more > 25
    const v = Soak.evaluate(evidence({ cap: leak }));
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toMatch(/over the last 5 minutes the with-mod working set grew/);
  });

  it("P1: a status silence of exactly 3 x 5 s passes, 20 s fails (cut(g) drops the posts in a g-long span, leaving a g+5 s gap)", () => {
    const full = evidence().captures["recorder.json"] as Array<{ at: string; path?: string }>;
    const cut = (gapMs: number) => full.filter((p) => p.path !== "/agent/v1/snapshots" || Date.parse(p.at) < START + 60 * MIN || Date.parse(p.at) >= START + 60 * MIN + gapMs);
    expect(Soak.evaluate(evidence({ posts: cut(10_000) })).pass).toBe(true); // 5 s post missing -> 10 s gap...
    expect(Soak.evaluate(evidence({ posts: cut(0) })).pass).toBe(true);
    const v = Soak.evaluate(evidence({ posts: cut(15_000) }));
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toMatch(/status snapshots went silent for 20\.0 s, more than 3 x its 5 s cadence/);
  });
});

describe("soak-native judge: the proposed conditions", () => {
  const snaps = (...ats: number[]) => ats.map((t) => ({ at: iso(t), status: 200, agent: "mod", path: "/agent/v1/snapshots" }));
  it("P1 silences: counts, the longest gap, and the window's edges", () => {
    const lines = [scanLine(FROM + 10 * MIN), scanLine(FROM + 40 * MIN), scanLine(FROM + 100 * MIN)];
    const s = Soak.silences(lines, ["Resource node scan"], snaps(FROM + 2000, FROM + 7000, FROM + 22_000), FROM, FROM + 105 * MIN);
    expect(s[0]).toMatchObject({ name: "Resource node scan", count: 3, longestGapMs: 60 * MIN, limitMs: 90 * MIN });
    // status: window start -> 2 s, 2 -> 7 s, 7 -> 22 s (15 s), then 22 s -> the end of the window (the long tail)
    expect(s[1]).toMatchObject({ name: "status snapshots", count: 3, limitMs: 15_000 });
    expect(s[1]!.longestGapMs).toBe(105 * MIN - 22_000);
  });
  it("P1 fails a gap just over 3 x cadence and passes exactly 3 x", () => {
    const at = (gap: number) => Soak.silenceProblems(Soak.silences([scanLine(FROM + MIN), scanLine(FROM + MIN + gap)], ["Resource node scan"], snaps(FROM, FROM + 5000), FROM, FROM + MIN + gap).filter((s) => s.name === "Resource node scan"));
    expect(at(90 * MIN)).toEqual([]);
    expect(at(90 * MIN + 1000).join()).toMatch(/Resource node scan went silent for .*more than 3 x its 1800 s cadence/);
  });
  it("P1 fails a reader that never read in the window, and a window with no snapshot at all", () => {
    const problems = Soak.silenceProblems(Soak.silences([], ["Factory read"], [], FROM, FROM + 10 * MIN));
    expect(problems.join("\n")).toMatch(/Factory read has no entry/);
    expect(problems.join("\n")).toMatch(/status snapshots has no entry/);
  });
  it("P1 uses the mod's cadences: 30 s for the factory-cadence readers, 600 s for the world layers", () => {
    expect(Soak.READ_CADENCE_S["Factory read"]).toBe(30);
    expect(Soak.READ_CADENCE_S["Buildables read"]).toBe(30);
    expect(Soak.READ_CADENCE_S["Map markers read"]).toBe(600);
    expect(Soak.READ_CADENCE_S["Resource node scan"]).toBe(1800);
  });
  it("P1 fails the whole verdict when the node scan goes quiet for 2 hours mid-day", () => {
    const lines = [`${stamp(START + 9000)}${PREFIX}Readers: players=native resourceNodes=native`];
    for (let t = START + 8 * MIN; t <= TO; t += 30 * MIN) if (t < START + 5 * 3600_000 || t > START + 7 * 3600_000) lines.push(scanLine(t));
    const v = Soak.evaluate(evidence({ lines }));
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toMatch(/soak \(proposed, owner to confirm\): Resource node scan went silent for \d+ s, more than 3 x its 1800 s cadence/);
  });
  it("P1 fails the verdict when the snapshots stop for the last stretch of the day", () => {
    const posts = (evidence().captures["recorder.json"] as Array<{ at: string; path?: string }>).filter((p) => p.path !== "/agent/v1/snapshots" || Date.parse(p.at) < TO - 10 * MIN);
    const v = Soak.evaluate(evidence({ posts }));
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toMatch(/status snapshots went silent for 605 s/);
  });
  it("P3: an Error line inside the window fails; one outside it is counted in the boot only", () => {
    const inside = `${stamp(START + 5 * 3600_000)}LogSatisDash: Error: world post failed`;
    const outside = `${stamp(START + 60_000)}LogSatisDash: Error: early`;
    const bad = Soak.evaluate(evidence({ errorLines: [inside] }));
    expect(bad.pass).toBe(false);
    expect(bad.reasons.join("\n")).toMatch(/1 Error line\(s\) from the mod in the window; the first: .*world post failed/);
    const early = Soak.evaluate(evidence({ errorLines: [outside] }));
    expect(early.reasons.join("\n")).toMatch(/0 Error line\(s\) \(1 in the whole boot\)/);
    expect(early.reasons.join("\n")).not.toMatch(/from the mod in the window/);
  });
  it("P3: Warning lines are counted, not judged", () => {
    const v = Soak.evaluate(evidence({ lines: [...evidence().lines, `${stamp(FROM + 5 * MIN)}LogSatisDash: Warning: slow`] }));
    expect(v.pass).toBe(true);
    expect(v.reasons.join("\n")).toMatch(/1 Warning line\(s\) \(report only\)/);
  });
  it("P2: the whole-day slope is reported and never fails by itself", () => {
    const rising = capture({ memoryWithMod: [0, 1, 2, 3].map((h, i) => ({ at: iso(FROM + h * 3600_000 * 8 + (i === 3 ? 0 : 0)), workingSetBytes: (6000 + h * 100) * MB })) });
    const text = Soak.memoryDay(rising.memoryWithMod);
    expect(text).toMatch(/4 samples over 24\.00 h, 6000\.0 MB at the end of the warmup, 6300\.0 MB at the end \(\+300\.0 MB\), peak 6300\.0 MB, least-squares slope \+12\.\d\d MB\/h/);
  });
  it("compacts a day of step lines into one entry per reader, and caps the diagnostics", () => {
    const text = Soak.evaluate(evidence()).reasons.join("\n");
    expect(text).toMatch(/step ceiling 2 ms held \(sliced native reads only, every logged read judged\): Resource node scan worst step 1\.75 ms over 48 read\(s\)/);
    expect(text.match(/longest step 1\.75 ms; Resource node/)).toBeNull();
    const many = Array.from({ length: 25 }, (_, i) => `diagnostic (report-only): x ${i}`);
    const out = Soak.compact(["a", ...many], []);
    expect(out.filter((r) => r.startsWith("diagnostic")).length).toBe(Soak.MAX_DIAGNOSTIC_LINES);
    expect(out.at(-1)).toMatch(/^5 more report-only/);
  });
  it("P2: fewer than 2 readable samples is said, not guessed", () => {
    expect(Soak.memoryDay([{ at: iso(FROM), workingSetBytes: null }])).toMatch(/fewer than 2 readable samples/);
  });
  it("a window that was not recorded cannot be judged", () => {
    const v = Soak.evaluate(evidence({ parts: { measured: null, snapshots: [] } }));
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toMatch(/measured window was not recorded/);
  });
});

describe("core-run.yml: the soak's timeout and option", () => {
  const wf = parse(readFileSync(fileURLToPath(new URL("../../.github/workflows/core-run.yml", import.meta.url)), "utf8")) as { on: { workflow_dispatch: { inputs: { run: { options: string[] } } } }; jobs: { run: { "timeout-minutes": unknown } } };
  it("offers soak-native", () => {
    expect(wf.on.workflow_dispatch.inputs.run.options).toContain("soak-native");
  });
  it("allows 26 hours for the soak only and keeps 90 minutes for every other run", () => {
    expect(wf.jobs.run["timeout-minutes"]).toBe("${{ inputs.run == 'soak-native' && 1560 || 90 }}");
    expect(1560).toBeGreaterThanOrEqual((SOAK_WINDOW_MS + TICK_WARMUP_MS) / MIN + 90); // the window and warmup, plus the 90 minutes every other run gets for build, boots and baseline
    expect(1560).toBeLessThan(5 * 24 * 60); // the self-hosted job limit is 5 days
  });
});
