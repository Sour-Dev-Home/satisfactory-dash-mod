// ADR-0058 Decision 6, rule 5 (with the owner's answer 4: no similarity job; this and the clean-room rules are the control).
// Every native reader is a directory `Native/<Reader>/` under mod/. Each reader has exactly one PROVENANCE.md, in any of
// its directories, with the fields below and the line "FRM source was not opened". docs/clean-room.md says how to fill it.
import { existsSync, readdirSync, readFileSync, statSync } from "node:fs";
import { join, relative, sep } from "node:path";

export const PROVENANCE_FILE = "PROVENANCE.md";
export const NATIVE_DIR = "Native";
export const NO_FRM_LINE = "FRM source was not opened";
const SKIP = new Set([".git", "node_modules", "Intermediate", "Binaries", "Saved"]);
const FIELDS = ["Reader", "Code", "Headers", "Survey", "Captures", "Author", "Date"] as const;

export interface ProvenanceReport {
  readers: string[];
  problems: string[];
}

function findNativeDirs(dir: string, out: string[]): void {
  for (const name of readdirSync(dir)) {
    if (SKIP.has(name)) continue;
    const path = join(dir, name);
    if (!statSync(path).isDirectory()) continue;
    if (name === NATIVE_DIR) out.push(path);
    findNativeDirs(path, out);
  }
}

function checkFile(repoRoot: string, reader: string, file: string, shown: string): string[] {
  const lines = readFileSync(file, "utf8").split(/\r?\n/);
  const out: string[] = [];
  const values = (field: string) => lines.filter((l) => l.startsWith(`${field}:`)).map((l) => l.slice(field.length + 1).trim());
  for (const f of FIELDS) if (!values(f).some((v) => v.length > 0)) out.push(`${shown}: missing "${f}:" line`);
  const named = values("Reader");
  if (named.length > 0 && named[0] !== reader) out.push(`${shown}: Reader is "${named[0]}", the directory is "${reader}"`);
  for (const code of values("Code")) if (code && !existsSync(join(repoRoot, code))) out.push(`${shown}: Code path ${code} does not exist`);
  for (const date of values("Date")) if (date && !/^\d{4}-\d{2}-\d{2}$/.test(date)) out.push(`${shown}: Date "${date}" is not YYYY-MM-DD`);
  if (!lines.some((l) => l.trim() === NO_FRM_LINE)) out.push(`${shown}: missing the line "${NO_FRM_LINE}"`);
  return out;
}

export function checkProvenance(repoRoot: string, modRoot = "mod"): ProvenanceReport {
  const problems: string[] = [];
  const top = join(repoRoot, modRoot);
  const natives: string[] = [];
  if (existsSync(top)) findNativeDirs(top, natives);
  const rel = (p: string) => relative(repoRoot, p).split(sep).join("/");
  const byReader = new Map<string, string[]>();
  for (const dir of natives) {
    for (const name of readdirSync(dir)) {
      const path = join(dir, name);
      if (!statSync(path).isDirectory()) {
        problems.push(`${rel(path)}: a file directly in a Native directory; put it in Native/<Reader>/`);
        continue;
      }
      byReader.set(name, [...(byReader.get(name) ?? []), path]);
    }
  }
  const readers = [...byReader.keys()].sort();
  for (const reader of readers) {
    const dirs = byReader.get(reader)!;
    const files = dirs.map((d) => join(d, PROVENANCE_FILE)).filter((f) => existsSync(f));
    if (files.length === 0) {
      problems.push(`native reader ${reader} (${dirs.map(rel).join(", ")}): no ${PROVENANCE_FILE}`);
    } else if (files.length > 1) {
      problems.push(`native reader ${reader}: more than one ${PROVENANCE_FILE} (${files.map(rel).join(", ")})`);
    } else {
      problems.push(...checkFile(repoRoot, reader, files[0]!, rel(files[0]!)));
    }
  }
  return { readers, problems };
}
