import { readFileSync } from "node:fs";
import { z } from "zod";

export const ToolchainSchema = z.strictObject({
  engine: z.string().min(1),
  sml: z.string().min(1),
  source: z.string().min(1),
});
export type Toolchain = z.infer<typeof ToolchainSchema>;

export function loadToolchain(path: string): Toolchain {
  return ToolchainSchema.parse(JSON.parse(readFileSync(path, "utf8")));
}

/** Each difference between the pinned toolchain and what the runner declares installed. Empty means they match. */
export function toolchainMismatches(required: Toolchain, installed: { engine: string; sml: string }): string[] {
  const out: string[] = [];
  if (installed.engine !== required.engine) out.push(`engine: required ${required.engine}, installed ${installed.engine}`);
  if (installed.sml !== required.sml) out.push(`sml: required ${required.sml}, installed ${installed.sml}`);
  return out;
}
