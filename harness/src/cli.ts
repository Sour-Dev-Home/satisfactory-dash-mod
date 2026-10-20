import { readFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { checkApi, scanSpikes } from "./check-api";
import { loadConfig } from "./config";
import { verifyContract } from "./contract";
import { RefusalError, RUNNABLE, realDeps, runSpike } from "./run";
import { loadToolchain, toolchainMismatches } from "./toolchain";
import type { SpikeId } from "./verdict";

export const HARNESS_ROOT = resolve(dirname(fileURLToPath(import.meta.url)), "..");
export const REPO_ROOT = resolve(HARNESS_ROOT, "..");

type Io = { log(s: string): void; error(s: string): void };
const consoleIo: Io = { log: (s) => console.log(s), error: (s) => console.error(s) };

export function runCheckApi(io: Io): number {
  const { missing, badRows } = checkApi(scanSpikes(REPO_ROOT), readFileSync(join(REPO_ROOT, "api-sources.md"), "utf8"));
  for (const b of badRows) io.error(b);
  for (const u of missing) io.error(`${u.path}:${u.line} ${u.kind} ${u.name} has no row in api-sources.md`);
  io.log(`check-api: ${missing.length} missing, ${badRows.length} bad rows`);
  return missing.length || badRows.length ? 1 : 0;
}

export async function main(argv: string[], io: Io = consoleIo): Promise<number> {
  switch (argv[0]) {
    case "check-api":
      return runCheckApi(io);
    case "verify-contract": {
      const problems = verifyContract(join(HARNESS_ROOT, "contract"));
      for (const p of problems) io.error(`contract: ${p}`);
      io.log(`verify-contract: ${problems.length} problems`);
      return problems.length ? 1 : 0;
    }
    case "selftest": {
      const path = process.env.SATIS_HARNESS_CONFIG;
      if (!path) {
        io.error("SATIS_HARNESS_CONFIG is not set (see docs/runner.md)");
        return 2;
      }
      const config = loadConfig(path);
      const mismatches = toolchainMismatches(loadToolchain(join(HARNESS_ROOT, "toolchain.json")), config.installed);
      for (const m of mismatches) io.error(`toolchain: ${m}`);
      const contract = await main(["verify-contract"], io);
      const api = runCheckApi(io);
      return mismatches.length || contract || api ? 1 : 0;
    }
    case "run": {
      const spike = argv[1] as SpikeId;
      const outAt = argv.indexOf("--out");
      const outDir = outAt === -1 ? undefined : argv[outAt + 1];
      if (!RUNNABLE.includes(spike) || !outDir) {
        io.error(`usage: harness run <${RUNNABLE.join(", ")}> --out <dir>`);
        return 2;
      }
      const path = process.env.SATIS_HARNESS_CONFIG;
      if (!path) {
        io.error("SATIS_HARNESS_CONFIG is not set (see docs/runner.md)");
        return 2;
      }
      try {
        const deps = realDeps(loadConfig(path), loadToolchain(join(HARNESS_ROOT, "toolchain.json")), outDir, REPO_ROOT);
        const { result, dir } = await runSpike(spike, deps);
        io.log(`${spike} (${result.claim}): ${result.pass ? "PASS" : "FAIL"}\n${result.reasons.join("\n")}\nevidence: ${dir}`);
        return result.pass ? 0 : 1;
      } catch (e) {
        if (e instanceof RefusalError) {
          io.error(`refused: ${e.message}`);
          return 3;
        }
        throw e;
      }
    }
    default:
      io.error("usage: harness <check-api|verify-contract|selftest|run>");
      return 2;
  }
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  process.exit(await main(process.argv.slice(2)));
}
