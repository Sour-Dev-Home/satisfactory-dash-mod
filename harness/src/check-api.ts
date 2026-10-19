import { readdirSync, readFileSync } from "node:fs";
import { basename, join } from "node:path";

export type UseKind = "include" | "symbol" | "method" | "module";
export interface Use { kind: UseKind; name: string; path: string; line: number }
export interface SourceFile { path: string; text: string }

const CODE_EXT = /\.(h|hpp|cpp)$/;

/** Blanks comments (and, if asked, string and char literals), keeping every newline so line numbers still match. */
function blank(text: string, strings: boolean): string {
  let out = "";
  let i = 0;
  const spaces = (chunk: string) => chunk.replace(/[^\n]/g, " ");
  while (i < text.length) {
    const c = text[i]!;
    const n = text[i + 1];
    if (c === "/" && (n === "/" || n === "*")) {
      const end = n === "/" ? text.indexOf("\n", i) : text.indexOf("*/", i + 2);
      const stop = end === -1 ? text.length : n === "/" ? end : end + 2;
      out += spaces(text.slice(i, stop));
      i = stop;
      continue;
    }
    if (c === '"' || c === "'") {
      let j = i + 1;
      while (j < text.length && text[j] !== c && text[j] !== "\n") j += text[j] === "\\" ? 2 : 1;
      const stop = Math.min(j + 1, text.length);
      out += strings ? spaces(text.slice(i, stop)) : text.slice(i, stop);
      i = stop;
      continue;
    }
    out += c;
    i++;
  }
  return out;
}

const lineAt = (text: string, index: number) => text.slice(0, index).split("\n").length;
const spikeOf = (path: string) => path.split("/").slice(0, 2).join("/");

interface Locals { types: Set<string>; methods: Set<string>; defines: Set<string>; headers: Set<string> }

