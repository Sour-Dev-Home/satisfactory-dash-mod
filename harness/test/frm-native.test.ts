import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { ResourceNodesLayerDataSchema } from "../contract/src/map";
import { canonNodes, countMergedGroups, mapFrmResourceNodes, mapNodeType, playerKeys, startSampler } from "../src/frm-native";
import { sessionHash } from "../src/status";

const FIXTURES = new URL("../../mod/SatisDashAgent/Source/SatisDashCoreTests/Fixtures/", import.meta.url);
const load = (name: string) => JSON.parse(readFileSync(fileURLToPath(new URL(name, FIXTURES)), "utf8"));

describe("mapFrmResourceNodes", () => {
  it("reproduces the bridge's golden from the bridge's FRM capture (the harness oracle and the mod agree on the form)", () => {
    expect(mapFrmResourceNodes(load("frm-getResourceNode.json"))).toEqual(load("frm-getResourceNode.expected.json"));
  });

  it("produces a layer the pinned contract accepts", () => {
    const r = ResourceNodesLayerDataSchema.safeParse(mapFrmResourceNodes(load("frm-getResourceNode.json")));
    expect(r.success ? [] : r.error.issues).toEqual([]);
  });

  it("refuses a record that is not what the Node agent's schema accepts", () => {
    expect(() => mapFrmResourceNodes([{ Name: "x", Purity: "Pure", NodeType: "Node", Exploited: "no", location: { x: 0, y: 0 } }])).toThrow();
  });

  it("corrects FRM's 'Inpure' to the contract's 'impure' and lowercases the rest", () => {
    const node = (Purity: string) => ({ Name: "Iron Ore", Purity, NodeType: "Node", Exploited: false, location: { x: 0, y: 0 } });
    expect(mapFrmResourceNodes([node("Inpure"), node("Impure"), node("Normal"), node("Pure")]).map((n) => n.purity)).toEqual(["impure", "impure", "normal", "pure"]);
  });

  it("passes FRM's ClassName through as className and omits the key when it is absent or empty", () => {
    const node = (extra: object) => ({ Name: "Iron Ore", Purity: "Pure", NodeType: "Node", Exploited: false, location: { x: 0, y: 0 }, ...extra });
    const out = mapFrmResourceNodes([node({ ClassName: "Desc_OreIron_C" }), node({}), node({ ClassName: "" })]);
    expect(out[0]!.className).toBe("Desc_OreIron_C");
    expect("className" in out[1]!).toBe(false);
    expect("className" in out[2]!).toBe(false);
  });

  it("the contract keeps className (the vendored schema does not strip it)", () => {
    const r = ResourceNodesLayerDataSchema.parse([{ type: "Iron Ore", purity: "pure", nodeType: "node", x: 0, y: 0, exploited: false, className: "Desc_OreIron_C" }]);
    expect(r[0]!.className).toBe("Desc_OreIron_C");
  });

  it("drops a node past 1,000,000 m and never yields -0", () => {
    const out = mapFrmResourceNodes([
      { Name: "far", Purity: "Pure", NodeType: "Node", Exploited: false, location: { x: 1e12, y: 0 } },
      { Name: "half", Purity: "Pure", NodeType: "Node", Exploited: false, location: { x: -50, y: 50 } },
    ]);
    expect(out).toHaveLength(1);
    expect(Object.is(out[0]!.x, 0)).toBe(true);
    expect(out[0]!.y).toBe(1);
  });
});

describe("mapNodeType", () => {
  it("names the three known kinds and camel-cases the rest on JavaScript's \\s", () => {
    expect(mapNodeType("Node")).toBe("node");
    expect(mapNodeType("Fracking Satellite")).toBe("frackingSatellite");
    expect(mapNodeType("Geyser")).toBe("geyser");
    expect(mapNodeType("Some New TYPE")).toBe("someNewType");
    expect(mapNodeType("some  new   TYPE")).toBe("someNewType");
  });
});

describe("canonNodes", () => {
  it("is order-independent", () => {
    const a = { type: "A", purity: "pure", nodeType: "node", x: 1, y: 2, exploited: false };
    const b = { ...a, x: 0 };
    expect(canonNodes([a, b])).toEqual(canonNodes([b, a]));
  });

  it("differs when only className differs, and treats an absent className as empty", () => {
    const a = { type: "A", purity: "pure", nodeType: "node", x: 1, y: 2, exploited: false };
    expect(canonNodes([{ ...a, className: "Desc_OreIron_C" }])).not.toEqual(canonNodes([a]));
    expect(canonNodes([{ ...a, className: "Desc_OreIron_C" }])).not.toEqual(canonNodes([{ ...a, className: "Desc_OreCopper_C" }]));
    expect(canonNodes([{ ...a, className: "" }])).toEqual(canonNodes([a]));
  });
});

describe("playerKeys", () => {
  it("keeps a hash and online, never the name, sorted", () => {
    const keys = playerKeys([{ Name: "player-b", Online: false, Location: { x: 1 } }, { Name: "player-a", Online: true }]);
    expect(keys).toEqual([`${sessionHash("player-a")}:true`, `${sessionHash("player-b")}:false`].sort());
    expect(keys.join()).not.toContain("player-");
  });
  it("refuses a record with no Online", () => {
    expect(() => playerKeys([{ Name: "player-a" }])).toThrow();
  });
});

describe("countMergedGroups", () => {
  it("counts groups with more than one circuit", () => {
    expect(countMergedGroups([{ AssociatedCircuits: [0, 3] }, { AssociatedCircuits: [1] }])).toBe(1);
  });
});

describe("startSampler", () => {
  it("keeps successful reads with their times and counts a failed read without keeping it", async () => {
    let t = 1000;
    let n = 0;
    const s = startSampler(
      async () => {
        n++;
        if (n === 2) throw new Error("down");
        return n;
      },
      { intervalMs: 10, now: () => (t += 5), sleep: () => new Promise<void>(() => undefined) },
    );
    await Promise.resolve();
    await s.take(); // the failed read
    await s.take();
    s.stop();
    expect(s.samples.map((x) => x.value)).toEqual([1, 3]);
    expect(s.samples[0]!.endedAt).toBeGreaterThan(s.samples[0]!.startedAt);
    expect(s.counts()).toEqual({ reads: 2, failed: 1 });
  });
});
