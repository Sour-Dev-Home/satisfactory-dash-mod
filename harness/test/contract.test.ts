import { mkdtempSync, mkdirSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { relativeImports, sha256, vendorContract, verifyContract } from "../src/contract";
import { checkSnapshot } from "../src/snapshot-check";

const SRC: Record<string, string> = {
  "packages/shared/src/agent.ts": 'import { z } from "zod";\nimport { A } from "./ids";\nimport { B } from "./status";\n',
  "packages/shared/src/status.ts": 'import { E } from "./envelope";\n',
  "packages/shared/src/envelope.ts": 'import { A } from "./ids";\n',
  "packages/shared/src/ids.ts": 'import { z } from "zod";\n',
};
const fetchFile = async (path: string) => {
  const text = SRC[path];
  if (text === undefined) throw new Error(`404 ${path}`);
  return text;
};
const COMMIT = "a".repeat(40);

let dir = "";
afterEach(() => dir && rmSync(dir, { recursive: true, force: true }));

function writeVendored(files: Record<string, string>, manifestFiles = files) {
  dir = mkdtempSync(join(tmpdir(), "contract-"));
  mkdirSync(join(dir, "src"));
  for (const [n, t] of Object.entries(files)) writeFileSync(join(dir, "src", n), t);
  const hashes = Object.fromEntries(Object.entries(manifestFiles).map(([n, t]) => [n, sha256(t)]));
  writeFileSync(join(dir, "manifest.json"), JSON.stringify({ repo: "r", dir: "d", commit: COMMIT, files: hashes }));
  return dir;
}

describe("relativeImports", () => {
  it("lists same-folder imports as .ts file names", () => {
    expect(relativeImports(SRC["packages/shared/src/agent.ts"]!)).toEqual(["ids.ts", "status.ts"]);
  });
});

describe("vendorContract", () => {
  it("follows the import closure from agent.ts and hashes each file", async () => {
    const { files, manifest } = await vendorContract(COMMIT, fetchFile);
    expect(Object.keys(files).sort()).toEqual(["agent.ts", "envelope.ts", "ids.ts", "status.ts"]);
    expect(manifest.commit).toBe(COMMIT);
    expect(manifest.files["ids.ts"]).toBe(sha256(SRC["packages/shared/src/ids.ts"]!));
  });

  it("refuses an import from outside the folder", async () => {
    const bad = async () => 'import { X } from "../other/x";\n';
    await expect(vendorContract(COMMIT, bad)).rejects.toThrow(/outside/);
  });
});

describe("verifyContract", () => {
  const files = { "a.ts": 'import { B } from "./b";\n', "b.ts": "export const B = 1;\n" };

  it("is clean when files match the manifest", () => {
    expect(verifyContract(writeVendored(files))).toEqual([]);
  });

  it("reports a changed file", () => {
    expect(verifyContract(writeVendored({ ...files, "b.ts": "export const B = 2;\n" }, files))).toEqual([
      `b.ts: changed since ${COMMIT.slice(0, 7)}`,
    ]);
  });

  it("reports a missing file, an extra file and an unvendored import", () => {
    const problems = verifyContract(
      writeVendored({ "a.ts": 'import { C } from "./c";\n', "x.ts": "" }, { "a.ts": 'import { C } from "./c";\n', "b.ts": "" }),
    );
    expect(problems.sort()).toEqual(["a.ts: imports c.ts, which is not vendored", "b.ts: missing", "x.ts: not in the manifest"]);
  });

  it("the committed copy matches satisfactory-dash ab4626f", () => {
    expect(verifyContract(join(import.meta.dirname, "..", "contract"))).toEqual([]);
  });
});

describe("checkSnapshot", () => {
  const minimal = { agentVersion: "mod/0.0", observedAt: "2026-10-19T16:00:00.000Z", reachable: false, paused: null };

  it("accepts the minimal unreachable snapshot", () => {
    expect(checkSnapshot(minimal)).toEqual({ ok: true });
  });

  it("refuses an unknown key (the schema is strict)", () => {
    const r = checkSnapshot({ ...minimal, extra: 1 });
    expect(r.ok).toBe(false);
  });

  it("refuses a missing agentVersion and names the path", () => {
    const { agentVersion: _, ...rest } = minimal;
    const r = checkSnapshot(rest);
    expect(r.ok === false && r.issues.some((i) => i.startsWith("agentVersion:"))).toBe(true);
  });
});

describe("vendored contract (S4-A: mapMarkers, droneRoutes, powerLines, pairedId, status fields)", () => {
  it("map.ts exports the three new layer schemas and all seven world layers", async () => {
    const map = await import("../contract/src/map");
    expect(map.MapMarkersLayerDataSchema).toBeDefined();
    expect(map.DroneRoutesLayerDataSchema).toBeDefined();
    expect(map.PowerLinesLayerDataSchema).toBeDefined();
    expect([...map.KNOWN_MAP_WORLD_LAYERS].sort()).toEqual(
      ["conveyors", "droneRoutes", "mapMarkers", "powerLines", "rails", "resourceNodes", "trainRoutes"].sort(),
    );
  });

  it("DronePortSchema accepts pairedId null, a string, and absent", async () => {
    const { DronePortSchema } = await import("../contract/src/map");
    const port = { id: "p1", name: "Port", paired: null, input: [], output: [], fuel: [], x: 0, y: 0 };
    expect(DronePortSchema.safeParse(port).success).toBe(true);
    expect(DronePortSchema.safeParse({ ...port, pairedId: null }).success).toBe(true);
    expect(DronePortSchema.safeParse({ ...port, paired: "Other", pairedId: "p2" }).success).toBe(true);
    expect(DronePortSchema.safeParse({ ...port, pairedId: 7 }).success).toBe(false);
  });

  it("SnapshotRequestSchema takes the three top-level status fields, optionally", async () => {
    const { SnapshotRequestSchema } = await import("../contract/src/agent");
    const base = { agentVersion: "mod/0.0", observedAt: "2026-10-19T16:00:00.000Z", reachable: false, paused: null };
    expect(SnapshotRequestSchema.safeParse(base).success).toBe(true);
    const full = { ...base, gameReachable: false, lastGameReadAt: "2026-10-19T15:59:00.000Z", modVersion: "1.2.3" };
    expect(SnapshotRequestSchema.safeParse(full).success).toBe(true);
    expect(SnapshotRequestSchema.safeParse({ ...full, modVersion: "" }).success).toBe(false);
    expect(SnapshotRequestSchema.safeParse({ ...full, modVersion: "v".repeat(65) }).success).toBe(false);
  });
});

describe("vendored contract (S4-B: extractors)", () => {
  const miner = {
    id: "m1",
    name: "Miner Mk.1",
    className: "Build_MinerMk1_C",
    recipe: null,
    ingredients: [],
    production: [{ name: "Iron Ore", className: "Desc_OreIron_C", currentPerMinute: 60, maxPerMinute: 60, percent: 100 }],
    isProducing: true,
    isPaused: false,
    isBackedUp: false,
  };

  it("AGENT_INPUT_MAX_EXTRACTORS is 5000", async () => {
    const agent = await import("../contract/src/agent");
    expect(agent.AGENT_INPUT_MAX_EXTRACTORS).toBe(5000);
  });

  it("AgentFactorySchema accepts one extractor, and the key absent", async () => {
    const { AgentFactorySchema } = await import("../contract/src/agent");
    expect(AgentFactorySchema.safeParse({ buildings: [], extractors: [miner] }).success).toBe(true);
    expect(AgentFactorySchema.safeParse({ buildings: [] }).success).toBe(true);
  });

  it("AgentFactorySchema rejects 5001 extractors", async () => {
    const { AgentFactorySchema } = await import("../contract/src/agent");
    const extractors = Array.from({ length: 5001 }, (_, i) => ({ ...miner, id: `m${i}` }));
    expect(AgentFactorySchema.safeParse({ buildings: [], extractors }).success).toBe(false);
  });
});

describe("vendored contract (S4-B: generators)", () => {
  const generator = {
    id: "g1",
    name: "Coal-Powered Generator",
    className: "Build_GeneratorCoal_C",
    currentMW: 75,
    capacityMW: 75,
    isProducing: true,
    fuel: { className: "Desc_Coal_C", name: "Coal", amount: 12 },
  };

  it("AGENT_INPUT_MAX_GENERATORS is 5000", async () => {
    const agent = await import("../contract/src/agent");
    expect(agent.AGENT_INPUT_MAX_GENERATORS).toBe(5000);
  });

  it("AgentPowerGeneratorSchema takes the full field set, fuel null, supplement absent or null", async () => {
    const { AgentPowerGeneratorSchema: S } = await import("../contract/src/agent");
    const full = {
      ...generator,
      location: { xM: 1, yM: 2, zM: 3, rotationDeg: 90 },
      circuitGroupId: 1,
      clockSpeedPercent: 100,
      supplement: { className: "Desc_Water_C", name: "Water", amount: 5 },
    };
    expect(S.safeParse(full).success).toBe(true);
    expect(S.safeParse({ ...generator, fuel: null }).success).toBe(true);
    expect(S.safeParse(generator).success).toBe(true);
    expect(S.safeParse({ ...generator, supplement: null }).success).toBe(true);
  });

  it("AgentPowerGeneratorSchema rejects an omitted fuel and a 201-character className", async () => {
    const { AgentPowerGeneratorSchema: S } = await import("../contract/src/agent");
    const { fuel: _fuel, ...noFuel } = generator;
    expect(S.safeParse(noFuel).success).toBe(false);
    expect(S.safeParse({ ...generator, className: "x".repeat(201) }).success).toBe(false);
    expect(S.safeParse({ ...generator, className: "x".repeat(200) }).success).toBe(true);
  });

  it("AgentPowerSchema accepts generators, and the key absent; rejects 5001", async () => {
    const { AgentPowerSchema } = await import("../contract/src/agent");
    expect(AgentPowerSchema.safeParse({ circuits: [], generators: [generator] }).success).toBe(true);
    expect(AgentPowerSchema.safeParse({ circuits: [] }).success).toBe(true);
    const generators = Array.from({ length: 5001 }, (_, i) => ({ ...generator, id: `g${i}` }));
    expect(AgentPowerSchema.safeParse({ circuits: [], generators }).success).toBe(false);
  });
});
