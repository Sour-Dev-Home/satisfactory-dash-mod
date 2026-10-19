import { createHash } from "node:crypto";
import { existsSync, readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { z } from "zod";

export const CONTRACT_REPO = "Sour-Dev-Home/satisfactory-dash";
export const CONTRACT_DIR = "packages/shared/src";
export const CONTRACT_ENTRY = "agent.ts";

export const ManifestSchema = z.strictObject({
  repo: z.string(),
  dir: z.string(),
  commit: z.string().regex(/^[0-9a-f]{40}$/),
  files: z.record(z.string(), z.string().regex(/^[0-9a-f]{64}$/)),
});
export type Manifest = z.infer<typeof ManifestSchema>;

export const sha256 = (text: string) => createHash("sha256").update(text, "utf8").digest("hex");

/** The same-folder modules a file imports, as file names ("./envelope" -> "envelope.ts"). */
export function relativeImports(text: string): string[] {
  const out = new Set<string>();
  for (const m of text.matchAll(/from\s+"\.\/([\w-]+)(?:\.(?:js|ts))?"/g)) out.add(`${m[1]}.ts`);
  return [...out].sort();
}

export async function vendorContract(
  commit: string,
  fetchFile: (path: string, commit: string) => Promise<string>,
): Promise<{ files: Record<string, string>; manifest: Manifest }> {
  const files: Record<string, string> = {};
  const queue = [CONTRACT_ENTRY];
  while (queue.length > 0) {
    const name = queue.shift()!;
    if (name in files) continue;
    const text = await fetchFile(`${CONTRACT_DIR}/${name}`, commit);
    if (/from\s+"\.\.\//.test(text)) throw new Error(`${name} imports from outside ${CONTRACT_DIR}`);
    files[name] = text;
    queue.push(...relativeImports(text).filter((f) => !(f in files)));
  }
  const hashes = Object.fromEntries(Object.keys(files).sort().map((f) => [f, sha256(files[f]!)]));
  return { files, manifest: ManifestSchema.parse({ repo: CONTRACT_REPO, dir: CONTRACT_DIR, commit, files: hashes }) };
}

/** Problems with the vendored copy in dir (manifest.json + src/). Empty means it is exactly the pinned commit's files. */
export function verifyContract(dir: string): string[] {
  const manifest = ManifestSchema.parse(JSON.parse(readFileSync(join(dir, "manifest.json"), "utf8")));
  const problems: string[] = [];
  for (const [name, hash] of Object.entries(manifest.files)) {
    const path = join(dir, "src", name);
    if (!existsSync(path)) {
      problems.push(`${name}: missing`);
      continue;
    }
    const text = readFileSync(path, "utf8");
    if (sha256(text) !== hash) problems.push(`${name}: changed since ${manifest.commit.slice(0, 7)}`);
    for (const dep of relativeImports(text)) {
      if (!(dep in manifest.files)) problems.push(`${name}: imports ${dep}, which is not vendored`);
    }
  }
  for (const f of readdirSync(join(dir, "src"))) if (!(f in manifest.files)) problems.push(`${f}: not in the manifest`);
  return problems;
}
