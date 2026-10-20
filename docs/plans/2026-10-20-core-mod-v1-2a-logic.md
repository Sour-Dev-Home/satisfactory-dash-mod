# Core mod v1, Plan 2A: the mod's logic, unit-tested Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build everything in the core mod that doesn't touch the game or the network (config, credential, snapshot JSON, response parsing, requests, the push queue, commands and the agent's state machine), with UE automation tests that run headless on the runner.

**Architecture:** One plugin, `mod/SatisDashAgent`, with two modules. `SatisDashCore` (Runtime) holds pure logic and depends only on `Core` and `Json`, so it compiles for Linux later and needs no game. `SatisDashCoreTests` (Editor) holds the automation tests, so no test code ships in a server build (S14). Plan 2B adds the third module, which reads the game (`FactoryDedicatedServer`), sends `FRequestSpec`s through UE's HTTP module and drives everything from `FTSTicker`. 2B consumes only the interfaces this plan produces.

**Tech Stack:** Unreal Engine 5.6.1-CSS, SML 3.12.0 (`harness/toolchain.json`), C++20, UE automation tests (`IMPLEMENT_SIMPLE_AUTOMATION_TEST`), the TypeScript harness (Node, zod 4, Vitest 5) for the test runner, the fixture check and `check-api`.

**Spec:** `docs/specs/2026-10-20-core-mod-v1-design.md` (approved, mod PR #19; Security section mod PR #24, merged at `123d458`). The build decision is satisfactory-dash ADR-0042 (#479). The contract is satisfactory-dash `packages/shared/src/agent.ts` at `ab4626f`, vendored in `harness/contract/` (`manifest.json`).

## Global Constraints

- Toolchain pinned: engine `5.6.1`, SML `3.12.0` (`harness/toolchain.json`); the plugin depends on SML `^3.12.0` and `GameVersion >=513885`, as the spikes do.
- `agentVersion` is `mod/0.1.0`, at most 32 characters (spec "The contract it speaks").
- `SatisDashCore` depends only on `Core` and `Json`: no FactoryGame, no SML, no HTTP module, no Windows-only API (spec "Portability").
- Test code lives only in `SatisDashCoreTests`, an `Editor` module (S14).
- Every automation test name starts `SatisDash.Core.`; flags are `SatisDash::Test::Flags` (`EditorContext | EngineFilter`).
- **Protocol strings are compared case-sensitively** (`Equals(X, ESearchCase::CaseSensitive)`): `FString::operator==` ignores case in UE, and command IDs, command types and secrets must not.
- **No crash path on outside data:** no `check()`, `ensure()` or `verify()` on anything read from a file or the network. Bad input becomes a state, a dropped item or a result code (spec "The game never notices the mod").
- Logs go only to `LogSatisDash`, at `Display` or `Warning`, **never `Error`**. One line per state change, never one per attempt. They never carry the secret, the code or the string `Bearer` (S8), only "present"/"absent" and HTTP status codes.
- Every SML, FactoryGame or engine symbol, include and module used under `mod/` has a row in `api-sources.md`. Its header line is looked up in the pinned checkout, never written from memory. `npm run harness -- check-api` must print `0 missing, 0 bad rows`. The rows given in this plan were looked up on 2026-10-20; every other row is looked up when the task adds it.
- No absolute local paths and no personal information in any committed file (CLAUDE.md). The runner's paths live in its own config, outside the repository.
- Numbers bound by the contract (ADR-0031 input bounds): strings at most 200 characters, at most 1000 circuits, finite numbers, and `>= 0` where the contract says so; battery percent 0–100.
- Limits copied from the spec and the Node agent: a 120-snapshot queue; backoff from 1 s doubling to 60 s with jitter; `Retry-After` capped at 300 s; gzip above 1024 bytes; responses capped at 64 KB; cadence clamped to 1–3600 s; command IDs 1–100 characters; at most 20 commands per poll; a 1000-entry ledger; commands more than 600 s from expiry skipped; a 15 s request timeout, and 25 s + 10 s for the long poll.

## Review Focus

- **A config edited by hand in Notepad or a host's web panel** (UTF-8 BOM, CRLF, a trailing `/` on the URL, a code pasted in lower case or with spaces). A person expects it to be accepted. Pinned in Task 2 (`SatisDash.Core.Config.File`, `.Parse`).
- **An edit that doesn't change the file's modification time** (a panel upload that keeps the timestamp, or two saves within the file system's time resolution). A person expects the new code to be tried within a minute. Pinned in Task 9: the trigger is "the modification time **or** the text changed" (`SatisDash.Core.Agent.ConfigChange`).
- **The queue drops the snapshot that is in flight** (the backend is slow and the queue is full). A person expects the next snapshot not to be thrown away when the slow send finally succeeds. Pinned in Task 8 (`SatisDash.Core.Pusher.InFlightDropped`).
- **Command IDs or types that differ only in case** (`abc` vs `ABC`, `SET_AUTO_PAUSE`). A person expects two different commands, and an unknown type to report `unsupported`. Pinned in Task 6 (`SatisDash.Core.Commands.CaseSensitive`).
- **A hostile or broken backend's numbers** (cadence `1e300`, a `Retry-After` of `1e309` or an HTTP date, a 64 KB + 1 body). A person expects the mod to clamp, ignore or refuse, never to spin or crash. Pinned in Task 5 (`SatisDash.Core.Responses.Cadence`, `.Limits`) and Task 8 (`SatisDash.Core.Pusher.RetryAfter`).

## Not in 2A (Plan 2B, written after 2A lands)

2B wires these interfaces into the game:
- the native readers (S6/S7's calls), HTTP through `FHttpModule`, and the ticker;
- the harness runs (enrol, revoke, command, outage, secrets, tick cost), the 30-minute parity run and the `security-reviewer` pass.

Three findings from the engine lookups for this plan need the owner before 2B builds the HTTP layer:
1. **Redirects (S3).** UE's curl backend sets `CURLOPT_FOLLOWLOCATION` to 1 unconditionally (`Engine/Source/Runtime/Online/HTTP/Private/Curl/CurlHttp.cpp:127`). No per-request option turns it off: `IHttpRequest::SetOption` (`Public/Interfaces/IHttpRequest.h:187`) knows only `HttpVersion` and `UnixSocketPath`. So S3 can't be met by a request setting. 2B's first task measures what actually happens: the harness's stub answers 302 to another loopback port, which must receive nothing. If that fails, it goes back to the owner with options.
   - [NEEDS VERIFICATION] whether this engine build uses curl or WinHttp on Windows.
   - [NEEDS VERIFICATION] whether its libcurl drops a custom `Authorization` header on a cross-host redirect.
2. **User-Agent.** 2A's requests set `User-Agent: satisfactory-dash-mod/0.1.0` (Task 7), as the Node agent sets its own. [NEEDS VERIFICATION] whether UE replaces or appends to it, since UE's default carries the project and OS. 2B's harness checks the header the stub receives.
3. **TLS floor (S2)** stays [NEEDS VERIFICATION] as the spec says. 2B checks it once and records it.

## How the tests run

- Task 1 lands as its **own PR first**: a `workflow_dispatch` workflow can only be dispatched once its file is on `main`.
- From Task 2 on, each task's test step runs locally:
  ```powershell
  pwsh scripts/test-mod.ps1 -Project <sml-project> -Engine <engine-root> -Out <tmp>\mod-test
  cd harness; npm run harness -- judge-mod-test <tmp>\mod-test --min <N>
  ```
  `<sml-project>` is the folder holding `FactoryGame.uproject`, and `<engine-root>` the folder holding `Engine\`. Both are the executor's own and are never committed. `<N>` is the task's expected test count.
- Before 2A's last PR merges, `gh workflow run mod-test.yml --ref <branch>` runs the same tests on the runner, and the owner approves its `ue-runner` deployment.

## File structure

```
mod/SatisDashAgent/
  SatisDashAgent.uplugin
  Source/SatisDashCore/
    SatisDashCore.Build.cs
    Public/SatisDash/Version.h       Task 1  agentVersion
    Public/SatisDash/Log.h           Task 1  LogSatisDash
    Public/SatisDash/JsonUtil.h      Task 2  parse, typed field, write
    Public/SatisDash/Files.h         Task 2  WriteFileAtomic
    Public/SatisDash/Config.h        Task 2  SatisDashAgent.cfg
    Public/SatisDash/Credential.h    Task 3  credential.json
    Public/SatisDash/Snapshot.h      Task 4  readings, Conform, snapshot JSON
    Public/SatisDash/Responses.h     Task 5  FHttpReply, ClassifyReply, response parsers
    Public/SatisDash/Commands.h      Task 6  ledger, PlanCommand, result body
    Public/SatisDash/Requests.h      Task 7  FRequestSpec builders, gzip
    Public/SatisDash/Pusher.h        Task 8  backoff, Retry-After, queue
    Public/SatisDash/Agent.h         Task 9  the state machine
    Public/SatisDash/RateLimit.h     Task 10 rate-limited reader-failure lines
    Private/<one .cpp per header>, SatisDashCoreModule.cpp
  Source/SatisDashCoreTests/
    SatisDashCoreTests.Build.cs
    Private/SatisDashCoreTestsModule.cpp, TestUtil.h, <Area>Test.cpp
    Fixtures/snapshot-world.json, snapshot-no-world.json      Task 4
scripts/test-mod.ps1                                          Task 1
harness/src/mod-test.ts, harness/test/mod-test.test.ts        Task 1
harness/test/snapshot-fixtures.test.ts                        Task 4
.github/workflows/mod-test.yml                                Task 1
```

---

### Task 1: The plugin scaffold, and its tests running headless

**Files:**
- Create: `mod/SatisDashAgent/SatisDashAgent.uplugin`
- Create: `mod/SatisDashAgent/Source/SatisDashCore/SatisDashCore.Build.cs`
- Create: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Version.h`, `Public/SatisDash/Log.h`
- Create: `mod/SatisDashAgent/Source/SatisDashCore/Private/Version.cpp`, `Private/SatisDashCoreModule.cpp`
- Create: `mod/SatisDashAgent/Source/SatisDashCoreTests/SatisDashCoreTests.Build.cs`
- Create: `mod/SatisDashAgent/Source/SatisDashCoreTests/Private/SatisDashCoreTestsModule.cpp`, `Private/TestUtil.h`, `Private/VersionTest.cpp`
- Create: `scripts/test-mod.ps1`, `harness/src/mod-test.ts`, `harness/test/mod-test.test.ts`, `.github/workflows/mod-test.yml`
- Modify: `harness/src/config.ts` (optional `modTest`), `harness/src/cli.ts` (`test-mod`, `judge-mod-test`, `scanSources`), `harness/src/check-api.ts` (scan `mod/`; excuse the plugin's own modules), `harness/test/check-api.test.ts`, `scripts/build-spike.ps1` (also clears `Mods\SatisDashAgent`), `api-sources.md`, `docs/runner.md`

**Interfaces:**
- Produces:
  - `FString SatisDash::AgentVersion()`, returning `"mod/0.1.0"`;
  - the `LogSatisDash` log category (`SATISDASHCORE_API`);
  - `SatisDash::Test::Flags`;
  - `harness test-mod --out <dir> [--min <n>]` and `harness judge-mod-test <dir> [--min <n>]`;
  - `scanSources(repoRoot)`.

- [ ] **Step 1: Write the failing harness tests**

`harness/test/mod-test.test.ts`:

```ts
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { RunnerConfigSchema } from "../src/config";
import { decodeReport, judgeAutomationReport, readAutomationReport } from "../src/mod-test";

const counts = { succeeded: 3, succeededWithWarnings: 1, failed: 0, notRun: 0, inProcess: 0 };
let dir = "";
afterEach(() => dir && rmSync(dir, { recursive: true, force: true }));

describe("decodeReport", () => {
  it("reads UTF-16 with a BOM, UTF-8 with a BOM and plain UTF-8", () => {
    const text = '{"a":1}';
    expect(decodeReport(Buffer.concat([Buffer.from([0xff, 0xfe]), Buffer.from(text, "utf16le")]))).toBe(text);
    expect(decodeReport(Buffer.concat([Buffer.from([0xef, 0xbb, 0xbf]), Buffer.from(text)]))).toBe(text);
    expect(decodeReport(Buffer.from(text))).toBe(text);
  });
});

describe("readAutomationReport", () => {
  it("reads the counts from index.json and ignores other fields", () => {
    dir = mkdtempSync(join(tmpdir(), "mod-test-"));
    writeFileSync(join(dir, "index.json"), JSON.stringify({ ...counts, devices: [], totalDuration: 1.5 }));
    expect(readAutomationReport(dir)).toEqual(counts);
  });
});

describe("judgeAutomationReport", () => {
  it("passes when nothing failed, everything ran and enough tests passed", () => {
    expect(judgeAutomationReport(counts, 4)).toEqual({ pass: true, reason: "4 passed" });
  });
  it("fails on a failed test", () => {
    expect(judgeAutomationReport({ ...counts, failed: 1 }, 1).pass).toBe(false);
  });
  it("fails when a test didn't run or didn't finish", () => {
    expect(judgeAutomationReport({ ...counts, notRun: 1 }, 1).pass).toBe(false);
    expect(judgeAutomationReport({ ...counts, inProcess: 1 }, 1).pass).toBe(false);
  });
  it("fails when fewer tests passed than expected (a test silently not compiled)", () => {
    expect(judgeAutomationReport(counts, 5)).toEqual({ pass: false, reason: "4 passed, fewer than the 5 expected" });
  });
});

describe("RunnerConfigSchema.modTest", () => {
  const base = {
    serverDir: "s", apiBaseUrl: "https://127.0.0.1:7777", buildCommand: ["b"], builtPluginDir: "{spike}",
    installed: { engine: "5.6.1", sml: "3.12.0" },
  };
  it("is optional", () => expect(RunnerConfigSchema.safeParse(base).success).toBe(true));
  it("takes the SML project and the engine root", () => {
    expect(RunnerConfigSchema.safeParse({ ...base, modTest: { project: "p", engine: "e" } }).success).toBe(true);
  });
  it("refuses unknown keys", () => {
    expect(RunnerConfigSchema.safeParse({ ...base, modTest: { project: "p", engine: "e", x: 1 } }).success).toBe(false);
  });
});
```

Add to `harness/test/check-api.test.ts` (it already imports from `../src/check-api`; add `scanSources` and `checkApi` to that import, and `mkdtempSync`, `mkdirSync`, `writeFileSync`, `rmSync`, `tmpdir` and `join` where they're missing):

```ts
describe("scanSources", () => {
  it("scans mod/ as well as spikes/, and excuses the plugin's own modules", () => {
    const root = mkdtempSync(join(tmpdir(), "scan-"));
    try {
      mkdirSync(join(root, "spikes", "S9"), { recursive: true });
      mkdirSync(join(root, "mod", "P", "Source", "A"), { recursive: true });
      mkdirSync(join(root, "mod", "P", "Source", "B"), { recursive: true });
      writeFileSync(join(root, "spikes", "S9", "x.cpp"), "void f() { FSpikeThing::Go(); }\n");
      writeFileSync(join(root, "mod", "P", "Source", "A", "A.Build.cs"),
        'PublicDependencyModuleNames.AddRange(new string[] { "Core" });\n');
      writeFileSync(join(root, "mod", "P", "Source", "B", "B.Build.cs"),
        'PublicDependencyModuleNames.AddRange(new string[] { "Core", "A" });\n');
      const files = scanSources(root);
      expect(files.map((f) => f.path)).toEqual([
        "spikes/S9/x.cpp", "mod/P/Source/A/A.Build.cs", "mod/P/Source/B/B.Build.cs",
      ]);
      const md = "| Symbol | Header | Source |\n| --- | --- | --- |\n| module:Core | Engine/Source/Runtime/Core/Core.Build.cs:1 | header only |\n";
      const names = checkApi(files, md).missing.map((u) => u.name);
      expect(names.some((n) => n.startsWith("FSpikeThing"))).toBe(true); // spikes/ is still checked
      expect(names).not.toContain("module:A"); // the plugin's own module needs no row
    } finally {
      rmSync(root, { recursive: true, force: true });
    }
  });
});
```

(If the table header in `api-sources.md`'s parser needs a different first line, copy the first two table lines from `api-sources.md` instead. `parseSourceMap` is the authority.)

- [ ] **Step 2: Run them to verify they fail**

Run: `cd harness; npx vitest run test/mod-test.test.ts test/check-api.test.ts --reporter=dot`
Expected: FAIL. `../src/mod-test` doesn't exist, and `scanSources` isn't exported.

- [ ] **Step 3: Write the harness code**

`harness/src/mod-test.ts`:

```ts
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { z } from "zod";

/**
 * UE's test-pass summary: FAutomatedTestPassResults (Engine/Source/Developer/AutomationController/Private/
 * AutomationControllerManager.h:110-147), written by FJsonObjectConverter::UStructToJsonObjectString to
 * <ReportExportPath>/index.json (AutomationControllerManager.cpp:670-672). The converter lower-cases the first letter
 * of each property name. Other fields (devices, tests, durations) are ignored.
 */
export const AutomationReportSchema = z.object({
  succeeded: z.number().int().min(0),
  succeededWithWarnings: z.number().int().min(0),
  failed: z.number().int().min(0),
  notRun: z.number().int().min(0),
  inProcess: z.number().int().min(0),
});
export type AutomationReport = z.infer<typeof AutomationReportSchema>;

/** index.json is written by FFileHelper::SaveStringToFile, which picks UTF-16 with a BOM for non-ASCII text. */
export function decodeReport(bytes: Buffer): string {
  if (bytes[0] === 0xff && bytes[1] === 0xfe) return bytes.subarray(2).toString("utf16le");
  if (bytes[0] === 0xef && bytes[1] === 0xbb && bytes[2] === 0xbf) return bytes.subarray(3).toString("utf8");
  return bytes.toString("utf8");
}

export function readAutomationReport(dir: string): AutomationReport {
  return AutomationReportSchema.parse(JSON.parse(decodeReport(readFileSync(join(dir, "index.json")))));
}

/** A pass needs no failure, nothing left unrun, and at least `minTests` passed (with or without warnings). */
export function judgeAutomationReport(r: AutomationReport, minTests: number): { pass: boolean; reason: string } {
  const passed = r.succeeded + r.succeededWithWarnings;
  if (r.failed > 0) return { pass: false, reason: `${r.failed} failed, ${passed} passed` };
  if (r.notRun > 0 || r.inProcess > 0) return { pass: false, reason: `${r.notRun} not run, ${r.inProcess} unfinished` };
  if (passed < minTests) return { pass: false, reason: `${passed} passed, fewer than the ${minTests} expected` };
  return { pass: true, reason: `${passed} passed` };
}
```

`harness/src/config.ts`, in `RunnerConfigSchema`, after `s5`:

```ts
  /** The core mod's unit tests (scripts/test-mod.ps1): the SML project folder and the engine root. */
  modTest: z.strictObject({ project: z.string().min(1), engine: z.string().min(1) }).optional(),
```

`harness/src/check-api.ts`: replace `scanSpikes` with the following, and change `cli.ts`'s import and its call in `runCheckApi` to `scanSources`:

```ts
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
```

(Add `existsSync` to its `node:fs` import.) In `extractUses`, a module that the same group builds is its own, not an outside dependency:

```ts
  for (const group of bySpike.values()) {
    const locals = localsOf(group);
    const ownModules = new Set(group.filter((f) => f.path.endsWith(".Build.cs")).map((f) => basename(f.path, ".Build.cs")));
    for (const f of group) {
      if (f.path.endsWith(".Build.cs")) uses.push(...modulesInBuildCs(f).filter((u) => !ownModules.has(u.name.slice("module:".length))));
      else if (f.path.endsWith(".uplugin")) uses.push(...pluginsInUplugin(f));
      else if (CODE_EXT.test(f.path)) uses.push(...usesInCode(f, locals));
    }
  }
```

`harness/src/cli.ts`: import `{ judgeAutomationReport, readAutomationReport }` from `./mod-test`, and add before `default:`:

```ts
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
```

Change the `default:` usage line to `"usage: harness <check-api|verify-contract|selftest|run|rejudge|test-mod|judge-mod-test>"`.

- [ ] **Step 4: Run the harness tests to verify they pass**

Run: `cd harness; npx vitest run test/mod-test.test.ts test/check-api.test.ts --reporter=dot; npm run typecheck`
Expected: PASS, and typecheck exits 0.

- [ ] **Step 5: Write the plugin scaffold and its first test**

`mod/SatisDashAgent/SatisDashAgent.uplugin`:

```json
{
	"FileVersion": 3,
	"Version": 1,
	"VersionName": "0.1.0",
	"SemVersion": "0.1.0",
	"GameVersion": ">=513885",
	"FriendlyName": "satis-dash agent",
	"Description": "Reports a dedicated server's status, power and auto-pause to a satis-dash backend, and applies its auto-pause commands.",
	"Category": "Modding",
	"CreatedBy": "Sour-Dev-Home",
	"CreatedByURL": "https://github.com/Sour-Dev-Home/satisfactory-dash-mod",
	"DocsURL": "",
	"MarketplaceURL": "",
	"SupportURL": "",
	"CanContainContent": false,
	"IsBetaVersion": false,
	"IsExperimentalVersion": true,
	"Installed": false,
	"Modules": [
		{ "Name": "SatisDashCore", "Type": "Runtime", "LoadingPhase": "Default" },
		{ "Name": "SatisDashCoreTests", "Type": "Editor", "LoadingPhase": "Default" }
	],
	"Plugins": [
		{ "Name": "SML", "Enabled": true, "SemVersion": "^3.12.0" }
	]
}
```

`Source/SatisDashCore/SatisDashCore.Build.cs`:

```cs
using UnrealBuildTool;

public class SatisDashCore : ModuleRules
{
	public SatisDashCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		// Pure logic only (spec "Portability"): no FactoryGame, SML or HTTP here. Plan 2B's module reads the game.
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "Json" });
	}
}
```

`Source/SatisDashCoreTests/SatisDashCoreTests.Build.cs`:

```cs
using UnrealBuildTool;

public class SatisDashCoreTests : ModuleRules
{
	public SatisDashCoreTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		// An Editor module: never built into a server package, so no test code ships (spec S14).
		PrivateDependencyModuleNames.AddRange(new string[] { "Core", "Json", "Projects", "SatisDashCore" });
	}
}
```

`Public/SatisDash/Version.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

namespace SatisDash
{
	/** What the mod sends as agentVersion: "mod/<version>", at most 32 characters (spec "The contract it speaks"). */
	SATISDASHCORE_API FString AgentVersion();
}
```

`Public/SatisDash/Log.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

/** The mod's only log category. Display or Warning, never Error; never the secret or the code (spec S8). */
SATISDASHCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogSatisDash, Log, All);
```

`Private/Version.cpp`:

```cpp
#include "SatisDash/Version.h"

FString SatisDash::AgentVersion()
{
	return TEXT("mod/0.1.0");
}
```

`Private/SatisDashCoreModule.cpp`:

```cpp
#include "Modules/ModuleManager.h"
#include "SatisDash/Log.h"

DEFINE_LOG_CATEGORY(LogSatisDash);

IMPLEMENT_MODULE(FDefaultModuleImpl, SatisDashCore)
```

`SatisDashCoreTests/Private/SatisDashCoreTestsModule.cpp`:

```cpp
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, SatisDashCoreTests)
```

`SatisDashCoreTests/Private/TestUtil.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

namespace SatisDash::Test
{
	/** Every SatisDash test: fast, editor-only, no world. */
	inline constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** A case-sensitive text check (FAutomationTestBase's FString comparison is not). */
	inline bool SameText(FAutomationTestBase& Test, const TCHAR* What, const FString& Actual, const FString& Expected)
	{
		if (Actual.Equals(Expected, ESearchCase::CaseSensitive))
		{
			return true;
		}
		Test.AddError(FString::Printf(TEXT("%s: expected '%s', got '%s'"), What, *Expected, *Actual));
		return false;
	}
}
```

`SatisDashCoreTests/Private/VersionTest.cpp`:

```cpp
#include "SatisDash/Log.h"
#include "SatisDash/Version.h"
#include "TestUtil.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSatisDashVersionTest, "SatisDash.Core.Version", SatisDash::Test::Flags)

bool FSatisDashVersionTest::RunTest(const FString& Parameters)
{
	const FString Version = SatisDash::AgentVersion();
	SatisDash::Test::SameText(*this, TEXT("agentVersion"), Version, TEXT("mod/0.1.0"));
	TestTrue(TEXT("at most 32 characters"), Version.Len() <= 32);
	return true;
}

// Probes how this project's test settings treat a Warning logged during a test. The mod logs failures at Warning, so
// the judge must still count such a test as passed (succeededWithWarnings).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSatisDashWarningProbeTest, "SatisDash.Core.WarningProbe", SatisDash::Test::Flags)

bool FSatisDashWarningProbeTest::RunTest(const FString& Parameters)
{
	UE_LOG(LogSatisDash, Warning, TEXT("warning probe (expected in the test run)"));
	return true;
}
```

- [ ] **Step 6: Write the test runner script, the workflow and the docs**

`scripts/test-mod.ps1`:

```powershell
# Builds the core mod (mod/SatisDashAgent) into the SML project's editor and runs its automation tests headless.
# The command is UE's own: "Automation RunTests <filter>; Quit" (Engine/Source/Developer/AutomationController/Private/
# AutomationCommandline.cpp:580, :696) quits once the queue is empty (:474), and -ReportExportPath= writes index.json
# there (AutomationControllerManager.cpp:194, :672). harness judge-mod-test reads it.
param(
	[Parameter(Mandatory)] [string] $Project,
	[Parameter(Mandatory)] [string] $Engine,
	[Parameter(Mandatory)] [string] $Out,
	[string] $Filter = 'SatisDash.'
)
$ErrorActionPreference = 'Stop'
# Same reason as build-spike.ps1: the SML project's pre-build step runs a PowerShell script.
$env:PSExecutionPolicyPreference = 'RemoteSigned'
$plugin = 'SatisDashAgent'
$repo = Split-Path $PSScriptRoot -Parent
$source = Join-Path $repo "mod\$plugin"
$uproject = Join-Path $Project 'FactoryGame.uproject'
if (-not (Test-Path "$source\$plugin.uplugin")) { throw "no plugin at $source" }
if (-not (Test-Path $uproject)) { throw "no SML project at $uproject" }

# FactoryEditor builds every plugin in Mods; spike plugins left there would build too.
Get-ChildItem (Join-Path $Project 'Mods') -Directory -Filter 'SatisMod*' | Remove-Item -Recurse -Force
$target = Join-Path $Project "Mods\$plugin"
robocopy $source $target /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }

& (Join-Path $Engine 'Engine\Build\BatchFiles\Build.bat') FactoryEditor Win64 Development "-project=$uproject" -WaitMutex -NoHotReload
if ($LASTEXITCODE -ne 0) { throw "FactoryEditor build failed ($LASTEXITCODE)" }

if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }
New-Item -ItemType Directory -Path $Out | Out-Null
& (Join-Path $Engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') $uproject "-ExecCmds=Automation RunTests $Filter; Quit" `
	"-ReportExportPath=$Out" -unattended -nullrhi -nosplash -nosound -nop4 -stdout -FullStdOutLogOutput
if ($LASTEXITCODE -ne 0) { throw "the editor exited with $LASTEXITCODE" }
if (-not (Test-Path (Join-Path $Out 'index.json'))) { throw "no index.json in $Out" }
Write-Output "tests ran -> $(Join-Path $Out 'index.json')"
```

`scripts/build-spike.ps1`: on the line that clears other spikes, also clear the core mod, so a half-built core mod never breaks a spike build:

```powershell
Get-ChildItem (Join-Path $Project 'Mods') -Directory |
	Where-Object { ($_.Name -like 'SatisMod*' -and $_.Name -ne $plugin) -or $_.Name -eq 'SatisDashAgent' } |
	Remove-Item -Recurse -Force
```

`.github/workflows/mod-test.yml` (copy the pinned action SHAs from `spike.yml`):

```yaml
name: mod-test

# The core mod's unit tests (UE automation tests) on the owner's self-hosted `ue` runner (docs/runner.md). This is a
# public repository, so this workflow must never trigger from a pull request: fork code would run on the owner's PC.
on:
  workflow_dispatch:
  push:
    branches: [main]
    paths: ["mod/**", "scripts/test-mod.ps1", "harness/src/mod-test.ts", ".github/workflows/mod-test.yml"]

permissions:
  contents: read

concurrency:
  group: ue-runner
  cancel-in-progress: false

jobs:
  test:
    runs-on: [self-hosted, windows, ue]
    environment: ue-runner
    timeout-minutes: 90
    defaults:
      run:
        shell: pwsh
        working-directory: harness
    steps:
      - uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7
        with:
          persist-credentials: false
      - uses: actions/setup-node@949feb2413d6458794dcd2491c4babbbce0c15c1 # v7
        with:
          node-version-file: harness/.node-version
      - run: npm ci
      - run: npm run harness -- check-api
      - run: npm run harness -- test-mod --out "$env:RUNNER_TEMP/mod-test"
      - if: always()
        uses: actions/upload-artifact@cf430e030ddbb5b0abf93d22962f4752f3646cd9 # v7.0.2
        with:
          name: mod-test-${{ github.run_id }}
          path: ${{ runner.temp }}/mod-test
          if-no-files-found: warn
          retention-days: 30
```

`docs/runner.md`: add a "Core mod tests" section saying three things:
- the runner config gains `"modTest": { "project": "<SML project folder>", "engine": "<engine root>" }`, the same two folders `buildCommand` already passes to `build-spike.ps1`;
- the owner adds it once;
- `mod-test.yml` runs `harness test-mod`, and needs the `ue-runner` deployment approved like `spike.yml`.

`api-sources.md`:
- the intro line becomes "used under `spikes/` and `mod/`";
- add these rows (looked up 2026-10-20), plus a row for every other symbol `check-api` reports, each looked up in the pinned checkout:

```
| Misc/AutomationTest.h | Engine/Source/Runtime/Core/Public/Misc/AutomationTest.h:1 | header only |
| IMPLEMENT_SIMPLE_AUTOMATION_TEST | Engine/Source/Runtime/Core/Public/Misc/AutomationTest.h:4161 | header only (inside #if WITH_AUTOMATION_WORKER, :4160) |
| EAutomationTestFlags | Engine/Source/Runtime/Core/Public/Misc/AutomationTest.h:85 | header only |
| EAutomationTestFlags::EditorContext | Engine/Source/Runtime/Core/Public/Misc/AutomationTest.h:90 | header only |
| EAutomationTestFlags::EngineFilter | Engine/Source/Runtime/Core/Public/Misc/AutomationTest.h:124 | header only |
| TestTrue | Engine/Source/Runtime/Core/Public/Misc/AutomationTest.h:2534 | header only |
| TestFalse | Engine/Source/Runtime/Core/Public/Misc/AutomationTest.h:2298 | header only |
| TestEqual | Engine/Source/Runtime/Core/Public/Misc/AutomationTest.h:1949 | header only (the int32 overload; there are many) |
```

- [ ] **Step 7: Run check-api, then the C++ tests**

Run: `cd harness; npm run harness -- check-api`
Expected: `check-api: 0 missing, 0 bad rows`. Add a looked-up row for each symbol it reports, such as `DECLARE_LOG_CATEGORY_EXTERN`, `DEFINE_LOG_CATEGORY`, `FDefaultModuleImpl`, `FAutomationTestBase::AddError`, `module:Projects` and `FString::Equals`, until it does.

Run the script and the judge as in "How the tests run", with `--min 2`.
Expected: `mod tests: PASS (2 passed)`. `index.json` shows `SatisDash.Core.WarningProbe` under `succeededWithWarnings`.
- If the probe **fails** (the project treats warnings as errors), that is a ruling: the mod logs failures at `Display`. Record it in the ledger and in the Global Constraints of 2B.
- If the editor can't run the tests headless at all, that is the spec's fallback (spec "Testing"): stop, and bring the owner a ruling to move these cases into harness checks against a stub backend.

- [ ] **Step 8: Commit, and open Task 1's own PR**

```bash
git add mod scripts harness .github/workflows/mod-test.yml api-sources.md docs/runner.md
git commit -m "Core mod: the plugin scaffold and its headless unit tests (Plan 2A Task 1)"
```

Open the PR. Once it merges, the owner adds `modTest` to the runner config and runs `mod-test.yml` once from `main`. Tasks 2–10 go on one branch cut from that `main`.

---

### Task 2: Config (`SatisDashAgent.cfg`) and the atomic file write

The spec puts the config at `FactoryGame/Configs/SatisDashAgent.cfg`. That's the path SML's config manager uses (`Mods/SML/Source/SML/Private/Configuration/ConfigManager.cpp:310`, `:317`). The mod reads and writes it as **plain JSON** and doesn't register with SML's `ConfigManager`, as spikes S4 and S5 did. Two of the spec's error-table rules decide that:
- a broken file is left as it is, never overwritten;
- a missing file is written with the defaults.

The mod must control both, and S4's registration call (`SatisModS4.cpp:34-35`) hands the file to SML, which writes it. **Ruling:** plain JSON, at SML's path. **Cost if wrong:** owners don't get an in-game config screen, which a dedicated server has no use for anyway.

**Files:**
- Create: `Source/SatisDashCore/Public/SatisDash/JsonUtil.h`, `Private/JsonUtil.cpp`
- Create: `Source/SatisDashCore/Public/SatisDash/Files.h`, `Private/Files.cpp`
- Create: `Source/SatisDashCore/Public/SatisDash/Config.h`, `Private/Config.cpp`
- Modify: `Source/SatisDashCoreTests/Private/TestUtil.h` (temp dir, read/write helpers)
- Test: `Source/SatisDashCoreTests/Private/ConfigTest.cpp`

**Interfaces:**
- Produces (namespace `SatisDash`):
  - `TSharedPtr<FJsonObject> Json::ParseObject(const FString&)`
  - `TSharedPtr<FJsonValue> Json::Field(const TSharedPtr<FJsonObject>&, const TCHAR* Name, EJson Type)`
  - `FString Json::Write(const TSharedRef<FJsonObject>&)` (condensed) and `FString Json::WritePretty(const TSharedRef<FJsonObject>&)`
  - `bool WriteFileAtomic(const FString& Path, const FString& Text)`
  - `DefaultBackendUrl`
  - `struct FAgentConfig { FString BackendUrl; FString EnrollmentCode; }`
  - `enum class EConfigStatus { Missing, Broken, Valid }`
  - `struct FConfigResult { EConfigStatus Status; FAgentConfig Config; FString BadField; }`
  - `IsAllowedBackendUrl`, `IsEnrollmentCode`, `ParseConfig`, `ConfigToJson`, `LoadConfigFile`, `BlankConfigCode`, `ConfigModTime`
  - in tests: `Test::FTempDir`, `Test::ReadText`, `Test::WriteText`

- [ ] **Step 1: Write the failing tests**

Add to `TestUtil.h` (with includes `HAL/FileManager.h`, `Misc/FileHelper.h`, `Misc/Guid.h`, `Misc/Paths.h`):

```cpp
namespace SatisDash::Test
{
	/** A fresh folder under Saved/SatisDashTests, deleted when it goes out of scope. */
	struct FTempDir
	{
		FString Path = FPaths::ProjectSavedDir() / TEXT("SatisDashTests") / FGuid::NewGuid().ToString();
		FTempDir() { IFileManager::Get().MakeDirectory(*Path, true); }
		~FTempDir() { IFileManager::Get().DeleteDirectory(*Path, false, true); }
		FString File(const TCHAR* Name) const { return Path / Name; }
	};

	inline FString ReadText(const FString& Path)
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *Path);
		return Text;
	}

	inline void WriteText(const FString& Path, const FString& Text)
	{
		FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
}
```

`ConfigTest.cpp`:

```cpp
#include "SatisDash/Config.h"
#include "TestUtil.h"

using namespace SatisDash;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConfigUrlTest, "SatisDash.Core.Config.BackendUrl", SatisDash::Test::Flags)

bool FConfigUrlTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("https host"), IsAllowedBackendUrl(TEXT("https://api.satis-manager.com")));
	TestTrue(TEXT("https host, port and path"), IsAllowedBackendUrl(TEXT("https://example.com:8443/base")));
	TestTrue(TEXT("http on 127.0.0.1"), IsAllowedBackendUrl(TEXT("http://127.0.0.1:3000")));
	TestTrue(TEXT("http on localhost"), IsAllowedBackendUrl(TEXT("http://LocalHost:3000")));
	TestFalse(TEXT("http elsewhere"), IsAllowedBackendUrl(TEXT("http://example.com")));
	TestFalse(TEXT("http look-alike host"), IsAllowedBackendUrl(TEXT("http://127.0.0.1.evil.example")));
	TestFalse(TEXT("http userinfo trick"), IsAllowedBackendUrl(TEXT("http://localhost@evil.example")));
	TestFalse(TEXT("https userinfo"), IsAllowedBackendUrl(TEXT("https://user:pw@example.com")));
	TestFalse(TEXT("no scheme"), IsAllowedBackendUrl(TEXT("example.com")));
	TestFalse(TEXT("another scheme"), IsAllowedBackendUrl(TEXT("ftp://example.com")));
	TestFalse(TEXT("no host"), IsAllowedBackendUrl(TEXT("https://")));
	TestFalse(TEXT("query"), IsAllowedBackendUrl(TEXT("https://example.com/?x=1")));
	TestFalse(TEXT("fragment"), IsAllowedBackendUrl(TEXT("https://example.com/#x")));
	TestFalse(TEXT("whitespace"), IsAllowedBackendUrl(TEXT("https://exa mple.com")));
	TestFalse(TEXT("backslash"), IsAllowedBackendUrl(TEXT("https://example.com\\x")));
	TestFalse(TEXT("port out of range"), IsAllowedBackendUrl(TEXT("https://example.com:99999")));
	TestFalse(TEXT("empty port"), IsAllowedBackendUrl(TEXT("https://example.com:")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConfigParseTest, "SatisDash.Core.Config.Parse", SatisDash::Test::Flags)

bool FConfigParseTest::RunTest(const FString& Parameters)
{
	const FConfigResult Ok = ParseConfig(TEXT("{\"backendUrl\":\"https://example.com/\",\"enrollmentCode\":\" ab3d-7xq2 \"}"));
	TestTrue(TEXT("valid"), Ok.Status == EConfigStatus::Valid);
	Test::SameText(*this, TEXT("trailing slash dropped"), Ok.Config.BackendUrl, TEXT("https://example.com"));
	Test::SameText(*this, TEXT("code trimmed and upper-cased"), Ok.Config.EnrollmentCode, TEXT("AB3D-7XQ2"));

	TestTrue(TEXT("no code field is an empty code"),
		ParseConfig(TEXT("{\"backendUrl\":\"https://example.com\"}")).Config.EnrollmentCode.IsEmpty());
	TestTrue(TEXT("unknown fields ignored"),
		ParseConfig(TEXT("{\"backendUrl\":\"https://example.com\",\"x\":1}")).Status == EConfigStatus::Valid);

	auto Bad = [this](const TCHAR* Text, const TCHAR* Field)
	{
		const FConfigResult R = ParseConfig(Text);
		TestTrue(FString::Printf(TEXT("broken: %s"), Text), R.Status == EConfigStatus::Broken);
		Test::SameText(*this, TEXT("bad field"), R.BadField, Field);
	};
	Bad(TEXT("not json"), TEXT("json"));
	Bad(TEXT("[]"), TEXT("json"));
	Bad(TEXT("{}"), TEXT("backendUrl"));
	Bad(TEXT("{\"backendUrl\":5}"), TEXT("backendUrl"));
	Bad(TEXT("{\"backendUrl\":\"http://example.com\"}"), TEXT("backendUrl"));
	Bad(TEXT("{\"backendUrl\":\"https://example.com\",\"enrollmentCode\":\"AB3D7XQ2\"}"), TEXT("enrollmentCode"));
	Bad(TEXT("{\"backendUrl\":\"https://example.com\",\"enrollmentCode\":\"AB1D-7XQ2\"}"), TEXT("enrollmentCode"));
	Bad(TEXT("{\"backendUrl\":\"https://example.com\",\"enrollmentCode\":12}"), TEXT("enrollmentCode"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConfigFileTest, "SatisDash.Core.Config.File", SatisDash::Test::Flags)

bool FConfigFileTest::RunTest(const FString& Parameters)
{
	const Test::FTempDir Dir;
	const FString Path = Dir.File(TEXT("SatisDashAgent.cfg"));

	const FConfigResult Missing = LoadConfigFile(Path);
	TestTrue(TEXT("a missing file is reported"), Missing.Status == EConfigStatus::Missing);
	Test::SameText(*this, TEXT("default url"), Missing.Config.BackendUrl, DefaultBackendUrl);
	const FConfigResult Written = ParseConfig(Test::ReadText(Path));
	TestTrue(TEXT("the default file is written and valid"), Written.Status == EConfigStatus::Valid);
	TestTrue(TEXT("with an empty code"), Written.Config.EnrollmentCode.IsEmpty());

	Test::WriteText(Path, TEXT("{ broken"));
	TestTrue(TEXT("broken"), LoadConfigFile(Path).Status == EConfigStatus::Broken);
	Test::SameText(*this, TEXT("a broken file is left as it is"), Test::ReadText(Path), TEXT("{ broken"));

	// Notepad: a UTF-8 BOM and CRLF line ends.
	FFileHelper::SaveStringToFile(TEXT("{\r\n  \"backendUrl\": \"https://example.com\",\r\n  \"enrollmentCode\": \"AB3D-7XQ2\"\r\n}\r\n"),
		*Path, FFileHelper::EEncodingOptions::ForceUTF8);
	const FConfigResult Edited = LoadConfigFile(Path);
	TestTrue(TEXT("BOM and CRLF are read"), Edited.Status == EConfigStatus::Valid);

	TestTrue(TEXT("blanking succeeds"), BlankConfigCode(Path, Edited.Config));
	const FConfigResult Blanked = LoadConfigFile(Path);
	TestTrue(TEXT("the code is blanked"), Blanked.Config.EnrollmentCode.IsEmpty());
	Test::SameText(*this, TEXT("the url is kept"), Blanked.Config.BackendUrl, TEXT("https://example.com"));
	TestFalse(TEXT("no temp file left"), IFileManager::Get().FileExists(*(Path + TEXT(".tmp"))));

	TestTrue(TEXT("a missing file's time"), ConfigModTime(Dir.File(TEXT("nope.cfg"))) == FDateTime::MinValue());
	TestTrue(TEXT("an existing file's time"), ConfigModTime(Path) > FDateTime::MinValue());
	return true;
}
```

- [ ] **Step 2: Run them to verify they fail**

Run the tests as in "How the tests run".
Expected: the build fails because `SatisDash/Config.h` doesn't exist. A build failure is this step's expected failure.

- [ ] **Step 3: Write the implementation**

`Public/SatisDash/JsonUtil.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace SatisDash::Json
{
	/** The text as a JSON object, or null when it isn't valid JSON or isn't an object. */
	SATISDASHCORE_API TSharedPtr<FJsonObject> ParseObject(const FString& Text);
	/** The named field when it exists and has exactly this type (TryGetStringField would turn a number into text). */
	SATISDASHCORE_API TSharedPtr<FJsonValue> Field(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, EJson Type);
	SATISDASHCORE_API FString Write(const TSharedRef<FJsonObject>& Object);
	/** Indented, for the file an owner edits by hand. */
	SATISDASHCORE_API FString WritePretty(const TSharedRef<FJsonObject>& Object);
}
```

`Private/JsonUtil.cpp`:

```cpp
#include "SatisDash/JsonUtil.h"

#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

TSharedPtr<FJsonObject> SatisDash::Json::ParseObject(const FString& Text)
{
	TSharedPtr<FJsonObject> Object;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
	{
		return nullptr;
	}
	return Object;
}

TSharedPtr<FJsonValue> SatisDash::Json::Field(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, EJson Type)
{
	if (!Object.IsValid())
	{
		return nullptr;
	}
	const TSharedPtr<FJsonValue> Value = Object->TryGetField(Name);
	return Value.IsValid() && Value->Type == Type ? Value : nullptr;
}

FString SatisDash::Json::Write(const TSharedRef<FJsonObject>& Object)
{
	FString Out;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Object, Writer);
	return Out;
}

FString SatisDash::Json::WritePretty(const TSharedRef<FJsonObject>& Object)
{
	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Object, Writer);
	return Out;
}
```

`Public/SatisDash/Files.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

namespace SatisDash
{
	/**
	 * Writes Text as UTF-8 without a BOM to Path + ".tmp", then moves it over Path, so a crash leaves the old file or
	 * the new one, never half of one (spec S11). Creates the folder. On Linux the file is made owner-only (0600)
	 * before the move.
	 */
	SATISDASHCORE_API bool WriteFileAtomic(const FString& Path, const FString& Text);
}
```

`Private/Files.cpp`:

```cpp
#include "SatisDash/Files.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if PLATFORM_LINUX
#include <sys/stat.h>
#endif

bool SatisDash::WriteFileAtomic(const FString& Path, const FString& Text)
{
	IFileManager& Files = IFileManager::Get();
	if (!Files.MakeDirectory(*FPaths::GetPath(Path), true))
	{
		return false;
	}
	const FString Temp = Path + TEXT(".tmp");
	if (!FFileHelper::SaveStringToFile(Text, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		return false;
	}
#if PLATFORM_LINUX
	if (chmod(TCHAR_TO_UTF8(*Temp), S_IRUSR | S_IWUSR) != 0)
	{
		Files.Delete(*Temp, false, false, true);
		return false;
	}
#endif
	return Files.Move(*Path, *Temp, true);
}
```

`Public/SatisDash/Config.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

namespace SatisDash
{
	/** The spec's default backend ("Shape", Config). */
	inline const TCHAR* DefaultBackendUrl = TEXT("https://api.satis-manager.com");

	struct FAgentConfig
	{
		/** Checked by IsAllowedBackendUrl, with no trailing slash. */
		FString BackendUrl;
		/** Empty, or a valid code in upper case. */
		FString EnrollmentCode;
	};

	enum class EConfigStatus : uint8
	{
		/** There was no file; one was written with the default URL and no code. Config holds those defaults. */
		Missing,
		/** The file can't be used. BadField names the first problem: "json", "backendUrl" or "enrollmentCode". */
		Broken,
		Valid,
	};

	struct FConfigResult
	{
		EConfigStatus Status = EConfigStatus::Broken;
		FAgentConfig Config;
		FString BadField;
	};

	/** S1: https:// with a host, or http:// only to 127.0.0.1 or localhost (the harness). No user info, query or fragment. */
	SATISDASHCORE_API bool IsAllowedBackendUrl(const FString& Url);
	/** The contract's code: four base32 characters, a dash, four more (EnrollmentCodeSchema). */
	SATISDASHCORE_API bool IsEnrollmentCode(const FString& Code);
	/** Trims both values, upper-cases the code and drops trailing slashes from the URL, then checks them. */
	SATISDASHCORE_API FConfigResult ParseConfig(const FString& Text);
	SATISDASHCORE_API FString ConfigToJson(const FAgentConfig& Config);
	/** Reads the file. A missing one is written with the defaults; a broken one is never overwritten. */
	SATISDASHCORE_API FConfigResult LoadConfigFile(const FString& Path);
	/** Rewrites the file with the code blanked, keeping its URL. */
	SATISDASHCORE_API bool BlankConfigCode(const FString& Path, const FAgentConfig& Config);
	/** The file's modification time; FDateTime::MinValue() when it doesn't exist. */
	SATISDASHCORE_API FDateTime ConfigModTime(const FString& Path);
}
```

`Private/Config.cpp`:

```cpp
#include "SatisDash/Config.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "SatisDash/Files.h"
#include "SatisDash/JsonUtil.h"

namespace
{
	bool IsBase32(TCHAR C)
	{
		return (C >= TEXT('A') && C <= TEXT('Z')) || (C >= TEXT('2') && C <= TEXT('7'));
	}
}

bool SatisDash::IsAllowedBackendUrl(const FString& Url)
{
	bool bHttps = false;
	FString Rest;
	if (Url.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase))
	{
		bHttps = true;
		Rest = Url.RightChop(8);
	}
	else if (Url.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase))
	{
		Rest = Url.RightChop(7);
	}
	else
	{
		return false;
	}
	for (const TCHAR C : Url)
	{
		if (C <= TEXT(' ') || C == 127 || C == TEXT('\\') || C == TEXT('?') || C == TEXT('#'))
		{
			return false;
		}
	}
	int32 Slash = INDEX_NONE;
	const FString Authority = Rest.FindChar(TEXT('/'), Slash) ? Rest.Left(Slash) : Rest;
	if (Authority.IsEmpty() || Authority.Contains(TEXT("@")))
	{
		return false;
	}
	FString Host = Authority;
	int32 Colon = INDEX_NONE;
	if (Authority.FindLastChar(TEXT(':'), Colon))
	{
		const FString Port = Authority.RightChop(Colon + 1);
		if (Port.IsEmpty() || Port.Len() > 5)
		{
			return false;
		}
		for (const TCHAR C : Port)
		{
			if (!FChar::IsDigit(C))
			{
				return false;
			}
		}
		const int32 Number = FCString::Atoi(*Port);
		if (Number < 1 || Number > 65535)
		{
			return false;
		}
		Host = Authority.Left(Colon);
	}
	if (Host.IsEmpty())
	{
		return false;
	}
	return bHttps || Host.Equals(TEXT("127.0.0.1"), ESearchCase::CaseSensitive) || Host.Equals(TEXT("localhost"), ESearchCase::IgnoreCase);
}

bool SatisDash::IsEnrollmentCode(const FString& Code)
{
	if (Code.Len() != 9 || Code[4] != TEXT('-'))
	{
		return false;
	}
	for (int32 I = 0; I < 9; ++I)
	{
		if (I != 4 && !IsBase32(Code[I]))
		{
			return false;
		}
	}
	return true;
}

SatisDash::FConfigResult SatisDash::ParseConfig(const FString& Text)
{
	FConfigResult Result;
	const TSharedPtr<FJsonObject> Object = Json::ParseObject(Text);
	if (!Object.IsValid())
	{
		Result.BadField = TEXT("json");
		return Result;
	}
	const TSharedPtr<FJsonValue> UrlValue = Json::Field(Object, TEXT("backendUrl"), EJson::String);
	FString Url = UrlValue.IsValid() ? UrlValue->AsString().TrimStartAndEnd() : FString();
	while (Url.EndsWith(TEXT("/")))
	{
		Url.LeftChopInline(1);
	}
	if (!IsAllowedBackendUrl(Url))
	{
		Result.BadField = TEXT("backendUrl");
		return Result;
	}
	FString Code;
	if (Object->HasField(TEXT("enrollmentCode")))
	{
		const TSharedPtr<FJsonValue> CodeValue = Json::Field(Object, TEXT("enrollmentCode"), EJson::String);
		Code = CodeValue.IsValid() ? CodeValue->AsString().TrimStartAndEnd().ToUpper() : FString();
		if (!CodeValue.IsValid() || (!Code.IsEmpty() && !IsEnrollmentCode(Code)))
		{
			Result.BadField = TEXT("enrollmentCode");
			return Result;
		}
	}
	Result.Status = EConfigStatus::Valid;
	Result.Config.BackendUrl = Url;
	Result.Config.EnrollmentCode = Code;
	return Result;
}

FString SatisDash::ConfigToJson(const FAgentConfig& Config)
{
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("backendUrl"), Config.BackendUrl);
	Object->SetStringField(TEXT("enrollmentCode"), Config.EnrollmentCode);
	return Json::WritePretty(Object);
}

SatisDash::FConfigResult SatisDash::LoadConfigFile(const FString& Path)
{
	if (!IFileManager::Get().FileExists(*Path))
	{
		FConfigResult Result;
		Result.Status = EConfigStatus::Missing;
		Result.Config.BackendUrl = DefaultBackendUrl;
		WriteFileAtomic(Path, ConfigToJson(Result.Config));
		return Result;
	}
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		FConfigResult Result;
		Result.BadField = TEXT("json");
		return Result;
	}
	return ParseConfig(Text);
}

bool SatisDash::BlankConfigCode(const FString& Path, const FAgentConfig& Config)
{
	FAgentConfig Blanked = Config;
	Blanked.EnrollmentCode.Reset();
	return WriteFileAtomic(Path, ConfigToJson(Blanked));
}

FDateTime SatisDash::ConfigModTime(const FString& Path)
{
	return IFileManager::Get().GetTimeStamp(*Path);
}
```

- [ ] **Step 4: Run check-api and the tests to verify they pass**

Run: `cd harness; npm run harness -- check-api`. Add a looked-up row for each new symbol it reports.
Expected: `0 missing, 0 bad rows`.
Run the tests with `--min 5`.
Expected: `mod tests: PASS (5 passed)`. If `GetTimeStamp` doesn't return `FDateTime::MinValue()` for a missing file, read its header comment, and make `ConfigModTime` check `FileExists` first (a ruling).

- [ ] **Step 5: Commit**

```bash
git add mod api-sources.md
git commit -m "Core mod: the config file, with S1's URL rules and an atomic write (Plan 2A Task 2)"
```

---

### Task 3: The credential store (`credential.json`)

**Files:**
- Create: `Source/SatisDashCore/Public/SatisDash/Credential.h`, `Private/Credential.cpp`
- Test: `Source/SatisDashCoreTests/Private/CredentialTest.cpp`

**Interfaces:**
- Consumes: `Json::ParseObject`, `Json::Field`, `Json::WritePretty`, `WriteFileAtomic` (Task 2); `Test::FTempDir`, `Test::WriteText`, `Test::ReadText` (Task 2).
- Produces:
  - `struct FCredential { FString AgentSecret; FString ServerId; }`
  - `bool IsAgentSecret(const FString&)`, `bool IsServerId(const FString&)`
  - `TOptional<FCredential> ParseCredential(const FString&)`, `FString CredentialToJson(const FCredential&)`
  - `class FCredentialStore { explicit FCredentialStore(FString Path); TOptional<FCredential> Load() const; bool Save(const FCredential&) const; bool Delete() const; bool Exists() const; }`

- [ ] **Step 1: Write the failing tests**

`CredentialTest.cpp`:

```cpp
#include "HAL/FileManager.h"
#include "SatisDash/Credential.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	// A fake secret of the contract's shape (43 base64url characters). Never a real credential (spec, spikes rule).
	const FString FakeSecret = FString::ChrN(42, TEXT('a')) + TEXT("_");

	FCredential Fake()
	{
		FCredential Credential;
		Credential.AgentSecret = FakeSecret;
		Credential.ServerId = TEXT("srv-1");
		return Credential;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCredentialFieldsTest, "SatisDash.Core.Credential.Fields", SatisDash::Test::Flags)

bool FCredentialFieldsTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("43 base64url characters"), IsAgentSecret(FakeSecret));
	TestFalse(TEXT("empty"), IsAgentSecret(TEXT("")));
	TestFalse(TEXT("a space"), IsAgentSecret(TEXT("abc def")));
	TestFalse(TEXT("CR LF (a header injection)"), IsAgentSecret(TEXT("abc\r\nX-Evil: 1")));
	TestFalse(TEXT("base64 '+', '/', '='"), IsAgentSecret(TEXT("ab+/=")));
	TestFalse(TEXT("over 256 characters"), IsAgentSecret(FString::ChrN(257, TEXT('a'))));
	TestTrue(TEXT("a server id"), IsServerId(TEXT("srv-1")));
	TestFalse(TEXT("upper case"), IsServerId(TEXT("SRV")));
	TestFalse(TEXT("33 characters"), IsServerId(FString::ChrN(33, TEXT('a'))));
	TestFalse(TEXT("empty id"), IsServerId(TEXT("")));

	const TOptional<FCredential> Parsed = ParseCredential(CredentialToJson(Fake()));
	TestTrue(TEXT("round trip"), Parsed.IsSet());
	if (Parsed.IsSet())
	{
		Test::SameText(*this, TEXT("secret"), Parsed->AgentSecret, FakeSecret);
		Test::SameText(*this, TEXT("server id"), Parsed->ServerId, TEXT("srv-1"));
	}
	TestFalse(TEXT("wrong type"), ParseCredential(TEXT("{\"agentSecret\":5,\"serverId\":\"srv-1\"}")).IsSet());
	TestFalse(TEXT("missing id"), ParseCredential(FString::Printf(TEXT("{\"agentSecret\":\"%s\"}"), *FakeSecret)).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCredentialStoreTest, "SatisDash.Core.Credential.Store", SatisDash::Test::Flags)

bool FCredentialStoreTest::RunTest(const FString& Parameters)
{
	const Test::FTempDir Dir;
	const FString Path = Dir.File(TEXT("SatisDash/credential.json"));
	const FCredentialStore Store(Path);

	TestFalse(TEXT("nothing saved yet"), Store.Load().IsSet());
	TestFalse(TEXT("no file yet"), Store.Exists());
	TestTrue(TEXT("save creates the folder"), Store.Save(Fake()));
	TestTrue(TEXT("load after save"), Store.Load().IsSet() && Store.Load()->AgentSecret.Equals(FakeSecret, ESearchCase::CaseSensitive));

	Test::WriteText(Path, TEXT("{ not json"));
	TestFalse(TEXT("a broken file loads as nothing"), Store.Load().IsSet());
	TestTrue(TEXT("but it exists"), Store.Exists());

	TestTrue(TEXT("delete"), Store.Delete());
	TestFalse(TEXT("gone"), Store.Exists());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCredentialCrashTest, "SatisDash.Core.Credential.Crash", SatisDash::Test::Flags)

bool FCredentialCrashTest::RunTest(const FString& Parameters)
{
	const Test::FTempDir Dir;
	const FString Path = Dir.File(TEXT("credential.json"));
	const FString Temp = Path + TEXT(".tmp");
	const FCredentialStore Store(Path);

	// A crash mid-write: the old file is whole and a half-written temp file is left beside it.
	Store.Save(Fake());
	Test::WriteText(Temp, TEXT("{\"agentSec"));
	TestTrue(TEXT("the whole file wins over a broken temp"), Store.Load().IsSet());

	// A crash between a replace's delete and its move: only a valid temp file is left.
	IFileManager::Get().Delete(*Path);
	Test::WriteText(Temp, CredentialToJson(Fake()));
	TestTrue(TEXT("a valid temp file is recovered"), Store.Load().IsSet());
	TestTrue(TEXT("and moved into place"), IFileManager::Get().FileExists(*Path));

	// Only a broken temp file: not enrolled.
	IFileManager::Get().Delete(*Path);
	Test::WriteText(Temp, TEXT("{\"agentSec"));
	TestFalse(TEXT("a broken temp file alone is nothing"), Store.Load().IsSet());

	TestTrue(TEXT("delete clears both"), Store.Delete() && !IFileManager::Get().FileExists(*Temp));
	return true;
}
```

- [ ] **Step 2: Run them to verify they fail**

Run the tests as in "How the tests run".
Expected: the build fails because `SatisDash/Credential.h` doesn't exist.

- [ ] **Step 3: Write the implementation**

`Public/SatisDash/Credential.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

namespace SatisDash
{
	struct FCredential
	{
		FString AgentSecret;
		FString ServerId;
	};

	/**
	 * 1-256 base64url characters. The contract's secret is 43 (EnrollResponseSchema); the wider bound keeps a future
	 * length change working, and the alphabet keeps CR, LF and spaces out of the Authorization header.
	 */
	SATISDASHCORE_API bool IsAgentSecret(const FString& Secret);
	/** ServerIdSchema: ^[a-z0-9-]{1,32}$. */
	SATISDASHCORE_API bool IsServerId(const FString& Id);
	SATISDASHCORE_API TOptional<FCredential> ParseCredential(const FString& Text);
	SATISDASHCORE_API FString CredentialToJson(const FCredential& Credential);

	/** Saved/SatisDash/credential.json (owner decision, spec "Shape"), and only there (S11). */
	class SATISDASHCORE_API FCredentialStore
	{
	public:
		explicit FCredentialStore(FString InPath);
		/**
		 * The saved credential, or none when there is none or it is broken. A valid Path.tmp with no Path (a crash
		 * between a replace's delete and its move) is moved into place first.
		 */
		TOptional<FCredential> Load() const;
		bool Save(const FCredential& Credential) const;
		/** Removes the file and any temp file. True when neither is left. */
		bool Delete() const;
		/** Whether the file is there, valid or not (for the "unreadable" log line). */
		bool Exists() const;

	private:
		FString Path;
	};
}
```

`Private/Credential.cpp`:

```cpp
#include "SatisDash/Credential.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "SatisDash/Files.h"
#include "SatisDash/JsonUtil.h"

bool SatisDash::IsAgentSecret(const FString& Secret)
{
	if (Secret.IsEmpty() || Secret.Len() > 256)
	{
		return false;
	}
	for (const TCHAR C : Secret)
	{
		const bool bOk = (C >= TEXT('A') && C <= TEXT('Z')) || (C >= TEXT('a') && C <= TEXT('z')) ||
			(C >= TEXT('0') && C <= TEXT('9')) || C == TEXT('-') || C == TEXT('_');
		if (!bOk)
		{
			return false;
		}
	}
	return true;
}

bool SatisDash::IsServerId(const FString& Id)
{
	if (Id.IsEmpty() || Id.Len() > 32)
	{
		return false;
	}
	for (const TCHAR C : Id)
	{
		if (!((C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('0') && C <= TEXT('9')) || C == TEXT('-')))
		{
			return false;
		}
	}
	return true;
}

TOptional<SatisDash::FCredential> SatisDash::ParseCredential(const FString& Text)
{
	const TSharedPtr<FJsonObject> Object = Json::ParseObject(Text);
	const TSharedPtr<FJsonValue> Secret = Json::Field(Object, TEXT("agentSecret"), EJson::String);
	const TSharedPtr<FJsonValue> ServerId = Json::Field(Object, TEXT("serverId"), EJson::String);
	if (!Secret.IsValid() || !ServerId.IsValid() || !IsAgentSecret(Secret->AsString()) || !IsServerId(ServerId->AsString()))
	{
		return {};
	}
	FCredential Credential;
	Credential.AgentSecret = Secret->AsString();
	Credential.ServerId = ServerId->AsString();
	return Credential;
}

FString SatisDash::CredentialToJson(const FCredential& Credential)
{
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("agentSecret"), Credential.AgentSecret);
	Object->SetStringField(TEXT("serverId"), Credential.ServerId);
	return Json::WritePretty(Object);
}

