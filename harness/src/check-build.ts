// Judges build-mod.ps1 -Platforms Win64+Linux's output (plan 2026-11-06-linux-server-build, Task 3): each server
// platform's tree has the plugin, a library for each shipped module and no test code, and both carry one version.
import { existsSync, readdirSync, readFileSync } from "node:fs";
import { join, relative } from "node:path";

const PLATFORMS = [
  { tree: "WindowsServer", binaries: "Binaries/Win64", ext: ".dll" },
  { tree: "LinuxServer", binaries: "Binaries/Linux", ext: ".so" },
] as const;
const MODULES = ["SatisDashCore", "SatisDashGame"] as const;
const PLUGIN = "SatisDashAgent.uplugin";

const filesUnder = (dir: string): string[] =>
  existsSync(dir) ? readdirSync(dir, { recursive: true, withFileTypes: true }).filter((e) => e.isFile()).map((e) => join(e.parentPath, e.name)) : [];

/** The problems found under root (root/WindowsServer, root/LinuxServer); empty means the build is good. */
export function checkBuild(root: string): string[] {
  const problems: string[] = [];
  const versions: string[] = [];
  for (const p of PLATFORMS) {
    const tree = join(root, p.tree);
    const uplugin = join(tree, PLUGIN);
    if (!existsSync(uplugin)) {
      problems.push(`${p.tree}: no ${PLUGIN}`);
      continue;
    }
    versions.push(`${p.tree} ${(JSON.parse(readFileSync(uplugin, "utf8")) as { SemVersion?: string }).SemVersion}`);
    const files = filesUnder(tree).map((f) => relative(tree, f).replaceAll("\\", "/"));
    for (const m of MODULES) {
      // A module's library is named after it (FactoryServer-SatisDashGame-Win64-Shipping.dll); "-" after the name
      // keeps SatisDashCoreTests from counting as SatisDashCore.
      if (!files.some((f) => f.startsWith(`${p.binaries}/`) && f.endsWith(p.ext) && f.includes(`${m}-`))) problems.push(`${p.tree}: no ${m} library (${p.ext})`);
    }
    for (const f of files.filter((f) => f.includes("SatisDashCoreTests"))) problems.push(`${p.tree}: test code shipped: ${f}`);
  }
  if (versions.length === PLATFORMS.length && new Set(versions.map((v) => v.split(" ")[1])).size > 1) {
    problems.push(`the platforms' versions differ: ${versions.join(", ")}`);
  }
  return problems;
}