function localsOf(files: SourceFile[]): Locals {
  const locals: Locals = { types: new Set(), methods: new Set(), defines: new Set(), headers: new Set() };
  for (const f of files) {
    if (/\.(h|hpp)$/.test(f.path)) locals.headers.add(basename(f.path));
    if (!CODE_EXT.test(f.path)) continue;
    const code = blank(f.text, true);
    // A definition (followed by ':' or '{'), not a forward declaration.
    for (const m of code.matchAll(/\b(?:class|struct|enum(?:\s+class)?)\s+(?:[A-Z0-9_]+_API\s+)?([A-Za-z_]\w*)\s*(?:final\s*)?[:{]/g)) {
      locals.types.add(m[1]!);
    }
    for (const m of code.matchAll(/^\s*#\s*define\s+([A-Za-z_]\w*)/gm)) locals.defines.add(m[1]!);
  }
  for (const f of files) {
    if (!CODE_EXT.test(f.path)) continue;
    for (const m of blank(f.text, true).matchAll(/\b([A-Z]\w*)::(~?[A-Za-z_]\w*)\s*\(/g)) {
      if (locals.types.has(m[1]!)) locals.methods.add(m[2]!);
    }
  }
  return locals;
}

function usesInCode(f: SourceFile, locals: Locals): Use[] {
  const uses: Use[] = [];
  const add = (kind: UseKind, name: string, line: number) => uses.push({ kind, name, path: f.path, line });
  blank(f.text, false).split("\n").forEach((raw, i) => {
    const m = /^\s*#\s*include\s*[<"]([^>"]+)[>"]/.exec(raw);
    if (!m) return;
    const inc = m[1]!;
    if (inc.endsWith(".generated.h") || locals.headers.has(basename(inc))) return;
    add("include", inc, i + 1);
  });
  blank(f.text, true).split("\n").forEach((line, i) => {
    if (/^\s*#\s*(include|define|pragma)/.test(line)) return;
    for (const m of line.matchAll(/\b([A-Z]\w*)::(~?[A-Za-z_]\w*)/g)) {
      if (!locals.types.has(m[1]!)) add("symbol", `${m[1]}::${m[2]}`, i + 1);
    }
    for (const m of line.matchAll(/(?:->|\.)\s*([A-Z]\w*)\s*\(/g)) {
      if (!locals.methods.has(m[1]!)) add("method", m[1]!, i + 1);
    }
    for (const m of line.matchAll(/\b([A-Z][A-Z0-9_]{2,})\s*\(/g)) {
      if (!locals.defines.has(m[1]!)) add("symbol", m[1]!, i + 1);
    }
    for (const m of line.matchAll(/(?<![\w:>.])([UAFIET][A-Z][A-Za-z0-9_]*)\b(?!\s*::)/g)) {
      const name = m[1]!;
      if (/^[A-Z0-9_]+$/.test(name) || locals.types.has(name)) continue;
      add("symbol", name, i + 1);
    }
  });
  return uses;
}

function modulesInBuildCs(f: SourceFile): Use[] {
  const text = blank(f.text, false);
  const uses: Use[] = [];
  const addStrings = (block: string, offset: number) => {
    for (const s of block.matchAll(/"([^"]+)"/g)) {
      uses.push({ kind: "module", name: `module:${s[1]}`, path: f.path, line: lineAt(text, offset + s.index!) });
    }
  };
  for (const m of text.matchAll(/DependencyModuleNames\s*\.\s*AddRange\s*\(\s*new\s+string\s*\[\s*\]\s*\{([^}]*)\}/g)) {
    addStrings(m[1]!, m.index! + m[0].indexOf(m[1]!));
  }
  for (const m of text.matchAll(/DependencyModuleNames\s*\.\s*Add\s*\(\s*("[^"]+")\s*\)/g)) {
    addStrings(m[1]!, m.index! + m[0].indexOf(m[1]!));
  }
  return uses;
}

/** Every external include, symbol, method and module dependency the spike sources use, deduplicated per line. */
export function extractUses(files: SourceFile[]): Use[] {
  const bySpike = new Map<string, SourceFile[]>();
  for (const f of files) bySpike.set(spikeOf(f.path), [...(bySpike.get(spikeOf(f.path)) ?? []), f]);
  const uses: Use[] = [];
  for (const group of bySpike.values()) {
    const locals = localsOf(group);
    for (const f of group) {
      if (f.path.endsWith(".Build.cs")) uses.push(...modulesInBuildCs(f));
      else if (CODE_EXT.test(f.path)) uses.push(...usesInCode(f, locals));
    }
  }
  const seen = new Set<string>();
  return uses.filter((u) => {
    const key = `${u.kind}|${u.name}|${u.path}|${u.line}`;
    return seen.has(key) ? false : (seen.add(key), true);
  });
}

export function parseSourceMap(md: string): { rows: Map<string, { header: string; source: string }>; badRows: string[] } {
  const rows = new Map<string, { header: string; source: string }>();
  const badRows: string[] = [];
  md.split(/\r?\n/).forEach((line, i) => {
    if (!line.trim().startsWith("|") || /^\|\s*-/.test(line.trim())) return;
    const cells = line.split("|").slice(1, -1).map((c) => c.trim().replace(/^`|`$/g, ""));
    if (cells[0] === "Symbol") return;
    const [symbol = "", header = "", source = ""] = cells;
    const where = `api-sources.md line ${i + 1}`;
    if (cells.length !== 3 || !symbol) return void badRows.push(`${where}: expected | Symbol | Header | Source |`);
    if (!/^\S+:\d+$/.test(header)) return void badRows.push(`${where}: header must be path:line, got "${header}"`);
    if (!source) return void badRows.push(`${where}: source is empty`);
    if (rows.has(symbol)) return void badRows.push(`${where}: duplicate row for ${symbol}`);
    rows.set(symbol, { header, source });
  });
  return { rows, badRows };
}

export function checkApi(files: SourceFile[], md: string): { missing: Use[]; badRows: string[] } {
  const { rows, badRows } = parseSourceMap(md);
  const keys = [...rows.keys()];
  const covered = (u: Use) => (u.kind === "method" ? keys.some((k) => k.endsWith(`::${u.name}`)) : rows.has(u.name));
  return { missing: extractUses(files).filter((u) => !covered(u)), badRows };
}

export function scanSpikes(repoRoot: string): SourceFile[] {
  const dir = join(repoRoot, "spikes");
  return readdirSync(dir, { recursive: true, encoding: "utf8" })
    .map((p) => p.replaceAll("\\", "/"))
    .filter((p) => CODE_EXT.test(p) || p.endsWith(".Build.cs"))
    .sort()
    .map((p) => ({ path: `spikes/${p}`, text: readFileSync(join(dir, p), "utf8") }));
}