SatisDash::FCredentialStore::FCredentialStore(FString InPath)
	: Path(MoveTemp(InPath))
{
}

TOptional<SatisDash::FCredential> SatisDash::FCredentialStore::Load() const
{
	IFileManager& Files = IFileManager::Get();
	const FString Temp = Path + TEXT(".tmp");
	FString Text;
	if (!Files.FileExists(*Path) && Files.FileExists(*Temp) && FFileHelper::LoadFileToString(Text, *Temp) && ParseCredential(Text).IsSet())
	{
		Files.Move(*Path, *Temp, true);
	}
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		return {};
	}
	return ParseCredential(Text);
}

bool SatisDash::FCredentialStore::Save(const FCredential& Credential) const
{
	return WriteFileAtomic(Path, CredentialToJson(Credential));
}

bool SatisDash::FCredentialStore::Delete() const
{
	IFileManager& Files = IFileManager::Get();
	const FString Temp = Path + TEXT(".tmp");
	Files.Delete(*Path, false, false, true);
	Files.Delete(*Temp, false, false, true);
	return !Files.FileExists(*Path) && !Files.FileExists(*Temp);
}

bool SatisDash::FCredentialStore::Exists() const
{
	return IFileManager::Get().FileExists(*Path);
}
```

- [ ] **Step 4: Run check-api and the tests to verify they pass**

Run check-api. Expected: `0 missing, 0 bad rows`, after adding looked-up rows.
Run the tests with `--min 8`. Expected: `mod tests: PASS (8 passed)`.

- [ ] **Step 5: Commit**

```bash
git add mod api-sources.md
git commit -m "Core mod: the credential store, atomic and crash-safe (Plan 2A Task 3)"
```

---

### Task 4: Readings, Conform and the snapshot JSON, with contract fixtures (S13)

The C++ test checks that the mod's JSON equals two committed fixtures. A Vitest test checks those fixtures against the pinned contract: valid by `SnapshotRequestSchema` (strict, so no extra field), with exactly the contract's status and circuit fields. A contract change fails Vitest, and a mod change fails the C++ test.

**Files:**
- Create: `Source/SatisDashCore/Public/SatisDash/Snapshot.h`, `Private/Snapshot.cpp`
- Create: `Source/SatisDashCoreTests/Fixtures/snapshot-world.json`, `Fixtures/snapshot-no-world.json`
- Create: `harness/test/snapshot-fixtures.test.ts`
- Test: `Source/SatisDashCoreTests/Private/SnapshotTest.cpp`

**Interfaces:**
- Consumes: `Json::ParseObject`, `Json::Field`, `Json::Write` (Task 2).
- Produces:
  - `struct FStatusReading { bool bTickHealthy; bool bIsGameRunning; bool bGamePaused; FString SessionName; int32 ConnectedPlayers; int32 PlayerLimit; double TickRate; double TotalGameDurationSeconds; }`
  - `struct FCircuitReading { int32 CircuitGroupId; double ProductionMW, ConsumptionMW, CapacityMW, MaxConsumptionMW; bool bFuseTriggered; double BatteryCapacityMWh, BatteryPercent, BatteryDifferentialMW; }`
  - `struct FSnapshot { FString AgentVersion; FDateTime ObservedAt; TOptional<bool> AutoPause; TOptional<FStatusReading> Status; TOptional<TArray<FCircuitReading>> Power; }`
  - `ClampString`, `ClampFinite`, `ClampNonNegative`, `ConformStatus`, `ConformCircuit`
  - `FString SnapshotToJson(const FSnapshot&)`
  - the constants `MaxStringLength = 200` and `MaxCircuits = 1000`

- [ ] **Step 1: Write the fixtures and the failing tests**

`Fixtures/snapshot-world.json`:

```json
{
  "agentVersion": "mod/0.1.0",
  "observedAt": "2026-10-20T13:26:42.134Z",
  "reachable": true,
  "paused": false,
  "settings": { "autoPause": true },
  "status": {
    "tickHealth": "healthy",
    "isGameRunning": true,
    "gamePaused": false,
    "sessionName": "Golden Session",
    "connectedPlayers": 1,
    "playerLimit": 4,
    "tickRate": 28.4,
    "totalGameDurationSeconds": 123456
  },
  "power": {
    "circuits": [
      {
        "circuitGroupId": 1,
        "productionMW": 1200.5,
        "consumptionMW": 0,
        "capacityMW": 1500,
        "maxConsumptionMW": 1400,
        "fuseTriggered": false,
        "batteryCapacityMWh": 100,
        "batteryPercent": 100,
        "batteryDifferentialMW": -12.5
      }
    ]
  }
}
```

`Fixtures/snapshot-no-world.json`:

```json
{
  "agentVersion": "mod/0.1.0",
  "observedAt": "2026-10-20T13:26:42.134Z",
  "reachable": true,
  "paused": false,
  "settings": { "autoPause": true },
  "status": {
    "tickHealth": "healthy",
    "isGameRunning": false,
    "gamePaused": false,
    "sessionName": "",
    "connectedPlayers": 0,
    "playerLimit": 4,
    "tickRate": 30,
    "totalGameDurationSeconds": 0
  }
}
```

`harness/test/snapshot-fixtures.test.ts`:

```ts
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { AgentPowerCircuitSchema, AgentStatusSchema, SnapshotRequestSchema } from "../contract/src/agent";

