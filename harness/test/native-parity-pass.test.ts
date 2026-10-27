import { readFileSync } from "node:fs";
import { describe, expect, it } from "vitest";
import { NATIVE_SOURCES, NATIVE_SOURCES_S2, NATIVE_SOURCES_S3 } from "../src/core-run";
import type { CoreEvidence } from "../src/core-run";
import type { CanonNode } from "../src/frm-native";
import * as Pass from "../../runs/native-parity/pass";

const T0 = Date.parse("2026-10-26T12:00:00.000Z");
const iso = (ms: number) => new Date(ms).toISOString();
const ms = (s: string) => Date.parse(s);
const group = (id: number, over: Partial<Record<string, number | boolean>> = {}) => ({
  groupId: id, production: 100, consumption: 80, capacity: 120, maxConsumption: 110, batteryCapacity: 0, batteryPercent: 0, batteryDifferential: 0, fuseTriggered: false, ...over,
});
const circuit = (id: number, over: Partial<Record<string, number | boolean>> = {}) => ({
  circuitGroupId: id, productionMW: 100, consumptionMW: 80, capacityMW: 120, maxConsumptionMW: 110, fuseTriggered: false, batteryCapacityMWh: 0, batteryPercent: 0, batteryDifferentialMW: 0, ...over,
});
const node = (x: number, over: Partial<CanonNode> = {}): CanonNode => ({ type: "Iron Ore", purity: "normal", nodeType: "node", x, y: 0, exploited: false, ...over });
// Synthetic hashes of synthetic names ("player-a", "player-b"); the judge only compares strings.
const KEYS = ["hash-a:true", "hash-b:false"];

function capture(): Pass.NativeParity {
  const rounds = Array.from({ length: Pass.NEED_ROUNDS + 5 }, (_, i) => iso(T0 + i * 5000));
  const nodes = [node(1), node(2), node(3, { exploited: true })];
  return {
    windowStartedAt: iso(T0), windowEndedAt: iso(T0 + 31 * 60_000), accepted: rounds.length, unrecorded: 0, refused: [],
    state: { codeConsumed: true, agentVersion: "mod/0.1.0", lastSeen: true, revoked: false },
    power: {
      rounds: rounds.map((observedAt) => ({ observedAt, circuits: [circuit(0), circuit(2, { batteryCapacityMWh: 10, batteryPercent: 50 })], before: [group(0), group(2, { batteryCapacity: 10, batteryPercent: 50 })], after: [group(0), group(2, { batteryCapacity: 10, batteryPercent: 50 })] })) as unknown as Pass.PowerRound[],
      frm: { reads: 100, failed: 0 }, mergedGroupsMax: 1,
    },
    players: {
      rounds: rounds.map((observedAt) => ({ observedAt, available: true, keys: [...KEYS] })),
      frmSamples: rounds.flatMap((o) => [{ startedAt: iso(ms(o) - 2000), endedAt: iso(ms(o) - 1900), value: [...KEYS] }, { startedAt: iso(ms(o) + 1000), endedAt: iso(ms(o) + 1100), value: [...KEYS] }]),
      frm: { reads: 100, failed: 0 },
    },
    nodes: {
      posts: [{ at: iso(T0 + 8 * 60_000), status: 200, data: nodes }],
      frmSamples: [{ startedAt: iso(T0 + 7 * 60_000), endedAt: iso(T0 + 7 * 60_000 + 500), value: "h1" }, { startedAt: iso(T0 + 9 * 60_000), endedAt: iso(T0 + 9 * 60_000 + 500), value: "h1" }],
      distinct: { h1: nodes }, frm: { reads: 2, failed: 0 },
    },
  };
}
// The mod's log lines as the evidence carries them: native readers (the Readers line and one completed scan), or a bridge-mode run.
const PREFIX = "[2026.10.26-12:00:05:123][  0]LogSatisDash: Display: ";
const NATIVE_LINES = [
  `${PREFIX}Readers: players=native resourceNodes=native`,
  `${PREFIX}Resource node scan (game thread only): 180000 actors in 90 steps, 612 nodes, longest step 1.75 ms, total 41.50 ms`,
];
const BRIDGE_LINES = [`${PREFIX}Readers: players=bridge resourceNodes=bridge`];
const evidence = (c: Pass.NativeParity, sources: unknown = NATIVE_SOURCES, lines: string[] = NATIVE_LINES): CoreEvidence =>
  ({ lines, errorLines: [], leaks: { code: false, secret: false, bearer: false }, captures: { "native-parity.json": c, "mod-config.json": sources } });

