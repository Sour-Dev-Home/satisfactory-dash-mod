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