const FIXTURES = new URL("../../mod/SatisDashAgent/Source/SatisDashCoreTests/Fixtures/", import.meta.url);
const load = (name: string) => JSON.parse(readFileSync(fileURLToPath(new URL(name, FIXTURES)), "utf8"));
const keys = (o: object) => Object.keys(o).sort();

describe("the core mod's snapshot fixtures, against the pinned contract (spec S13)", () => {
  for (const name of ["snapshot-world.json", "snapshot-no-world.json"]) {
    it(`${name} is a valid snapshot (strict: no field the contract doesn't define)`, () => {
      const r = SnapshotRequestSchema.safeParse(load(name));
      expect(r.success ? [] : r.error.issues).toEqual([]);
    });
    it(`${name}'s status has exactly the contract's status fields`, () => {
      expect(keys(load(name).status)).toEqual(keys(AgentStatusSchema.shape));
    });
  }
  it("a circuit has exactly the contract's circuit fields", () => {
    expect(keys(load("snapshot-world.json").power.circuits[0])).toEqual(keys(AgentPowerCircuitSchema.shape));
  });
  it("the top level is only what v1 sends: no factory, players or mapLive", () => {
    expect(keys(load("snapshot-world.json"))).toEqual(
      ["agentVersion", "observedAt", "paused", "power", "reachable", "settings", "status"],
    );
  });
});
```

`SnapshotTest.cpp`:

```cpp
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "SatisDash/JsonUtil.h"
#include "SatisDash/Snapshot.h"
#include "TestUtil.h"

#include <limits>

using namespace SatisDash;

namespace
{
	FString Fixture(const TCHAR* Name)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SatisDashAgent"));
		FString Text;
		if (Plugin.IsValid())
		{
			FFileHelper::LoadFileToString(Text, *(Plugin->GetBaseDir() / TEXT("Source/SatisDashCoreTests/Fixtures") / Name));
		}
		return Text;
	}

	FSnapshot Base()
	{
		FSnapshot Snapshot;
		Snapshot.AgentVersion = TEXT("mod/0.1.0");
		Snapshot.ObservedAt = FDateTime(2026, 10, 20, 13, 26, 42, 134);
		Snapshot.AutoPause = true;
		return Snapshot;
	}

	/** Same JSON value, compared as data: field order and 5 vs 5.0 don't matter. */
	bool SameJson(const FString& A, const FString& B)
	{
		const TSharedPtr<FJsonObject> Left = Json::ParseObject(A);
		const TSharedPtr<FJsonObject> Right = Json::ParseObject(B);
		return Left.IsValid() && Right.IsValid() &&
			FJsonValue::CompareEqual(FJsonValueObject(Left), FJsonValueObject(Right));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnapshotClampTest, "SatisDash.Core.Snapshot.Clamp", SatisDash::Test::Flags)

