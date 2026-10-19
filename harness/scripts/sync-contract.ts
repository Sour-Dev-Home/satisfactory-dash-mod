import { execFileSync } from "node:child_process";
import { mkdirSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { CONTRACT_REPO, vendorContract } from "../src/contract";

const at = process.argv.indexOf("--commit");
const commit = at === -1 ? undefined : process.argv[at + 1];
if (!commit || !/^[0-9a-f]{40}$/.test(commit)) {
  console.error("usage: npm run sync-contract -- --commit <40-hex satisfactory-dash sha>");
  process.exit(2);
}
const fetchFile = async (path: string, ref: string) =>
  execFileSync("gh", ["api", "-H", "Accept: application/vnd.github.raw+json", `repos/${CONTRACT_REPO}/contents/${path}?ref=${ref}`], {
    encoding: "utf8",
    maxBuffer: 16 * 1024 * 1024,
  });
const { files, manifest } = await vendorContract(commit, fetchFile);
const dir = join(import.meta.dirname, "..", "contract");
rmSync(join(dir, "src"), { recursive: true, force: true });
mkdirSync(join(dir, "src"), { recursive: true });
for (const [name, text] of Object.entries(files)) writeFileSync(join(dir, "src", name), text);
writeFileSync(join(dir, "manifest.json"), `${JSON.stringify(manifest, null, 2)}\n`);
console.log(`vendored ${Object.keys(files).length} files at ${commit.slice(0, 7)}`);
