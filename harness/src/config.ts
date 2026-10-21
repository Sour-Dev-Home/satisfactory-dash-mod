import { readFileSync } from "node:fs";
import { join } from "node:path";
import { z } from "zod";

export const LOOPBACK_HOSTS = new Set(["127.0.0.1", "localhost", "[::1]"]);

const isLoopbackHttps = (value: string) => {
  try {
    const url = new URL(value);
    return url.protocol === "https:" && LOOPBACK_HOSTS.has(url.hostname);
  } catch {
    return false;
  }
};

/**
 * The runner's own config, kept outside the repository (the path is in SATIS_HARNESS_CONFIG). It holds every value no
 * captured source states: the test server's ports (in serverArgs), the Alpakit build command and its output folder.
 */
export const RunnerConfigSchema = z.strictObject({
  serverDir: z.string().min(1),
  serverExe: z.string().min(1).default("FactoryServer.exe"),
  apiBaseUrl: z.string().refine(isLoopbackHttps, "apiBaseUrl must be https on loopback (127.0.0.1, localhost or [::1])"),
  serverArgs: z.array(z.string()).default([]),
  logPath: z.string().min(1).optional(),
  buildCommand: z
    .array(z.string().min(1))
    .min(1, "buildCommand is empty: fill it from the captured Alpakit page (G2-prep)"),
  builtPluginDir: z.string().includes("{spike}"),
  installed: z.strictObject({ engine: z.string().min(1), sml: z.string().min(1) }),
  /** S3: Ficsit Remote Monitoring's web server on the test server (satisfactory-dash ADR-0039 G0 addendum 2). */
  frmBaseUrl: z
    .string()
    .refine((v) => {
      try {
        const u = new URL(v);
        return u.protocol === "http:" && LOOPBACK_HOSTS.has(u.hostname);
      } catch {
        return false;
      }
    }, "frmBaseUrl must be http on loopback")
    .optional(),
  /** S3: the staged copy of the owner's save, uploaded and loaded each run (docs/runner.md, "S3's world"). */
  s3World: z.string().min(1).optional(),
  /** S5 only, local runs: a satisfactory-dash checkout at the pinned commit, and the relay's loopback port. */
  s5: z.strictObject({ checkout: z.string().min(1), port: z.number().int().min(1024).max(65535) }).optional(),
  /** Backend runs (ruling 10): the host file the owner's backend host writes (outside the repository), and its port. */
  backendHost: z.strictObject({ file: z.string().min(1), port: z.number().int().min(1024).max(65535) }).optional(),
  /** The core mod's unit tests (scripts/test-mod.ps1): the SML project folder and the engine root. */
  modTest: z.strictObject({ project: z.string().min(1), engine: z.string().min(1) }).optional(),
});
export type RunnerConfig = z.infer<typeof RunnerConfigSchema>;

export function loadConfig(path: string): RunnerConfig {
  return RunnerConfigSchema.parse(JSON.parse(readFileSync(path, "utf8")));
}

/** sml-dedicated-servers-2026-09-27.adoc (FAQ, "Log Files"): a dedicated server's log "could" be here; S0 verifies it. */
export function logPathOf(cfg: RunnerConfig): string {
  return cfg.logPath ?? join(cfg.serverDir, "FactoryGame", "Saved", "Logs", "FactoryGame.log");
}

export function pluginDirFor(cfg: RunnerConfig, spike: string): string {
  return cfg.builtPluginDir.replaceAll("{spike}", spike);
}
