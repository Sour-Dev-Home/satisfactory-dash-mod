import { execFileSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { realExec, startBackend } from "./backend";
import { hostUntilStopped, serveBackendHost } from "./backend-host";
import { checkApi, scanSources } from "./check-api";
import { loadConfig } from "./config";
import { type CoreRunId, realCoreDeps, runCore } from "./core-run";
import { CORE_RUNS } from "./core-runs";
import { verifyContract } from "./contract";
import { judgeAutomationReport, readAutomationReport } from "./mod-test";
import { rejudge } from "./rejudge";
import { RefusalError, RUNNABLE, realDeps, runSpike } from "./run";
import { loadToolchain, toolchainMismatches } from "./toolchain";
import type { SpikeId } from "./verdict";

export const HARNESS_ROOT = resolve(dirname(fileURLToPath(import.meta.url)), "..");
export const REPO_ROOT = resolve(HARNESS_ROOT, "..");

type Io = { log(s: string): void; error(s: string): void };
const consoleIo: Io = { log: (s) => console.log(s), error: (s) => console.error(s) };

export function runCheckApi(io: Io): number {
  const { missing, badRows } = checkApi(scanSources(REPO_ROOT), readFileSync(join(REPO_ROOT, "api-sources.md"), "utf8"));
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
    case "core-run": {
      const id = argv[1] as CoreRunId;
      const outAt = argv.indexOf("--out");
      const outDir = outAt === -1 ? undefined : argv[outAt + 1];
      const spec = CORE_RUNS[id];
      if (!spec || !outDir) {
        io.error(`usage: harness core-run <${Object.keys(CORE_RUNS).join(", ")}> --out <dir>`);
        return 2;
      }
      const path = process.env.SATIS_HARNESS_CONFIG;
      if (!path) {
        io.error("SATIS_HARNESS_CONFIG is not set (see docs/runner.md)");
        return 2;
      }
      try {
        const deps = realCoreDeps(loadConfig(path), loadToolchain(join(HARNESS_ROOT, "toolchain.json")), outDir, REPO_ROOT);
        const { result, dir } = await runCore(id, spec, deps);
        io.log(`${id}: ${result.pass ? "PASS" : "FAIL"}\n${result.reasons.join("\n")}\nevidence: ${dir}`);
        return result.pass ? 0 : 1;
      } catch (e) {
        if (e instanceof RefusalError) {
          io.error(`refused: ${e.message}`);
          return 3;
        }
        throw e;
      }
    }
    case "backend-host": {
      // The owner's session only (ruling 10): Docker runs as the owner; the runner reaches it through the host file.
      const path = process.env.SATIS_HARNESS_CONFIG;
      if (!path) {
        io.error("SATIS_HARNESS_CONFIG is not set (see docs/runner.md)");
        return 2;
      }
      const config = loadConfig(path);
      if (!config.s5 || !config.backendHost) {
        io.error("backend-host needs s5 and backendHost in the runner config (see docs/runner.md, \"The backend host\")");
        return 2;
      }
      const hostCfg = config.backendHost;
      const s5 = config.s5;
      await hostUntilStopped({
        start: () => startBackend({ checkout: s5.checkout, relayFile: join(HARNESS_ROOT, "compose", "s5-relay.yaml"), port: s5.port, exec: realExec }),
        serve: (backend) => serveBackendHost({ backend, file: hostCfg.file, port: hostCfg.port }),
        // process.on, not once: the handler stays, so a second Ctrl+C never falls back to the default kill.
        onStop: (handler) => {
          process.on("SIGINT", handler);
          process.on("SIGTERM", handler);
        },
        log: (s) => io.log(s),
      });
      return 0;
    }
    case "rejudge": {
      const dir = argv[1];
      if (!dir) {
        io.error("usage: harness rejudge <bundle dir>");
        return 2;
      }
      // The rule's commit: the CI's, else this checkout's.
      const commit = process.env.GITHUB_SHA ?? execFileSync("git", ["rev-parse", "HEAD"], { cwd: REPO_ROOT, encoding: "utf8" }).trim();
      const out = rejudge(dir, { now: new Date(), commit });
      io.log(`${out.spike} (${out.claim}) re-judged at ${commit.slice(0, 7)}: ${out.verdict.pass ? "PASS" : "FAIL"} (the run said ${out.original.pass ? "PASS" : "FAIL"})\n${out.verdict.reasons.join("\n")}`);
      return out.verdict.pass ? 0 : 1;
    }
    case "judge-mod-test":
    case "test-mod": {
      const minAt = argv.indexOf("--min");
      const minTests = minAt === -1 ? 1 : Number(argv[minAt + 1]);
      const outAt = argv.indexOf("--out");
      const outDir = argv[0] === "test-mod" ? (outAt === -1 ? undefined : argv[outAt + 1]) : argv[1];
      if (!outDir || !Number.isInteger(minTests) || minTests < 1) {
        io.error("usage: harness test-mod --out <dir> [--min <n>] | harness judge-mod-test <dir> [--min <n>]");
        return 2;
      }
      if (argv[0] === "test-mod") {
        const path = process.env.SATIS_HARNESS_CONFIG;
        if (!path) {
          io.error("SATIS_HARNESS_CONFIG is not set (see docs/runner.md)");
          return 2;
        }
        const modTest = loadConfig(path).modTest;
        if (!modTest) {
          io.error("modTest is not set in the runner config (see docs/runner.md)");
          return 2;
        }
        try {
          execFileSync("pwsh", ["-NoProfile", "-File", join(REPO_ROOT, "scripts", "test-mod.ps1"),
            "-Project", modTest.project, "-Engine", modTest.engine, "-Out", resolve(outDir)], { stdio: "inherit" });
        } catch {
          io.error("test-mod.ps1 failed (its output is above)");
          return 1;
        }
      }
      const verdict = judgeAutomationReport(readAutomationReport(resolve(outDir)), minTests);
      io.log(`mod tests: ${verdict.pass ? "PASS" : "FAIL"} (${verdict.reason})`);
      return verdict.pass ? 0 : 1;
    }
    default:
      io.error("usage: harness <check-api|verify-contract|selftest|run|core-run|backend-host|rejudge|test-mod|judge-mod-test>");
      return 2;
  }
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  process.exit(await main(process.argv.slice(2)));
}