describe("native-parity judge", () => {
  it("passes a clean capture and names each reader", () => {
    const v = Pass.evaluate(evidence(capture()));
    expect(v.reasons.filter((r) => r.includes("FAIL"))).toEqual([]);
    expect(v.pass).toBe(true);
    expect(v.reasons.some((r) => r.startsWith("R0 power PASS"))).toBe(true);
    expect(v.reasons.some((r) => r.startsWith("R1 players PASS"))).toBe(true);
    expect(v.reasons.some((r) => r.startsWith("R4 resource nodes PASS"))).toBe(true);
  });

  it("fails players and resource nodes on a bridge-mode run whose values equal FRM's, power keeps its verdict", () => {
    const v = Pass.evaluate(evidence(capture(), NATIVE_SOURCES, BRIDGE_LINES));
    expect(v.pass).toBe(false);
    const text = v.reasons.join("\n");
    expect(text).toMatch(/R1 players FAIL: .*players=bridge/);
    expect(text).toMatch(/R4 resource nodes FAIL: .*resourceNodes=bridge/);
    expect(text).toMatch(/R4 resource nodes FAIL: .*no "Resource node scan \(game thread only\):" line/);
    expect(text).toMatch(/R0 power PASS/);
  });

  it("names the missing line: players need the Readers line, nodes need it and a scan line", () => {
    const none = Pass.evaluate(evidence(capture(), NATIVE_SOURCES, []));
    expect(none.reasons.join("\n")).toMatch(/R1 players FAIL: .*no "Readers:" line/);
    expect(none.reasons.join("\n")).toMatch(/R4 resource nodes FAIL: .*no "Readers:" line/);
    const noScan = Pass.evaluate(evidence(capture(), NATIVE_SOURCES, [NATIVE_LINES[0]!]));
    expect(noScan.pass).toBe(false);
    expect(noScan.reasons.join("\n")).toMatch(/R1 players PASS/);
    expect(noScan.reasons.join("\n")).toMatch(/R4 resource nodes FAIL: .*no "Resource node scan \(game thread only\):" line/);
  });

  it("fails power alone when a value is 5% off (the other readers keep their verdicts)", () => {
    const c = capture();
    c.power.rounds[10]!.circuits[0]!.productionMW = 105; // FRM says 100 either side; 3% of 100 is 3
    const v = Pass.judgeReaders(c);
    expect(v.power.pass).toBe(false);
    expect(v.power.reasons.join("\n")).toMatch(/group 0 productionMW mod 105 vs FRM 100/);
    expect(v.players.pass).toBe(true);
    expect(v.resourceNodes.pass).toBe(true);
    expect(Pass.evaluate(evidence(c)).pass).toBe(false);
  });

  it("fails players alone on one differing round", () => {
    const c = capture();
    c.players.rounds[3]!.keys = ["hash-a:true"];
    const v = Pass.judgeReaders(c);
    expect(v.players.pass).toBe(false);
    expect(v.power.pass && v.resourceNodes.pass).toBe(true);
  });

  it("lists node differences and fails resource nodes alone", () => {
    const c = capture();
    // Separate copies: the post loses node 3 and gains node 9; FRM's distinct list (h1) is left as it was.
    c.nodes.posts[0]!.data = [...(c.nodes.distinct.h1 as CanonNode[]).slice(0, 2), node(9)];
    const v = Pass.judgeReaders(c);
    expect(v.resourceNodes.pass).toBe(false);
    const text = v.resourceNodes.reasons.join("\n");
    expect(text).toContain("in FRM only");
    expect(text).toContain("in the mod only");
    expect(text).toContain("1 in FRM and not in the mod, 1 in the mod and not in FRM");
    expect(c.nodes.distinct.h1).toHaveLength(3);
    expect(v.power.pass && v.players.pass).toBe(true);
  });

  it("compares className: layers that differ only by className fail, and equal ones pass", () => {
    const withClass = (name: string) => (c: ReturnType<typeof capture>) => {
      c.nodes.posts[0]!.data = (c.nodes.posts[0]!.data as CanonNode[]).map((n) => ({ ...n, className: name }));
    };
    const same = capture();
    withClass("Desc_OreIron_C")(same);
    same.nodes.distinct.h1 = same.nodes.distinct.h1!.map((n) => ({ ...n, className: "Desc_OreIron_C" }));
    expect(Pass.judgeReaders(same).resourceNodes.pass).toBe(true);
    const differ = capture();
    withClass("Desc_OreIron_C")(differ);
    differ.nodes.distinct.h1 = differ.nodes.distinct.h1!.map((n) => ({ ...n, className: "Desc_OreCopper_C" }));
    const v = Pass.judgeReaders(differ);
    expect(v.resourceNodes.pass).toBe(false);
    expect(v.resourceNodes.reasons.join("\n")).toContain("Desc_OreIron_C");
  });

  it("fails resource nodes when the post is not a valid layer, or no post landed", () => {
    const bad = capture();
    bad.nodes.posts[0]!.data = [{ type: "x" }];
    expect(Pass.judgeReaders(bad).resourceNodes.reasons.join("\n")).toContain("not valid against the pinned contract");
    const none = capture();
    none.nodes.posts = [];
    expect(Pass.judgeReaders(none).resourceNodes.reasons.join("\n")).toContain("posted no resourceNodes layer");
  });

  it("needs enough compared power rounds", () => {
    const c = capture();
    c.power.rounds = c.power.rounds.slice(0, 10);
    expect(Pass.judgeReaders(c).power.reasons.join("\n")).toMatch(/compared in only 10 rounds, need 288/);
  });

  it("fails a reader whose FRM reads failed in the window", () => {
    const c = capture();
    c.power.frm.failed = 1;
    c.players.frm.failed = 1;
    const v = Pass.judgeReaders(c);
    expect(v.power.reasons.join("\n")).toContain("getPower reads in the window failed");
    expect(v.players.reasons.join("\n")).toContain("getPlayer reads in the window failed");
  });

  it("reports fields the world never exercised, without failing", () => {
    const c = capture();
    for (const r of c.power.rounds) {
      r.circuits = [circuit(0)];
      r.before = [group(0)];
      r.after = [group(0)];
    }
    c.power.mergedGroupsMax = 0;
    for (const r of c.players.rounds) r.keys = [];
    c.players.frmSamples = c.players.frmSamples.map((s) => ({ ...s, value: [] }));
    c.nodes.posts[0]!.data = (c.nodes.posts[0]!.data as CanonNode[]).map((n) => ({ ...n, exploited: false }));
    c.nodes.distinct.h1 = c.nodes.distinct.h1!.map((n) => ({ ...n, exploited: false }));
    const v = Pass.judgeReaders(c);
    expect(v.power.pass && v.players.pass && v.resourceNodes.pass).toBe(true);
    expect(v.power.reasons.join("\n")).toContain("NOT EXERCISED");
    expect(v.players.reasons.join("\n")).toContain("NOT EXERCISED");
    expect(v.resourceNodes.reasons.join("\n")).toContain("NOT EXERCISED");
  });

  it("is red when the run did not put the native sources in the mod's config, whatever the readers say", () => {
    expect(Pass.evaluate(evidence(capture(), { playersSource: "bridge", resourceNodesSource: "native" })).pass).toBe(false);
    expect(Pass.evaluate(evidence(capture(), null)).pass).toBe(false);
  });

  it("is red when the window is short or the mod's posts were refused", () => {
    const short = capture();
    short.windowEndedAt = iso(T0 + 10 * 60_000);
    expect(Pass.evaluate(evidence(short)).pass).toBe(false);
    const refused = capture();
    refused.refused = [{ at: iso(T0), status: 400 }];
    expect(Pass.evaluate(evidence(refused)).pass).toBe(false);
  });

  it("keeps the harness's key names equal to the mod's", () => {
    const cpp = readFileSync(new URL("../../mod/SatisDashAgent/Source/SatisDashCore/Private/Config.cpp", import.meta.url), "utf8");
    for (const key of Object.keys(NATIVE_SOURCES)) expect(cpp).toContain(`TEXT("${key}")`);
  });

  it("writes four native keys for S2, leaves S1's two unchanged, and every key is one the mod reads", () => {
    expect(NATIVE_SOURCES).toEqual({ playersSource: "native", resourceNodesSource: "native" });
    expect(NATIVE_SOURCES_S2).toEqual({ playersSource: "native", resourceNodesSource: "native", factorySource: "native", trainsSource: "native" });
    const cpp = readFileSync(new URL("../../mod/SatisDashAgent/Source/SatisDashCore/Private/Config.cpp", import.meta.url), "utf8");
    for (const key of Object.keys(NATIVE_SOURCES_S2)) expect(cpp).toContain(`TEXT("${key}")`);
  });

  it("writes six native keys for S3 (S2's four plus railsSource and conveyorsSource), and every key is one the mod reads", () => {
    expect(NATIVE_SOURCES_S3).toEqual({ ...NATIVE_SOURCES_S2, railsSource: "native", conveyorsSource: "native" });
    expect(NATIVE_SOURCES).toEqual({ playersSource: "native", resourceNodesSource: "native" });
    expect(NATIVE_SOURCES_S2).toEqual({ playersSource: "native", resourceNodesSource: "native", factorySource: "native", trainsSource: "native" });
    const cpp = readFileSync(new URL("../../mod/SatisDashAgent/Source/SatisDashCore/Private/Config.cpp", import.meta.url), "utf8");
    for (const key of Object.keys(NATIVE_SOURCES_S3)) expect(cpp).toContain(`TEXT("${key}")`);
  });
});
