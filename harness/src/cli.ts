import { readFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { checkApi, scanSpikes } from "./check-api";

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
    default:
      io.error("usage: harness <check-api>");
      return 2;
  }
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  process.exit(await main(process.argv.slice(2)));
}
