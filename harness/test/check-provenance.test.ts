import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { main } from "../src/cli";
import { checkProvenance } from "../src/check-provenance";

const dirs: string[] = [];
const repo = () => {
  const d = mkdtempSync(join(tmpdir(), "prov-"));
  dirs.push(d);
  return d;
};
afterEach(() => dirs.splice(0).forEach((d) => rmSync(d, { recursive: true, force: true })));

const put = (root: string, path: string, text = "x") => {
  mkdirSync(dirname(join(root, path)), { recursive: true });
  writeFileSync(join(root, path), text);
};

const good = (reader: string, code = "mod/Core/Private/Native/Players/Players.cpp") =>
  [
    `Reader: ${reader}`,
    `Code: ${code}`,
    "Headers: Source/FactoryGame/Public/FGGameState.h:519",
    "Survey: mod-native-readers-s1-headers-2026-10-25.md",
    "Captures: frm-getPlayer-2026-09-27-full.json (HTTP output only)",
    "Author: a subagent",
    "Date: 2026-10-26",
    "",
    "FRM source was not opened",
    "",
  ].join("\n");

describe("checkProvenance", () => {
  it("passes a reader whose directory holds a complete PROVENANCE.md, and counts it", () => {
    const r = repo();
    put(r, "mod/Core/Private/Native/Players/Players.cpp");
    put(r, "mod/Core/Private/Native/Players/PROVENANCE.md", good("Players"));
    expect(checkProvenance(r)).toEqual({ readers: ["Players"], problems: [] });
  });

  it("fails a reader directory without the file", () => {
    const r = repo();
    put(r, "mod/Core/Private/Native/Nodes/Nodes.cpp");
    const out = checkProvenance(r);
    expect(out.readers).toEqual(["Nodes"]);
    expect(out.problems).toEqual(["native reader Nodes (mod/Core/Private/Native/Nodes): no PROVENANCE.md"]);
  });

  it("accepts one file for a reader that has a Public and a Private directory", () => {
    const r = repo();
    put(r, "mod/Core/Public/SatisDash/Native/Players/Players.h");
    put(r, "mod/Core/Private/Native/Players/Players.cpp");
    put(r, "mod/Core/Private/Native/Players/PROVENANCE.md", good("Players"));
    expect(checkProvenance(r).problems).toEqual([]);
  });

  it("fails two files for one reader", () => {
    const r = repo();
    put(r, "mod/A/Native/Players/PROVENANCE.md", good("Players", "mod/A/Native/Players/PROVENANCE.md"));
    put(r, "mod/B/Native/Players/PROVENANCE.md", good("Players", "mod/A/Native/Players/PROVENANCE.md"));
    expect(checkProvenance(r).problems[0]).toMatch(/more than one PROVENANCE\.md/);
  });

  it("fails a loose file directly in a Native directory", () => {
    const r = repo();
    put(r, "mod/Core/Private/Native/Loose.cpp");
    expect(checkProvenance(r).problems).toEqual(["mod/Core/Private/Native/Loose.cpp: a file directly in a Native directory; put it in Native/<Reader>/"]);
  });

  it("names every defect in the file", () => {
    const r = repo();
    put(r, "mod/Core/Native/Players/PROVENANCE.md", "Reader: Other\nCode: mod/missing.cpp\nDate: yesterday\n");
    const problems = checkProvenance(r).problems.join("\n");
    expect(problems).toContain('Reader is "Other", the directory is "Players"');
    expect(problems).toContain("Code path mod/missing.cpp does not exist");
    expect(problems).toContain('Date "yesterday" is not YYYY-MM-DD');
    expect(problems).toContain('missing "Headers:" line');
    expect(problems).toContain('missing the line "FRM source was not opened"');
  });

  it("ignores Native directories outside mod/ and a repository with no mod/ at all", () => {
    const r = repo();
    put(r, "harness/Native/Thing/x.ts");
    expect(checkProvenance(r)).toEqual({ readers: [], problems: [] });
  });
});

describe("check-provenance command", () => {
  it("reports the counts and exits 0 on the real repository", async () => {
    const lines: string[] = [];
    const code = await main(["check-provenance"], { log: (s) => lines.push(s), error: (s) => lines.push(s) });
    expect(lines.at(-1)).toMatch(/^check-provenance: \d+ native readers, 0 problems$/);
    expect(code).toBe(0);
  });
});
