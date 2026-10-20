import { existsSync, readdirSync, readFileSync } from "node:fs";
import { basename, join } from "node:path";

export type UseKind = "include" | "symbol" | "method" | "module";
export interface Use { kind: UseKind; name: string; path: string; line: number }
export interface SourceFile { path: string; text: string }

const CODE_EXT = /\.(h|hpp|inl|c|cc|cpp|cxx)$/;
const HEADER_EXT = /\.(h|hpp|inl)$/;

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
    // A digit separator (1'000, 0xFF'FF) is not a char literal.
    if (c === "'" && /(?:^|[^\w])[0-9][\w']*$/.test(text.slice(Math.max(0, i - 32), i)) && /[0-9A-Fa-f]/.test(n ?? "")) {
      out += c;
      i++;
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

const spikeOf = (path: string) => path.split("/").slice(0, 2).join("/");

interface Locals { types: Set<string>; functions: Set<string>; defines: Set<string>; headers: Set<string> }

/** Words that can precede a call but are not a return type, so "return Foo();" never declares Foo. */
const NOT_A_TYPE = new Set(["return", "else", "throw", "new", "delete", "case", "co_return", "co_await", "co_yield", "goto"]);
const QUALIFIERS = String.raw`\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?(?:final\s*)?`;

/** Namespaces the engine owns: a code's own "namespace UE::Mine" never excuses them. */
const ENGINE_ROOTS = new Set(["UE", "UE4", "UE5", "std", "Chaos"]);

function localsOf(files: SourceFile[]): Locals {
  const locals: Locals = { types: new Set(), functions: new Set(), defines: new Set(), headers: new Set() };
  for (const f of files) {
    if (HEADER_EXT.test(f.path)) locals.headers.add(basename(f.path));
    if (!CODE_EXT.test(f.path)) continue;
    const code = blank(f.text, true);
    // A definition (followed by ':' or '{'), not a forward declaration.
    for (const m of code.matchAll(/\b(?:class|struct|enum(?:\s+class)?)\s+(?:[A-Z0-9_]+_API\s+)?([A-Za-z_]\w*)\s*(?:final\s*)?[:{]/g)) {
      locals.types.add(m[1]!);
    }
    for (const m of code.matchAll(/^\s*#\s*define\s+([A-Za-z_]\w*)/gm)) locals.defines.add(m[1]!);
    // The code's own namespaces, and the test classes that IMPLEMENT_*_AUTOMATION_TEST declares by macro.
    // Only definitions ("namespace A {", "namespace A::B {"), and only the first component: never "using namespace X;"
    // or "namespace x = y;", and never an engine root, which stays checked.
    for (const m of code.matchAll(/\bnamespace\s+([A-Za-z_]\w*)(?:\s*::\s*[A-Za-z_]\w*)*\s*\{/g)) {
      if (!ENGINE_ROOTS.has(m[1]!)) locals.types.add(m[1]!);
    }
    for (const m of code.matchAll(/\bIMPLEMENT_(?:SIMPLE|COMPLEX)_AUTOMATION_TEST\s*\(\s*([A-Za-z_]\w*)/g)) locals.types.add(m[1]!);
  }
  for (const f of files) {
    if (!CODE_EXT.test(f.path)) continue;
    const code = blank(f.text, true);
    // Methods of the spike's own classes, defined as Local::Name(...).
    for (const m of code.matchAll(/\b([A-Z]\w*)\s*::\s*(~?[A-Za-z_]\w*)\s*\(/g)) {
      if (locals.types.has(m[1]!)) locals.functions.add(m[2]!);
    }
    // Functions the spike defines with a body, or (in its own headers) declares: "<type> Name(...) {" / ";".
    const ending = HEADER_EXT.test(f.path) ? "[{;]" : "\\{";
    const defined = new RegExp(String.raw`\b(\w+)[>*&]?\s+[*&]?([A-Z]\w*)\s*\([^;{}()]*\)${QUALIFIERS}${ending}`, "g");
    for (const m of code.matchAll(defined)) {
      if (!NOT_A_TYPE.has(m[1]!)) locals.functions.add(m[2]!);
    }
  }
  return locals;
}

function lineIndex(text: string): (index: number) => number {
  const starts = [0];
  for (let i = 0; i < text.length; i++) if (text[i] === "\n") starts.push(i + 1);
  return (index) => {
    let lo = 0;
    let hi = starts.length - 1;
    while (lo < hi) {
      const mid = (lo + hi + 1) >> 1;
      if (starts[mid]! <= index) lo = mid;
      else hi = mid - 1;
    }
    return lo + 1;
  };
}

/** Blanks #include and #pragma lines and the "#define NAME(args)" head, but keeps a define's body, which can use APIs. */
function blankDirectives(code: string): string {
  const spaces = (s: string) => s.replace(/[^\n]/g, " ");
  return code
    .replace(/^[ \t]*#[ \t]*(?:include|pragma)\b.*$/gm, spaces)
    .replace(/^[ \t]*#[ \t]*define[ \t]+\w+(?:\([^)\n]*\))?/gm, spaces);
}

const NOT_AFTER_ACCESS = String.raw`(?<!::\s*|->\s*|\.\s*)`;
const TEMPLATE_ARGS = String.raw`(?:<[^<>;()]*>)?`;

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
  const code = blankDirectives(blank(f.text, true));
  const lineOf = lineIndex(code);
  const allCaps = (name: string) => /^[A-Z0-9_]+$/.test(name);
  // Qualified names, whole chains, across whitespace and newlines: UE::Tasks::Launch, FHttpModule::Get.
  for (const m of code.matchAll(/(?<![\w:])\b([A-Z]\w*)((?:\s*::\s*~?[A-Za-z_]\w*)+)/g)) {
    if (!locals.types.has(m[1]!)) add("symbol", (m[1]! + m[2]!).replace(/\s+/g, ""), lineOf(m.index!));
  }
  // Method calls; a local method is excused only when called on this.
  for (const m of code.matchAll(new RegExp(String.raw`(\bthis\s*)?(?:->|\.)\s*([A-Z]\w*)\s*${TEMPLATE_ARGS}\s*\(`, "g"))) {
    if (!(m[1] && locals.functions.has(m[2]!))) add("method", m[2]!, lineOf(m.index! + m[0].indexOf(m[2]!)));
  }
  // Macros.
  for (const m of code.matchAll(/\b([A-Z][A-Z0-9_]{2,})\s*\(/g)) {
    if (!locals.defines.has(m[1]!)) add("symbol", m[1]!, lineOf(m.index!));
  }
  // Unqualified calls: GetWorld(), Cast<AActor>(...), NewObject<UObject>().
  for (const m of code.matchAll(new RegExp(String.raw`${NOT_AFTER_ACCESS}\b([A-Z]\w*)\s*${TEMPLATE_ARGS}\s*\(`, "g"))) {
    const name = m[1]!;
    if (allCaps(name) || locals.types.has(name) || locals.functions.has(name) || locals.defines.has(name)) continue;
    add("symbol", name, lineOf(m.index!));
  }
  // Engine-style type and global names: U/A/F/I/E/T/G followed by a capital.
  for (const m of code.matchAll(new RegExp(String.raw`${NOT_AFTER_ACCESS}\b([UAFIETG][A-Z][A-Za-z0-9_]*)\b(?!\s*::)`, "g"))) {
    const name = m[1]!;
    if (allCaps(name) || locals.types.has(name)) continue;
    add("symbol", name, lineOf(m.index!));
  }
  return uses;
}

function modulesInBuildCs(f: SourceFile): Use[] {
  const text = blank(f.text, false);
  const lineOf = lineIndex(text);
  const uses: Use[] = [];
  const addStrings = (block: string, offset: number) => {
    for (const s of block.matchAll(/"([^"]+)"/g)) {
      uses.push({ kind: "module", name: `module:${s[1]}`, path: f.path, line: lineOf(offset + s.index!) });
    }
  };
  for (const m of text.matchAll(/\w*ModuleNames\s*\.\s*AddRange\s*\(\s*new\s*(?:string\s*)?\[\s*\]\s*\{([^}]*)\}/g)) {
    addStrings(m[1]!, m.index! + m[0].indexOf(m[1]!));
  }
  for (const m of text.matchAll(/\w*ModuleNames\s*\.\s*Add\s*\(\s*("[^"]+")\s*\)/g)) {
    addStrings(m[1]!, m.index! + m[0].indexOf(m[1]!));
  }
  return uses;
}

/** Plugin dependencies in a .uplugin ("Plugins": [{ "Name": ... }]). An unreadable file is itself a use without a row. */
function pluginsInUplugin(f: SourceFile): Use[] {
  try {
    const meta = JSON.parse(f.text.replace(/^﻿/, "")) as { Plugins?: Array<{ Name?: unknown }> };
    const lineOf = lineIndex(f.text);
    return (meta.Plugins ?? []).map((p) => {
      const name = String(p.Name);
      const at = f.text.search(new RegExp(String.raw`"Name"\s*:\s*"${name.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}"`));
      return { kind: "module" as const, name: `plugin:${name}`, path: f.path, line: lineOf(Math.max(at, 0)) };
    });
  } catch {
    return [{ kind: "module", name: "plugin:<unreadable .uplugin>", path: f.path, line: 1 }];
  }
}

/** Every external include, symbol, method and module dependency the spike sources use, deduplicated per line. */
export function extractUses(files: SourceFile[]): Use[] {
  const bySpike = new Map<string, SourceFile[]>();
  for (const f of files) bySpike.set(spikeOf(f.path), [...(bySpike.get(spikeOf(f.path)) ?? []), f]);
  const uses: Use[] = [];
  for (const group of bySpike.values()) {
    const locals = localsOf(group);
    const ownModules = new Set(group.filter((f) => f.path.endsWith(".Build.cs")).map((f) => basename(f.path, ".Build.cs")));
    for (const f of group) {
      if (f.path.endsWith(".Build.cs")) uses.push(...modulesInBuildCs(f).filter((u) => !ownModules.has(u.name.slice("module:".length))));
      else if (f.path.endsWith(".uplugin")) uses.push(...pluginsInUplugin(f));
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

/** Every source under spikes/ and mod/ (the core mod), each group checked on its own (spikeOf: the first two path parts). */
export function scanSources(repoRoot: string, roots: string[] = ["spikes", "mod"]): SourceFile[] {
  return roots
    .filter((root) => existsSync(join(repoRoot, root)))
    .flatMap((root) =>
      readdirSync(join(repoRoot, root), { recursive: true, encoding: "utf8" })
        .map((p) => p.replaceAll("\\", "/"))
        .filter((p) => CODE_EXT.test(p) || p.endsWith(".Build.cs") || p.endsWith(".uplugin"))
        .sort()
        .map((p) => ({ path: `${root}/${p}`, text: readFileSync(join(repoRoot, root, p), "utf8") })),
    );
}
