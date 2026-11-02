import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { z } from "zod";
import { SPIKE_IDS } from "./verdict";

const Versions = z.strictObject({ engine: z.string(), sml: z.string() });

export const ResultSchema = z.strictObject({
  spike: z.enum(SPIKE_IDS),
  claim: z.string().regex(/^C\d+$/),
  pass: z.boolean(),
  reasons: z.array(z.string()).min(1),
  startedAt: z.iso.datetime(),
  endedAt: z.iso.datetime(),
  toolchain: z.strictObject({ required: Versions, declared: Versions }),
  versionLines: z.array(z.string()),
  modLines: z.array(z.string()),
  harnessCommit: z.string().min(1),
  runUrl: z.string().nullable(),
});
export type Result = z.infer<typeof ResultSchema>;

/** Home-directory paths name the machine's user. Bundles are public artifacts, so they never carry one. */
export function redact(text: string): string {
  // A separator is a slash, a backslash, or the escaped backslash JSON writes ("\\").
  return text
    .replace(/[A-Za-z]:(?:\\{1,2}|\/)Users(?:\\{1,2}|\/)[^\\/\s"]+/g, "<home>")
    .replace(/\/(?:home|Users)\/[^/\s"]+/g, "<home>");
}

export function writeEvidence(dir: string, result: Result, captures: Record<string, unknown>): string {
  const parsed = ResultSchema.parse(JSON.parse(redact(JSON.stringify(result))));
  return writeBundle(dir, parsed.spike, parsed.startedAt, parsed, captures);
}

/** Writes result.json and the captures under <dir>/<name>-<startedAt>, redacted (shared by spikes and core runs). */
export function writeBundle(dir: string, name: string, startedAt: string, result: unknown, captures: Record<string, unknown>): string {
  const out = join(dir, `${name}-${startedAt.replace(/[:.]/g, "-")}`);
  mkdirSync(out, { recursive: true });
  for (const name of Object.keys(captures)) {
    if (!/^[\w.-]+$/.test(name) || name.startsWith(".")) throw new Error(`bad capture name: ${name}`);
  }
  writeFileSync(join(out, "result.json"), redact(`${JSON.stringify(result, null, 2)}\n`));
  for (const [name, value] of Object.entries(captures)) {
    const text = typeof value === "string" ? value : captureText(value);
    writeFileSync(join(out, name), redact(text));
  }
  return out;
}

/**
 * A capture whose compact JSON passes this many characters is written compact, the rest indented. Owner 2026-11-02: the S4-B
 * native-parity.json was 82 MB indented and 38 MB compact; the data is identical, only the whitespace goes.
 */
export const COMPACT_CAPTURE_CHARS = 1_000_000;

function captureText(value: unknown): string {
  const compact = JSON.stringify(value);
  return `${compact !== undefined && compact.length > COMPACT_CAPTURE_CHARS ? compact : JSON.stringify(value, null, 2)}\n`;
}