bool FSnapshotClampTest::RunTest(const FString& Parameters)
{
	const double NaN = std::numeric_limits<double>::quiet_NaN();
	const double Inf = std::numeric_limits<double>::infinity();
	TestEqual(TEXT("a long string is cut to 200"), ClampString(FString::ChrN(250, TEXT('x'))).Len(), 200);
	FString Emoji = FString::ChrN(199, TEXT('x'));
	Emoji.AppendChar(TCHAR(0xD83D));
	Emoji.AppendChar(TCHAR(0xDE00));
	Emoji.AppendChar(TEXT('y'));
	TestEqual(TEXT("a surrogate pair is never split"), ClampString(Emoji).Len(), 199);
	TestEqual(TEXT("negative to 0"), ClampNonNegative(-1.0), 0.0);
	TestEqual(TEXT("NaN to 0"), ClampNonNegative(NaN), 0.0);
	TestEqual(TEXT("infinity to 0"), ClampNonNegative(Inf), 0.0);
	TestEqual(TEXT("a value is kept"), ClampNonNegative(5.5), 5.5);
	TestEqual(TEXT("a finite negative is kept"), ClampFinite(-12.5), -12.5);
	TestEqual(TEXT("NaN to 0 (signed)"), ClampFinite(NaN), 0.0);

	FCircuitReading Circuit;
	Circuit.BatteryPercent = 100.0000001;
	Circuit.ConsumptionMW = -0.0000001;
	Circuit.BatteryDifferentialMW = NaN;
	const FCircuitReading Conformed = ConformCircuit(Circuit);
	TestEqual(TEXT("battery percent at most 100"), Conformed.BatteryPercent, 100.0);
	TestEqual(TEXT("float noise below 0"), Conformed.ConsumptionMW, 0.0);
	TestEqual(TEXT("differential NaN"), Conformed.BatteryDifferentialMW, 0.0);

	FStatusReading Status;
	Status.ConnectedPlayers = -3;
	Status.TickRate = NaN;
	Status.SessionName = FString::ChrN(250, TEXT('s'));
	const FStatusReading Clean = ConformStatus(Status);
	TestEqual(TEXT("players at least 0"), Clean.ConnectedPlayers, 0);
	TestEqual(TEXT("tick rate NaN"), Clean.TickRate, 0.0);
	TestEqual(TEXT("session name cut"), Clean.SessionName.Len(), 200);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnapshotFixturesTest, "SatisDash.Core.Snapshot.Fixtures", SatisDash::Test::Flags)

bool FSnapshotFixturesTest::RunTest(const FString& Parameters)
{
	FSnapshot World = Base();
	FStatusReading Status;
	Status.bTickHealthy = true;
	Status.bIsGameRunning = true;
	Status.SessionName = TEXT("Golden Session");
	Status.ConnectedPlayers = 1;
	Status.PlayerLimit = 4;
	Status.TickRate = 28.4;
	Status.TotalGameDurationSeconds = 123456;
	World.Status = Status;
	FCircuitReading Circuit;
	Circuit.CircuitGroupId = 1;
	Circuit.ProductionMW = 1200.5;
	Circuit.ConsumptionMW = -0.0000001; // float noise: conformed to 0
	Circuit.CapacityMW = 1500;
	Circuit.MaxConsumptionMW = 1400;
	Circuit.BatteryCapacityMWh = 100;
	Circuit.BatteryPercent = 100.0000001; // float noise: conformed to 100
	Circuit.BatteryDifferentialMW = -12.5;
	World.Power = TArray<FCircuitReading>{ Circuit };
	const FString WorldFixture = Fixture(TEXT("snapshot-world.json"));
	TestFalse(TEXT("the world fixture was found"), WorldFixture.IsEmpty());
	TestTrue(TEXT("matches snapshot-world.json"), SameJson(SnapshotToJson(World), WorldFixture));

	FSnapshot NoWorld = Base();
	FStatusReading Idle;
	Idle.PlayerLimit = 4;
	Idle.TickRate = 30;
	NoWorld.Status = Idle;
	const FString NoWorldFixture = Fixture(TEXT("snapshot-no-world.json"));
	TestFalse(TEXT("the no-world fixture was found"), NoWorldFixture.IsEmpty());
	TestTrue(TEXT("matches snapshot-no-world.json"), SameJson(SnapshotToJson(NoWorld), NoWorldFixture));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnapshotShapeTest, "SatisDash.Core.Snapshot.Shape", SatisDash::Test::Flags)

bool FSnapshotShapeTest::RunTest(const FString& Parameters)
{
	// No status read: paused is null and there's no status part; reachable is still true (the mod never sends false).
	const TSharedPtr<FJsonObject> Bare = Json::ParseObject(SnapshotToJson(Base()));
	TestTrue(TEXT("paused is null"), Json::Field(Bare, TEXT("paused"), EJson::Null).IsValid());
	TestFalse(TEXT("no status"), Bare->HasField(TEXT("status")));
	TestTrue(TEXT("reachable"), Json::Field(Bare, TEXT("reachable"), EJson::Boolean).IsValid() && Bare->GetBoolField(TEXT("reachable")));

	FSnapshot NoSettings = Base();
	NoSettings.AutoPause.Reset();
	TestFalse(TEXT("no settings when auto-pause wasn't read"), Json::ParseObject(SnapshotToJson(NoSettings))->HasField(TEXT("settings")));

	FSnapshot Many = Base();
	Many.Power = TArray<FCircuitReading>();
	Many.Power->SetNum(MaxCircuits + 1);
	const TSharedPtr<FJsonObject> Capped = Json::ParseObject(SnapshotToJson(Many));
	const TSharedPtr<FJsonValue> Circuits = Json::Field(Capped->GetObjectField(TEXT("power")), TEXT("circuits"), EJson::Array);
	TestEqual(TEXT("at most 1000 circuits"), Circuits.IsValid() ? Circuits->AsArray().Num() : -1, MaxCircuits);
	return true;
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cd harness; npx vitest run test/snapshot-fixtures.test.ts --reporter=dot`
Expected: PASS. The fixtures are hand-written from the contract, so this test passes now. **To prove it can fail,** temporarily add `"extra": 1` to a fixture's top level, see it FAIL with an unrecognized-key issue, then remove it.
Run the C++ tests. Expected: the build fails because `SatisDash/Snapshot.h` doesn't exist.

- [ ] **Step 3: Write the implementation**

`Public/SatisDash/Snapshot.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

namespace SatisDash
{
	/** ADR-0031's input bounds, from the contract (agent.ts AGENT_INPUT_MAX_*). */
	inline constexpr int32 MaxStringLength = 200;
	inline constexpr int32 MaxCircuits = 1000;

	/** One status reading (AgentStatusSchema), as the native reader (Plan 2B, C8) fills it. */
	struct FStatusReading
	{
		bool bTickHealthy = true;
		bool bIsGameRunning = false;
		bool bGamePaused = false;
		FString SessionName;
		int32 ConnectedPlayers = 0;
		int32 PlayerLimit = 0;
		double TickRate = 0;
		double TotalGameDurationSeconds = 0;
	};

	/** One power circuit group (AgentPowerCircuitSchema: no status, which the backend derives). */
	struct FCircuitReading
	{
		int32 CircuitGroupId = 0;
		double ProductionMW = 0;
		double ConsumptionMW = 0;
		double CapacityMW = 0;
		double MaxConsumptionMW = 0;
		bool bFuseTriggered = false;
		double BatteryCapacityMWh = 0;
		double BatteryPercent = 0;
		double BatteryDifferentialMW = 0;
	};

	/** One pass's readings (spec "Data flow"). A part is unset when it wasn't due or its reader failed. */
	struct FSnapshot
	{
		FString AgentVersion;
		FDateTime ObservedAt;
		TOptional<bool> AutoPause;
		TOptional<FStatusReading> Status;
		TOptional<TArray<FCircuitReading>> Power;
	};

	/** At most 200 UTF-16 code units (the contract's string length, as JavaScript counts it), never splitting a pair. */
	SATISDASHCORE_API FString ClampString(const FString& Value);
	/** NaN and infinities become 0. */
	SATISDASHCORE_API double ClampFinite(double Value);
	/** ClampFinite, and negatives become 0. */
	SATISDASHCORE_API double ClampNonNegative(double Value);
	SATISDASHCORE_API FStatusReading ConformStatus(const FStatusReading& In);
	SATISDASHCORE_API FCircuitReading ConformCircuit(const FCircuitReading& In);
	/**
	 * The snapshot body (SnapshotRequestSchema), conformed. reachable is always true: the mod never sends false
	 * (spec "Data flow"). paused is the status reading's gamePaused, or null with no status.
	 */
	SATISDASHCORE_API FString SnapshotToJson(const FSnapshot& Snapshot);
}
```

`Private/Snapshot.cpp`:

```cpp
#include "SatisDash/Snapshot.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "SatisDash/JsonUtil.h"

FString SatisDash::ClampString(const FString& Value)
{
	if (Value.Len() <= MaxStringLength)
	{
		return Value;
	}
	int32 Keep = MaxStringLength;
	const TCHAR Last = Value[Keep - 1];
	if (Last >= 0xD800 && Last <= 0xDBFF)
	{
		--Keep; // a high surrogate whose pair would be cut off
	}
	return Value.Left(Keep);
}

double SatisDash::ClampFinite(double Value)
{
	return FMath::IsFinite(Value) ? Value : 0.0;
}

double SatisDash::ClampNonNegative(double Value)
{
	return FMath::Max(0.0, ClampFinite(Value));
}

SatisDash::FStatusReading SatisDash::ConformStatus(const FStatusReading& In)
{
	FStatusReading Out = In;
	Out.SessionName = ClampString(In.SessionName);
	Out.ConnectedPlayers = FMath::Max(0, In.ConnectedPlayers);
	Out.PlayerLimit = FMath::Max(0, In.PlayerLimit);
	Out.TickRate = ClampNonNegative(In.TickRate);
	Out.TotalGameDurationSeconds = ClampNonNegative(In.TotalGameDurationSeconds);
	return Out;
}

SatisDash::FCircuitReading SatisDash::ConformCircuit(const FCircuitReading& In)
{
	FCircuitReading Out = In;
	Out.ProductionMW = ClampNonNegative(In.ProductionMW);
	Out.ConsumptionMW = ClampNonNegative(In.ConsumptionMW);
	Out.CapacityMW = ClampNonNegative(In.CapacityMW);
	Out.MaxConsumptionMW = ClampNonNegative(In.MaxConsumptionMW);
	Out.BatteryCapacityMWh = ClampNonNegative(In.BatteryCapacityMWh);
	Out.BatteryPercent = FMath::Min(100.0, ClampNonNegative(In.BatteryPercent));
	Out.BatteryDifferentialMW = ClampFinite(In.BatteryDifferentialMW);
	return Out;
}

namespace
{
	TSharedRef<FJsonObject> StatusToJson(const SatisDash::FStatusReading& S)
	{
		const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("tickHealth"), S.bTickHealthy ? TEXT("healthy") : TEXT("slow"));
		O->SetBoolField(TEXT("isGameRunning"), S.bIsGameRunning);
		O->SetBoolField(TEXT("gamePaused"), S.bGamePaused);
		O->SetStringField(TEXT("sessionName"), S.SessionName);
		O->SetNumberField(TEXT("connectedPlayers"), S.ConnectedPlayers);
		O->SetNumberField(TEXT("playerLimit"), S.PlayerLimit);
		O->SetNumberField(TEXT("tickRate"), S.TickRate);
		O->SetNumberField(TEXT("totalGameDurationSeconds"), S.TotalGameDurationSeconds);
		return O;
	}

	TSharedRef<FJsonObject> CircuitToJson(const SatisDash::FCircuitReading& C)
	{
		const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("circuitGroupId"), C.CircuitGroupId);
		O->SetNumberField(TEXT("productionMW"), C.ProductionMW);
		O->SetNumberField(TEXT("consumptionMW"), C.ConsumptionMW);
		O->SetNumberField(TEXT("capacityMW"), C.CapacityMW);
		O->SetNumberField(TEXT("maxConsumptionMW"), C.MaxConsumptionMW);
		O->SetBoolField(TEXT("fuseTriggered"), C.bFuseTriggered);
		O->SetNumberField(TEXT("batteryCapacityMWh"), C.BatteryCapacityMWh);
		O->SetNumberField(TEXT("batteryPercent"), C.BatteryPercent);
		O->SetNumberField(TEXT("batteryDifferentialMW"), C.BatteryDifferentialMW);
		return O;
	}
}

FString SatisDash::SnapshotToJson(const FSnapshot& Snapshot)
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("agentVersion"), Snapshot.AgentVersion);
	Root->SetStringField(TEXT("observedAt"), Snapshot.ObservedAt.ToIso8601());
	Root->SetBoolField(TEXT("reachable"), true);
	if (Snapshot.Status.IsSet())
	{
		Root->SetBoolField(TEXT("paused"), Snapshot.Status->bGamePaused);
	}
	else
	{
		Root->SetField(TEXT("paused"), MakeShared<FJsonValueNull>());
	}
	if (Snapshot.AutoPause.IsSet())
	{
		const TSharedRef<FJsonObject> Settings = MakeShared<FJsonObject>();
		Settings->SetBoolField(TEXT("autoPause"), Snapshot.AutoPause.GetValue());
		Root->SetObjectField(TEXT("settings"), Settings);
	}
	if (Snapshot.Status.IsSet())
	{
		Root->SetObjectField(TEXT("status"), StatusToJson(ConformStatus(Snapshot.Status.GetValue())));
	}
	if (Snapshot.Power.IsSet())
	{
		TArray<TSharedPtr<FJsonValue>> Circuits;
		const int32 Count = FMath::Min(Snapshot.Power->Num(), MaxCircuits);
		for (int32 I = 0; I < Count; ++I)
		{
			Circuits.Add(MakeShared<FJsonValueObject>(CircuitToJson(ConformCircuit((*Snapshot.Power)[I]))));
		}
		const TSharedRef<FJsonObject> Power = MakeShared<FJsonObject>();
		Power->SetArrayField(TEXT("circuits"), Circuits);
		Root->SetObjectField(TEXT("power"), Power);
	}
	return Json::Write(Root);
}
```

- [ ] **Step 4: Run check-api and the tests to verify they pass**

Run: `cd harness; npx vitest run test/snapshot-fixtures.test.ts --reporter=dot; npm run harness -- check-api`
Expected: Vitest PASS (6 tests); check-api `0 missing, 0 bad rows`, after adding looked-up rows (e.g. `FJsonValue::CompareEqual`, `IPluginManager::Get`, `Interfaces/IPluginManager.h`).
Run the C++ tests with `--min 11`. Expected: `mod tests: PASS (11 passed)`.

- [ ] **Step 5: Commit**

```bash
git add mod harness/test/snapshot-fixtures.test.ts api-sources.md
git commit -m "Core mod: Conform and the snapshot JSON, checked against the contract (Plan 2A Task 4)"
```

---

### Task 5: Replies and the backend's responses (S4, S5)

**Ruling (S4 against S5):**
- S4 says an out-of-range value rejects the whole response.
- S5 says cadence values are clamped to 1–3600.
- S5 is the specific rule for cadence, so cadence is clamped. A cadence of the wrong type, or a value that isn't finite, still rejects the response (S4).
- **Cost if wrong:** a hostile cadence of 0 runs at 1 s, not refused. 1 s is the contract's own floor.

**Files:**
- Create: `Source/SatisDashCore/Public/SatisDash/Responses.h`, `Private/Responses.cpp`
- Modify: `Source/SatisDashCoreTests/Private/TestUtil.h` (reply and fixture helpers)
- Test: `Source/SatisDashCoreTests/Private/ResponsesTest.cpp`

**Interfaces:**
- Consumes: `Json::*` (Task 2); `FCredential`, `IsAgentSecret`, `IsServerId` (Task 3).
- Produces:
  - `struct FHttpReply { int32 Code; FString ContentType; FString RetryAfter; TArray<uint8> Body; }` (Code 0 means no answer)
  - `enum class EReplyKind { Ok, Transient, AuthRejected, Fatal }`, `EReplyKind ClassifyReply(int32 Code, bool bAuthenticated)`
  - `struct FCadence { int32 StatusSeconds = 5; int32 PowerSeconds = 5; int32 FactorySeconds = 30; }`
  - `struct FEnrollResponse { FCredential Credential; FCadence Cadence; }`, `struct FSnapshotResponse { FCadence Cadence; bool bCommandsPending; }`
  - `struct FAgentCommand { FString Id; FString Type; TSharedPtr<FJsonObject> Params; FDateTime ExpiresAt; }`
  - `ReadJsonBody`, `ParseCadence`, `ParseEnrollResponse`, `ParseSnapshotResponse`, `ParseCommandsResponse`
  - the constants `MaxResponseBytes`, `MaxCommandIdLength`, `MaxCommandsPerPoll`
  - in tests: `Test::FakeSecret()`, `Test::Reply(Code, Json, ContentType)`, `Test::CadenceJson`, `Test::EnrollJson()`, `Test::SnapshotAnswerJson`

- [ ] **Step 1: Write the failing tests**

Add to `TestUtil.h` (include `SatisDash/Responses.h`):

```cpp
namespace SatisDash::Test
{
	/** A fake secret of the contract's shape. Never a real credential. */
	inline FString FakeSecret() { return FString::ChrN(42, TEXT('a')) + TEXT("_"); }

	inline const TCHAR* CadenceJson = TEXT("{\"statusSeconds\":5,\"powerSeconds\":5,\"factorySeconds\":30}");
	inline const TCHAR* SnapshotAnswerJson =
		TEXT("{\"cadence\":{\"statusSeconds\":5,\"powerSeconds\":5,\"factorySeconds\":30},\"commandsPending\":false}");

	inline FString EnrollJson()
	{
		return FString::Printf(TEXT("{\"agentSecret\":\"%s\",\"serverId\":\"srv-1\",\"cadence\":%s}"), *FakeSecret(), CadenceJson);
	}

	inline SatisDash::FHttpReply Reply(int32 Code, const FString& Json, const FString& ContentType = TEXT("application/json"))
	{
		SatisDash::FHttpReply Out;
		Out.Code = Code;
		Out.ContentType = ContentType;
		const FTCHARToUTF8 Utf8(*Json);
		Out.Body.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		return Out;
	}
}
```

`ResponsesTest.cpp`:

```cpp
#include "SatisDash/Responses.h"
#include "TestUtil.h"

using namespace SatisDash;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResponsesClassifyTest, "SatisDash.Core.Responses.Classify", SatisDash::Test::Flags)

bool FResponsesClassifyTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("200"), ClassifyReply(200, true) == EReplyKind::Ok);
	TestTrue(TEXT("201"), ClassifyReply(201, false) == EReplyKind::Ok);
	TestTrue(TEXT("302 is refused, never followed"), ClassifyReply(302, true) == EReplyKind::Fatal);
	TestTrue(TEXT("401 with a credential halts"), ClassifyReply(401, true) == EReplyKind::AuthRejected);
	TestTrue(TEXT("401 on enrol is a refusal"), ClassifyReply(401, false) == EReplyKind::Fatal);
	TestTrue(TEXT("400"), ClassifyReply(400, true) == EReplyKind::Fatal);
	TestTrue(TEXT("404"), ClassifyReply(404, false) == EReplyKind::Fatal);
	TestTrue(TEXT("429"), ClassifyReply(429, true) == EReplyKind::Transient);
	TestTrue(TEXT("500"), ClassifyReply(500, true) == EReplyKind::Transient);
	TestTrue(TEXT("503"), ClassifyReply(503, true) == EReplyKind::Transient);
	TestTrue(TEXT("no answer"), ClassifyReply(0, true) == EReplyKind::Transient);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResponsesLimitsTest, "SatisDash.Core.Responses.Limits", SatisDash::Test::Flags)

bool FResponsesLimitsTest::RunTest(const FString& Parameters)
{
	const FString Answer = Test::SnapshotAnswerJson;
	TestTrue(TEXT("json with a charset"), ParseSnapshotResponse(Test::Reply(200, Answer, TEXT("application/json; charset=utf-8"))).IsSet());
	TestFalse(TEXT("html"), ParseSnapshotResponse(Test::Reply(200, Answer, TEXT("text/html"))).IsSet());
	TestFalse(TEXT("no content type"), ParseSnapshotResponse(Test::Reply(200, Answer, TEXT(""))).IsSet());
	const FString Exact = FString::ChrN(MaxResponseBytes - Answer.Len(), TEXT(' ')) + Answer;
	TestTrue(TEXT("exactly 64 KB"), ParseSnapshotResponse(Test::Reply(200, Exact)).IsSet());
	TestFalse(TEXT("64 KB and one byte"), ParseSnapshotResponse(Test::Reply(200, TEXT(" ") + Exact)).IsSet());
	TestFalse(TEXT("malformed"), ParseSnapshotResponse(Test::Reply(200, TEXT("{\"cadence\":"))).IsSet());
	TestFalse(TEXT("an array"), ParseSnapshotResponse(Test::Reply(200, TEXT("[]"))).IsSet());
	TestFalse(TEXT("an empty body"), ParseSnapshotResponse(Test::Reply(200, TEXT(""))).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResponsesEnrollTest, "SatisDash.Core.Responses.Enroll", SatisDash::Test::Flags)

bool FResponsesEnrollTest::RunTest(const FString& Parameters)
{
	const TOptional<FEnrollResponse> Ok = ParseEnrollResponse(Test::Reply(201, Test::EnrollJson()));
	TestTrue(TEXT("valid"), Ok.IsSet());
	if (Ok.IsSet())
	{
		Test::SameText(*this, TEXT("secret"), Ok->Credential.AgentSecret, Test::FakeSecret());
		Test::SameText(*this, TEXT("server id"), Ok->Credential.ServerId, TEXT("srv-1"));
		TestEqual(TEXT("cadence"), Ok->Cadence.StatusSeconds, 5);
	}
	TestFalse(TEXT("200 is not the contract's 201"), ParseEnrollResponse(Test::Reply(200, Test::EnrollJson())).IsSet());
	auto Body = [](const TCHAR* Secret, const TCHAR* ServerId)
	{
		return FString::Printf(TEXT("{\"agentSecret\":%s,\"serverId\":%s,\"cadence\":%s}"), Secret, ServerId, Test::CadenceJson);
	};
	TestFalse(TEXT("secret of the wrong type"), ParseEnrollResponse(Test::Reply(201, Body(TEXT("5"), TEXT("\"srv-1\"")))).IsSet());
	TestFalse(TEXT("secret with CR LF"), ParseEnrollResponse(Test::Reply(201, Body(TEXT("\"ab\\r\\nX: 1\""), TEXT("\"srv-1\"")))).IsSet());
	TestFalse(TEXT("bad server id"), ParseEnrollResponse(Test::Reply(201,
		Body(*FString::Printf(TEXT("\"%s\""), *Test::FakeSecret()), TEXT("\"Not An Id\"")))).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResponsesCadenceTest, "SatisDash.Core.Responses.Cadence", SatisDash::Test::Flags)

bool FResponsesCadenceTest::RunTest(const FString& Parameters)
{
	auto Status = [](const TCHAR* Value) -> TOptional<int32>
	{
		const TOptional<FSnapshotResponse> R = ParseSnapshotResponse(Test::Reply(200, FString::Printf(
			TEXT("{\"cadence\":{\"statusSeconds\":%s,\"powerSeconds\":5,\"factorySeconds\":30},\"commandsPending\":true}"), Value)));
		return R.IsSet() ? TOptional<int32>(R->Cadence.StatusSeconds) : TOptional<int32>();
	};
	TestEqual(TEXT("0 is clamped to 1"), Status(TEXT("0")).Get(-1), 1);
	TestEqual(TEXT("10^6 is clamped to 3600"), Status(TEXT("1000000")).Get(-1), 3600);
	TestEqual(TEXT("1e300 is clamped to 3600"), Status(TEXT("1e300")).Get(-1), 3600);
	TestEqual(TEXT("-5 is clamped to 1"), Status(TEXT("-5")).Get(-1), 1);
	TestEqual(TEXT("2.6 rounds to 3"), Status(TEXT("2.6")).Get(-1), 3);
	TestFalse(TEXT("a string rejects the response"), Status(TEXT("\"5\"")).IsSet());
	TestFalse(TEXT("null rejects the response"), Status(TEXT("null")).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResponsesSnapshotTest, "SatisDash.Core.Responses.Snapshot", SatisDash::Test::Flags)

bool FResponsesSnapshotTest::RunTest(const FString& Parameters)
{
	const TOptional<FSnapshotResponse> Ok = ParseSnapshotResponse(Test::Reply(200, FString::Printf(
		TEXT("{\"cadence\":%s,\"commandsPending\":true,\"extra\":1}"), Test::CadenceJson)));
	TestTrue(TEXT("valid, unknown field ignored"), Ok.IsSet() && Ok->bCommandsPending);
	TestFalse(TEXT("commandsPending missing"), ParseSnapshotResponse(Test::Reply(200, FString::Printf(
		TEXT("{\"cadence\":%s}"), Test::CadenceJson))).IsSet());
	TestFalse(TEXT("a 503 has no answer to parse"), ParseSnapshotResponse(Test::Reply(503, Test::SnapshotAnswerJson)).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResponsesCommandsTest, "SatisDash.Core.Responses.Commands", SatisDash::Test::Flags)

bool FResponsesCommandsTest::RunTest(const FString& Parameters)
{
	auto One = [](const FString& Id, const TCHAR* Params, const TCHAR* ExpiresAt)
	{
		return FString::Printf(TEXT("{\"id\":\"%s\",\"type\":\"set_auto_pause\",\"params\":%s,\"expiresAt\":\"%s\",\"x\":1}"),
			*Id, Params, ExpiresAt);
	};
	auto Parse = [](const FString& Items) { return ParseCommandsResponse(Test::Reply(200, FString::Printf(TEXT("{\"commands\":[%s]}"), *Items))); };
	const TCHAR* Expires = TEXT("2026-10-20T12:05:00.000Z");

	TestTrue(TEXT("empty"), Parse(TEXT("")).IsSet() && Parse(TEXT(""))->Num() == 0);
	const TOptional<TArray<FAgentCommand>> Ok = Parse(One(TEXT("c1"), TEXT("{\"enabled\":true}"), Expires));
	TestTrue(TEXT("one command"), Ok.IsSet() && Ok->Num() == 1);
	if (Ok.IsSet() && Ok->Num() == 1)
	{
		Test::SameText(*this, TEXT("id"), (*Ok)[0].Id, TEXT("c1"));
		TestTrue(TEXT("expiry parsed"), (*Ok)[0].ExpiresAt == FDateTime(2026, 10, 20, 12, 5, 0));
	}
	TestFalse(TEXT("empty id"), Parse(One(TEXT(""), TEXT("{}"), Expires)).IsSet());
	TestFalse(TEXT("101-character id"), Parse(One(FString::ChrN(101, TEXT('i')), TEXT("{}"), Expires)).IsSet());
	TestTrue(TEXT("100-character id"), Parse(One(FString::ChrN(100, TEXT('i')), TEXT("{}"), Expires)).IsSet());
	TestFalse(TEXT("params not an object"), Parse(One(TEXT("c1"), TEXT("[]"), Expires)).IsSet());
	TestFalse(TEXT("bad expiry"), Parse(One(TEXT("c1"), TEXT("{}"), TEXT("tomorrow"))).IsSet());

	TArray<FString> Many;
	for (int32 I = 0; I < 25; ++I)
	{
		Many.Add(One(FString::Printf(TEXT("c%d"), I), TEXT("{}"), Expires));
	}
	const TOptional<TArray<FAgentCommand>> Capped = Parse(FString::Join(Many, TEXT(",")));
	TestEqual(TEXT("at most 20 per poll"), Capped.IsSet() ? Capped->Num() : -1, MaxCommandsPerPoll);
	return true;
}
```

- [ ] **Step 2: Run them to verify they fail**

Run the C++ tests. Expected: the build fails because `SatisDash/Responses.h` doesn't exist.

- [ ] **Step 3: Write the implementation**

`Public/SatisDash/Responses.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "SatisDash/Credential.h"

namespace SatisDash
{
	/** One HTTP call's outcome, as Plan 2B's transport fills it. Code 0 = no answer (timeout, refused, TLS failure). */
	struct FHttpReply
	{
		int32 Code = 0;
		FString ContentType;
		FString RetryAfter;
		TArray<uint8> Body;
	};

	enum class EReplyKind : uint8 { Ok, Transient, AuthRejected, Fatal };

	/**
	 * The Node agent's rules (agent/src/backendClient.ts): 2xx is Ok; any 3xx is Fatal (a redirect is refused, S3);
	 * 401 on an authenticated call is AuthRejected; 429, 5xx and no answer are Transient; anything else is Fatal.
	 */
	SATISDASHCORE_API EReplyKind ClassifyReply(int32 Code, bool bAuthenticated);

	inline constexpr int32 MaxResponseBytes = 64 * 1024;
	inline constexpr int32 MinCadenceSeconds = 1;
	inline constexpr int32 MaxCadenceSeconds = 3600;
	inline constexpr int32 MaxCommandIdLength = 100;
	inline constexpr int32 MaxCommandsPerPoll = 20;

	/** CadenceSchema. The defaults are today's backend values (spec "Data flow"; factory from the schema). */
	struct FCadence
	{
		int32 StatusSeconds = 5;
		int32 PowerSeconds = 5;
		int32 FactorySeconds = 30;
	};

	struct FEnrollResponse
	{
		FCredential Credential;
		FCadence Cadence;
	};

	struct FSnapshotResponse
	{
		FCadence Cadence;
		bool bCommandsPending = false;
	};

	struct FAgentCommand
	{
		FString Id;
		FString Type;
		TSharedPtr<FJsonObject> Params;
		FDateTime ExpiresAt;
	};

	/** S4: at most 64 KB, Content-Type application/json, UTF-8, a JSON object. Null otherwise. */
	SATISDASHCORE_API TSharedPtr<FJsonObject> ReadJsonBody(const FHttpReply& Reply);
	/** S5: each value a finite number, rounded and clamped to 1-3600. Unset when one is missing or not a number. */
	SATISDASHCORE_API TOptional<FCadence> ParseCadence(const TSharedPtr<FJsonObject>& Object);
	/** 201 only (EnrollResponseSchema), with a well-formed secret and server id. */
	SATISDASHCORE_API TOptional<FEnrollResponse> ParseEnrollResponse(const FHttpReply& Reply);
	SATISDASHCORE_API TOptional<FSnapshotResponse> ParseSnapshotResponse(const FHttpReply& Reply);
	/** Every command must be well formed, or the whole response is refused (S4); at most 20 are kept. */
	SATISDASHCORE_API TOptional<TArray<FAgentCommand>> ParseCommandsResponse(const FHttpReply& Reply);
}
```

`Private/Responses.cpp`:

```cpp
#include "SatisDash/Responses.h"

#include "Dom/JsonValue.h"
#include "SatisDash/JsonUtil.h"

SatisDash::EReplyKind SatisDash::ClassifyReply(int32 Code, bool bAuthenticated)
{
	if (Code >= 200 && Code < 300)
	{
		return EReplyKind::Ok;
	}
	if (Code == 401 && bAuthenticated)
	{
		return EReplyKind::AuthRejected;
	}
	if (Code == 0 || Code == 429 || Code >= 500)
	{
		return EReplyKind::Transient;
	}
	return EReplyKind::Fatal;
}

TSharedPtr<FJsonObject> SatisDash::ReadJsonBody(const FHttpReply& Reply)
{
	if (Reply.Body.Num() > MaxResponseBytes)
	{
		return nullptr;
	}
	FString Type = Reply.ContentType;
	int32 Semicolon = INDEX_NONE;
	if (Type.FindChar(TEXT(';'), Semicolon))
	{
		Type.LeftInline(Semicolon);
	}
	if (!Type.TrimStartAndEnd().Equals(TEXT("application/json"), ESearchCase::IgnoreCase))
	{
		return nullptr;
	}
	const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Reply.Body.GetData()), Reply.Body.Num());
	return Json::ParseObject(FString::ConstructFromPtrSize(Text.Get(), Text.Length()));
}

TOptional<SatisDash::FCadence> SatisDash::ParseCadence(const TSharedPtr<FJsonObject>& Object)
{
	auto Seconds = [&Object](const TCHAR* Name) -> TOptional<int32>
	{
		const TSharedPtr<FJsonValue> Value = Json::Field(Object, Name, EJson::Number);
		if (!Value.IsValid() || !FMath::IsFinite(Value->AsNumber()))
		{
			return {};
		}
		// Clamp as a double first: rounding 1e300 to an int would overflow.
		return FMath::RoundToInt(FMath::Clamp(Value->AsNumber(), double(MinCadenceSeconds), double(MaxCadenceSeconds)));
	};
	const TOptional<int32> Status = Seconds(TEXT("statusSeconds"));
	const TOptional<int32> Power = Seconds(TEXT("powerSeconds"));
	const TOptional<int32> Factory = Seconds(TEXT("factorySeconds"));
	if (!Status.IsSet() || !Power.IsSet() || !Factory.IsSet())
	{
		return {};
	}
	FCadence Cadence;
	Cadence.StatusSeconds = Status.GetValue();
	Cadence.PowerSeconds = Power.GetValue();
	Cadence.FactorySeconds = Factory.GetValue();
	return Cadence;
}

TOptional<SatisDash::FEnrollResponse> SatisDash::ParseEnrollResponse(const FHttpReply& Reply)
{
	if (Reply.Code != 201)
	{
		return {};
	}
	const TSharedPtr<FJsonObject> Object = ReadJsonBody(Reply);
	const TSharedPtr<FJsonValue> Secret = Json::Field(Object, TEXT("agentSecret"), EJson::String);
	const TSharedPtr<FJsonValue> ServerId = Json::Field(Object, TEXT("serverId"), EJson::String);
	const TSharedPtr<FJsonValue> CadenceValue = Json::Field(Object, TEXT("cadence"), EJson::Object);
	if (!Secret.IsValid() || !ServerId.IsValid() || !CadenceValue.IsValid() ||
		!IsAgentSecret(Secret->AsString()) || !IsServerId(ServerId->AsString()))
	{
		return {};
	}
	const TOptional<FCadence> Cadence = ParseCadence(CadenceValue->AsObject());
	if (!Cadence.IsSet())
	{
		return {};
	}
	FEnrollResponse Out;
	Out.Credential.AgentSecret = Secret->AsString();
	Out.Credential.ServerId = ServerId->AsString();
	Out.Cadence = Cadence.GetValue();
	return Out;
}

TOptional<SatisDash::FSnapshotResponse> SatisDash::ParseSnapshotResponse(const FHttpReply& Reply)
{
	if (ClassifyReply(Reply.Code, true) != EReplyKind::Ok)
	{
		return {};
	}
	const TSharedPtr<FJsonObject> Object = ReadJsonBody(Reply);
	const TSharedPtr<FJsonValue> CadenceValue = Json::Field(Object, TEXT("cadence"), EJson::Object);
	const TSharedPtr<FJsonValue> Pending = Json::Field(Object, TEXT("commandsPending"), EJson::Boolean);
	if (!CadenceValue.IsValid() || !Pending.IsValid())
	{
		return {};
	}
	const TOptional<FCadence> Cadence = ParseCadence(CadenceValue->AsObject());
	if (!Cadence.IsSet())
	{
		return {};
	}
	FSnapshotResponse Out;
	Out.Cadence = Cadence.GetValue();
	Out.bCommandsPending = Pending->AsBool();
	return Out;
}

TOptional<TArray<SatisDash::FAgentCommand>> SatisDash::ParseCommandsResponse(const FHttpReply& Reply)
{
	if (ClassifyReply(Reply.Code, true) != EReplyKind::Ok)
	{
		return {};
	}
	const TSharedPtr<FJsonValue> List = Json::Field(ReadJsonBody(Reply), TEXT("commands"), EJson::Array);
	if (!List.IsValid())
	{
		return {};
	}
	TArray<FAgentCommand> Commands;
	for (const TSharedPtr<FJsonValue>& Item : List->AsArray())
	{
		const TSharedPtr<FJsonObject> Object = Item.IsValid() && Item->Type == EJson::Object ? Item->AsObject() : nullptr;
		const TSharedPtr<FJsonValue> Id = Json::Field(Object, TEXT("id"), EJson::String);
		const TSharedPtr<FJsonValue> Type = Json::Field(Object, TEXT("type"), EJson::String);
		const TSharedPtr<FJsonValue> Params = Json::Field(Object, TEXT("params"), EJson::Object);
		const TSharedPtr<FJsonValue> Expires = Json::Field(Object, TEXT("expiresAt"), EJson::String);
		FAgentCommand Command;
		if (!Id.IsValid() || !Type.IsValid() || !Params.IsValid() || !Expires.IsValid() ||
			!FDateTime::ParseIso8601(*Expires->AsString(), Command.ExpiresAt))
		{
			return {};
		}
		Command.Id = Id->AsString();
		if (Command.Id.IsEmpty() || Command.Id.Len() > MaxCommandIdLength)
		{
			return {};
		}
		Command.Type = Type->AsString();
		Command.Params = Params->AsObject();
		if (Commands.Num() < MaxCommandsPerPoll)
		{
			Commands.Add(MoveTemp(Command));
		}
	}
	return Commands;
}
```

- [ ] **Step 4: Run check-api and the tests to verify they pass**

Run check-api. Expected: `0 missing, 0 bad rows`, after adding looked-up rows.
- If `FString::ConstructFromPtrSize` isn't in this engine's `UnrealString.h.inl`, use the constructor that header does declare for a pointer and a length (a ruling).
- If `FDateTime::ParseIso8601`'s header says it doesn't convert an offset to UTC, record that as a ruling, since the backend sends `Z` times.

Run the C++ tests with `--min 17`. Expected: `mod tests: PASS (17 passed)`.

- [ ] **Step 5: Commit**

```bash
git add mod api-sources.md
git commit -m "Core mod: reply rules and response parsing, size-capped and clamped (Plan 2A Task 5)"
```

---

### Task 6: Commands: the ledger and what to do with each one (S6)

**Files:**
- Create: `Source/SatisDashCore/Public/SatisDash/Commands.h`, `Private/Commands.cpp`
- Test: `Source/SatisDashCoreTests/Private/CommandsTest.cpp`

**Interfaces:**
- Consumes: `FAgentCommand`, `EReplyKind` (Task 5); `Json::*` (Task 2).
- Produces:
  - `struct FCommandResult { bool bOk; FString Code; }`, `CommandOk()`, `CommandFailed(const TCHAR* Code)`, `CommandResultToJson`
  - `class FCommandLedger { enum class EVerdict { New, Running, Done }; EVerdict Begin(const FString& Id, const FDateTime& ExpiresAt, const FDateTime& Now, FCommandResult& OutDone); void Finish(const FString& Id, const FCommandResult&); int32 Num() const; static constexpr int32 MaxEntries = 1000; }`
  - `enum class ECommandAction { Skip, Report, SetAutoPause }`, `struct FCommandPlan { ECommandAction Action; bool bEnabled; FCommandResult Result; }`
  - `FCommandPlan PlanCommand(const FAgentCommand&, FCommandLedger&, const FDateTime& Now)`
  - `bool ShouldRetryResult(int32 AttemptsMade, EReplyKind Kind, const FDateTime& ExpiresAt, const FDateTime& Now)`
  - the constants `MaxCommandLifetimeSeconds = 600` and `ResultAttempts = 5`

**How 2B uses it:**
1. For each polled command, it calls `PlanCommand`.
2. `SetAutoPause` means: run it natively, then call `Ledger.Finish(Id, CommandOk() or CommandFailed(TEXT("upstream_error")))` and report.
3. `Report` means: post `Result` as it is.
4. `Skip` means: do nothing.

- [ ] **Step 1: Write the failing tests**

`CommandsTest.cpp`:

```cpp
#include "SatisDash/Commands.h"
#include "SatisDash/JsonUtil.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	const FDateTime Now(2026, 10, 20, 12, 0, 0);

	FAgentCommand Command(const TCHAR* Id, const TCHAR* Type, const TCHAR* Params, FTimespan ExpiresIn = FTimespan::FromMinutes(5))
	{
		FAgentCommand C;
		C.Id = Id;
		C.Type = Type;
		C.Params = Json::ParseObject(Params);
		C.ExpiresAt = Now + ExpiresIn;
		return C;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommandsLedgerTest, "SatisDash.Core.Commands.Ledger", SatisDash::Test::Flags)

bool FCommandsLedgerTest::RunTest(const FString& Parameters)
{
	FCommandLedger Ledger;
	FCommandResult Done;
	const FDateTime Later = Now + FTimespan::FromMinutes(5);
	TestTrue(TEXT("first sight"), Ledger.Begin(TEXT("a"), Later, Now, Done) == FCommandLedger::EVerdict::New);
	TestTrue(TEXT("again while running"), Ledger.Begin(TEXT("a"), Later, Now, Done) == FCommandLedger::EVerdict::Running);
	Ledger.Finish(TEXT("a"), CommandOk());
	TestTrue(TEXT("again once finished"), Ledger.Begin(TEXT("a"), Later, Now, Done) == FCommandLedger::EVerdict::Done && Done.bOk);

	// Past its expiry an entry is purged: the backend won't send it again.
	TestTrue(TEXT("expired entries are purged"), Ledger.Begin(TEXT("b"), Later, Later + FTimespan::FromSeconds(1), Done) == FCommandLedger::EVerdict::New);
	TestTrue(TEXT("so a is new again"), Ledger.Begin(TEXT("a"), Later + FTimespan::FromMinutes(5), Later + FTimespan::FromSeconds(1), Done) == FCommandLedger::EVerdict::New);

	FCommandLedger Full;
	for (int32 I = 0; I <= FCommandLedger::MaxEntries; ++I)
	{
		Full.Begin(FString::Printf(TEXT("id%d"), I), Later, Now, Done);
	}
	TestEqual(TEXT("bounded"), Full.Num(), FCommandLedger::MaxEntries);
	TestTrue(TEXT("the oldest was evicted"), Full.Begin(TEXT("id0"), Later, Now, Done) == FCommandLedger::EVerdict::New);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommandsCaseTest, "SatisDash.Core.Commands.CaseSensitive", SatisDash::Test::Flags)

bool FCommandsCaseTest::RunTest(const FString& Parameters)
{
	FCommandLedger Ledger;
	FCommandResult Done;
	const FDateTime Later = Now + FTimespan::FromMinutes(5);
	Ledger.Begin(TEXT("abc"), Later, Now, Done);
	TestTrue(TEXT("ABC is another command"), Ledger.Begin(TEXT("ABC"), Later, Now, Done) == FCommandLedger::EVerdict::New);
	const FCommandPlan Upper = PlanCommand(Command(TEXT("c9"), TEXT("SET_AUTO_PAUSE"), TEXT("{\"enabled\":true}")), Ledger, Now);
	TestTrue(TEXT("a type in the wrong case is unsupported"), Upper.Action == ECommandAction::Report && !Upper.Result.bOk);
	Test::SameText(*this, TEXT("its code"), Upper.Result.Code, TEXT("unsupported"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommandsPlanTest, "SatisDash.Core.Commands.Plan", SatisDash::Test::Flags)

bool FCommandsPlanTest::RunTest(const FString& Parameters)
{
	FCommandLedger Ledger;
	const FCommandPlan Run = PlanCommand(Command(TEXT("c1"), TEXT("set_auto_pause"), TEXT("{\"enabled\":false}")), Ledger, Now);
	TestTrue(TEXT("set_auto_pause runs"), Run.Action == ECommandAction::SetAutoPause && !Run.bEnabled);
	TestTrue(TEXT("not twice while it runs"), PlanCommand(Command(TEXT("c1"), TEXT("set_auto_pause"), TEXT("{\"enabled\":false}")), Ledger, Now).Action == ECommandAction::Skip);
	Ledger.Finish(TEXT("c1"), CommandFailed(TEXT("upstream_error")));
	const FCommandPlan Again = PlanCommand(Command(TEXT("c1"), TEXT("set_auto_pause"), TEXT("{\"enabled\":false}")), Ledger, Now);
	TestTrue(TEXT("a repeat reports the first result, never runs again"), Again.Action == ECommandAction::Report && !Again.Result.bOk);
	Test::SameText(*this, TEXT("the first result's code"), Again.Result.Code, TEXT("upstream_error"));

	auto Unsupported = [&](const TCHAR* Id, const TCHAR* Type, const TCHAR* Params)
	{
		const FCommandPlan P = PlanCommand(Command(Id, Type, Params), Ledger, Now);
		TestTrue(FString::Printf(TEXT("%s %s is unsupported"), Type, Params), P.Action == ECommandAction::Report && P.Result.Code.Equals(TEXT("unsupported"), ESearchCase::CaseSensitive));
	};
	Unsupported(TEXT("u1"), TEXT("restart_server"), TEXT("{}"));
	Unsupported(TEXT("u2"), TEXT("set_auto_pause"), TEXT("{}"));
	Unsupported(TEXT("u3"), TEXT("set_auto_pause"), TEXT("{\"enabled\":\"true\"}"));
	Unsupported(TEXT("u4"), TEXT("set_auto_pause"), TEXT("{\"enabled\":true,\"x\":1}"));
	TestTrue(TEXT("an unsupported repeat is reported, not re-planned"), PlanCommand(Command(TEXT("u1"), TEXT("restart_server"), TEXT("{}")), Ledger, Now).Action == ECommandAction::Report);

	TestTrue(TEXT("expired: skipped"), PlanCommand(Command(TEXT("e1"), TEXT("set_auto_pause"), TEXT("{\"enabled\":true}"), FTimespan::FromSeconds(-1)), Ledger, Now).Action == ECommandAction::Skip);
	TestTrue(TEXT("over 10 minutes out: skipped"), PlanCommand(Command(TEXT("e2"), TEXT("set_auto_pause"), TEXT("{\"enabled\":true}"), FTimespan::FromMinutes(11)), Ledger, Now).Action == ECommandAction::Skip);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommandsResultTest, "SatisDash.Core.Commands.Result", SatisDash::Test::Flags)

bool FCommandsResultTest::RunTest(const FString& Parameters)
{
	Test::SameText(*this, TEXT("ok"), CommandResultToJson(CommandOk()), TEXT("{\"ok\":true}"));
	Test::SameText(*this, TEXT("failed"), CommandResultToJson(CommandFailed(TEXT("unsupported"))), TEXT("{\"ok\":false,\"code\":\"unsupported\"}"));
	Test::SameText(*this, TEXT("never free text"), CommandFailed(TEXT("disk on fire")).Code, TEXT("upstream_error"));
	const FDateTime Later = Now + FTimespan::FromMinutes(1);
	TestTrue(TEXT("retry a transient failure"), ShouldRetryResult(1, EReplyKind::Transient, Later, Now));
	TestFalse(TEXT("not after 5 attempts"), ShouldRetryResult(5, EReplyKind::Transient, Later, Now));
	TestFalse(TEXT("not a refusal"), ShouldRetryResult(1, EReplyKind::Fatal, Later, Now));
	TestFalse(TEXT("not once expired"), ShouldRetryResult(1, EReplyKind::Transient, Now, Later));
	return true;
}
```

- [ ] **Step 2: Run them to verify they fail**

Run the C++ tests. Expected: the build fails because `SatisDash/Commands.h` doesn't exist.

- [ ] **Step 3: Write the implementation**

`Public/SatisDash/Commands.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Responses.h"

namespace SatisDash
{
	/** CommandResultRequestSchema: ok, or one of four codes; never free text. */
	struct FCommandResult
	{
		bool bOk = false;
		FString Code;
	};

	SATISDASHCORE_API FCommandResult CommandOk();
	/** One of unsupported, upstream_unreachable, upstream_auth_rejected, upstream_error; anything else becomes upstream_error. */
	SATISDASHCORE_API FCommandResult CommandFailed(const TCHAR* Code);
	SATISDASHCORE_API FString CommandResultToJson(const FCommandResult& Result);

	/** agent/src/ledger.ts: which command IDs were seen, so none runs twice (S6). In memory, bounded, purged at expiry. */
	class SATISDASHCORE_API FCommandLedger
	{
	public:
		static constexpr int32 MaxEntries = 1000;
		enum class EVerdict : uint8 { New, Running, Done };

		/** Records Id when new. Done fills OutDone with the result Finish recorded. IDs compare case-sensitively. */
		EVerdict Begin(const FString& Id, const FDateTime& ExpiresAt, const FDateTime& Now, FCommandResult& OutDone);
		void Finish(const FString& Id, const FCommandResult& Result);
		int32 Num() const { return Entries.Num(); }

	private:
		struct FEntry
		{
			FString Id;
			FDateTime ExpiresAt;
			TOptional<FCommandResult> Result;
		};
		TArray<FEntry> Entries; // oldest first
	};

	inline constexpr int32 MaxCommandLifetimeSeconds = 600;
	inline constexpr int32 ResultAttempts = 5;

	enum class ECommandAction : uint8
	{
		/** Expired, too far out, or already running: nothing to do. */
		Skip,
		/** Post Result: an unsupported command, or a repeat of a finished one. */
		Report,
		/** Run set_auto_pause natively with bEnabled, then Finish the ledger entry and report. */
		SetAutoPause,
	};

	struct FCommandPlan
	{
		ECommandAction Action = ECommandAction::Skip;
		bool bEnabled = false;
		FCommandResult Result;
	};

	/** commandRunner.ts's handle(): only set_auto_pause with exactly {"enabled": <bool>} runs (S6). */
	SATISDASHCORE_API FCommandPlan PlanCommand(const FAgentCommand& Command, FCommandLedger& Ledger, const FDateTime& Now);
	/** commandRunner.ts's report(): up to 5 attempts, only on a transient failure, and never past the command's expiry. */
	SATISDASHCORE_API bool ShouldRetryResult(int32 AttemptsMade, EReplyKind Kind, const FDateTime& ExpiresAt, const FDateTime& Now);
}
```

`Private/Commands.cpp`:

```cpp
#include "SatisDash/Commands.h"

#include "Dom/JsonValue.h"
#include "SatisDash/JsonUtil.h"

SatisDash::FCommandResult SatisDash::CommandOk()
{
	FCommandResult Result;
	Result.bOk = true;
	return Result;
}

SatisDash::FCommandResult SatisDash::CommandFailed(const TCHAR* Code)
{
	static const TCHAR* Known[] = { TEXT("unsupported"), TEXT("upstream_unreachable"), TEXT("upstream_auth_rejected"), TEXT("upstream_error") };
	FCommandResult Result;
	Result.Code = TEXT("upstream_error");
	for (const TCHAR* K : Known)
	{
		if (FCString::Strcmp(Code, K) == 0)
		{
			Result.Code = K;
		}
	}
	return Result;
}

FString SatisDash::CommandResultToJson(const FCommandResult& Result)
{
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetBoolField(TEXT("ok"), Result.bOk);
	if (!Result.bOk)
	{
		Object->SetStringField(TEXT("code"), Result.Code);
	}
	return Json::Write(Object);
}

SatisDash::FCommandLedger::EVerdict SatisDash::FCommandLedger::Begin(const FString& Id, const FDateTime& ExpiresAt, const FDateTime& Now, FCommandResult& OutDone)
{
	Entries.RemoveAll([&Now](const FEntry& Entry) { return Entry.ExpiresAt <= Now; });
	for (const FEntry& Entry : Entries)
	{
		if (Entry.Id.Equals(Id, ESearchCase::CaseSensitive))
		{
			if (!Entry.Result.IsSet())
			{
				return EVerdict::Running;
			}
			OutDone = Entry.Result.GetValue();
			return EVerdict::Done;
		}
	}
	Entries.Add({ Id, ExpiresAt, {} });
	if (Entries.Num() > MaxEntries)
	{
		Entries.RemoveAt(0, Entries.Num() - MaxEntries);
	}
	return EVerdict::New;
}

void SatisDash::FCommandLedger::Finish(const FString& Id, const FCommandResult& Result)
{
	for (FEntry& Entry : Entries)
	{
		if (Entry.Id.Equals(Id, ESearchCase::CaseSensitive))
		{
			Entry.Result = Result;
		}
	}
}

namespace
{
	/** SetAutoPauseParamsSchema is strict: exactly one field, "enabled", a boolean. */
	TOptional<bool> AutoPauseEnabled(const TSharedPtr<FJsonObject>& Params)
	{
		if (!Params.IsValid() || Params->Values.Num() != 1)
		{
			return {};
		}
		const TSharedPtr<FJsonValue> Enabled = SatisDash::Json::Field(Params, TEXT("enabled"), EJson::Boolean);
		return Enabled.IsValid() ? TOptional<bool>(Enabled->AsBool()) : TOptional<bool>();
	}
}

SatisDash::FCommandPlan SatisDash::PlanCommand(const FAgentCommand& Command, FCommandLedger& Ledger, const FDateTime& Now)
{
	FCommandPlan Plan;
	const FTimespan Left = Command.ExpiresAt - Now;
	if (Left <= FTimespan::Zero() || Left.GetTotalSeconds() > MaxCommandLifetimeSeconds)
	{
		return Plan;
	}
	FCommandResult Done;
	switch (Ledger.Begin(Command.Id, Command.ExpiresAt, Now, Done))
	{
	case FCommandLedger::EVerdict::Running:
		return Plan;
	case FCommandLedger::EVerdict::Done:
		Plan.Action = ECommandAction::Report;
		Plan.Result = Done;
		return Plan;
	case FCommandLedger::EVerdict::New:
		break;
	}
	const TOptional<bool> Enabled = Command.Type.Equals(TEXT("set_auto_pause"), ESearchCase::CaseSensitive)
		? AutoPauseEnabled(Command.Params) : TOptional<bool>();
	if (!Enabled.IsSet())
	{
		Plan.Action = ECommandAction::Report;
		Plan.Result = CommandFailed(TEXT("unsupported"));
		Ledger.Finish(Command.Id, Plan.Result);
		return Plan;
	}
	Plan.Action = ECommandAction::SetAutoPause;
	Plan.bEnabled = Enabled.GetValue();
	return Plan;
}

bool SatisDash::ShouldRetryResult(int32 AttemptsMade, EReplyKind Kind, const FDateTime& ExpiresAt, const FDateTime& Now)
{
	return AttemptsMade < ResultAttempts && Kind == EReplyKind::Transient && Now < ExpiresAt;
}
```

- [ ] **Step 4: Run check-api and the tests to verify they pass**

Run check-api. Expected: `0 missing, 0 bad rows`, after adding looked-up rows.
Run the C++ tests with `--min 21`. Expected: `mod tests: PASS (21 passed)`.

- [ ] **Step 5: Commit**

```bash
git add mod api-sources.md
git commit -m "Core mod: the command ledger and plan, set_auto_pause only (Plan 2A Task 6)"
```

---

### Task 7: Requests: what each call sends (S7, gzip)

**Files:**
- Create: `Source/SatisDashCore/Public/SatisDash/Requests.h`, `Private/Requests.cpp`
- Test: `Source/SatisDashCoreTests/Private/RequestsTest.cpp`

**Interfaces:**
- Consumes: `FAgentConfig` (Task 2), `FCredential` (Task 3), `FCommandResult`, `CommandResultToJson` (Task 6), `AgentVersion()` (Task 1), `Json::*` (Task 2).
- Produces:
  - `struct FRequestSpec { FString Verb; FString Url; TArray<TPair<FString, FString>> Headers; TArray<uint8> Body; float TimeoutSeconds; }`
  - `FString PercentEncode(const FString&)`
  - `BuildEnrollRequest(const FAgentConfig&)`
  - `BuildSnapshotRequest(const FAgentConfig&, const FCredential&, const FString& SnapshotJson)`
  - `BuildCommandsPoll(const FAgentConfig&, const FCredential&)`
  - `BuildResultRequest(const FAgentConfig&, const FCredential&, const FString& CommandId, const FCommandResult&)`
  - the constants `RequestTimeoutSeconds = 15`, `PollWaitSeconds = 25`, `LongPollGraceSeconds = 10` and `GzipOverBytes = 1024`

2B copies each `FRequestSpec` field onto an `IHttpRequest` (`SetVerb`, `SetURL`, `SetHeader`, `SetContent`, `SetTimeout`), and adds nothing.

- [ ] **Step 1: Write the failing tests**

`RequestsTest.cpp`:

```cpp
#include "SatisDash/JsonUtil.h"
#include "SatisDash/Requests.h"
#include "SatisDash/Version.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	FAgentConfig Config()
	{
		FAgentConfig C;
		C.BackendUrl = TEXT("https://example.com/base");
		C.EnrollmentCode = TEXT("AB3D-7XQ2");
		return C;
	}

	FCredential Credential()
	{
		FCredential C;
		C.AgentSecret = Test::FakeSecret();
		C.ServerId = TEXT("srv-1");
		return C;
	}

	FString Header(const FRequestSpec& Spec, const TCHAR* Name)
	{
		for (const TPair<FString, FString>& H : Spec.Headers)
		{
			if (H.Key.Equals(Name, ESearchCase::IgnoreCase))
			{
				return H.Value;
			}
		}
		return FString();
	}

	FString BodyText(const FRequestSpec& Spec)
	{
		const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Spec.Body.GetData()), Spec.Body.Num());
		return FString::ConstructFromPtrSize(Text.Get(), Text.Length());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRequestsEnrollTest, "SatisDash.Core.Requests.Enroll", SatisDash::Test::Flags)

bool FRequestsEnrollTest::RunTest(const FString& Parameters)
{
	const FRequestSpec Spec = BuildEnrollRequest(Config());
	Test::SameText(*this, TEXT("verb"), Spec.Verb, TEXT("POST"));
	Test::SameText(*this, TEXT("url"), Spec.Url, TEXT("https://example.com/base/agent/v1/enroll"));
	TestTrue(TEXT("no Authorization on enrol"), Header(Spec, TEXT("Authorization")).IsEmpty());
	Test::SameText(*this, TEXT("content type"), Header(Spec, TEXT("Content-Type")), TEXT("application/json"));
	Test::SameText(*this, TEXT("user agent: the mod's version only"), Header(Spec, TEXT("User-Agent")), TEXT("satisfactory-dash-") + AgentVersion());
	const TSharedPtr<FJsonObject> Body = Json::ParseObject(BodyText(Spec));
	TestTrue(TEXT("body is JSON"), Body.IsValid());
	if (Body.IsValid())
	{
		Test::SameText(*this, TEXT("code"), Body->GetStringField(TEXT("code")), TEXT("AB3D-7XQ2"));
		Test::SameText(*this, TEXT("agentVersion"), Body->GetStringField(TEXT("agentVersion")), AgentVersion());
		TestEqual(TEXT("exactly two fields (EnrollRequestSchema is strict)"), Body->Values.Num(), 2);
	}
	TestEqual(TEXT("timeout"), Spec.TimeoutSeconds, RequestTimeoutSeconds);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRequestsAuthTest, "SatisDash.Core.Requests.Authenticated", SatisDash::Test::Flags)

bool FRequestsAuthTest::RunTest(const FString& Parameters)
{
	const FString Bearer = TEXT("Bearer ") + Test::FakeSecret();
	const FRequestSpec Snapshot = BuildSnapshotRequest(Config(), Credential(), TEXT("{\"a\":1}"));
	const FRequestSpec Poll = BuildCommandsPoll(Config(), Credential());
	const FRequestSpec Result = BuildResultRequest(Config(), Credential(), TEXT("a/b?c"), CommandFailed(TEXT("upstream_error")));

	Test::SameText(*this, TEXT("snapshot url"), Snapshot.Url, TEXT("https://example.com/base/agent/v1/snapshots"));
	Test::SameText(*this, TEXT("poll verb"), Poll.Verb, TEXT("GET"));
	Test::SameText(*this, TEXT("poll url"), Poll.Url, TEXT("https://example.com/base/agent/v1/commands?waitSeconds=25"));
	TestEqual(TEXT("poll timeout: the wait plus grace"), Poll.TimeoutSeconds, 35.f);
	TestEqual(TEXT("poll has no body"), Poll.Body.Num(), 0);
	Test::SameText(*this, TEXT("result url, id percent-encoded"), Result.Url, TEXT("https://example.com/base/agent/v1/commands/a%2Fb%3Fc/result"));
	Test::SameText(*this, TEXT("result body"), BodyText(Result), TEXT("{\"ok\":false,\"code\":\"upstream_error\"}"));

	for (const FRequestSpec* Spec : { &Snapshot, &Poll, &Result })
	{
		Test::SameText(*this, TEXT("Authorization header"), Header(*Spec, TEXT("Authorization")), Bearer);
		TestFalse(TEXT("the secret is never in a URL (S7)"), Spec->Url.Contains(Test::FakeSecret(), ESearchCase::CaseSensitive));
		Test::SameText(*this, TEXT("accept"), Header(*Spec, TEXT("Accept")), TEXT("application/json"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRequestsGzipTest, "SatisDash.Core.Requests.Gzip", SatisDash::Test::Flags)

bool FRequestsGzipTest::RunTest(const FString& Parameters)
{
	auto OfBytes = [](int32 Bytes) { return TEXT("{\"p\":\"") + FString::ChrN(Bytes - 8, TEXT('x')) + TEXT("\"}"); };
	const FRequestSpec Small = BuildSnapshotRequest(Config(), Credential(), OfBytes(1024));
	TestEqual(TEXT("1024 bytes: sent as is"), Small.Body.Num(), 1024);
	TestTrue(TEXT("no Content-Encoding"), Header(Small, TEXT("Content-Encoding")).IsEmpty());

	const FRequestSpec Big = BuildSnapshotRequest(Config(), Credential(), OfBytes(1025));
	Test::SameText(*this, TEXT("1025 bytes: gzipped"), Header(Big, TEXT("Content-Encoding")), TEXT("gzip"));
	TestTrue(TEXT("gzip magic bytes"), Big.Body.Num() > 2 && Big.Body[0] == 0x1f && Big.Body[1] == 0x8b);
	TestTrue(TEXT("smaller than the input"), Big.Body.Num() < 1025);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRequestsEncodeTest, "SatisDash.Core.Requests.PercentEncode", SatisDash::Test::Flags)

bool FRequestsEncodeTest::RunTest(const FString& Parameters)
{
	Test::SameText(*this, TEXT("unreserved kept"), PercentEncode(TEXT("AZaz09-._~")), TEXT("AZaz09-._~"));
	Test::SameText(*this, TEXT("space"), PercentEncode(TEXT("a b")), TEXT("a%20b"));
	Test::SameText(*this, TEXT("UTF-8"), PercentEncode(TEXT("é")), TEXT("%C3%A9"));
	return true;
}
```

- [ ] **Step 2: Run them to verify they fail**

Run the C++ tests. Expected: the build fails because `SatisDash/Requests.h` doesn't exist.

- [ ] **Step 3: Write the implementation**

`Public/SatisDash/Requests.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Commands.h"
#include "SatisDash/Config.h"
#include "SatisDash/Credential.h"

namespace SatisDash
{
	/** One call to make, built in full here so Plan 2B's transport only copies it onto an IHttpRequest. */
	struct FRequestSpec
	{
		FString Verb;
		FString Url;
		TArray<TPair<FString, FString>> Headers;
		TArray<uint8> Body;
		float TimeoutSeconds = 15.f;
	};

	// backendClient.ts: REQUEST_TIMEOUT_MS, LONG_POLL_GRACE_MS, GZIP_OVER_BYTES; the contract's longest wait (25 s).
	inline constexpr float RequestTimeoutSeconds = 15.f;
	inline constexpr int32 PollWaitSeconds = 25;
	inline constexpr float LongPollGraceSeconds = 10.f;
	inline constexpr int32 GzipOverBytes = 1024;

	/** RFC 3986: unreserved characters kept, every other UTF-8 byte as %XX. For a command ID in a path. */
	SATISDASHCORE_API FString PercentEncode(const FString& Value);
	/** POST /agent/v1/enroll with {code, agentVersion}. No Authorization header. */
	SATISDASHCORE_API FRequestSpec BuildEnrollRequest(const FAgentConfig& Config);
	/** POST /agent/v1/snapshots. The secret goes only in the Authorization header (S7). */
	SATISDASHCORE_API FRequestSpec BuildSnapshotRequest(const FAgentConfig& Config, const FCredential& Credential, const FString& SnapshotJson);
	/** GET /agent/v1/commands?waitSeconds=25, with a 35 s timeout. */
	SATISDASHCORE_API FRequestSpec BuildCommandsPoll(const FAgentConfig& Config, const FCredential& Credential);
	/** POST /agent/v1/commands/<id>/result. */
	SATISDASHCORE_API FRequestSpec BuildResultRequest(const FAgentConfig& Config, const FCredential& Credential, const FString& CommandId, const FCommandResult& Result);
}
```

`Private/Requests.cpp`:

```cpp
#include "SatisDash/Requests.h"

#include "Misc/Compression.h"
#include "SatisDash/JsonUtil.h"
#include "SatisDash/Version.h"

namespace
{
	SatisDash::FRequestSpec Start(const TCHAR* Verb, const FString& Url)
	{
		SatisDash::FRequestSpec Spec;
		Spec.Verb = Verb;
		Spec.Url = Url;
		Spec.TimeoutSeconds = SatisDash::RequestTimeoutSeconds;
		Spec.Headers.Add({ TEXT("Accept"), TEXT("application/json") });
		// The Node agent sends its own version the same way. Nothing else about the machine (contract: agentVersion).
		Spec.Headers.Add({ TEXT("User-Agent"), TEXT("satisfactory-dash-") + SatisDash::AgentVersion() });
		return Spec;
	}

	void Authorize(SatisDash::FRequestSpec& Spec, const SatisDash::FCredential& Credential)
	{
		Spec.Headers.Add({ TEXT("Authorization"), TEXT("Bearer ") + Credential.AgentSecret });
	}

	void SetJsonBody(SatisDash::FRequestSpec& Spec, const FString& Json)
	{
		const FTCHARToUTF8 Utf8(*Json);
		TArray<uint8> Raw(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		Spec.Headers.Add({ TEXT("Content-Type"), TEXT("application/json") });
		if (Raw.Num() > SatisDash::GzipOverBytes)
		{
			int32 Size = FCompression::CompressMemoryBound(NAME_Gzip, Raw.Num());
			TArray<uint8> Packed;
			Packed.SetNumUninitialized(Size);
			if (FCompression::CompressMemory(NAME_Gzip, Packed.GetData(), Size, Raw.GetData(), Raw.Num()))
			{
				Packed.SetNum(Size);
				Spec.Headers.Add({ TEXT("Content-Encoding"), TEXT("gzip") });
				Spec.Body = MoveTemp(Packed);
				return;
			}
		}
		Spec.Body = MoveTemp(Raw); // compression failing is not a reason to lose the snapshot
	}
}

FString SatisDash::PercentEncode(const FString& Value)
{
	const FTCHARToUTF8 Utf8(*Value);
	FString Out;
	for (int32 I = 0; I < Utf8.Length(); ++I)
	{
		const uint8 B = static_cast<uint8>(Utf8.Get()[I]);
		const bool bUnreserved = (B >= 'A' && B <= 'Z') || (B >= 'a' && B <= 'z') || (B >= '0' && B <= '9') ||
			B == '-' || B == '.' || B == '_' || B == '~';
		Out += bUnreserved ? FString::Chr(TCHAR(B)) : FString::Printf(TEXT("%%%02X"), B);
	}
	return Out;
}

SatisDash::FRequestSpec SatisDash::BuildEnrollRequest(const FAgentConfig& Config)
{
	FRequestSpec Spec = Start(TEXT("POST"), Config.BackendUrl + TEXT("/agent/v1/enroll"));
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("code"), Config.EnrollmentCode);
	Body->SetStringField(TEXT("agentVersion"), AgentVersion());
	SetJsonBody(Spec, Json::Write(Body));
	return Spec;
}

SatisDash::FRequestSpec SatisDash::BuildSnapshotRequest(const FAgentConfig& Config, const FCredential& Credential, const FString& SnapshotJson)
{
	FRequestSpec Spec = Start(TEXT("POST"), Config.BackendUrl + TEXT("/agent/v1/snapshots"));
	Authorize(Spec, Credential);
	SetJsonBody(Spec, SnapshotJson);
	return Spec;
}

SatisDash::FRequestSpec SatisDash::BuildCommandsPoll(const FAgentConfig& Config, const FCredential& Credential)
{
	FRequestSpec Spec = Start(TEXT("GET"), Config.BackendUrl + FString::Printf(TEXT("/agent/v1/commands?waitSeconds=%d"), PollWaitSeconds));
	Spec.TimeoutSeconds = PollWaitSeconds + LongPollGraceSeconds;
	Authorize(Spec, Credential);
	return Spec;
}

SatisDash::FRequestSpec SatisDash::BuildResultRequest(const FAgentConfig& Config, const FCredential& Credential, const FString& CommandId, const FCommandResult& Result)
{
	FRequestSpec Spec = Start(TEXT("POST"), Config.BackendUrl + TEXT("/agent/v1/commands/") + PercentEncode(CommandId) + TEXT("/result"));
	Authorize(Spec, Credential);
	SetJsonBody(Spec, CommandResultToJson(Result));
	return Spec;
}
```

- [ ] **Step 4: Run check-api and the tests to verify they pass**

Run check-api. Expected: `0 missing, 0 bad rows`, after adding looked-up rows. These include `FCompression::CompressMemoryBound` (`Engine/Source/Runtime/Core/Public/Misc/Compression.h:71`), `FCompression::CompressMemory` (`Compression.h:110`) and `NAME_Gzip` (`Engine/Source/Runtime/Core/Public/UObject/UnrealNames.inl:207`).
Run the C++ tests with `--min 25`. Expected: `mod tests: PASS (25 passed)`.

- [ ] **Step 5: Commit**

```bash
git add mod api-sources.md
git commit -m "Core mod: request builders, secret in the header only, gzip over 1 KB (Plan 2A Task 7)"
```

---

### Task 8: The pusher: backoff, Retry-After and the 120-snapshot queue

**Files:**
- Create: `Source/SatisDashCore/Public/SatisDash/Pusher.h`, `Private/Pusher.cpp`
- Test: `Source/SatisDashCoreTests/Private/PusherTest.cpp`

**Interfaces:**
- Consumes: `FHttpReply`, `ClassifyReply`, `ParseSnapshotResponse`, `FSnapshotResponse` (Task 5); `LogSatisDash` (Task 1).
- Produces:
  - `double BackoffSeconds(int32 Attempt, double Random01)`
  - `TOptional<double> ParseRetryAfterSeconds(const FString&)`
  - the constants `MaxQueuedSnapshots = 120` and `MaxRetryAfterSeconds = 300`
  - `class FPusher { explicit FPusher(TFunction<double()> Random01); void Enqueue(FString Json); TOptional<FString> StartSend(double Now); FOutcome FinishSend(const FHttpReply&, double Now); void Clear(); int32 Num() const; int32 Failures() const; }`, with `struct FOutcome { bool bHalt; TOptional<FSnapshotResponse> Response; }`

How 2B uses it:
- Each tick, `StartSend(Now)` gives the JSON to post through `BuildSnapshotRequest` (Task 7). Its reply goes to `FinishSend`.
- `bHalt` goes to `FAgent::OnAuthRejected` (Task 9).
- `Response` updates the cadence and the commands poll.

- [ ] **Step 1: Write the failing tests**

`PusherTest.cpp`:

```cpp
#include "SatisDash/Pusher.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	FHttpReply Ok() { return Test::Reply(200, Test::SnapshotAnswerJson); }

	FHttpReply Status(int32 Code, const TCHAR* RetryAfter = TEXT(""))
	{
		FHttpReply Reply = Test::Reply(Code, TEXT("{}"));
		Reply.RetryAfter = RetryAfter;
		return Reply;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPusherBackoffTest, "SatisDash.Core.Pusher.Backoff", SatisDash::Test::Flags)

bool FPusherBackoffTest::RunTest(const FString& Parameters)
{
	// backoff.ts: the ceiling doubles from 1 s to 60 s; the delay is uniform in [ceiling / 2, ceiling].
	TestEqual(TEXT("first, low"), BackoffSeconds(0, 0.0), 0.5);
	TestEqual(TEXT("first, high"), BackoffSeconds(0, 1.0), 1.0);
	TestEqual(TEXT("second"), BackoffSeconds(1, 1.0), 2.0);
	TestEqual(TEXT("sixth"), BackoffSeconds(5, 1.0), 32.0);
	TestEqual(TEXT("capped at 60"), BackoffSeconds(6, 1.0), 60.0);
	TestEqual(TEXT("capped, low"), BackoffSeconds(6, 0.0), 30.0);
	TestEqual(TEXT("far past the cap"), BackoffSeconds(1000, 1.0), 60.0);
	TestEqual(TEXT("a negative attempt is the first"), BackoffSeconds(-3, 1.0), 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPusherRetryAfterTest, "SatisDash.Core.Pusher.RetryAfter", SatisDash::Test::Flags)

bool FPusherRetryAfterTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("seconds"), ParseRetryAfterSeconds(TEXT("120")).Get(-1.0), 120.0);
	TestEqual(TEXT("decimal"), ParseRetryAfterSeconds(TEXT(" 1.5 ")).Get(-1.0), 1.5);
	TestEqual(TEXT("capped at 300"), ParseRetryAfterSeconds(TEXT("1000")).Get(-1.0), 300.0);
	TestFalse(TEXT("empty"), ParseRetryAfterSeconds(TEXT("")).IsSet());
	TestFalse(TEXT("negative"), ParseRetryAfterSeconds(TEXT("-1")).IsSet());
	TestFalse(TEXT("an HTTP date (backendClient.ts ignores it too)"), ParseRetryAfterSeconds(TEXT("Wed, 21 Oct 2015 07:28:00 GMT")).IsSet());
	TestFalse(TEXT("junk"), ParseRetryAfterSeconds(TEXT("soon")).IsSet());
	TestFalse(TEXT("overflow to infinity"), ParseRetryAfterSeconds(TEXT("1e309")).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPusherQueueTest, "SatisDash.Core.Pusher.Queue", SatisDash::Test::Flags)

bool FPusherQueueTest::RunTest(const FString& Parameters)
{
	FPusher Pusher([] { return 1.0; });
	for (int32 I = 0; I <= MaxQueuedSnapshots; ++I)
	{
		Pusher.Enqueue(FString::Printf(TEXT("s%d"), I));
	}
	TestEqual(TEXT("at most 120"), Pusher.Num(), MaxQueuedSnapshots);
	Test::SameText(*this, TEXT("the oldest was dropped"), Pusher.StartSend(0).Get(FString()), TEXT("s1"));
	Pusher.Clear();
	TestEqual(TEXT("cleared"), Pusher.Num(), 0);
	TestFalse(TEXT("nothing to send"), Pusher.StartSend(0).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPusherSendTest, "SatisDash.Core.Pusher.Send", SatisDash::Test::Flags)

bool FPusherSendTest::RunTest(const FString& Parameters)
{
	FPusher Pusher([] { return 1.0; });
	Pusher.Enqueue(TEXT("a"));
	TestTrue(TEXT("send"), Pusher.StartSend(0).IsSet());
	TestFalse(TEXT("one at a time"), Pusher.StartSend(0).IsSet());
	const FPusher::FOutcome Sent = Pusher.FinishSend(Ok(), 0);
	TestTrue(TEXT("sent: the answer is parsed"), Sent.Response.IsSet() && !Sent.bHalt);
	TestEqual(TEXT("and the snapshot is gone"), Pusher.Num(), 0);

	Pusher.Enqueue(TEXT("b"));
	Pusher.StartSend(0);
	Pusher.FinishSend(Status(503), 0);
	TestEqual(TEXT("kept after a 503"), Pusher.Num(), 1);
	TestFalse(TEXT("waits the first backoff"), Pusher.StartSend(0.99).IsSet());
	TestTrue(TEXT("retries after 1 s"), Pusher.StartSend(1.0).IsSet());
	Pusher.FinishSend(Status(0), 1.0);
	TestFalse(TEXT("then 2 s"), Pusher.StartSend(2.99).IsSet());
	TestTrue(TEXT("retries after 2 s more"), Pusher.StartSend(3.0).IsSet());
	Pusher.FinishSend(Status(429, TEXT("120")), 3.0);
	TestFalse(TEXT("Retry-After wins over a shorter backoff"), Pusher.StartSend(122.9).IsSet());
	TestTrue(TEXT("after Retry-After"), Pusher.StartSend(123.0).IsSet());
	Pusher.FinishSend(Status(503, TEXT("100000")), 123.0);
	TestTrue(TEXT("Retry-After capped at 300 s"), Pusher.StartSend(423.0).IsSet());
	Pusher.FinishSend(Test::Reply(200, TEXT("{\"nope\":1}")), 423.0);
	TestEqual(TEXT("a 2xx not in the expected form is retried"), Pusher.Num(), 1);
	TestEqual(TEXT("and counts as a failure"), Pusher.Failures(), 5);
	Pusher.StartSend(10000);
	Pusher.FinishSend(Ok(), 10000);
	TestEqual(TEXT("recovered"), Pusher.Failures(), 0);

	Pusher.Enqueue(TEXT("c"));
	Pusher.StartSend(10000);
	Pusher.FinishSend(Status(400), 10000);
	TestEqual(TEXT("a refusal drops the snapshot"), Pusher.Num(), 0);

	Pusher.Enqueue(TEXT("d"));
	Pusher.StartSend(10000);
	TestTrue(TEXT("401 halts"), Pusher.FinishSend(Status(401), 10000).bHalt);
	TestEqual(TEXT("and keeps the snapshot (2B clears it on halt)"), Pusher.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPusherInFlightTest, "SatisDash.Core.Pusher.InFlightDropped", SatisDash::Test::Flags)

bool FPusherInFlightTest::RunTest(const FString& Parameters)
{
	FPusher Pusher([] { return 1.0; });
	for (int32 I = 0; I < MaxQueuedSnapshots; ++I)
	{
		Pusher.Enqueue(FString::Printf(TEXT("s%d"), I));
	}
	Test::SameText(*this, TEXT("s0 in flight"), Pusher.StartSend(0).Get(FString()), TEXT("s0"));
	Pusher.Enqueue(TEXT("s120")); // the queue is full: s0, in flight, is dropped for room
	Pusher.FinishSend(Ok(), 0);
	TestEqual(TEXT("s0's success doesn't pop s1"), Pusher.Num(), MaxQueuedSnapshots);
	Test::SameText(*this, TEXT("s1 is next"), Pusher.StartSend(0).Get(FString()), TEXT("s1"));
	return true;
}
```

- [ ] **Step 2: Run them to verify they fail**

Run the C++ tests. Expected: the build fails because `SatisDash/Pusher.h` doesn't exist.

- [ ] **Step 3: Write the implementation**

`Public/SatisDash/Pusher.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Responses.h"

namespace SatisDash
{
	inline constexpr int32 MaxQueuedSnapshots = 120;
	inline constexpr double MaxRetryAfterSeconds = 300.0;

	/** backoff.ts: the ceiling is min(60, 2^attempt) s; the delay is uniform in [ceiling / 2, ceiling]. */
	SATISDASHCORE_API double BackoffSeconds(int32 Attempt, double Random01);
	/** backendClient.ts: Retry-After in seconds, capped at 300 s; an HTTP date, a negative or junk is ignored. */
	SATISDASHCORE_API TOptional<double> ParseRetryAfterSeconds(const FString& Header);

	/** pusher.ts and queue.ts: an in-memory queue, oldest first, sent one at a time with backoff (spec "Shape"). */
	class SATISDASHCORE_API FPusher
	{
	public:
		struct FOutcome
		{
			/** 401: the agent must halt (FAgent::OnAuthRejected). */
			bool bHalt = false;
			/** The backend's answer to a snapshot it took. */
			TOptional<FSnapshotResponse> Response;
		};

		explicit FPusher(TFunction<double()> InRandom01);
		/** Adds a snapshot, dropping the oldest past 120. Logs once when dropping starts. */
		void Enqueue(FString SnapshotJson);
		/** The snapshot to send now: none while one is in flight, while a retry waits, or when the queue is empty. */
		TOptional<FString> StartSend(double Now);
		/** The reply to the send StartSend began. */
		FOutcome FinishSend(const FHttpReply& Reply, double Now);
		void Clear();
		int32 Num() const { return Queue.Num(); }
		int32 Failures() const { return FailureCount; }

	private:
		struct FItem
		{
			uint64 Seq = 0;
			FString Json;
		};
		void PopIfFront(const TOptional<uint64>& Seq);

		TFunction<double()> Random01;
		TArray<FItem> Queue;
		uint64 NextSeq = 0;
		TOptional<uint64> InFlight;
		double NextAttemptAt = 0;
		int32 FailureCount = 0;
		int32 DroppedInStreak = 0;
		int32 Refused = 0;
	};
}
```

`Private/Pusher.cpp`:

```cpp
#include "SatisDash/Pusher.h"

#include "SatisDash/Log.h"

double SatisDash::BackoffSeconds(int32 Attempt, double Random01)
{
	const int32 Exponent = FMath::Clamp(Attempt, 0, 30);
	const double Ceiling = FMath::Min(60.0, FMath::Pow(2.0, double(Exponent)));
	return Ceiling / 2.0 + FMath::Clamp(Random01, 0.0, 1.0) * (Ceiling / 2.0);
}

TOptional<double> SatisDash::ParseRetryAfterSeconds(const FString& Header)
{
	const FString Value = Header.TrimStartAndEnd();
	if (Value.IsEmpty() || !Value.IsNumeric())
	{
		return {};
	}
	const double Seconds = FCString::Atod(*Value);
	if (!FMath::IsFinite(Seconds) || Seconds < 0)
	{
		return {};
	}
	return FMath::Min(Seconds, MaxRetryAfterSeconds);
}

SatisDash::FPusher::FPusher(TFunction<double()> InRandom01)
	: Random01(MoveTemp(InRandom01))
{
}

void SatisDash::FPusher::Enqueue(FString SnapshotJson)
{
	Queue.Add({ NextSeq++, MoveTemp(SnapshotJson) });
	if (Queue.Num() > MaxQueuedSnapshots)
	{
		const int32 Dropped = Queue.Num() - MaxQueuedSnapshots;
		Queue.RemoveAt(0, Dropped);
		if (DroppedInStreak == 0)
		{
			UE_LOG(LogSatisDash, Warning, TEXT("The snapshot queue is full (%d); dropping the oldest until the backend answers"), MaxQueuedSnapshots);
		}
		DroppedInStreak += Dropped;
	}
}

TOptional<FString> SatisDash::FPusher::StartSend(double Now)
{
	if (InFlight.IsSet() || Queue.Num() == 0 || Now < NextAttemptAt)
	{
		return {};
	}
	InFlight = Queue[0].Seq;
	return Queue[0].Json;
}

void SatisDash::FPusher::PopIfFront(const TOptional<uint64>& Seq)
{
	// The sent snapshot may have been dropped for room while it was in flight (pusher.ts: queue.peek() === head).
	if (Seq.IsSet() && Queue.Num() > 0 && Queue[0].Seq == Seq.GetValue())
	{
		Queue.RemoveAt(0);
	}
}

SatisDash::FPusher::FOutcome SatisDash::FPusher::FinishSend(const FHttpReply& Reply, double Now)
{
	FOutcome Out;
	const TOptional<uint64> Sent = InFlight;
	InFlight.Reset();
	const EReplyKind Kind = ClassifyReply(Reply.Code, true);
	if (Kind == EReplyKind::AuthRejected)
	{
		Out.bHalt = true;
		return Out;
	}
	if (Kind == EReplyKind::Fatal)
	{
		PopIfFront(Sent);
		++Refused;
		if (Refused == 1 || Refused % 100 == 0)
		{
			UE_LOG(LogSatisDash, Warning, TEXT("The backend refused a snapshot (%d); dropped it (%d refused so far)"), Reply.Code, Refused);
		}
		return Out;
	}
	if (Kind == EReplyKind::Ok)
	{
		Out.Response = ParseSnapshotResponse(Reply);
		if (Out.Response.IsSet())
		{
			PopIfFront(Sent);
			if (FailureCount > 0)
			{
				UE_LOG(LogSatisDash, Display, TEXT("Pushing snapshots recovered after %d attempts (%d queued, %d dropped)"), FailureCount, Queue.Num(), DroppedInStreak);
				FailureCount = 0;
				DroppedInStreak = 0;
			}
			return Out;
		}
	}
	// Transient, or a 2xx not in the expected form: backendClient.ts retries both.
	if (FailureCount == 0)
	{
		UE_LOG(LogSatisDash, Warning, TEXT("Pushing snapshots failed (%d); retrying with backoff (%d queued)"), Reply.Code, Queue.Num());
	}
	const double Delay = FMath::Max(ParseRetryAfterSeconds(Reply.RetryAfter).Get(0.0), BackoffSeconds(FailureCount, Random01()));
	++FailureCount;
	NextAttemptAt = Now + Delay;
	return Out;
}

void SatisDash::FPusher::Clear()
{
	Queue.Reset();
	InFlight.Reset();
	NextAttemptAt = 0;
	FailureCount = 0;
	DroppedInStreak = 0;
}
```

- [ ] **Step 4: Run check-api and the tests to verify they pass**

Run check-api. Expected: `0 missing, 0 bad rows`, after adding looked-up rows.
- If `FString::IsNumeric` accepts `1e309`, the `IsFinite` check still refuses it.
- If `IsNumeric` refuses `1.5`, parse the value with `FDefaultValueHelper::ParseDouble` instead (a ruling).

Run the C++ tests with `--min 30`. Expected: `mod tests: PASS (30 passed)`.

- [ ] **Step 5: Commit**

```bash
git add mod api-sources.md
git commit -m "Core mod: the pusher, with the Node agent's backoff and queue rules (Plan 2A Task 8)"
```

---

### Task 9: The agent's state machine: enrol, halt, fail closed (S10)

States and what moves them:

| State | Meaning | Leaves when |
| --- | --- | --- |
| `Idle` | No credential, no code | the config gains a code → `Enrolling` |
| `ConfigBroken` | The config can't be used | it is fixed → whichever state it then allows |
| `Enrolling` | A code and no credential | a 201 → `Running`; a refusal → `CodeRejected`; a transient failure waits out the backoff |
| `CodeRejected` | The backend refused the code | the config changes → `Enrolling` again, even with the same code |
| `Running` | Enrolled | a 401 on any call → `Halted` |
| `Halted` | Revoked; the credential is deleted | the config changes with a new code → `Enrolling` |

**The config check** runs each minute (2B's ticker). It re-reads the file when its modification time **or** its text changed. **Ruling:** the spec names the modification time only. Comparing the text as well catches an edit that keeps the timestamp, at the cost of reading a small file once a minute. **Cost if wrong:** none beyond that read.

**Files:**
- Create: `Source/SatisDashCore/Public/SatisDash/Agent.h`, `Private/Agent.cpp`
- Test: `Source/SatisDashCoreTests/Private/AgentTest.cpp`

**Interfaces:**
- Consumes:
  - `LoadConfigFile`, `BlankConfigCode`, `ConfigModTime`, `FAgentConfig`, `FConfigResult` (Task 2);
  - `FCredentialStore`, `FCredential` (Task 3);
  - `FHttpReply`, `ClassifyReply`, `ParseEnrollResponse`, `FCadence` (Task 5);
  - `BuildEnrollRequest`, `FRequestSpec` (Task 7);
  - `BackoffSeconds` (Task 8).
- Produces:
  - `enum class EAgentState { Idle, ConfigBroken, Enrolling, CodeRejected, Running, Halted }`
  - `class FAgent { FAgent(FString ConfigPath, FString CredentialPath, TFunction<double()> Random01); void Boot(); void CheckConfig(); EAgentState GetState() const; const FAgentConfig& GetConfig() const; const FCadence& GetCadence() const; void SetCadence(const FCadence&); const FCredential* GetCredential() const; TOptional<FRequestSpec> StartEnroll(double Now); void FinishEnroll(const FHttpReply&, double Now); void OnAuthRejected(); }`

**How 2B uses it:**
- 2B builds paths from `FPaths::ProjectDir()/Configs/SatisDashAgent.cfg` and `FPaths::ProjectSavedDir()/SatisDash/credential.json`.
- It calls `Boot` once and `CheckConfig` every 60 s.
- It sends `StartEnroll`'s request and feeds the reply to `FinishEnroll`.
- It pushes and polls only while `GetCredential()` is non-null.

- [ ] **Step 1: Write the failing tests**

`AgentTest.cpp`:

```cpp
#include "HAL/FileManager.h"
#include "SatisDash/Agent.h"
#include "SatisDash/Config.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	struct FFixture
	{
		Test::FTempDir Dir;
		FString ConfigPath = Dir.File(TEXT("Configs/SatisDashAgent.cfg"));
		FString CredentialPath = Dir.File(TEXT("SatisDash/credential.json"));

		void WriteConfig(const TCHAR* Code) const
		{
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(ConfigPath), true);
			Test::WriteText(ConfigPath, FString::Printf(TEXT("{\"backendUrl\":\"https://example.com\",\"enrollmentCode\":\"%s\"}"), Code));
		}

		void WriteCredential() const
		{
			FCredential C;
			C.AgentSecret = Test::FakeSecret();
			C.ServerId = TEXT("srv-1");
			FCredentialStore(CredentialPath).Save(C);
		}

		FAgent Agent() const { return FAgent(ConfigPath, CredentialPath, [] { return 1.0; }); }
		FString Code() const { return ParseConfig(Test::ReadText(ConfigPath)).Config.EnrollmentCode; }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAgentBootTest, "SatisDash.Core.Agent.Boot", SatisDash::Test::Flags)

bool FAgentBootTest::RunTest(const FString& Parameters)
{
	{
		const FFixture F;
		FAgent Agent = F.Agent();
		Agent.Boot();
		TestTrue(TEXT("first boot: idle"), Agent.GetState() == EAgentState::Idle);
		TestTrue(TEXT("and the config is written"), IFileManager::Get().FileExists(*F.ConfigPath));
	}
	{
		// The crash case: the credential was written but the code not yet blanked. The credential wins.
		const FFixture F;
		F.WriteConfig(TEXT("AB3D-7XQ2"));
		F.WriteCredential();
		FAgent Agent = F.Agent();
		Agent.Boot();
		TestTrue(TEXT("credential wins: running"), Agent.GetState() == EAgentState::Running);
		TestTrue(TEXT("the code is blanked then"), F.Code().IsEmpty());
		TestTrue(TEXT("never enrols with it"), !Agent.StartEnroll(0).IsSet());
	}
	{
		const FFixture F;
		F.WriteConfig(TEXT(""));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(F.CredentialPath), true);
		Test::WriteText(F.CredentialPath, TEXT("{ broken"));
		FAgent Agent = F.Agent();
		Agent.Boot();
		TestTrue(TEXT("a broken credential is not enrolled"), Agent.GetState() == EAgentState::Idle);
		TestTrue(TEXT("and never a Bearer"), Agent.GetCredential() == nullptr);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAgentEnrolTest, "SatisDash.Core.Agent.Enrol", SatisDash::Test::Flags)

bool FAgentEnrolTest::RunTest(const FString& Parameters)
{
	const FFixture F;
	F.WriteConfig(TEXT("AB3D-7XQ2"));
	FAgent Agent = F.Agent();
	Agent.Boot();
	TestTrue(TEXT("enrolling"), Agent.GetState() == EAgentState::Enrolling);
	const TOptional<FRequestSpec> First = Agent.StartEnroll(0);
	TestTrue(TEXT("an enrol request"), First.IsSet() && First->Url.EndsWith(TEXT("/agent/v1/enroll")));
	TestFalse(TEXT("one at a time"), Agent.StartEnroll(0).IsSet());

	Agent.FinishEnroll(Test::Reply(503, TEXT("{}")), 0);
	TestTrue(TEXT("still enrolling after a 503"), Agent.GetState() == EAgentState::Enrolling);
	TestFalse(TEXT("waits the backoff"), Agent.StartEnroll(0.5).IsSet());
	TestTrue(TEXT("then retries"), Agent.StartEnroll(1.0).IsSet());

	Agent.FinishEnroll(Test::Reply(201, Test::EnrollJson()), 1.0);
	TestTrue(TEXT("running"), Agent.GetState() == EAgentState::Running);
	TestTrue(TEXT("credential saved"), FCredentialStore(F.CredentialPath).Load().IsSet());
	TestTrue(TEXT("code blanked"), F.Code().IsEmpty());
	TestTrue(TEXT("credential in memory"), Agent.GetCredential() != nullptr);
	TestEqual(TEXT("cadence from the answer"), Agent.GetCadence().StatusSeconds, 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAgentRejectedTest, "SatisDash.Core.Agent.CodeRejected", SatisDash::Test::Flags)

bool FAgentRejectedTest::RunTest(const FString& Parameters)
{
	const FFixture F;
	F.WriteConfig(TEXT("AB3D-7XQ2"));
	FAgent Agent = F.Agent();
	Agent.Boot();
	Agent.StartEnroll(0);
	Agent.FinishEnroll(Test::Reply(404, TEXT("{}")), 0);
	TestTrue(TEXT("rejected"), Agent.GetState() == EAgentState::CodeRejected);
	TestFalse(TEXT("the code is kept"), F.Code().IsEmpty());
	TestFalse(TEXT("and not retried"), Agent.StartEnroll(1000).IsSet());
	Agent.CheckConfig();
	TestTrue(TEXT("an unchanged config doesn't retry"), Agent.GetState() == EAgentState::CodeRejected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAgentConfigChangeTest, "SatisDash.Core.Agent.ConfigChange", SatisDash::Test::Flags)

bool FAgentConfigChangeTest::RunTest(const FString& Parameters)
{
	const FFixture F;
	F.WriteConfig(TEXT("AB3D-7XQ2"));
	FAgent Agent = F.Agent();
	Agent.Boot();
	Agent.StartEnroll(0);
	Agent.FinishEnroll(Test::Reply(400, TEXT("{}")), 0);

	// The same text, saved again: only the modification time moves.
	IFileManager::Get().SetTimeStamp(*F.ConfigPath, ConfigModTime(F.ConfigPath) + FTimespan::FromMinutes(1));
	Agent.CheckConfig();
	TestTrue(TEXT("a new modification time retries"), Agent.GetState() == EAgentState::Enrolling);
	Agent.StartEnroll(0);
	Agent.FinishEnroll(Test::Reply(400, TEXT("{}")), 0);

	// A new code with the old modification time (an upload that keeps the timestamp).
	const FDateTime Kept = ConfigModTime(F.ConfigPath);
	F.WriteConfig(TEXT("QQQQ-2222"));
	IFileManager::Get().SetTimeStamp(*F.ConfigPath, Kept);
	Agent.CheckConfig();
	TestTrue(TEXT("new text retries even with the same time"), Agent.GetState() == EAgentState::Enrolling);
	Test::SameText(*this, TEXT("with the new code"), Agent.GetConfig().EnrollmentCode, TEXT("QQQQ-2222"));

	Test::WriteText(F.ConfigPath, TEXT("{ broken"));
	Agent.CheckConfig();
	TestTrue(TEXT("broken"), Agent.GetState() == EAgentState::ConfigBroken);
	F.WriteConfig(TEXT(""));
	Agent.CheckConfig();
	TestTrue(TEXT("fixed, no code: idle"), Agent.GetState() == EAgentState::Idle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAgentHaltTest, "SatisDash.Core.Agent.Halt", SatisDash::Test::Flags)

bool FAgentHaltTest::RunTest(const FString& Parameters)
{
	const FFixture F;
	F.WriteConfig(TEXT(""));
	F.WriteCredential();
	FAgent Agent = F.Agent();
	Agent.Boot();
	Agent.OnAuthRejected();
	TestTrue(TEXT("halted"), Agent.GetState() == EAgentState::Halted);
	TestTrue(TEXT("no credential to send"), Agent.GetCredential() == nullptr);
	TestFalse(TEXT("the file is deleted"), IFileManager::Get().FileExists(*F.CredentialPath));
	Agent.CheckConfig();
	TestTrue(TEXT("stays halted"), Agent.GetState() == EAgentState::Halted);
	F.WriteConfig(TEXT("AB3D-7XQ2"));
	Agent.CheckConfig();
	TestTrue(TEXT("a new code re-enrols"), Agent.GetState() == EAgentState::Enrolling);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAgentFailClosedTest, "SatisDash.Core.Agent.FailClosed", SatisDash::Test::Flags)

bool FAgentFailClosedTest::RunTest(const FString& Parameters)
{
	const FFixture F;
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(F.ConfigPath), true);
	Test::WriteText(F.ConfigPath, TEXT("{\"backendUrl\":\"http://example.com\"}"));
	F.WriteCredential();
	FAgent Agent = F.Agent();
	Agent.Boot();
	TestTrue(TEXT("a broken config with a credential: broken"), Agent.GetState() == EAgentState::ConfigBroken);
	TestTrue(TEXT("and nothing is sent: no credential handed out"), Agent.GetCredential() == nullptr);
	Test::SameText(*this, TEXT("the broken file is left as it is"), Test::ReadText(F.ConfigPath), TEXT("{\"backendUrl\":\"http://example.com\"}"));
	return true;
}
```

- [ ] **Step 2: Run them to verify they fail**

Run the C++ tests. Expected: the build fails because `SatisDash/Agent.h` doesn't exist.

- [ ] **Step 3: Write the implementation**

`Public/SatisDash/Agent.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Config.h"
#include "SatisDash/Credential.h"
#include "SatisDash/Requests.h"
#include "SatisDash/Responses.h"

namespace SatisDash
{
	enum class EAgentState : uint8 { Idle, ConfigBroken, Enrolling, CodeRejected, Running, Halted };

	/** Config, credential and enrolment (spec "Shape": Config, CredentialStore, Enroller, Halt; error table rows 1-7). */
	class SATISDASHCORE_API FAgent
	{
	public:
		FAgent(FString InConfigPath, FString InCredentialPath, TFunction<double()> InRandom01);

		/** Reads the credential and the config (writing a default config when missing) and settles the state. */
		void Boot();
		/** Each minute: re-reads the config when its modification time or its text changed. */
		void CheckConfig();

		EAgentState GetState() const { return State; }
		const FAgentConfig& GetConfig() const { return Config; }
		const FCadence& GetCadence() const { return Cadence; }
		void SetCadence(const FCadence& InCadence) { Cadence = InCadence; }
		/** Only while Running: S10's "no snapshot without a valid credential". */
		const FCredential* GetCredential() const;

		/** Enrolling, nothing in flight and the backoff passed: the enrol request to send. */
		TOptional<FRequestSpec> StartEnroll(double Now);
		void FinishEnroll(const FHttpReply& Reply, double Now);
		/** A 401 on any authenticated call while Running: deletes the credential and halts. */
		void OnAuthRejected();

	private:
		void Settle(const FConfigResult& Loaded);
		void Enter(EAgentState Next, const FString& Line, bool bWarning);
		void RememberConfigFile();

		FString ConfigPath;
		FCredentialStore Credentials;
		TFunction<double()> Random01;
		EAgentState State = EAgentState::Idle;
		bool bBooted = false;
		FAgentConfig Config;
		TOptional<FCredential> Credential;
		FCadence Cadence;
		FDateTime ConfigTime;
		FString ConfigText;
		bool bEnrollInFlight = false;
		int32 EnrollFailures = 0;
		double NextEnrollAt = 0;
	};
}
```

`Private/Agent.cpp`:

```cpp
#include "SatisDash/Agent.h"

#include "Misc/FileHelper.h"
#include "SatisDash/Log.h"
#include "SatisDash/Pusher.h"

SatisDash::FAgent::FAgent(FString InConfigPath, FString InCredentialPath, TFunction<double()> InRandom01)
	: ConfigPath(MoveTemp(InConfigPath))
	, Credentials(MoveTemp(InCredentialPath))
	, Random01(MoveTemp(InRandom01))
{
}

const SatisDash::FCredential* SatisDash::FAgent::GetCredential() const
{
	return State == EAgentState::Running && Credential.IsSet() ? &Credential.GetValue() : nullptr;
}

void SatisDash::FAgent::Boot()
{
	Credential = Credentials.Load();
	if (!Credential.IsSet() && Credentials.Exists())
	{
		UE_LOG(LogSatisDash, Warning, TEXT("The credential file can't be read; treating this server as not enrolled"));
	}
	const FConfigResult Loaded = LoadConfigFile(ConfigPath);
	RememberConfigFile();
	Settle(Loaded);
	bBooted = true;
}

void SatisDash::FAgent::CheckConfig()
{
	if (bEnrollInFlight)
	{
		return; // decide after the answer: the next check sees the change
	}
	FString Text;
	FFileHelper::LoadFileToString(Text, *ConfigPath);
	if (ConfigModTime(ConfigPath) == ConfigTime && Text.Equals(ConfigText, ESearchCase::CaseSensitive))
	{
		return;
	}
	const FConfigResult Loaded = LoadConfigFile(ConfigPath);
	RememberConfigFile();
	Settle(Loaded);
}

void SatisDash::FAgent::RememberConfigFile()
{
	ConfigTime = ConfigModTime(ConfigPath);
	ConfigText.Reset();
	FFileHelper::LoadFileToString(ConfigText, *ConfigPath);
}

void SatisDash::FAgent::Settle(const FConfigResult& Loaded)
{
	if (Loaded.Status == EConfigStatus::Broken)
	{
		Enter(EAgentState::ConfigBroken, FString::Printf(
			TEXT("SatisDashAgent.cfg can't be used (bad %s); the mod is idle until it is fixed"), *Loaded.BadField), true);
		return;
	}
	const FString PreviousCode = Config.EnrollmentCode;
	Config = Loaded.Config;
	if (Credential.IsSet())
	{
		if (!Config.EnrollmentCode.IsEmpty())
		{
			// One code never enrols twice (spec error table): the credential wins and the code is blanked.
			UE_LOG(LogSatisDash, Display, TEXT("An enrolment code is in SatisDashAgent.cfg, but this server is already enrolled; blanking it (revoke the server in the dashboard to enrol again)"));
			BlankConfigCode(ConfigPath, Config);
			Config.EnrollmentCode.Reset();
			RememberConfigFile();
		}
		Enter(EAgentState::Running, TEXT("Enrolled; reporting to the backend"), false);
		return;
	}
	if (!Config.EnrollmentCode.IsEmpty())
	{
		if (State != EAgentState::Enrolling || !Config.EnrollmentCode.Equals(PreviousCode, ESearchCase::CaseSensitive))
		{
			EnrollFailures = 0;
			NextEnrollAt = 0;
		}
		Enter(EAgentState::Enrolling, TEXT("Enrolling with the code from SatisDashAgent.cfg"), false);
		return;
	}
	Enter(EAgentState::Idle, TEXT("Not enrolled: add an enrolment code from the dashboard to SatisDashAgent.cfg"), false);
}

void SatisDash::FAgent::Enter(EAgentState Next, const FString& Line, bool bWarning)
{
	if (bBooted && State == Next)
	{
		return; // one line per state change, never one per check
	}
	State = Next;
	if (bWarning)
	{
		UE_LOG(LogSatisDash, Warning, TEXT("%s"), *Line);
	}
	else
	{
		UE_LOG(LogSatisDash, Display, TEXT("%s"), *Line);
	}
}

TOptional<SatisDash::FRequestSpec> SatisDash::FAgent::StartEnroll(double Now)
{
	if (State != EAgentState::Enrolling || bEnrollInFlight || Now < NextEnrollAt)
	{
		return {};
	}
	bEnrollInFlight = true;
	return BuildEnrollRequest(Config);
}

void SatisDash::FAgent::FinishEnroll(const FHttpReply& Reply, double Now)
{
	if (!bEnrollInFlight)
	{
		return;
	}
	bEnrollInFlight = false;
	const EReplyKind Kind = ClassifyReply(Reply.Code, false);
	if (Kind == EReplyKind::Ok)
	{
		if (const TOptional<FEnrollResponse> Enrolled = ParseEnrollResponse(Reply))
		{
			if (!Credentials.Save(Enrolled->Credential))
			{
				UE_LOG(LogSatisDash, Warning, TEXT("Enrolled, but the credential file couldn't be written; enrolment will be lost when the server restarts"));
			}
			Credential = Enrolled->Credential;
			Cadence = Enrolled->Cadence;
			BlankConfigCode(ConfigPath, Config);
			Config.EnrollmentCode.Reset();
			RememberConfigFile();
			EnrollFailures = 0;
			Enter(EAgentState::Running, TEXT("Enrolled; reporting to the backend"), false);
			return;
		}
	}
	if (Kind == EReplyKind::Fatal)
	{
		Enter(EAgentState::CodeRejected, FString::Printf(
			TEXT("The backend refused the enrolment code (%d); get a new code from the dashboard"), Reply.Code), true);
		return;
	}
	// Transient, or a 201 not in the expected form: backendClient.ts retries both.
	if (EnrollFailures == 0)
	{
		UE_LOG(LogSatisDash, Warning, TEXT("Enrolment failed (%d); retrying with backoff"), Reply.Code);
	}
	NextEnrollAt = Now + BackoffSeconds(EnrollFailures, Random01());
	++EnrollFailures;
}

void SatisDash::FAgent::OnAuthRejected()
{
	if (State != EAgentState::Running)
	{
		return;
	}
	Credentials.Delete();
	Credential.Reset();
	Enter(EAgentState::Halted, TEXT("The backend rejected the credential (401): this server was revoked in the dashboard. Stopped; add a new code to SatisDashAgent.cfg to enrol again"), true);
}
```

- [ ] **Step 4: Run check-api and the tests to verify they pass**

Run check-api. Expected: `0 missing, 0 bad rows`, after adding looked-up rows (e.g. `IFileManager::SetTimeStamp`).
Run the C++ tests with `--min 36`. Expected: `mod tests: PASS (36 passed)`.

- [ ] **Step 5: Commit**

```bash
git add mod api-sources.md
git commit -m "Core mod: the agent's state machine, enrol to halt, failing closed (Plan 2A Task 9)"
```

---

### Task 10: Logging rules (S8, S9) and the whole-suite check

**Files:**
- Create: `Source/SatisDashCore/Public/SatisDash/RateLimit.h`, `Private/RateLimit.cpp`
- Modify: `Source/SatisDashCoreTests/Private/TestUtil.h` (log capture)
- Test: `Source/SatisDashCoreTests/Private/LogTest.cpp`

**Interfaces:**
- Consumes: `FAgent` (Task 9), `FPusher` (Task 8), `LogSatisDash` (Task 1).
- Produces:
  - `class FRateLimitedLog { explicit FRateLimitedLog(double IntervalSeconds = 600.0); bool ShouldLog(const FString& Key, double Now); }`, for 2B's reader-failure lines (spec error table, "A reader fails");
  - in tests: `Test::FLogCapture`.

- [ ] **Step 1: Write the failing tests**

Add to `TestUtil.h` (include `Misc/OutputDevice.h`, `Misc/ScopeLock.h`, `SatisDash/Log.h`):

```cpp
namespace SatisDash::Test
{
	/** Collects every LogSatisDash line written while it lives. */
	class FLogCapture : public FOutputDevice
	{
	public:
		FLogCapture() { GLog->AddOutputDevice(this); }
		virtual ~FLogCapture() override { GLog->RemoveOutputDevice(this); }
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Category == LogSatisDash.GetCategoryName())
			{
				FScopeLock Lock(&Guard);
				Lines.Add(Text);
			}
		}
		TArray<FString> Take()
		{
			GLog->Flush();
			FScopeLock Lock(&Guard);
			return Lines;
		}

	private:
		FCriticalSection Guard;
		TArray<FString> Lines;
	};
}
```

`LogTest.cpp`:

```cpp
#include "SatisDash/Agent.h"
#include "SatisDash/Pusher.h"
#include "SatisDash/RateLimit.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	int32 Count(const TArray<FString>& Lines, const TCHAR* Part)
	{
		int32 N = 0;
		for (const FString& Line : Lines)
		{
			N += Line.Contains(Part) ? 1 : 0;
		}
		return N;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLogRateLimitTest, "SatisDash.Core.Log.RateLimit", SatisDash::Test::Flags)

bool FLogRateLimitTest::RunTest(const FString& Parameters)
{
	FRateLimitedLog Limit(600.0);
	TestTrue(TEXT("first"), Limit.ShouldLog(TEXT("power"), 0));
	TestFalse(TEXT("again too soon"), Limit.ShouldLog(TEXT("power"), 599));
	TestTrue(TEXT("another part"), Limit.ShouldLog(TEXT("status"), 1));
	TestTrue(TEXT("after the interval"), Limit.ShouldLog(TEXT("power"), 600));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLogOnceTest, "SatisDash.Core.Log.OncePerStreak", SatisDash::Test::Flags)

bool FLogOnceTest::RunTest(const FString& Parameters)
{
	Test::FLogCapture Capture;
	FPusher Pusher([] { return 1.0; });
	Pusher.Enqueue(TEXT("{}"));
	double Now = 0;
	for (int32 I = 0; I < 5; ++I)
	{
		Pusher.StartSend(Now);
		Pusher.FinishSend(Test::Reply(503, TEXT("{}")), Now);
		Now += 100;
	}
	Pusher.StartSend(Now);
	Pusher.FinishSend(Test::Reply(200, Test::SnapshotAnswerJson), Now);
	const TArray<FString> Lines = Capture.Take();
	TestEqual(TEXT("one line when failures start"), Count(Lines, TEXT("Pushing snapshots failed")), 1);
	TestEqual(TEXT("one line when they stop"), Count(Lines, TEXT("Pushing snapshots recovered")), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLogNoSecretsTest, "SatisDash.Core.Log.NoSecrets", SatisDash::Test::Flags)

bool FLogNoSecretsTest::RunTest(const FString& Parameters)
{
	Test::FLogCapture Capture;
	const Test::FTempDir Dir;
	const FString ConfigPath = Dir.File(TEXT("SatisDashAgent.cfg"));
	auto WriteCode = [&](const TCHAR* Code)
	{
		Test::WriteText(ConfigPath, FString::Printf(TEXT("{\"backendUrl\":\"https://example.com\",\"enrollmentCode\":\"%s\"}"), Code));
	};

	// Every logging path: enrol with a failure, a pasted code while enrolled, revoke, a rejected code, a pusher streak.
	WriteCode(TEXT("AB3D-7XQ2"));
	FAgent Agent(ConfigPath, Dir.File(TEXT("credential.json")), [] { return 1.0; });
	Agent.Boot();
	Agent.StartEnroll(0);
	Agent.FinishEnroll(Test::Reply(503, TEXT("{}")), 0);
	Agent.StartEnroll(10);
	Agent.FinishEnroll(Test::Reply(201, Test::EnrollJson()), 10);
	WriteCode(TEXT("ZZZZ-7777"));
	Agent.CheckConfig();
	Agent.OnAuthRejected();
	WriteCode(TEXT("QQQQ-2222"));
	Agent.CheckConfig();
	Agent.StartEnroll(20);
	Agent.FinishEnroll(Test::Reply(404, TEXT("{}")), 20);
	FPusher Pusher([] { return 1.0; });
	Pusher.Enqueue(TEXT("{}"));
	Pusher.StartSend(0);
	Pusher.FinishSend(Test::Reply(400, TEXT("{}")), 0);

	const TArray<FString> Lines = Capture.Take();
	TestTrue(TEXT("lines were captured"), Lines.Num() >= 5);
	for (const FString& Line : Lines)
	{
		TestFalse(TEXT("no secret"), Line.Contains(Test::FakeSecret(), ESearchCase::CaseSensitive));
		TestFalse(TEXT("no part of the secret"), Line.Contains(Test::FakeSecret().Left(8), ESearchCase::CaseSensitive));
		TestFalse(TEXT("no first code"), Line.Contains(TEXT("AB3D-7XQ2"), ESearchCase::IgnoreCase));
		TestFalse(TEXT("no pasted code"), Line.Contains(TEXT("ZZZZ-7777"), ESearchCase::IgnoreCase));
		TestFalse(TEXT("no rejected code"), Line.Contains(TEXT("QQQQ-2222"), ESearchCase::IgnoreCase));
		TestFalse(TEXT("no Bearer"), Line.Contains(TEXT("Bearer"), ESearchCase::IgnoreCase));
	}
	return true;
}
```

- [ ] **Step 2: Run them to verify they fail**

Run the C++ tests. Expected: the build fails because `SatisDash/RateLimit.h` doesn't exist.

- [ ] **Step 3: Write the implementation**

`Public/SatisDash/RateLimit.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

namespace SatisDash
{
	/** For lines that could repeat every pass, such as a reader failing (spec error table): once per key per interval. */
	class SATISDASHCORE_API FRateLimitedLog
	{
	public:
		explicit FRateLimitedLog(double InIntervalSeconds = 600.0);
		/** True the first time a key is seen, then at most once per interval. */
		bool ShouldLog(const FString& Key, double Now);

	private:
		double IntervalSeconds;
		TMap<FString, double> LastAt;
	};
}
```

`Private/RateLimit.cpp`:

```cpp
#include "SatisDash/RateLimit.h"

SatisDash::FRateLimitedLog::FRateLimitedLog(double InIntervalSeconds)
	: IntervalSeconds(InIntervalSeconds)
{
}

bool SatisDash::FRateLimitedLog::ShouldLog(const FString& Key, double Now)
{
	if (const double* Last = LastAt.Find(Key); Last != nullptr && Now - *Last < IntervalSeconds)
	{
		return false;
	}
	LastAt.Add(Key, Now);
	return true;
}
```

- [ ] **Step 4: Run the whole suite**

```bash
cd harness
npm run typecheck
npx vitest run --reporter=dot
npm run harness -- check-api
npm run harness -- verify-contract
```

Expected:
- typecheck exits 0;
- Vitest is all green (read its summary line, not the exit code of a pipe);
- `check-api: 0 missing, 0 bad rows`;
- `verify-contract: 0 problems`.

Run the C++ tests with `--min 39`. Expected: `mod tests: PASS (39 passed)`, with `failed: 0` and `notRun: 0` in `index.json`.

- [ ] **Step 5: Commit, push, and run the suite on the runner**

```bash
git add mod api-sources.md
git commit -m "Core mod: rate-limited lines and the no-secrets log check (Plan 2A Task 10)"
git push -u origin HEAD
gh workflow run mod-test.yml --ref "$(git branch --show-current)"
```

The owner approves the `ue-runner` deployment. Expected: the run's `test-mod` step prints `mod tests: PASS (39 passed)`. Then open the PR for Tasks 2–10.

---

## Self-review (done when this plan was written)

- **Spec coverage:**

  | Spec item | Where |
  | --- | --- |
  | Missing config | Tasks 2 and 9 |
  | Broken config | Tasks 2 and 9 |
  | Code rejected | Task 9 |
  | Enrol network failure | Task 9 |
  | Broken credential | Tasks 3 and 9 |
  | The crash case | Tasks 3 and 9 |
  | 401 | Tasks 8 and 9 |
  | Long outage: the queue | Task 8 |
  | Long outage: commands pause | Plan 2B |
  | Unknown command | Task 6 |
  | `set_auto_pause` failing | Task 6's `CommandFailed` and ledger; Plan 2B runs it |
  | A reader failing | Task 10's rate limit; Plan 2B's readers |
  | S1 | Task 2 |
  | S4, S5 | Task 5 |
  | S6 | Tasks 5 and 6 |
  | S7 | Task 7 |
  | S8 | Task 10 (unit); Plan 2B harness check |
  | S9 | The state lines in Tasks 8 and 9; Plan 2B harness runs |
  | S10 | Task 9 |
  | S11 | Tasks 2 and 3; Linux mode in sub-project 4 |
  | S13 | Task 4 |
  | S14 | Task 1 (the Editor test module) |
  | S2, S3, S12 | Plan 2B, with the redirect finding above |
  | The spec's unit-test list ("Testing", item 1) | Tasks 2–9 |
  | "The plan's first task proves these tests run headless" | Task 1 |
- **Placeholders:** none. Every code step has the code, and every run step has its command and expected output. `api-sources.md` rows beyond those given are looked up at execution, by the rule that forbids writing them from memory.
- **Type consistency:** these names were checked across tasks: `FHttpReply`, `EReplyKind`, `FCadence`, `FAgentCommand`, `FCommandResult`, `FRequestSpec`, `FPusher::FOutcome`, `FAgent`, and the helpers `Test::Reply`, `Test::FakeSecret` and `Test::EnrollJson`.
- **Review Focus:** each of the five has a test named in its owning task.
