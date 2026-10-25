# Native readers, Sprint S1 (power, players, resource nodes) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The mod reads who is connected (R1) and the resource nodes (R4) from the game itself, closes the proof for the rest of power (R0), and one combined 30-minute parity run plus one tick-cost run judge all three readers, with a clean-room control (contributor note, PR checkbox, provenance check in CI) in place before any of it merges.

**Architecture:**
- A per-reader source switch (`bridge` or `native`, default `bridge`) lives in the mod's config file as a hidden stopgap. The contract, the endpoint and the agent version do not move.
- R1 and R4 are pure logic in `SatisDashCore` (mapping, caps, sorting, the exact world body) plus two small game-side readers in `GameReader.cpp`. R4 enumerates the world's actors in slices of 2,000 per 0.5 s driver tick and feeds the existing world sender, which hashes, gzips and posts it exactly as it does an FRM body.
- R0 needs no reader code: `GameReader::ReadPower` already fills every contract field (verified below). R0's work is the proof and its provenance file.
- The harness gains two new core runs, `native-parity` and `tick-cost-native`, with new pass files. No existing pass condition changes.

**Tech Stack:** Unreal Engine 5.6.1-CSS, SML 3.12.0 (`harness/toolchain.json`), C++20, UE automation tests, and the TypeScript harness (Node, zod 4, Vitest 5).

**Spec:** ADR-0058 in the satisfactory-dash repo (`docs-vault/wiki/decisions/0058-native-readers-remove-frm.md`: Decisions 1-6, "Owner's answers (2026-10-25)", "Build tasks" 2 and 4 for sprint S1) and the S1 header survey (`mod-native-readers-s1-headers-2026-10-25.md`, satisfactory-dash `docs-vault/raw-sources/`; confirm the folder when committing provenance files). The owner's answers override the ADR where they differ: no similarity-check job; readers grouped into sprints with one plan, one header survey, one combined 30-minute parity run, one tick-cost run and one evidence PR each; a reader that misses its tolerance stays on the bridge while the rest switch; tolerances approved as written in Decision 3.

## Global Constraints

Everything in Plans 3A and 3B's Global Constraints still holds (pure Core, test naming `SatisDash.Core.<Area>.<Case>`, unity-build prefixes for file-local helpers, case-sensitive protocol strings, no `check()` on outside data, logs only to `LogSatisDash` at `Display` or `Warning` and rate-limited, `check-api` at `0 missing, 0 bad rows`). Added or restated for S1:

- **Clean room (CLAUDE.md, ADR-0058 Decision 6).** No session, task or reviewer opens FicsitRemoteMonitoring's source in any form, and nobody searches the web for it. Allowed: the pinned game and engine headers, SML docs, our own captures (FRM's HTTP output as a black box), our own Node adapter code. A task that cannot name its header does the lookup first (`api-sources.md` row with `path:line`) and never guesses. If anyone sees FRM source by accident: stop, tell the owner, record it in that reader's `PROVENANCE.md`; a different session rewrites the reader from headers only.
- **The header wins.** Where the S1 survey and the pinned header disagree this plan follows the header and records both (see "Lookups made for this plan").
- **Evidence is machine-written.** Nobody edits a `result.json` or a capture. A reader that misses its tolerance stays on `bridge`; nobody weakens a pass condition (`spikes/S#/pass.ts`, `runs/*/pass.ts`). New judgements are new files.
- **Loopback and the test server only.** The runs talk to `127.0.0.1` and the test server (`docs/runner.md`). Never the owner's real server.
- **Player names are personal data about third parties (ADR-0029).** The native player reader reads a name and online/offline, nothing else (no id, location, inventory). Names never reach a log line, a test name, a capture or a commit: evidence carries `sessionHash(name)` only (the recorder already does this for snapshots).
- **Contract and goldens do not move.** `players` and `resourceNodes` keep the shapes in `harness/contract/src/players.ts` and `map.ts` (the vendored pin). The `resourceNodes` item has exactly `type, purity, nodeType, x, y, exploited`; the optional `className` is **not** emitted, because the bridge's `frm-getResourceNode.expected.json` golden does not carry it (the survey's `className` row is therefore out of S1).
- **Memory and cost (owner: the mod stays light).** The native readers add: players, one pass over `PlayerArray` and one copy of the offline-player list every 5 s (a few dozen entries); resource nodes, one sliced pass every 30 minutes (2,000 actors per 0.5 s tick, a class check each) that keeps only the nodes it finds (at most 10,000 readings, about 1 MB worst case, about 100 KB in the captured world), then a body of at most 2,000,000 bytes on the worker as today. No per-tick allocation otherwise. The actual cost is measured, not asserted: the scan logs its actor count, node count, steps, longest step and total time once per scan (counts only), and `tick-cost-native` judges tick rate and memory with the existing limits.
- **Test counts.** The mod-test judge's last count on main is **142** (counted in the 31 `Private/*Test.cpp` files: 142 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` macros). New UE tests: Task 2 adds 3 (145), Task 3 adds 4 (149); Tasks 1, 4 and 5 add none. The `--min` floor in `mod-test.yml` stays 142 until the owner raises it (Task 3 proposes 149; a higher floor cannot make a failing run pass).
- **Runs.** Every run is dispatched by the owner (`mod-test`, `native-parity`, `tick-cost-native`); executors never start them. The test server only, never the owner's real server.
- Commits end with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. No personal data, no local paths in anything committed.

## Lookups made for this plan (header wins)

Looked up in the pinned SML checkout (`Source/FactoryGame/Public`) and the pinned engine on 2026-10-25. Where the S1 survey differs, the header is followed.

| Need | Header, `path:line` | Notes |
| --- | --- | --- |
| Offline (known, not connected) players: the survey's `[NEEDS VERIFICATION]` | `FGGameState.h:519` `TArray<FCachedPlayerInfo> GetOfflineCachedPlayerInfosInSession() const;` ("all players that are not currently online in the session"); `FGGameState.h:515` `GetAllPlayerInfoData()` ("all players that have ever partaken in the current session") | Both public (public block `FGGameState.h:119-531`). The survey found no player list in `AFGGameState`; this is it. Whether it equals what the dashboard saw through FRM is the parity run's question (R1), not an assumption. |
| Name of a cached player | `Online/PlayerInfoCache.h:49` `FCachedPlayerInfo`; `:54` `PerPlatformInfo`; `:62` `GetLastSeenServicesType()`; `:73` `GetForPlatform(...)` (returns `const FCachedPlayerPlatformInfo*`); `:10` `FCachedPlayerPlatformInfo`; `:20` `PlayerName` | Name = the last-seen platform's entry, else the first entry. |
| Connected players | `GameFramework/GameStateBase.h:55` `PlayerArray` (public, `:36`); `GameFramework/PlayerState.h:232` `GetPlayerName()`; `Engine/World.h:3727` `UWorld::GetGameState<T>()` (casts `GameState`) | `AFGGameState` is `FGGameState.h:116`. |
| Enumerate resource nodes | `Engine/Level.h:432` `ULevel::Actors` (public, block from `:426`); `Engine/World.h:3443` `UWorld::GetLevels()`; `FGResourceNodeManager.h` lists are `private:` (`Resources/FGResourceNodeManager.h:116-138`), so no subsystem list is usable | Sliced scan over `Level->Actors`, not `TActorIterator` (an iterator cannot be paused across ticks safely). |
| Node classes | `Resources/FGResourceNode.h:66` `AFGResourceNode : AFGResourceNodeBase`; `Resources/FGResourceDeposit.h:15` `AFGResourceDeposit : AFGResourceNode`; `FGResourceNodeGeyser.h:15` `AFGResourceNodeGeyser : AFGResourceNode` (directly under `Public`, not `Resources/`); fracking satellite `Resources/FGResourceNodeFrackingSatellite.h:23`; fracking core `Resources/FGResourceNodeFrackingCore.h:14` is **not** an `AFGResourceNode` | Deposits derive from `AFGResourceNode`, so the class check alone would include them: the reader filters on `GetResourceNodeType()`. FRM's capture holds Node, Fracking Satellite and Geyser only. |
| Node fields | `Resources/FGResourceNodeBase.h:24` `enum class EResourceNodeType : uint8 { Node (:26), FrackingSatellite (:27), FrackingCore (:28), Geyser (:29), Deposit (:30), Invalid }`; `:176` `GetResourceNodeType()`; `:180` `GetResourceName()` (FText, "Localized name"); `:140` `IsOccupied()`; `Resources/FGResourceNode.h:26` `enum EResourcePurity` (plain enum: `RP_Inpure :28, RP_Normal :29, RP_Pure :30, RP_MAX :31`); `:113` `GetResourcePurity()` | `GetResourceName()` is localised; the server's culture and FRM's `Name` spelling are checked by the parity run. |
| Position | `Engine/Classes/GameFramework/Actor.h:2505` `AActor::GetActorLocation()` (centimetres) | The survey's `:1580` is `K2_GetActorLocation`, the Blueprint wrapper; the C++ accessor is `:2505`. |
| R0 fields | `FGPowerCircuit.h:57` `FPowerCircuitStats` (`:101` capacity, `:104` produced, `:107` consumed, `:110` max consumption); `:163` `IsFuseTriggered`; `:211` `HasBatteries`; `:215` `GetBatterySumPowerStore`; `:219` `GetBatterySumPowerStoreCapacity`; `:227` `GetBatterySumPowerInput`; `:231` `GetBatterySumPowerOutput`; `FGCircuit.h:71` `GetCircuitGroupID` | Already used by `GameReader.cpp:83-116`. No new symbol. |

**R0 is already complete in code** (checked against `power.ts`, `Snapshot.cpp:50-93` and `GameReader.cpp:83-116`): production, consumption, capacity, max consumption, fuse, battery capacity, battery percent (0-100, zero without capacity), battery differential (signed) and the circuit-group merge (lowest circuit id speaks for a group; groups ordered by id; capped at 1000) are all read natively, and `SatisDash.Core.GameValues.Groups` and `.Battery` pin the mapping. What is **not** proven is how the values behave in the test world: the existing `parity` run (2026-10-21) already compares every power field against the Node agent, but its evidence does not say whether the world had batteries, a merged group or a tripped fuse. S1 therefore adds no R0 reader code. R0 gets (a) its provenance file (Task 1), (b) a power judgement against FRM's `getPower` in the combined run with the exercised-field counts recorded (Task 4), and (c) the same 3% bar as before. See the open question about fields the world never exercises.

## Review Focus

- **Nobody is connected, but players are known:** a server with no one online still has remembered players. Expected: native mode posts `available: true` with those players `online: false` (never `available: false`, which tells the dashboard "FRM is absent"). Pinned in `SatisDash.Core.NativePlayers.Map`, `SatisDash.Core.Driver.NativePlayers`. Whether the game's remembered list equals what FRM returned is judged by the parity run (R1), which fails closed.
- **More players than the contract holds, or a long name:** 300 known players, a 250-character name. Expected: at most 256 kept, connected players first, names cut to 200. Pinned in `SatisDash.Core.NativePlayers.Map`.
- **A native reader that cannot read leaves its part out; it does not fall back to FRM, and it does not look absent:** no game state, or no world. Expected: players part left out of the snapshot with one rate-limited warning; the resource-node scan retries 30 s later, never reads FRM. Pinned in `SatisDash.Core.Driver.NativePlayers`, `SatisDash.Core.Driver.NativeNodes`.
- **Bad node data:** a node at `1e12` cm, 5,001 nodes, the same nodes found in a different order, a 250-character resource name, an `.5` metre half. Expected: out-of-bounds nodes dropped and counted, the first 5,000 (in sorted order) kept, byte-identical bodies whatever the scan order (so an unchanged world never reposts), halves toward +infinity as `Math.round`. Pinned in `SatisDash.Core.NativeNodes.Rules`, `SatisDash.Core.NativeNodes.Golden`.
- **A wrong source value in the config:** `"Native"`, `"native "`, a number. Expected: the config is Broken with the key named, as for `frmPort`; an unset key means the default. Pinned in `SatisDash.Core.Config.Sources`.
- **The native source must not touch FRM for that reader, and must not stop FRM for the others:** with both sources native, no `getPlayer` or `getResourceNode` request is ever sent while `getFactory` still is. Pinned in the two driver tests.
- **A reader the world never exercises:** a fuse that never trips, no batteries, no merged group, nobody online. Expected: the judge still compares what exists, and its reasons say which fields were never exercised, so the owner can rule (open question 1). Pinned in `runs/native-parity` tests (`reports fields the world never exercised`).

## Not in S1

The switch PRs (flip `DefaultPlayersSource` / `DefaultResourceNodesSource`, one small PR each, after their reader passes), the dash evidence PR (`raw-sources/mod-native-readers/`, `mod-claims.md` rows), the `className` field, R2-R6, and the removal release. They are listed again at the end as follow-ups. There is no `powerSource` key: power has no bridge path to fall back to.

## How the tests run

- Mod: each task's UE test step runs locally:
  ```powershell
  pwsh scripts/test-mod.ps1 -Project <sml-project> -Engine <engine-root> -Out <tmp>\mod-test
  cd harness; npm run harness -- judge-mod-test <tmp>\mod-test --min <N>
  ```
  `<sml-project>` holds `FactoryGame.uproject` and `<engine-root>` holds `Engine\`; both are the executor's own and never committed. `<N>` is the task's expected count: **142 on main today**, 145 after Task 2, 149 after Task 3 (unchanged after that). One build per task at most.
- `check-api`: `cd harness; npm run harness -- check-api` ends every task that touches `mod/` at `0 missing, 0 bad rows`. The rows each task lists were looked up for this plan; anything else `check-api` reports is looked up in the pinned headers and added in the same task, never written from memory.
- Harness: `cd harness; npx vitest run <file> --reporter=dot` while iterating; once at the end of Task 1, 4 and 5: `npx vitest run --reporter=dot` and `npm run typecheck`.
- Before the mod PR merges, the owner dispatches `mod-test` on the branch (`gh workflow run mod-test.yml --ref <branch>`, approves the `ue-runner` deployment), then `native-parity` and `tick-cost-native` through the backend host (`docs/runner.md`, "The backend host"). `native-parity` needs `frmBaseUrl` in the runner config and takes about 45 minutes plus the build.

**PRs:** Task 1 is **PR S1-A** (the control; merge it first). Tasks 2-5 are **PR S1-B**. Owner may merge them as one.

## File structure

`mod/...` is `mod/SatisDashAgent/Source`.

| File | New/Modified | Responsibility |
| --- | --- | --- |
| `docs/clean-room.md` | New (T1) | The contributor note: rules 1-5 verbatim, layout, provenance format |
| `.github/pull_request_template.md` | New (T1) | The "FRM source was not opened" checkbox |
| `harness/src/check-provenance.ts`, `harness/test/check-provenance.test.ts` | New (T1) | The CI check: a `Native/<Reader>/` directory needs a valid `PROVENANCE.md` |
| `harness/src/cli.ts`, `.github/workflows/ci.yml`, `README.md` | Modified (T1) | `check-provenance` command and CI step; README pointer |
| `mod/SatisDashGame/Private/Native/Power/PROVENANCE.md` | New (T1) | R0's provenance (the code stays in `GameReader.cpp`, `GameValues.cpp`) |
| `mod/SatisDashCore/Public/SatisDash/Config.h`, `Private/Config.cpp` | Modified (T2) | `ESource`, `playersSource`, `resourceNodesSource` |
| `mod/SatisDashCore/Public/SatisDash/Ports.h` | Modified (T2, T3) | `IGameReader::ReadPlayers`, the node scan |
| `mod/SatisDashCore/Public/SatisDash/Native/Players/NativePlayers.h`, `Private/Native/Players/NativePlayers.cpp`, `.../PROVENANCE.md` | New (T2) | `NativePlayersPart` |
| `mod/SatisDashCore/Public/SatisDash/FrmBridge.h`, `Private/FrmBridge.cpp` | Modified (T2, T3) | `RequestPass(…, bPlayers)`; `SubmitNativeWorld` |
| `mod/SatisDashCore/Private/Driver.cpp` | Modified (T2, T3) | Native players in the snapshot; the game reader passed to the world sender |
| `mod/SatisDashCore/Public/SatisDash/Native/ResourceNodes/NativeResourceNodes.h`, `Private/Native/ResourceNodes/NativeResourceNodes.cpp`, `.../PROVENANCE.md` | New (T3) | Node readings, `BuildNativeResourceNodes` |
| `mod/SatisDashCore/Public/SatisDash/FrmResourceNodes.h`, `Private/FrmResourceNodes.cpp` | Modified (T3) | `AppendResourceNodeRecord` shared by the FRM parse and the native builder |
| `mod/SatisDashCore/Public/SatisDash/FrmWorld.h`, `Private/FrmWorld.cpp` | Modified (T3) | `FWorldRead` for a prebuilt native body |
| `mod/SatisDashCore/Public/SatisDash/WorldSender.h`, `Private/WorldSender.cpp` | Modified (T3) | The `Scanning` phase |
| `mod/SatisDashGame/Private/GameReader.h`, `GameReader.cpp` | Modified (T2, T3) | `ReadPlayers`, the sliced node scan |
| `mod/SatisDashCoreTests/Private/ConfigTest.cpp`, `DriverFakes.h`, `NativePlayersTest.cpp`, `DriverNativeTest.cpp`, `NativeNodesTest.cpp`, `FrmWorldTest.cpp` | New/Modified | Tests |
| `api-sources.md` | Modified (T2, T3) | Rows for every new symbol |
| `docs/native-readers.md` | New (T2), Modified (T3) | Reader status and the hidden source override, documented as a stopgap |
| `harness/src/frm-native.ts`, `harness/test/frm-native.test.ts` | New (T4) | FRM `getPlayer` / `getResourceNode` samplers and the canonical forms |
| `runs/native-parity/pass.ts`, `harness/test/native-parity-pass.test.ts` | New (T4) | The per-reader judgement |
| `harness/src/core-runs/native-parity.ts` | New (T4) | The drive |
| `harness/src/recorder.ts`, `harness/test/recorder.test.ts` | Modified (T4) | `keepWorldData` option |
| `harness/src/core-run.ts`, `harness/src/core-runs/index.ts`, `.github/workflows/core-run.yml`, `docs/runner.md` | Modified (T4, T5) | New run ids, `modConfig`, workflow options, docs |
| `runs/tick-cost-native/pass.ts`, `harness/src/core-runs/tick-cost-native.ts`, `harness/src/core-runs/tick-cost.ts`, `harness/test/tick-cost-native-pass.test.ts` | New/Modified (T5) | The tick-cost run with the native readers on |

---

### Task 1: The clean-room control (ADR build task 2) and R0's provenance

**Files:**
- Create: `docs/clean-room.md`, `.github/pull_request_template.md`
- Create: `harness/src/check-provenance.ts`, `harness/test/check-provenance.test.ts`
- Create: `mod/SatisDashAgent/Source/SatisDashGame/Private/Native/Power/PROVENANCE.md`
- Modify: `harness/src/cli.ts`, `.github/workflows/ci.yml`, `README.md`

**Interfaces:**
- Produces: `checkProvenance(repoRoot: string, modRoot?: string): { readers: string[]; problems: string[] }`; the CLI command `check-provenance`; the layout rule every later task follows:
  - a native reader is a directory `Native/<Reader>/` anywhere under `mod/`;
  - exactly one `PROVENANCE.md` per reader name across all its directories (a reader may have a Public, a Private and a Game directory; only one holds the file);
  - nothing but reader directories directly inside a `Native/` directory.

**Layout decision (for the owner to see):** the provenance file is `PROVENANCE.md`; the directory is `Native/<Reader>/`. Pure code of a reader lives in `SatisDashCore/Public/SatisDash/Native/<Reader>/` and `Private/Native/<Reader>/` (so it stays testable by `SatisDashCoreTests`); game-thread code stays in `GameReader.cpp` and is named in the provenance file's `Code:` lines (the check verifies each path exists). The existing power reader is not moved: its provenance file sits in a `Native/Power/` directory that holds nothing else.

- [ ] **Step 1: Write the failing test**

Create `harness/test/check-provenance.test.ts`:

```ts
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { main } from "../src/cli";
import { checkProvenance } from "../src/check-provenance";

const dirs: string[] = [];
const repo = () => {
  const d = mkdtempSync(join(tmpdir(), "prov-"));
  dirs.push(d);
  return d;
};
afterEach(() => dirs.splice(0).forEach((d) => rmSync(d, { recursive: true, force: true })));

const put = (root: string, path: string, text = "x") => {
  mkdirSync(dirname(join(root, path)), { recursive: true });
  writeFileSync(join(root, path), text);
};

const good = (reader: string, code = "mod/Core/Private/Native/Players/Players.cpp") =>
  [
    `Reader: ${reader}`,
    `Code: ${code}`,
    "Headers: Source/FactoryGame/Public/FGGameState.h:519",
    "Survey: mod-native-readers-s1-headers-2026-10-25.md",
    "Captures: frm-getPlayer-2026-09-27-full.json (HTTP output only)",
    "Author: a subagent",
    "Date: 2026-10-26",
    "",
    "FRM source was not opened",
    "",
  ].join("\n");

describe("checkProvenance", () => {
  it("passes a reader whose directory holds a complete PROVENANCE.md, and counts it", () => {
    const r = repo();
    put(r, "mod/Core/Private/Native/Players/Players.cpp");
    put(r, "mod/Core/Private/Native/Players/PROVENANCE.md", good("Players"));
    expect(checkProvenance(r)).toEqual({ readers: ["Players"], problems: [] });
  });

  it("fails a reader directory without the file", () => {
    const r = repo();
    put(r, "mod/Core/Private/Native/Nodes/Nodes.cpp");
    const out = checkProvenance(r);
    expect(out.readers).toEqual(["Nodes"]);
    expect(out.problems).toEqual(["native reader Nodes (mod/Core/Private/Native/Nodes): no PROVENANCE.md"]);
  });

  it("accepts one file for a reader that has a Public and a Private directory", () => {
    const r = repo();
    put(r, "mod/Core/Public/SatisDash/Native/Players/Players.h");
    put(r, "mod/Core/Private/Native/Players/Players.cpp");
    put(r, "mod/Core/Private/Native/Players/PROVENANCE.md", good("Players"));
    expect(checkProvenance(r).problems).toEqual([]);
  });

  it("fails two files for one reader", () => {
    const r = repo();
    put(r, "mod/A/Native/Players/PROVENANCE.md", good("Players", "mod/A/Native/Players/PROVENANCE.md"));
    put(r, "mod/B/Native/Players/PROVENANCE.md", good("Players", "mod/A/Native/Players/PROVENANCE.md"));
    expect(checkProvenance(r).problems[0]).toMatch(/more than one PROVENANCE\.md/);
  });

  it("fails a loose file directly in a Native directory", () => {
    const r = repo();
    put(r, "mod/Core/Private/Native/Loose.cpp");
    expect(checkProvenance(r).problems).toEqual(["mod/Core/Private/Native/Loose.cpp: a file directly in a Native directory; put it in Native/<Reader>/"]);
  });

  it("names every defect in the file", () => {
    const r = repo();
    put(r, "mod/Core/Native/Players/PROVENANCE.md", "Reader: Other\nCode: mod/missing.cpp\nDate: yesterday\n");
    const problems = checkProvenance(r).problems.join("\n");
    expect(problems).toContain('Reader is "Other", the directory is "Players"');
    expect(problems).toContain("Code path mod/missing.cpp does not exist");
    expect(problems).toContain('Date "yesterday" is not YYYY-MM-DD');
    expect(problems).toContain('missing "Headers:" line');
    expect(problems).toContain('missing the line "FRM source was not opened"');
  });

  it("ignores Native directories outside mod/ and a repository with no mod/ at all", () => {
    const r = repo();
    put(r, "harness/Native/Thing/x.ts");
    expect(checkProvenance(r)).toEqual({ readers: [], problems: [] });
  });
});

describe("check-provenance command", () => {
  it("reports the counts and exits 0 on the real repository", async () => {
    const lines: string[] = [];
    const code = await main(["check-provenance"], { log: (s) => lines.push(s), error: (s) => lines.push(s) });
    expect(lines.at(-1)).toMatch(/^check-provenance: \d+ native readers, 0 problems$/);
    expect(code).toBe(0);
  });
});
```

- [ ] **Step 2: Run it to see it fail**

Run: `cd harness; npx vitest run test/check-provenance.test.ts --reporter=dot`
Expected: FAIL (`../src/check-provenance` does not exist).

- [ ] **Step 3: Write the check**

Create `harness/src/check-provenance.ts`:

```ts
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
```

- [ ] **Step 4: Add the command**

In `harness/src/cli.ts`: add `import { checkProvenance } from "./check-provenance";` beside the `check-api` import; add after `runCheckApi`:

```ts
export function runCheckProvenance(io: Io): number {
  const { readers, problems } = checkProvenance(REPO_ROOT);
  for (const p of problems) io.error(p);
  io.log(`check-provenance: ${readers.length} native readers, ${problems.length} problems`);
  return problems.length ? 1 : 0;
}
```

add `case "check-provenance": return runCheckProvenance(io);` after the `check-api` case, and add `check-provenance` to the usage string (`<check-api|check-provenance|verify-contract|...>`).

- [ ] **Step 5: R0's provenance file**

Create `mod/SatisDashAgent/Source/SatisDashGame/Private/Native/Power/PROVENANCE.md` (use the day you run this task for `Date:`, and your own session or subagent for `Author:`):

```
Reader: Power
Code: mod/SatisDashAgent/Source/SatisDashGame/Private/GameReader.cpp
Code: mod/SatisDashAgent/Source/SatisDashCore/Private/GameValues.cpp
Code: mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/GameValues.h
Headers: Source/FactoryGame/Public/FGCircuitSubsystem.h:32, :49, :88
Headers: Source/FactoryGame/Public/FGCircuit.h:57, :71
Headers: Source/FactoryGame/Public/FGPowerCircuit.h:57-110 (FPowerCircuitStats), :148, :163, :171, :211, :215, :219, :227, :231
Survey: mod-native-readers-s1-headers-2026-10-25.md (satisfactory-dash docs-vault/raw-sources); earlier: sml-power-network-2026-10-19.md
Captures: frm-getPower-2026-09-22-*.json (FRM HTTP output only, as a black box, for field meanings)
Author: <your session or subagent name>
Date: <YYYY-MM-DD>

The power reader predates this file (spike S3, core mod v1). It was written from the game headers above; its code stays in
GameReader.cpp and GameValues.cpp, which is why this directory holds only this file.

FRM source was not opened
```

- [ ] **Step 6: The note, the PR template, CI and README**

Create `docs/clean-room.md`:

```markdown
# Clean room: native readers

The mod reads the game itself instead of calling FicsitRemoteMonitoring (FRM). FRM states no licence, so nobody who
writes a native reader reads FRM's code (ADR-0058 in the satisfactory-dash repository, Decision 6, with the owner's
answer 4 of 2026-10-25: there is no similarity-check job; these rules and the provenance file are the control).

## The rules (verbatim from ADR-0058, Decision 6)

1. **Never open FRM's repository files.** Not on GitHub, not in a clone, not through code search, not in a cache, not
   in a release archive, and not FRM's installed plugin files or packaged assets on any machine. This includes its
   `Source`, `Content`, `JSON`, `Patches`, `www` and `docs` folders and its issues' code snippets.
2. **Allowed sources only:** the game's own headers shipped with the SML starter project; SML's documentation; exports of
   the *game's* assets made with FModel or similar (never FRM's); our own captures, including FRM's HTTP *output* from a
   running server, used as a black box; FRM's published API documentation pages at `docs.ficsit.app`, for endpoint and
   field names only (as ADR-0055 already does); the Unreal Engine reference.
3. **Our names come from the game or from our contract,** never from FRM's response field names. Internal variables,
   types and file names follow the game's property names and the `/agent/v1` schemas. Where FRM's output is the only
   spelling of a value (an enum string passed through, such as a train `Status`), the mapping table is written from
   observed output, not from FRM's code.
4. **If someone sees FRM source by accident** (a search hit, a pasted snippet): stop, tell the owner in the mod session,
   record it in the provenance file, and the reader it touched is rewritten by a different session working from headers
   only. The exposed session does not touch that reader again.
5. **Provenance record per reader,** a file in the mod repo beside the reader: the header files and lines used (the
   survey note's name), the SML pages, the captures used as an oracle, the author session or subagent, the date, and the
   line "FRM source was not opened". The reader's PR template carries the same checkbox, and CI fails a reader directory
   without its provenance file.

## Layout and the provenance file

- A native reader is a directory `Native/<Reader>/` under `mod/`. Pure code (testable by `SatisDashCoreTests`) lives in
  `SatisDashCore/Public/SatisDash/Native/<Reader>/` and `SatisDashCore/Private/Native/<Reader>/`; game-thread code may
  stay in `GameReader.cpp`. Nothing but reader directories sits directly inside a `Native/` directory.
- Each reader has exactly one `PROVENANCE.md`, in any one of its directories, with these lines (one value per line; `Code:`,
  `Headers:` and `Captures:` may repeat):

  ```
  Reader: <the directory name>
  Code: <repo-relative path of a file that implements the reader; it must exist>
  Headers: <header path:line, from the pinned SML or engine checkout>
  Survey: <the header-survey note's name>
  Captures: <FRM HTTP output files used as an oracle, or "none">
  Author: <session or subagent>
  Date: <YYYY-MM-DD>

  FRM source was not opened
  ```

- `cd harness; npm run harness -- check-provenance` runs the check; CI runs it on every push and pull request.
- The same symbols still need their `api-sources.md` rows (`check-api`).
```

Create `.github/pull_request_template.md`:

```markdown
## What changed

## Checks

- [ ] FRM source was not opened. I did not read, search, clone or run FicsitRemoteMonitoring's source, plugin files or packaged assets while writing this change (`docs/clean-room.md`). Tick this on every PR. A change under a `Native/` directory also needs its `PROVENANCE.md`.
- [ ] Every SML, FactoryGame or Unreal Engine symbol used has a row in `api-sources.md` (`npm run harness -- check-api`).
- [ ] No personal data and no local paths are committed.
```

In `.github/workflows/ci.yml`, in the `harness` job after `- run: npm run harness -- check-api`, add `      - run: npm run harness -- check-provenance`.

In `README.md`, under "What's here", add a bullet: ``- `docs/clean-room.md` — the rules for native readers (never open FicsitRemoteMonitoring's source), the `Native/<Reader>/` layout and the `PROVENANCE.md` that CI checks.``

- [ ] **Step 7: Run the tests, the check and the typecheck**

Run: `cd harness; npx vitest run test/check-provenance.test.ts --reporter=dot` then `npm run harness -- check-provenance` then `npx vitest run --reporter=dot` and `npm run typecheck`.
Expected: all PASS; the command prints `check-provenance: 1 native readers, 0 problems`. (`test/workflows.test.ts` must still pass: `ci.yml` only gained a step.)

- [ ] **Step 8: Commit**

```bash
git add docs/clean-room.md .github/pull_request_template.md .github/workflows/ci.yml README.md harness/src/check-provenance.ts harness/src/cli.ts harness/test/check-provenance.test.ts mod/SatisDashAgent/Source/SatisDashGame/Private/Native/Power/PROVENANCE.md
git commit -m "Clean-room control: contributor note, PR checkbox, provenance check in CI (ADR-0058 task 2)"
```

---

### Task 2: The source switch and R1, players

**Files:**
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Config.h`, `Private/Config.cpp`
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Ports.h`
- Create: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Native/Players/NativePlayers.h`
- Create: `mod/SatisDashAgent/Source/SatisDashCore/Private/Native/Players/NativePlayers.cpp`, `.../PROVENANCE.md`
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/FrmBridge.h`, `Private/FrmBridge.cpp`, `Private/Driver.cpp`
- Modify: `mod/SatisDashAgent/Source/SatisDashGame/Private/GameReader.h`, `GameReader.cpp`
- Test: `mod/SatisDashAgent/Source/SatisDashCoreTests/Private/ConfigTest.cpp`, `DriverFakes.h`, `NativePlayersTest.cpp` (new), `DriverNativeTest.cpp` (new)
- Modify: `api-sources.md`; Create: `docs/native-readers.md`

**Interfaces:**
- Consumes: Task 1's layout and check.
- Produces:
  - `enum class ESource : uint8 { Bridge, Native }`; `FAgentConfig::PlayersSource`, `ResourceNodesSource` (`TOptional<ESource>`); `GetPlayersSource()`, `GetResourceNodesSource()`; `DefaultPlayersSource`, `DefaultResourceNodesSource` (the switch PRs flip these two constants and nothing else).
  - `IGameReader::ReadPlayers()` → `TOptional<TArray<FPlayerReading>>` (default `{}`).
  - `FPlayersPart NativePlayersPart(const TArray<FPlayerReading>&)`.
  - `FFrmBridge::RequestPass(bool bFactoryDue, bool bPlayers = true)`.

- [ ] **Step 1: Write the failing tests**

Add to the end of `ConfigTest.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConfigSourcesTest, "SatisDash.Core.Config.Sources", SatisDash::Test::Flags)

bool FConfigSourcesTest::RunTest(const FString& Parameters)
{
	const FString Base = TEXT("{\"backendUrl\":\"https://api.satis-manager.com\"");
	const FConfigResult None = ParseConfig(Base + TEXT("}"));
	TestTrue(TEXT("no key: valid"), None.Status == EConfigStatus::Valid);
	TestTrue(TEXT("no key: players default"), None.Config.GetPlayersSource() == DefaultPlayersSource);
	TestTrue(TEXT("no key: resource nodes default"), None.Config.GetResourceNodesSource() == DefaultResourceNodesSource);
	TestFalse(TEXT("no key: nothing set"), None.Config.PlayersSource.IsSet() || None.Config.ResourceNodesSource.IsSet());

	const FConfigResult Native = ParseConfig(Base + TEXT(",\"playersSource\":\"native\",\"resourceNodesSource\":\"bridge\"}"));
	TestTrue(TEXT("both keys: valid"), Native.Status == EConfigStatus::Valid);
	TestTrue(TEXT("players native"), Native.Config.GetPlayersSource() == ESource::Native);
	TestTrue(TEXT("resource nodes bridge"), Native.Config.GetResourceNodesSource() == ESource::Bridge);

	for (const TCHAR* Bad : { TEXT("\"Native\""), TEXT("\"native \""), TEXT("\"\""), TEXT("1"), TEXT("true"), TEXT("null") })
	{
		const FConfigResult R = ParseConfig(Base + TEXT(",\"playersSource\":") + Bad + TEXT("}"));
		TestTrue(*FString::Printf(TEXT("%s is refused"), Bad), R.Status == EConfigStatus::Broken);
		TestEqual(*FString::Printf(TEXT("%s names the key"), Bad), R.BadField, FString(TEXT("playersSource")));
	}
	TestEqual(TEXT("the other key is named too"), ParseConfig(Base + TEXT(",\"resourceNodesSource\":\"x\"}")).BadField, FString(TEXT("resourceNodesSource")));

	// Written back only when set, and kept when the code is blanked after enrolment.
	const FString Json = ConfigToJson(Native.Config);
	TestTrue(TEXT("round trip keeps the keys"), Json.Contains(TEXT("\"playersSource\": \"native\"")) && Json.Contains(TEXT("\"resourceNodesSource\": \"bridge\"")));
	TestFalse(TEXT("unset keys are not written"), ConfigToJson(None.Config).Contains(TEXT("Source")));
	return true;
}
```

(`WritePretty` prints `"key": "value"` with one space after the colon, as the existing `frmPort` round-trip assertions in this file show; if the existing file uses another spacing, match it.)

Create `NativePlayersTest.cpp`:

```cpp
#include "SatisDash/Native/Players/NativePlayers.h"
#include "TestUtil.h"

using namespace SatisDash;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativePlayersMapTest, "SatisDash.Core.NativePlayers.Map", SatisDash::Test::Flags)

bool FNativePlayersMapTest::RunTest(const FString& Parameters)
{
	TArray<FPlayerReading> In;
	In.Add({ TEXT("Away"), false });
	In.Add({ TEXT("Here"), true });
	const FPlayersPart Part = NativePlayersPart(In);
	TestTrue(TEXT("a read list is available, whoever is in it"), Part.bAvailable);
	TestEqual(TEXT("both kept"), Part.Players.Num(), 2);
	TestEqual(TEXT("connected players first"), Part.Players[0].Name, FString(TEXT("Here")));
	TestTrue(TEXT("and online"), Part.Players[0].bOnline);
	TestFalse(TEXT("the remembered one is offline"), Part.Players[1].bOnline);
	TestEqual(TEXT("the JSON is the part the bridge sends"), PlayersToJson(Part),
		FString(TEXT("{\"available\":true,\"players\":[{\"name\":\"Here\",\"online\":true},{\"name\":\"Away\",\"online\":false}]}")));

	const FPlayersPart Empty = NativePlayersPart({});
	TestTrue(TEXT("nobody known: still available"), Empty.bAvailable);
	TestEqual(TEXT("nobody known: no players"), PlayersToJson(Empty), FString(TEXT("{\"available\":true,\"players\":[]}")));

	// 300 remembered players and one connected: the cap keeps the connected one, as MaxPlayers (256) says.
	TArray<FPlayerReading> Many;
	for (int32 I = 0; I < 300; ++I)
	{
		Many.Add({ FString::Printf(TEXT("Old%d"), I), false });
	}
	Many.Add({ TEXT("Now"), true });
	const FPlayersPart Capped = NativePlayersPart(Many);
	TestEqual(TEXT("at most 256"), Capped.Players.Num(), MaxPlayers);
	TestEqual(TEXT("the connected player survives the cap"), Capped.Players[0].Name, FString(TEXT("Now")));
	TestEqual(TEXT("the rest keep the game's order"), Capped.Players[1].Name, FString(TEXT("Old0")));

	// Names are cut to 200 characters, as ParsePlayers does.
	TArray<FPlayerReading> Long;
	Long.Add({ FString::ChrN(250, TEXT('n')), true });
	TestEqual(TEXT("a long name is cut to 200"), NativePlayersPart(Long).Players[0].Name.Len(), 200);
	return true;
}
```

In `DriverFakes.h` extend `FFakeGame` (add members next to `Power`), and `FDriverFixture::writeConfig`/`enrol` (an `Extra` string of extra JSON fields):

```cpp
		/** R1: unset = the game state can't be read. */
		TOptional<TArray<SatisDash::FPlayerReading>> Players;
		int32 PlayerReads = 0;
		virtual TOptional<TArray<SatisDash::FPlayerReading>> ReadPlayers() override { ++PlayerReads; return Players; }
```

```cpp
		void writeConfig(const TCHAR* Url, const TCHAR* Code, const TCHAR* Extra = TEXT("")) const
		{
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(ConfigPath), true);
			WriteText(ConfigPath, FString::Printf(TEXT("{\"backendUrl\":\"%s\",\"enrollmentCode\":\"%s\"%s}"), Url, Code, Extra));
		}
```

```cpp
		void enrol(SatisDash::FDriver& Driver, const FString& Secret, const TCHAR* Extra = TEXT(""))
		{
			writeConfig(TEXT("http://127.0.0.1:1"), TEXT("AB3D-7XQ2-K5MN"), Extra);
```

(the rest of `enrol` is unchanged).

Create `DriverNativeTest.cpp` with the players test (Task 3 appends the nodes test to this file):

```cpp
#include "DriverFakes.h"
#include "SatisDash/Driver.h"

using namespace SatisDash;
using SatisDash::Test::FDriverFixture;

namespace
{
	const TCHAR* DriverNativeSnapshots = TEXT("/agent/v1/snapshots");

	/** Runs the driver for Seconds, answering every open snapshot post with 200; returns the bodies it answered. */
	TArray<FString> DriverNativeRun(FDriverFixture& F, FDriver& Driver, double Seconds)
	{
		TArray<FString> Bodies;
		for (double Done = 0; Done < Seconds; Done += 0.5)
		{
			F.run(Driver, 0.5);
			while (F.Net.pending(DriverNativeSnapshots) > 0)
			{
				Bodies.Add(F.Net.lastBody(DriverNativeSnapshots));
				F.Net.answer(DriverNativeSnapshots, SatisDash::Test::SnapshotOk(false));
			}
		}
		return Bodies;
	}

	bool DriverNativeAny(const TArray<FString>& Bodies, const TCHAR* Needle)
	{
		return Bodies.ContainsByPredicate([Needle](const FString& B) { return B.Contains(Needle); });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverNativePlayersTest, "SatisDash.Core.Driver.NativePlayers", SatisDash::Test::Flags)

bool FDriverNativePlayersTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	F.Game.Players = TArray<FPlayerReading>{ { TEXT("Here"), true }, { TEXT("Away"), false } };
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret(), TEXT(",\"playersSource\":\"native\""));

	TArray<FString> Bodies = DriverNativeRun(F, Driver, 20);
	TestTrue(TEXT("snapshots carry the game's players, connected first"),
		DriverNativeAny(Bodies, TEXT("\"players\":{\"available\":true,\"players\":[{\"name\":\"Here\",\"online\":true},{\"name\":\"Away\",\"online\":false}]}")));
	TestTrue(TEXT("the game was read on the status cadence"), F.Game.PlayerReads >= 3);
	TestEqual(TEXT("FRM is never asked for players in native mode"), F.Net.sent(TEXT("/getPlayer")), 0);
	TestTrue(TEXT("the bridge still runs for what is not native yet"), F.Net.sent(TEXT("/getFactory")) > 0);

	// Nobody known: an empty list is still a read list, not "FRM absent".
	F.Game.Players = TArray<FPlayerReading>();
	Bodies = DriverNativeRun(F, Driver, 10);
	TestTrue(TEXT("an empty list is available"), DriverNativeAny(Bodies, TEXT("\"players\":{\"available\":true,\"players\":[]}")));

	// The game state can't be read: the part is left out, and nothing says FRM is absent.
	F.Game.Players.Reset();
	Bodies = DriverNativeRun(F, Driver, 10);
	TestFalse(TEXT("an unreadable list leaves the part out"), DriverNativeAny(Bodies, TEXT("\"players\":")));
	TestFalse(TEXT("and does not claim unavailability"), DriverNativeAny(Bodies, TEXT("\"available\":false")));
	TestEqual(TEXT("still no FRM read for players"), F.Net.sent(TEXT("/getPlayer")), 0);

	// The default config (no key) is the bridge, unchanged: the first pass asks FRM.
	FDriverFixture G;
	G.Game.Players = TArray<FPlayerReading>{ { TEXT("Here"), true } };
	FDriver Bridge{ G.ConfigPath, G.CredentialPath, G.Net, G.Game, G.Worker, G.clock() };
	G.enrol(Bridge, SatisDash::Test::FakeSecret());
	G.run(Bridge, 2);
	TestEqual(TEXT("default: FRM is asked"), G.Net.pending(TEXT("/getPlayer")), 1);
	TestEqual(TEXT("default: the game's list is not read"), G.Game.PlayerReads, 0);
	return true;
}
```

- [ ] **Step 2: Run to confirm they fail to compile**

Run the `test-mod.ps1` command from "How the tests run". Expected: build failure (`ESource`, `NativePlayersPart`, `ReadPlayers`, the `Extra` parameter do not exist). (This is the RED run; if the executor's machine makes it slow, one build here and one at Step 8 is the task's budget, so skip this run and only build at Step 8.)

- [ ] **Step 3: The config**

In `Config.h`, after `DefaultFrmPort`:

```cpp
	/**
	 * Where a reader gets its data (ADR-0058 Decision 4): FRM through the bridge, or the game itself. One source per reader.
	 */
	enum class ESource : uint8 { Bridge, Native };
	/** What a host gets with no key set. The switch PR for a reader flips its constant here and changes nothing else. */
	inline constexpr ESource DefaultPlayersSource = ESource::Bridge;
	inline constexpr ESource DefaultResourceNodesSource = ESource::Bridge;
```

In `FAgentConfig`, after `FrmToken`:

```cpp
		/**
		 * Hidden stopgap overrides, documented in docs/native-readers.md and deleted with the bridge (ADR-0058, R7): the
		 * keys "playersSource" and "resourceNodesSource", each exactly "bridge" or "native". Unset = the default above,
		 * and not written back.
		 */
		TOptional<ESource> PlayersSource;
		TOptional<ESource> ResourceNodesSource;

		ESource GetPlayersSource() const { return PlayersSource.Get(DefaultPlayersSource); }
		ESource GetResourceNodesSource() const { return ResourceNodesSource.Get(DefaultResourceNodesSource); }
```

and extend the `Broken` comment: `"json", "backendUrl", "enrollmentCode", "frmPort", "frmToken", "playersSource" or "resourceNodesSource"`.

In `Config.cpp`, inside the anonymous namespace at the top add (a file-local name, unity-build safe):

```cpp
	/** Reads an optional "bridge" / "native" key. False when the key is there and is anything else. */
	bool ConfigReadSource(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, TOptional<SatisDash::ESource>& Out)
	{
		if (!Object->HasField(Key))
		{
			return true;
		}
		const TSharedPtr<FJsonValue> Value = SatisDash::Json::Field(Object, Key, EJson::String);
		if (!Value.IsValid())
		{
			return false;
		}
		const FString Text = Value->AsString();
		if (Text.Equals(TEXT("bridge"), ESearchCase::CaseSensitive))
		{
			Out = SatisDash::ESource::Bridge;
			return true;
		}
		if (Text.Equals(TEXT("native"), ESearchCase::CaseSensitive))
		{
			Out = SatisDash::ESource::Native;
			return true;
		}
		return false;
	}

	const TCHAR* ConfigSourceName(SatisDash::ESource Source)
	{
		return Source == SatisDash::ESource::Native ? TEXT("native") : TEXT("bridge");
	}
```

In `ParseConfig`, after the `frmToken` block and before `Result.Status = EConfigStatus::Valid;`:

```cpp
	TOptional<ESource> PlayersSource;
	if (!ConfigReadSource(Object, TEXT("playersSource"), PlayersSource))
	{
		Result.BadField = TEXT("playersSource");
		return Result;
	}
	TOptional<ESource> ResourceNodesSource;
	if (!ConfigReadSource(Object, TEXT("resourceNodesSource"), ResourceNodesSource))
	{
		Result.BadField = TEXT("resourceNodesSource");
		return Result;
	}
```

and after `Result.Config.FrmToken = FrmToken;` add `Result.Config.PlayersSource = PlayersSource; Result.Config.ResourceNodesSource = ResourceNodesSource;`. In `ConfigToJson`, after the `frmToken` block:

```cpp
	if (Config.PlayersSource.IsSet())
	{
		Object->SetStringField(TEXT("playersSource"), ConfigSourceName(Config.PlayersSource.GetValue()));
	}
	if (Config.ResourceNodesSource.IsSet())
	{
		Object->SetStringField(TEXT("resourceNodesSource"), ConfigSourceName(Config.ResourceNodesSource.GetValue()));
	}
```

(`Json::Field`'s second parameter takes the key as the existing `Field(Object, TEXT("backendUrl"), ...)` calls pass it; if it is `const FString&`, `Key` converts implicitly.)

- [ ] **Step 4: The players mapping**

Create `Public/SatisDash/Native/Players/NativePlayers.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "SatisDash/FrmPlayers.h"

namespace SatisDash
{
	/**
	 * R1 (ADR-0058): what the game's player lists gave, as the contract's players part (AgentPlayersSchema, ADR-0029: a name
	 * and online, nothing else). At most MaxPlayers are kept, connected players first so a long list of remembered players
	 * never pushes out someone who is online; the rest keep the order they were given in; names are cut to 200. A part built
	 * here is always available: an unreadable game state is the caller's "leave the part out", never "available: false".
	 */
	SATISDASHCORE_API FPlayersPart NativePlayersPart(const TArray<FPlayerReading>& Readings);
}
```

Create `Private/Native/Players/NativePlayers.cpp`:

```cpp
#include "SatisDash/Native/Players/NativePlayers.h"

#include "SatisDash/Snapshot.h"

SatisDash::FPlayersPart SatisDash::NativePlayersPart(const TArray<FPlayerReading>& Readings)
{
	FPlayersPart Part;
	Part.bAvailable = true;
	for (const bool bOnlinePass : { true, false })
	{
		for (const FPlayerReading& Reading : Readings)
		{
			if (Reading.bOnline != bOnlinePass)
			{
				continue;
			}
			if (Part.Players.Num() == MaxPlayers)
			{
				return Part;
			}
			Part.Players.Add({ ClampString(Reading.Name), Reading.bOnline });
		}
	}
	return Part;
}
```

Create `Private/Native/Players/PROVENANCE.md` (real date and author; `Captures:` names the capture used only to confirm the contract fields, `frm-getPlayer-2026-09-27-full.json`):

```
Reader: Players
Code: mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Native/Players/NativePlayers.h
Code: mod/SatisDashAgent/Source/SatisDashCore/Private/Native/Players/NativePlayers.cpp
Code: mod/SatisDashAgent/Source/SatisDashGame/Private/GameReader.cpp
Headers: Source/FactoryGame/Public/FGGameState.h:116, :515, :519
Headers: Source/FactoryGame/Public/Online/PlayerInfoCache.h:10, :20, :49, :54, :62, :73
Headers: Engine/Source/Runtime/Engine/Classes/GameFramework/GameStateBase.h:55
Headers: Engine/Source/Runtime/Engine/Classes/GameFramework/PlayerState.h:41, :232
Headers: Engine/Source/Runtime/Engine/Classes/Engine/World.h:3727
Survey: mod-native-readers-s1-headers-2026-10-25.md (satisfactory-dash docs-vault/raw-sources); the offline-player lookup is in docs/plans/2026-10-25-native-readers-s1.md, "Lookups made"
Captures: frm-getPlayer-2026-09-27-full.json (FRM HTTP output only; the oracle for which players are listed)
Author: <your session or subagent name>
Date: <YYYY-MM-DD>

FRM source was not opened
```

- [ ] **Step 5: The port, the bridge flag and the driver**

`Ports.h`: add `#include "SatisDash/FrmPlayers.h"` and, in `IGameReader` after `ReadPower`:

```cpp
		/**
		 * R1: every player the game knows in this session, connected ones online and the rest offline (names only, ADR-0029).
		 * Unset when the game state can't be read. Only called when the config's playersSource is "native".
		 */
		virtual TOptional<TArray<FPlayerReading>> ReadPlayers() { return {}; }
```

`FrmBridge.h`: change the declaration and comment of `RequestPass`:

```cpp
		/**
		 * A sample pass that read the game: queues getPlayer (the status cadence) unless bPlayers is false (the players
		 * reader is native), and getFactory when bFactoryDue.
		 */
		void RequestPass(bool bFactoryDue, bool bPlayers = true);
```

`FrmBridge.cpp`: signature `void SatisDash::FFrmBridge::RequestPass(bool bFactoryDue, bool bPlayers)`; wrap the existing `FrmLanes.Enqueue(FFrmRead{ &FrmGetPlayer, ... });` statement in `if (bPlayers) { ... }` (move nothing else).

`Driver.cpp`: add `#include "SatisDash/Native/Players/NativePlayers.h"`. In `TickSample`, replace

```cpp
		Bridge.RequestPass(bFactoryDue);
		Snapshot.PlayersJson = Bridge.TakePlayers(Now, 2.0 * Cadence.StatusSeconds);
```

with

```cpp
		const bool bNativePlayers = Agent.GetConfig().GetPlayersSource() == ESource::Native;
		Bridge.RequestPass(bFactoryDue, !bNativePlayers);
		if (bNativePlayers)
		{
			// R1 (ADR-0058): the game's own lists, small enough to serialise here. An unreadable list leaves the part out.
			if (const TOptional<TArray<FPlayerReading>> Readings = Game.ReadPlayers())
			{
				Snapshot.PlayersJson = PlayersToJson(NativePlayersPart(Readings.GetValue()));
			}
			else
			{
				NoteFailedPart(TEXT("players"), Now);
			}
		}
		else
		{
			Snapshot.PlayersJson = Bridge.TakePlayers(Now, 2.0 * Cadence.StatusSeconds);
		}
```

In `Driver.h` update the `PartLog` comment to "one key per snapshot part (status, auto-pause, power, players)" and the class Memory paragraph's "at most three keys (status, auto-pause, power)" to four.

- [ ] **Step 6: The game-side reader**

`GameReader.h`: add `virtual TOptional<TArray<FPlayerReading>> ReadPlayers() override;` after `ReadPower`, and update the class comment to "…as S6 (status), S7 (auto-pause), S3 (power) and ADR-0058 R1 (players) proved them".

`GameReader.cpp`: add includes `#include "FGGameState.h"`, `#include "GameFramework/GameStateBase.h"`, `#include "GameFramework/PlayerState.h"`, `#include "Online/PlayerInfoCache.h"` (keep them with the other includes, alphabetical), and append:

```cpp
TOptional<TArray<SatisDash::FPlayerReading>> SatisDash::FGameReader::ReadPlayers()
{
	UWorld* Current = World.Get();
	const AFGGameState* State = Current != nullptr ? Current->GetGameState<AFGGameState>() : nullptr;
	if (State == nullptr)
	{
		return {};
	}
	TArray<FPlayerReading> Out;
	// Connected: the replicated player list (GameStateBase.h:55). Offline: the players the session remembers who are not
	// online (FGGameState.h:519), named by the platform they were last seen on, else their first identity. Names are
	// personal data: they are copied into the reading and never logged.
	for (const APlayerState* Player : State->PlayerArray)
	{
		if (Player != nullptr)
		{
			Out.Add({ Player->GetPlayerName(), true });
		}
	}
	for (const FCachedPlayerInfo& Info : State->GetOfflineCachedPlayerInfosInSession())
	{
		const FCachedPlayerPlatformInfo* Platform = Info.GetForPlatform(Info.GetLastSeenServicesType());
		if (Platform == nullptr && !Info.PerPlatformInfo.IsEmpty())
		{
			Platform = &Info.PerPlatformInfo[0];
		}
		if (Platform != nullptr)
		{
			Out.Add({ Platform->PlayerName, false });
		}
	}
	return Out;
}
```

- [ ] **Step 7: api-sources rows and the docs**

Add to `api-sources.md` (look each line up again before committing; the lines below were read from the pinned checkout for this plan; `Source` is `header only` unless noted):

| Symbol | Header | Source |
| --- | --- | --- |
| FGGameState.h | Source/FactoryGame/Public/FGGameState.h:1 | header only |
| AFGGameState | Source/FactoryGame/Public/FGGameState.h:116 | header only |
| AFGGameState::GetOfflineCachedPlayerInfosInSession | Source/FactoryGame/Public/FGGameState.h:519 | header only (public; returns a copy of the players not online in the session; the S1 survey did not find it) |
| Online/PlayerInfoCache.h | Source/FactoryGame/Public/Online/PlayerInfoCache.h:1 | header only |
| FCachedPlayerInfo | Source/FactoryGame/Public/Online/PlayerInfoCache.h:49 | header only |
| FCachedPlayerInfo::PerPlatformInfo | Source/FactoryGame/Public/Online/PlayerInfoCache.h:54 | header only |
| FCachedPlayerInfo::GetLastSeenServicesType | Source/FactoryGame/Public/Online/PlayerInfoCache.h:62 | header only |
| FCachedPlayerInfo::GetForPlatform | Source/FactoryGame/Public/Online/PlayerInfoCache.h:73 | header only (returns `const FCachedPlayerPlatformInfo*`, null when none) |
| FCachedPlayerPlatformInfo | Source/FactoryGame/Public/Online/PlayerInfoCache.h:10 | header only |
| FCachedPlayerPlatformInfo::PlayerName | Source/FactoryGame/Public/Online/PlayerInfoCache.h:20 | header only |
| GameFramework/GameStateBase.h | Engine/Source/Runtime/Engine/Classes/GameFramework/GameStateBase.h:1 | header only |
| AGameStateBase::PlayerArray | Engine/Source/Runtime/Engine/Classes/GameFramework/GameStateBase.h:55 | header only (public) |
| GameFramework/PlayerState.h | Engine/Source/Runtime/Engine/Classes/GameFramework/PlayerState.h:1 | header only |
| APlayerState | Engine/Source/Runtime/Engine/Classes/GameFramework/PlayerState.h:41 | header only |
| APlayerState::GetPlayerName | Engine/Source/Runtime/Engine/Classes/GameFramework/PlayerState.h:232 | header only |
| UWorld::GetGameState | Engine/Source/Runtime/Engine/Classes/Engine/World.h:3727 | header only (template, `Cast<T>(GameState)`) |

Create `docs/native-readers.md`:

```markdown
# Native readers: status and the source switch

ADR-0058 (satisfactory-dash) replaces FicsitRemoteMonitoring (FRM) reader by reader. Rules for writing a reader are in
`docs/clean-room.md`.

| Reader | Sprint | Source today | Notes |
| --- | --- | --- | --- |
| Status, auto-pause | core v1 | native | proven (S6, S7) |
| Power (R0) | S1 | native | read natively since core v1; S1 judges every field against FRM (`native-parity`) |
| Players (R1) | S1 | `bridge` (default) | native reader built; default flips in its switch PR after `native-parity` passes |
| Resource nodes (R4) | S1 | `bridge` (default) | same |

## The hidden source override (a stopgap, not a setting)

Until the bridge is removed (ADR-0058, R7) a host can force a reader's source in the mod's config file
(`SatisDashAgent.cfg`, the same JSON as `backendUrl`):

    "playersSource": "native"
    "resourceNodesSource": "bridge"

The value is exactly `bridge` or `native` (anything else makes the config Broken, as a bad `frmPort` does). With no key a
reader uses its default (`DefaultPlayersSource`, `DefaultResourceNodesSource` in `Config.h`). `bridge` needs FRM installed.
The keys are deleted with the bridge in the removal release; do not build on them. There is no key for power: it has no
bridge path.
```

- [ ] **Step 8: Build, test, check**

Run: the `test-mod.ps1` command, then `npm run harness -- judge-mod-test <tmp>\mod-test --min 145`, then `npm run harness -- check-api` and `npm run harness -- check-provenance`.
Expected: **145** tests pass (142 + `Config.Sources`, `NativePlayers.Map`, `Driver.NativePlayers`); `check-api` at `0 missing, 0 bad rows` (anything it reports is looked up and added); `check-provenance: 2 native readers, 0 problems`.

- [ ] **Step 9: Commit**

```bash
git add -A mod api-sources.md docs/native-readers.md
git commit -m "Source switch and native players reader (ADR-0058 S1, R1)"
```

---

### Task 3: R4, resource nodes natively

**Files:**
- Create: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Native/ResourceNodes/NativeResourceNodes.h`
- Create: `mod/SatisDashAgent/Source/SatisDashCore/Private/Native/ResourceNodes/NativeResourceNodes.cpp`, `.../PROVENANCE.md`
- Modify: `Public/SatisDash/FrmResourceNodes.h`, `Private/FrmResourceNodes.cpp`, `Public/SatisDash/FrmWorld.h`, `Private/FrmWorld.cpp`, `Public/SatisDash/FrmBridge.h`, `Private/FrmBridge.cpp`, `Public/SatisDash/WorldSender.h`, `Private/WorldSender.cpp`, `Public/SatisDash/Ports.h`, `Private/Driver.cpp` (all under `SatisDashCore`)
- Modify: `mod/SatisDashAgent/Source/SatisDashGame/Private/GameReader.h`, `GameReader.cpp`
- Test: `SatisDashCoreTests/Private/NativeNodesTest.cpp` (new), `FrmWorldTest.cpp`, `DriverFakes.h`, `DriverNativeTest.cpp`
- Modify: `api-sources.md`, `docs/native-readers.md`, `.github/workflows/mod-test.yml` (owner's call, see Step 9)

**Interfaces:**
- Consumes: Task 2's `ESource`, `GetResourceNodesSource()`, `IGameReader`, `DriverFakes.h` `Extra` parameter.
- Produces:
  - `enum class ENodePurity : uint8 { Impure, Normal, Pure }`, `enum class ENodeKind : uint8 { Node, FrackingSatellite, Geyser }`, `struct FResourceNodeReading { FString Resource; ENodePurity Purity; ENodeKind Kind; double XCm; double YCm; bool bExploited; }`.
  - `struct FNativeWorldBody { FString Json; int32 DataFrom; int32 DataEnd; int32 Items; int32 Dropped; int32 Over; int64 Bytes; }`; `FNativeWorldBody BuildNativeResourceNodes(const FString& ObservedAt, const TArray<FResourceNodeReading>&)`.
  - `IGameReader::BeginResourceNodeScan()` → `bool`; `IGameReader::StepResourceNodeScan(int32 MaxActors)` → `TOptional<TArray<FResourceNodeReading>>` (unset until the pass has seen every actor).
  - `inline constexpr int32 NodeScanActorsPerStep = 2000;` `inline constexpr double NodeScanRetrySeconds = 30.0;`
  - `FFrmBridge::SubmitNativeWorld(EWorldLayer, FNativeWorldBody, TOptional<FBlake3Hash> LastData)` → `bool`; `FWorldContext::Game` (`IGameReader*`).
  - `AppendResourceNodeRecord(FString& Out, const FString& Type, const FString& Purity, const FString& NodeType, double Mx, double My, bool bExploited)`.

- [ ] **Step 1: Write the failing tests**

Create `NativeNodesTest.cpp`:

```cpp
#include "SatisDash/Native/ResourceNodes/NativeResourceNodes.h"
#include "SatisDash/FrmResourceNodes.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	FResourceNodeReading NativeNodesNode(const TCHAR* Resource, ENodePurity Purity, ENodeKind Kind, double XCm, double YCm, bool bExploited)
	{
		FResourceNodeReading R;
		R.Resource = Resource;
		R.Purity = Purity;
		R.Kind = Kind;
		R.XCm = XCm;
		R.YCm = YCm;
		R.bExploited = bExploited;
		return R;
	}

	FString NativeNodesData(const FNativeWorldBody& Body)
	{
		return Body.Json.Mid(Body.DataFrom, Body.DataEnd - Body.DataFrom);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeNodesGoldenTest, "SatisDash.Core.NativeNodes.Golden", SatisDash::Test::Flags)

bool FNativeNodesGoldenTest::RunTest(const FString& Parameters)
{
	// The bridge's golden (frm-getResourceNode.expected.json) in the game's own units: centimetres, enum values.
	TArray<FResourceNodeReading> In;
	In.Add(NativeNodesNode(TEXT("Crude Oil"), ENodePurity::Impure, ENodeKind::FrackingSatellite, 50, -50, true));
	In.Add(NativeNodesNode(TEXT("Copper Ore"), ENodePurity::Pure, ENodeKind::Node, 100, 200, false));
	In.Add(NativeNodesNode(TEXT("Geothermal"), ENodePurity::Pure, ENodeKind::Geyser, 0, 0, false));
	In.Add(NativeNodesNode(TEXT("Iron Ore"), ENodePurity::Normal, ENodeKind::Node, -12345, 67890, false));
	const FNativeWorldBody Body = BuildNativeResourceNodes(TEXT("2026-10-25T12:00:00.000Z"), In);
	TestEqual(TEXT("four items"), Body.Items, 4);
	TestEqual(TEXT("the whole body is the world body the bridge posts"), Body.Json,
		FString(TEXT("{\"observedAt\":\"2026-10-25T12:00:00.000Z\",\"data\":[")
			TEXT("{\"type\":\"Iron Ore\",\"purity\":\"normal\",\"nodeType\":\"node\",\"x\":-123,\"y\":679,\"exploited\":false},")
			TEXT("{\"type\":\"Geothermal\",\"purity\":\"pure\",\"nodeType\":\"geyser\",\"x\":0,\"y\":0,\"exploited\":false},")
			TEXT("{\"type\":\"Crude Oil\",\"purity\":\"impure\",\"nodeType\":\"frackingSatellite\",\"x\":1,\"y\":0,\"exploited\":true},")
			TEXT("{\"type\":\"Copper Ore\",\"purity\":\"pure\",\"nodeType\":\"node\",\"x\":1,\"y\":2,\"exploited\":false}]}")));
	TestEqual(TEXT("the data starts at the array"), Body.Json[Body.DataFrom], TEXT('['));
	TestEqual(TEXT("and ends after its bracket"), Body.Json[Body.DataEnd - 1], TEXT(']'));
	TestEqual(TEXT("bytes are the UTF-8 length"), Body.Bytes, int64(Body.Json.Len()));

	// The same record text the FRM parse writes for the same values (one shared writer, so the two sources can't drift).
	FString Direct;
	AppendResourceNodeRecord(Direct, TEXT("Iron Ore"), TEXT("Normal"), TEXT("node"), -123, 679, false);
	TestEqual(TEXT("the shared record writer lowercases purity"), Direct, FString(TEXT("{\"type\":\"Iron Ore\",\"purity\":\"normal\",\"nodeType\":\"node\",\"x\":-123,\"y\":679,\"exploited\":false}")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeNodesRulesTest, "SatisDash.Core.NativeNodes.Rules", SatisDash::Test::Flags)

bool FNativeNodesRulesTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("nothing found is an empty layer"), BuildNativeResourceNodes(TEXT("T"), {}).Json, FString(TEXT("{\"observedAt\":\"T\",\"data\":[]}")));

	// Halves go toward +infinity, as Math.round does: 150 cm is 2 m, -150 cm is -1 m.
	TArray<FResourceNodeReading> Halves;
	Halves.Add(NativeNodesNode(TEXT("A"), ENodePurity::Pure, ENodeKind::Node, 150, -150, false));
	TestTrue(TEXT("rounding"), NativeNodesData(BuildNativeResourceNodes(TEXT("T"), Halves)).Contains(TEXT("\"x\":2,\"y\":-1")));

	// A node past +/-1,000,000 m is dropped and counted; it is not an error.
	TArray<FResourceNodeReading> Far;
	Far.Add(NativeNodesNode(TEXT("A"), ENodePurity::Pure, ENodeKind::Node, 1.0e12, 0, false));
	Far.Add(NativeNodesNode(TEXT("B"), ENodePurity::Pure, ENodeKind::Node, 0, 0, false));
	const FNativeWorldBody FarBody = BuildNativeResourceNodes(TEXT("T"), Far);
	TestEqual(TEXT("one kept"), FarBody.Items, 1);
	TestEqual(TEXT("one dropped"), FarBody.Dropped, 1);

	// The order the game gave them in does not change the body, so an unchanged world never reposts.
	TArray<FResourceNodeReading> Forward;
	for (int32 I = 0; I < 20; ++I)
	{
		Forward.Add(NativeNodesNode(*FString::Printf(TEXT("R%d"), I % 3), ENodePurity::Normal, ENodeKind::Node, I * 700, (20 - I) * 300, I % 2 == 0));
	}
	TArray<FResourceNodeReading> Backward;
	for (int32 I = Forward.Num() - 1; I >= 0; --I)
	{
		Backward.Add(Forward[I]);
	}
	TestEqual(TEXT("scan order is irrelevant"), BuildNativeResourceNodes(TEXT("T"), Forward).Json, BuildNativeResourceNodes(TEXT("T"), Backward).Json);

	// The cap: 5,001 nodes keep the first 5,000 in sorted order and count the one over.
	TArray<FResourceNodeReading> Many;
	for (int32 I = 0; I < MaxResourceNodes + 1; ++I)
	{
		Many.Add(NativeNodesNode(TEXT("Iron Ore"), ENodePurity::Normal, ENodeKind::Node, double(I) * 100.0, 0, false));
	}
	const FNativeWorldBody ManyBody = BuildNativeResourceNodes(TEXT("T"), Many);
	TestEqual(TEXT("5,000 kept"), ManyBody.Items, MaxResourceNodes);
	TestEqual(TEXT("one over"), ManyBody.Over, 1);
	TestTrue(TEXT("the one left out is the last in sorted order"), !ManyBody.Json.Contains(FString::Printf(TEXT("\"x\":%d,"), MaxResourceNodes)));

	// A long resource name is cut to 200 characters.
	TArray<FResourceNodeReading> Long;
	Long.Add(NativeNodesNode(*FString::ChrN(250, TEXT('r')), ENodePurity::Pure, ENodeKind::Node, 0, 0, false));
	TestTrue(TEXT("name cut"), NativeNodesData(BuildNativeResourceNodes(TEXT("T"), Long)).Contains(FString(TEXT("\"type\":\"")) + FString::ChrN(200, TEXT('r')) + TEXT("\",")));
	return true;
}
```

Append to `FrmWorldTest.cpp` (include `SatisDash/Native/ResourceNodes/NativeResourceNodes.h` at the top; `FBlake3` comes through `FrmWorld.h`):

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmWorldNativeTest, "SatisDash.Core.FrmWorld.Native", SatisDash::Test::Flags)

bool FFrmWorldNativeTest::RunTest(const FString& Parameters)
{
	TArray<FResourceNodeReading> In;
	FResourceNodeReading R;
	R.Resource = TEXT("Iron Ore");
	R.Purity = ENodePurity::Normal;
	R.Kind = ENodeKind::Node;
	R.XCm = 100;
	R.YCm = 200;
	In.Add(R);

	FNativeWorldBody Body = BuildNativeResourceNodes(TEXT("T"), In);
	FBlake3 Hasher;
	Hasher.Update(*Body.Json + Body.DataFrom, uint64(Body.DataEnd - Body.DataFrom) * sizeof(TCHAR));
	const FBlake3Hash Expected = Hasher.Finalize();

	FWorldRead First{ EWorldLayer::ResourceNodes, BuildNativeResourceNodes(TEXT("T"), In), TOptional<FBlake3Hash>() };
	TestTrue(TEXT("a built body is one step"), First.Step());
	FWorldOutcome Ready = First.TakeOutcome();
	TestTrue(TEXT("ready to post"), Ready.Kind == FWorldOutcome::EKind::Ready);
	TestEqual(TEXT("one item"), Ready.Items, 1);
	TestTrue(TEXT("the data hash covers exactly the array text, as for an FRM body"), Ready.DataHash == Expected);
	TestTrue(TEXT("a body to post"), Ready.Body.Bytes.Num() > 0);

	FWorldRead Same{ EWorldLayer::ResourceNodes, BuildNativeResourceNodes(TEXT("T2"), In), TOptional<FBlake3Hash>(Expected) };
	TestTrue(TEXT("same data, one step"), Same.Step());
	TestTrue(TEXT("a new observedAt over the same nodes is not a change"), Same.TakeOutcome().Kind == FWorldOutcome::EKind::SameData);

	FWorldRead Over{ EWorldLayer::ResourceNodes, BuildNativeResourceNodes(TEXT("T"), In), TOptional<FBlake3Hash>(), 10 };
	TestTrue(TEXT("over the cap, one step"), Over.Step());
	TestTrue(TEXT("over the byte cap is skipped"), Over.TakeOutcome().Kind == FWorldOutcome::EKind::OverCap);
	return true;
}
```

Extend `FFakeGame` in `DriverFakes.h`:

```cpp
		/** R4: what a finished scan returns; BeginsOk false = no world. A scan finishes on its StepsToFinish-th step. */
		TArray<SatisDash::FResourceNodeReading> Nodes;
		bool bHasWorld = true;
		int32 StepsToFinish = 3;
		int32 ScanBegins = 0;
		int32 ScanSteps = 0;
		virtual bool BeginResourceNodeScan() override { ++ScanBegins; ScanSteps = 0; return bHasWorld; }
		virtual TOptional<TArray<SatisDash::FResourceNodeReading>> StepResourceNodeScan(int32 MaxActors) override
		{
			return ++ScanSteps >= StepsToFinish ? TOptional<TArray<SatisDash::FResourceNodeReading>>(Nodes) : TOptional<TArray<SatisDash::FResourceNodeReading>>();
		}
```

Append to `DriverNativeTest.cpp`:

```cpp
namespace
{
	const TCHAR* DriverNativeNodesPost = TEXT("/agent/v1/world/resourceNodes");
	const TCHAR* DriverNativeAccepted = TEXT("{\"accepted\":true,\"unchanged\":false}");

	/** Answers snapshots, and every FRM read (for the layers and parts still on the bridge) with a refusal, each tick. */
	void DriverNativeRoutine(FDriverFixture& F)
	{
		while (F.Net.pending(DriverNativeSnapshots) > 0)
		{
			F.Net.answer(DriverNativeSnapshots, SatisDash::Test::SnapshotOk(false));
		}
		for (const TCHAR* Path : { TEXT("/getPlayer"), TEXT("/getFactory"), TEXT("/getTrains"), TEXT("/getTrainStation"), TEXT("/getTrainRails"), TEXT("/getResourceNode") })
		{
			F.Net.answer(Path, SatisDash::Test::Reply(0, FString()));
		}
	}

	bool DriverNativeRunUntil(FDriverFixture& F, FDriver& Driver, const TFunction<bool()>& reached, double Seconds)
	{
		for (double Spent = 0; Spent < Seconds; Spent += 0.5)
		{
			DriverNativeRoutine(F);
			if (reached())
			{
				return true;
			}
			F.run(Driver, 0.5);
		}
		DriverNativeRoutine(F);
		return reached();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverNativeNodesTest, "SatisDash.Core.Driver.NativeNodes", SatisDash::Test::Flags)

bool FDriverNativeNodesTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FResourceNodeReading Iron;
	Iron.Resource = TEXT("Iron Ore");
	Iron.Purity = ENodePurity::Normal;
	Iron.Kind = ENodeKind::Node;
	Iron.XCm = -12345;
	Iron.YCm = 67890;
	Iron.bExploited = false;
	F.Game.Nodes.Add(Iron);
	F.Game.bHasWorld = false; // no world yet: the scan can't start
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret(), TEXT(",\"resourceNodesSource\":\"native\""));

	// The first read is due at 480 s x jitter (the fake clock's Random01 is 1.0, so 528 s). With no world it retries each 30 s.
	TestTrue(TEXT("the scan is attempted"), DriverNativeRunUntil(F, Driver, [&F] { return F.Game.ScanBegins >= 1; }, 600));
	TestTrue(TEXT("and retried while there is no world"), DriverNativeRunUntil(F, Driver, [&F] { return F.Game.ScanBegins >= 3; }, 90));
	TestEqual(TEXT("nothing posted without a world"), F.Net.sent(DriverNativeNodesPost), 0);

	F.Game.bHasWorld = true;
	TestTrue(TEXT("the layer is posted once the scan finishes"), DriverNativeRunUntil(F, Driver, [&F] { return F.Net.pending(DriverNativeNodesPost) > 0; }, 120));
	const FString Body = F.Net.lastBody(DriverNativeNodesPost);
	TestTrue(TEXT("the data is the mapped nodes"), Body.Contains(TEXT("\"data\":[{\"type\":\"Iron Ore\",\"purity\":\"normal\",\"nodeType\":\"node\",\"x\":-123,\"y\":679,\"exploited\":false}]")));
	TestEqual(TEXT("FRM is never asked for resource nodes in native mode"), F.Net.sent(TEXT("/getResourceNode")), 0);
	F.Net.answer(DriverNativeNodesPost, SatisDash::Test::Reply(200, DriverNativeAccepted));

	// The next cycle (1,800 s x jitter later) scans again, finds the same nodes and posts nothing.
	const int32 Begins = F.Game.ScanBegins;
	TestTrue(TEXT("the next cycle scans again"), DriverNativeRunUntil(F, Driver, [&F, Begins] { return F.Game.ScanBegins > Begins; }, 2100));
	DriverNativeRunUntil(F, Driver, []() { return false; }, 30);
	TestEqual(TEXT("an unchanged world is not posted again"), F.Net.sent(DriverNativeNodesPost), 1);

	// A changed world is.
	FResourceNodeReading Oil = Iron;
	Oil.Resource = TEXT("Crude Oil");
	Oil.Kind = ENodeKind::FrackingSatellite;
	F.Game.Nodes.Add(Oil);
	TestTrue(TEXT("a changed layer posts"), DriverNativeRunUntil(F, Driver, [&F] { return F.Net.pending(DriverNativeNodesPost) > 0; }, 2100));
	return true;
}
```

- [ ] **Step 2: The node types and the builder**

Create `Public/SatisDash/Native/ResourceNodes/NativeResourceNodes.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

namespace SatisDash
{
	/** The purity the game reports (EResourcePurity), by the contract's words. */
	enum class ENodePurity : uint8 { Impure, Normal, Pure };
	/** The node types the map draws (EResourceNodeType): fracking cores and deposits are not part of the layer. */
	enum class ENodeKind : uint8 { Node, FrackingSatellite, Geyser };

	/** One resource node as the game reports it, before mapping. Positions are in game units (centimetres). */
	struct FResourceNodeReading
	{
		FString Resource;
		ENodePurity Purity = ENodePurity::Normal;
		ENodeKind Kind = ENodeKind::Node;
		double XCm = 0;
		double YCm = 0;
		bool bExploited = false;
	};

	/** Actors looked at per driver tick during a node scan (about 0.1 ms of class checks). */
	inline constexpr int32 NodeScanActorsPerStep = 2000;
	/** After a scan could not start (no world yet): when to try again. */
	inline constexpr double NodeScanRetrySeconds = 30.0;

	/** A world body built whole: the text, where the data array sits in it, and the counts FWorldRead reports. */
	struct FNativeWorldBody
	{
		FString Json;
		/** Where the array's "[" is in Json, and one past its "]" (the hash covers exactly that). */
		int32 DataFrom = 0;
		int32 DataEnd = 0;
		int32 Items = 0;
		/** Readings with a coordinate outside +/-1,000,000 m. */
		int32 Dropped = 0;
		/** Readings past the 5,000-item cap. */
		int32 Over = 0;
		/** The whole body's UTF-8 bytes. */
		int64 Bytes = 0;
	};

	/**
	 * R4 (ADR-0058): the resourceNodes world body {"observedAt":...,"data":[...]} from the game's own nodes, byte for byte the
	 * form FResourceNodesParse writes from FRM (one shared record writer). Positions go to whole metres as the map does
	 * (Math.round of cm / 100), nodes outside +/-1,000,000 m are dropped, and the nodes are sorted by (x, y, resource,
	 * purity, type, exploited) before the 5,000-item cap, so the same world always gives the same bytes whatever order the
	 * scan found it in.
	 */
	SATISDASHCORE_API FNativeWorldBody BuildNativeResourceNodes(const FString& ObservedAt, const TArray<FResourceNodeReading>& Readings);
}
```

Create `Private/Native/ResourceNodes/NativeResourceNodes.cpp`:

```cpp
#include "SatisDash/Native/ResourceNodes/NativeResourceNodes.h"

#include "SatisDash/FrmMap.h"
#include "SatisDash/FrmRecords.h"
#include "SatisDash/FrmResourceNodes.h"
#include "SatisDash/Snapshot.h"

namespace
{
	const TCHAR* NativeNodesPurityName(SatisDash::ENodePurity Purity)
	{
		switch (Purity)
		{
		case SatisDash::ENodePurity::Impure: return TEXT("impure");
		case SatisDash::ENodePurity::Pure: return TEXT("pure");
		default: return TEXT("normal");
		}
	}

	const TCHAR* NativeNodesKindName(SatisDash::ENodeKind Kind)
	{
		switch (Kind)
		{
		case SatisDash::ENodeKind::FrackingSatellite: return TEXT("frackingSatellite");
		case SatisDash::ENodeKind::Geyser: return TEXT("geyser");
		default: return TEXT("node");
		}
	}

	struct FNativeNodesKept
	{
		double Mx;
		double My;
		const SatisDash::FResourceNodeReading* Reading;
	};

	bool NativeNodesLess(const FNativeNodesKept& A, const FNativeNodesKept& B)
	{
		if (A.Mx != B.Mx)
		{
			return A.Mx < B.Mx;
		}
		if (A.My != B.My)
		{
			return A.My < B.My;
		}
		const int32 ByResource = A.Reading->Resource.Compare(B.Reading->Resource, ESearchCase::CaseSensitive);
		if (ByResource != 0)
		{
			return ByResource < 0;
		}
		if (A.Reading->Purity != B.Reading->Purity)
		{
			return uint8(A.Reading->Purity) < uint8(B.Reading->Purity);
		}
		if (A.Reading->Kind != B.Reading->Kind)
		{
			return uint8(A.Reading->Kind) < uint8(B.Reading->Kind);
		}
		return !A.Reading->bExploited && B.Reading->bExploited;
	}
}

SatisDash::FNativeWorldBody SatisDash::BuildNativeResourceNodes(const FString& ObservedAt, const TArray<FResourceNodeReading>& Readings)
{
	FNativeWorldBody Out;
	TArray<FNativeNodesKept> Kept;
	Kept.Reserve(Readings.Num());
	for (const FResourceNodeReading& Reading : Readings)
	{
		const double Mx = ToWholeMetres(Reading.XCm);
		const double My = ToWholeMetres(Reading.YCm);
		if (!InWholeMetreBounds(Mx) || !InWholeMetreBounds(My))
		{
			++Out.Dropped;
			continue;
		}
		Kept.Add({ Mx, My, &Reading });
	}
	Kept.Sort(NativeNodesLess);
	Out.Json = WorldBodyPrefix(ObservedAt);
	Out.DataFrom = Out.Json.Len();
	Out.Json.AppendChar(TEXT('['));
	for (const FNativeNodesKept& Node : Kept)
	{
		if (Out.Items == MaxResourceNodes)
		{
			++Out.Over;
			continue;
		}
		if (Out.Items > 0)
		{
			Out.Json.AppendChar(TEXT(','));
		}
		AppendResourceNodeRecord(Out.Json, Node.Reading->Resource, NativeNodesPurityName(Node.Reading->Purity), NativeNodesKindName(Node.Reading->Kind), Node.Mx, Node.My, Node.Reading->bExploited);
		++Out.Items;
	}
	Out.Json.AppendChar(TEXT(']'));
	Out.DataEnd = Out.Json.Len();
	Out.Json += TEXT("}");
	Out.Bytes = FTCHARToUTF8_Convert::ConvertedLength(*Out.Json, Out.Json.Len());
	return Out;
}
```

Create `Private/Native/ResourceNodes/PROVENANCE.md` (real date and author):

```
Reader: ResourceNodes
Code: mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Native/ResourceNodes/NativeResourceNodes.h
Code: mod/SatisDashAgent/Source/SatisDashCore/Private/Native/ResourceNodes/NativeResourceNodes.cpp
Code: mod/SatisDashAgent/Source/SatisDashGame/Private/GameReader.cpp
Headers: Source/FactoryGame/Public/Resources/FGResourceNodeBase.h:24-30, :93, :140, :176, :180
Headers: Source/FactoryGame/Public/Resources/FGResourceNode.h:26-31, :66, :113
Headers: Source/FactoryGame/Public/Resources/FGResourceDeposit.h:15
Headers: Engine/Source/Runtime/Engine/Classes/Engine/Level.h:422, :432
Headers: Engine/Source/Runtime/Engine/Classes/Engine/World.h:3443
Headers: Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h:2505
Survey: mod-native-readers-s1-headers-2026-10-25.md (satisfactory-dash docs-vault/raw-sources); enumeration and position lines corrected in docs/plans/2026-10-25-native-readers-s1.md, "Lookups made"
Captures: frm-getResourceNode-2026-09-27-trimmed.json and the bridge's own frm-getResourceNode.expected.json (our output; the oracle for the layer's contents)
Author: <your session or subagent name>
Date: <YYYY-MM-DD>

FRM source was not opened
```

- [ ] **Step 3: One record writer for both sources**

`FrmResourceNodes.h`: add before the class:

```cpp
	/**
	 * Appends one resourceNodes record {"type","purity","nodeType","x","y","exploited"} with the contract's bounds: type cut to
	 * 200, purity lowercased and cut to MaxNodeFieldChars, nodeType (already mapped) cut to MaxNodeFieldChars, x and y whole
	 * metres. Shared by FResourceNodesParse (from FRM) and BuildNativeResourceNodes (from the game).
	 */
	SATISDASHCORE_API void AppendResourceNodeRecord(FString& Out, const FString& Type, const FString& Purity, const FString& NodeType, double Mx, double My, bool bExploited);
```

`FrmResourceNodes.cpp`: add the definition (the same calls the parse made inline):

```cpp
void SatisDash::AppendResourceNodeRecord(FString& Out, const FString& Type, const FString& Purity, const FString& NodeType, double Mx, double My, bool bExploited)
{
	Out += TEXT("{\"type\":");
	SatisDash::Json::AppendString(Out, ClampString(Type));
	Out += TEXT(",\"purity\":");
	SatisDash::Json::AppendString(Out, ClampStringTo(Purity.ToLower(), MaxNodeFieldChars));
	Out += TEXT(",\"nodeType\":");
	SatisDash::Json::AppendString(Out, ClampStringTo(NodeType, MaxNodeFieldChars));
	Out += TEXT(",\"x\":");
	Out.AppendInt(int32(Mx));
	Out += TEXT(",\"y\":");
	Out.AppendInt(int32(My));
	Out += bExploited ? TEXT(",\"exploited\":true}") : TEXT(",\"exploited\":false}");
}
```

and in `FResourceNodesParse::ReadRecord` replace the block from `BeginRecord();` through `Out += Exploited.GetValue() ? ...;` with:

```cpp
			BeginRecord();
			AppendResourceNodeRecord(Out, Name.GetValue(), Purity.GetValue(), MapNodeType(NodeType.GetValue()), Mx, My, Exploited.GetValue());
```

(`Purity` is a `TOptional<FString>`, so `Purity.GetValue()`.) The existing `FrmResourceNodes.*` tests and `.Golden` guard that nothing changed.

- [ ] **Step 4: The world read for a prebuilt body**

`FrmWorld.h`: add `#include "SatisDash/Native/ResourceNodes/NativeResourceNodes.h"`; in `FWorldRead` add a constructor and members:

```cpp
		/**
		 * A native body, already built whole (R4): nothing to hash or parse, so the first Step settles it. Same outcome kinds
		 * as an FRM body except SameRaw; RawHash is the data hash.
		 */
		FWorldRead(EWorldLayer InLayer, FNativeWorldBody InBuilt, TOptional<FBlake3Hash> InLastData, int64 InMaxBodyBytes = WorldLayerMaxBytes);
```

and in the private section: change `enum class EStage : uint8 { Hashing, Parsing, Done };` to `{ Hashing, Parsing, Built, Done }`, add `FNativeWorldBody Built;`, and replace `void Finish();` with

```cpp
		void Finish();
		/** Everything after parsing: hash the data, apply the cap, encode. Json is the whole body text. */
		void Settle(const FString& Json, int32 DataFrom, int32 DataEnd, int32 Items, int32 Dropped, int32 Over, int64 Bytes);
```

`FrmWorld.cpp`: add the constructor

```cpp
SatisDash::FWorldRead::FWorldRead(EWorldLayer InLayer, FNativeWorldBody InBuilt, TOptional<FBlake3Hash> InLastData, int64 InMaxBodyBytes)
	: Layer{InLayer}
	, LastData{InLastData}
	, MaxBodyBytes{InMaxBodyBytes}
	, Stage{EStage::Built}
	, Built{MoveTemp(InBuilt)}
{
}
```

(keep the member initialiser order identical to the declaration order in the header; move `Stage` and `Built` declarations if the compiler warns about order). In `Step()` add a case before `default:`:

```cpp
	case EStage::Built:
	{
		Settle(Built.Json, Built.DataFrom, Built.DataEnd, Built.Items, Built.Dropped, Built.Over, Built.Bytes);
		Outcome.RawHash = Outcome.DataHash;
		Built.Json.Empty();
		Stage = EStage::Done;
		return true;
	}
```

Rename the existing `Finish()` body to `Settle(...)`: replace its first lines that read from `Array` (`Outcome.Items = Array.Kept(); ... const FString& Json = Array.PeekJson(); ... DataHasher.Update(*Json + Array.DataFrom(), uint64(Array.DataEnd() - Array.DataFrom()) * sizeof(TCHAR));`) with the parameters (`Outcome.Items = Items; Outcome.Dropped = Dropped; Outcome.Over = Over; Outcome.Bytes = Bytes;` and `DataHasher.Update(*Json + DataFrom, uint64(DataEnd - DataFrom) * sizeof(TCHAR));`), delete every `Owned.Reset();` inside it, and add the new `Finish()`:

```cpp
void SatisDash::FWorldRead::Finish()
{
	const FFrmArrayParse& Array = *Owned;
	Settle(Array.PeekJson(), Array.DataFrom(), Array.DataEnd(), Array.Kept(), Array.Dropped(), Array.Over(), Array.Utf8Bytes());
	Owned.Reset();
}
```

(`Settle` keeps `Outcome.DataHash`, the `SameData` / `OverCap` / `EncodeBody(Json)` logic untouched.) The existing `SatisDash.Core.FrmWorld.*` tests guard the FRM path.

- [ ] **Step 5: The bridge, the sender and the driver**

`FrmBridge.h`: add after `RequestWorld`:

```cpp
		/**
		 * R4: a world body built by a native reader. It takes the FRM path's place from the worker on: data hash, cap, encode,
		 * then TakeWorld. False while that layer is requested, being read, or has an outcome waiting.
		 */
		bool SubmitNativeWorld(EWorldLayer Layer, FNativeWorldBody Body, TOptional<FBlake3Hash> LastData);
```

and a second constructor on `FWorldJob`:

```cpp
			FWorldJob(EWorldLayer InLayer, FNativeWorldBody InBuilt, TOptional<FBlake3Hash> InLastData)
				: Work{InLayer, MoveTemp(InBuilt), InLastData}
			{
			}
```

`FrmBridge.cpp`: add

```cpp
bool SatisDash::FFrmBridge::SubmitNativeWorld(EWorldLayer Layer, FNativeWorldBody Body, TOptional<FBlake3Hash> LastData)
{
	FWorldSlot& Slot = World[int32(Layer)];
	if (Slot.bRequested || Slot.Job.IsValid() || Slot.Outcome.IsSet())
	{
		return false;
	}
	Slot.bRequested = true;
	Slot.Job = MakeShared<FWorldJob>(Layer, MoveTemp(Body), LastData);
	return true;
}
```

`WorldSender.h`: add `#include "SatisDash/Ports.h"` is already there; add `IGameReader* Game = nullptr;` as the last member of `FWorldContext` (comment: "the native readers, when a layer's source is native"); change `enum class EPhase : uint8 { Waiting, Reading, Pending, Posting };` to include `Scanning` (`{ Waiting, Scanning, Reading, Pending, Posting }`).

`WorldSender.cpp`: add `#include "SatisDash/Native/ResourceNodes/NativeResourceNodes.h"`, an anonymous-namespace helper

```cpp
namespace
{
	/** R4: the resourceNodes layer comes from the game itself when the config says so and a reader is there. */
	bool WorldSenderIsNative(SatisDash::EWorldLayer Layer, const SatisDash::FWorldContext& Context)
	{
		return Layer == SatisDash::EWorldLayer::ResourceNodes && Context.Game != nullptr && Context.Config != nullptr
			&& Context.Config->GetResourceNodesSource() == SatisDash::ESource::Native;
	}
}
```

and replace the `Waiting` case of `TickLayer` with:

```cpp
	case EPhase::Waiting:
		if (Context.Now >= L.NextAt)
		{
			if (WorldSenderIsNative(Layer, Context))
			{
				if (Context.Game->BeginResourceNodeScan())
				{
					L.Phase = EPhase::Scanning;
				}
				else
				{
					L.NextAt = Context.Now + NodeScanRetrySeconds; // no world yet; FRM is not asked
				}
			}
			else if (Bridge.RequestWorld(Layer, Context.Utc.ToIso8601(), L.RawHash, L.DataHash))
			{
				L.Phase = EPhase::Reading;
			}
		}
		return;
	case EPhase::Scanning:
	{
		if (!WorldSenderIsNative(Layer, Context))
		{
			ScheduleNext(Layer, Context.Now); // the source changed under a scan: drop it
			return;
		}
		TOptional<TArray<FResourceNodeReading>> Found = Context.Game->StepResourceNodeScan(NodeScanActorsPerStep);
		if (!Found.IsSet())
		{
			return;
		}
		if (Bridge.SubmitNativeWorld(Layer, BuildNativeResourceNodes(Context.Utc.ToIso8601(), Found.GetValue()), L.DataHash))
		{
			L.Phase = EPhase::Reading;
		}
		else
		{
			ScheduleNext(Layer, Context.Now);
		}
		return;
	}
```

(the old `Waiting` case's body is replaced; the `Reading`, `Pending` and `Posting` cases are unchanged, and the world sender's `Settle` already handles `Ready`, `SameData` and `OverCap`).

`Driver.cpp`: in `Tick`, the world sender context becomes `WorldLayers.Tick({ Now, Clock.UtcNow(), &Config, Credential, !bSendInFlight && Pusher.Num() == 0 && !Bridge.EncodePending(), &Game });`.

- [ ] **Step 6: The port and the game-side scan**

`Ports.h`: add `#include "SatisDash/Native/ResourceNodes/NativeResourceNodes.h"` and in `IGameReader`:

```cpp
		/**
		 * R4: starts a pass over the world's actors looking for resource nodes. False when there is no world. A pass takes many
		 * ticks: call StepResourceNodeScan until it returns a value. Only called when resourceNodesSource is "native".
		 */
		virtual bool BeginResourceNodeScan() { return false; }
		/** Looks at up to MaxActors more actors. Unset until the pass has seen every actor, then every node found (once). */
		virtual TOptional<TArray<FResourceNodeReading>> StepResourceNodeScan(int32 MaxActors) { return {}; }
```

`GameReader.h`: add `class ULevel;` beside the other forward declarations; the overrides

```cpp
		virtual bool BeginResourceNodeScan() override;
		virtual TOptional<TArray<FResourceNodeReading>> StepResourceNodeScan(int32 MaxActors) override;
```

and in `private:`

```cpp
		/** The node scan (R4): the levels to look through, where it is, what it found, and its cost (counts and times only). */
		TArray<TWeakObjectPtr<ULevel>> ScanLevels;
		int32 ScanLevel = 0;
		int32 ScanIndex = 0;
		int32 ScanActors = 0;
		int32 ScanSteps = 0;
		double ScanLongestStep = 0;
		double ScanTotal = 0;
		TArray<FResourceNodeReading> ScanFound;
```

`GameReader.cpp`: add includes `#include "Engine/Level.h"`, `#include "Resources/FGResourceNode.h"`, `#include "Resources/FGResourceNodeBase.h"`, `#include "SatisDash/Log.h"`, and append:

```cpp
bool SatisDash::FGameReader::BeginResourceNodeScan()
{
	UWorld* Current = World.Get();
	if (Current == nullptr)
	{
		return false;
	}
	ScanLevels.Reset();
	for (ULevel* Level : Current->GetLevels())
	{
		ScanLevels.Add(Level);
	}
	ScanLevel = 0;
	ScanIndex = 0;
	ScanActors = 0;
	ScanSteps = 0;
	ScanLongestStep = 0;
	ScanTotal = 0;
	ScanFound.Reset();
	return true;
}

TOptional<TArray<SatisDash::FResourceNodeReading>> SatisDash::FGameReader::StepResourceNodeScan(int32 MaxActors)
{
	const double StepStart = FPlatformTime::Seconds();
	// The node list is kept bounded whatever the world holds (twice the map's own cap).
	const int32 MaxFound = 2 * MaxResourceNodes;
	int32 Budget = MaxActors;
	while (Budget > 0 && ScanLevel < ScanLevels.Num())
	{
		const ULevel* Level = ScanLevels[ScanLevel].Get();
		if (Level == nullptr || ScanIndex >= Level->Actors.Num())
		{
			++ScanLevel;
			ScanIndex = 0;
			continue;
		}
		AActor* Actor = Level->Actors[ScanIndex++];
		--Budget;
		++ScanActors;
		const AFGResourceNode* Node = Cast<AFGResourceNode>(Actor);
		if (Node == nullptr || ScanFound.Num() >= MaxFound)
		{
			continue;
		}
		FResourceNodeReading Reading;
		// Fracking cores are not AFGResourceNode, and deposits are: the type decides what the map draws (FGResourceNodeBase.h:24).
		switch (Node->GetResourceNodeType())
		{
		case EResourceNodeType::Node: Reading.Kind = ENodeKind::Node; break;
		case EResourceNodeType::FrackingSatellite: Reading.Kind = ENodeKind::FrackingSatellite; break;
		case EResourceNodeType::Geyser: Reading.Kind = ENodeKind::Geyser; break;
		default: continue;
		}
		switch (Node->GetResourcePurity())
		{
		case RP_Inpure: Reading.Purity = ENodePurity::Impure; break;
		case RP_Normal: Reading.Purity = ENodePurity::Normal; break;
		case RP_Pure: Reading.Purity = ENodePurity::Pure; break;
		default: continue;
		}
		Reading.Resource = Node->GetResourceName().ToString();
		const FVector Location = Node->GetActorLocation();
		Reading.XCm = Location.X;
		Reading.YCm = Location.Y;
		Reading.bExploited = Node->IsOccupied();
		ScanFound.Add(MoveTemp(Reading));
	}
	const double Spent = FPlatformTime::Seconds() - StepStart;
	++ScanSteps;
	ScanTotal += Spent;
	ScanLongestStep = FMath::Max(ScanLongestStep, Spent);
	if (ScanLevel < ScanLevels.Num())
	{
		return {};
	}
	// Counts and times only: no node names (they are not personal, but the rule is no data in logs).
	UE_LOG(LogSatisDash, Display, TEXT("Resource node scan: %d actors in %d steps, %d nodes, longest step %.2f ms, total %.2f ms"),
		ScanActors, ScanSteps, ScanFound.Num(), ScanLongestStep * 1000.0, ScanTotal * 1000.0);
	return MoveTemp(ScanFound);
}
```

(`Level->Actors[ScanIndex++]` is a `TObjectPtr<AActor>`; the assignment to `AActor*` is its implicit conversion. `GetResourceName()` returns `FText`.)

- [ ] **Step 7: api-sources rows and the provenance check**

Add to `api-sources.md` (lines read from the pinned checkout; look each up again):

| Symbol | Header | Source |
| --- | --- | --- |
| Resources/FGResourceNode.h | Source/FactoryGame/Public/Resources/FGResourceNode.h:1 | header only |
| AFGResourceNode | Source/FactoryGame/Public/Resources/FGResourceNode.h:66 | header only (AFGResourceDeposit, FGResourceDeposit.h:15, and AFGResourceNodeGeyser, FGResourceNodeGeyser.h:15, derive from it; fracking cores do not) |
| AFGResourceNode::GetResourcePurity | Source/FactoryGame/Public/Resources/FGResourceNode.h:113 | header only |
| RP_Inpure | Source/FactoryGame/Public/Resources/FGResourceNode.h:28 | header only (EResourcePurity :26 is a plain enum; the enumerator is spelled RP_Inpure, DisplayName "Impure") |
| RP_Normal | Source/FactoryGame/Public/Resources/FGResourceNode.h:29 | header only |
| RP_Pure | Source/FactoryGame/Public/Resources/FGResourceNode.h:30 | header only |
| Resources/FGResourceNodeBase.h | Source/FactoryGame/Public/Resources/FGResourceNodeBase.h:1 | header only |
| EResourceNodeType | Source/FactoryGame/Public/Resources/FGResourceNodeBase.h:24 | header only |
| EResourceNodeType::Node | Source/FactoryGame/Public/Resources/FGResourceNodeBase.h:26 | header only |
| EResourceNodeType::FrackingSatellite | Source/FactoryGame/Public/Resources/FGResourceNodeBase.h:27 | header only |
| EResourceNodeType::Geyser | Source/FactoryGame/Public/Resources/FGResourceNodeBase.h:29 | header only |
| AFGResourceNodeBase::GetResourceNodeType | Source/FactoryGame/Public/Resources/FGResourceNodeBase.h:176 | header only |
| AFGResourceNodeBase::GetResourceName | Source/FactoryGame/Public/Resources/FGResourceNodeBase.h:180 | header only (FText, "Localized name") |
| AFGResourceNodeBase::IsOccupied | Source/FactoryGame/Public/Resources/FGResourceNodeBase.h:140 | header only |
| Engine/Level.h | Engine/Source/Runtime/Engine/Classes/Engine/Level.h:1 | header only |
| ULevel | Engine/Source/Runtime/Engine/Classes/Engine/Level.h:422 | header only |
| ULevel::Actors | Engine/Source/Runtime/Engine/Classes/Engine/Level.h:432 | header only (public) |
| UWorld::GetLevels | Engine/Source/Runtime/Engine/Classes/Engine/World.h:3443 | header only |
| AActor::GetActorLocation | Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h:2505 | header only (the survey's :1580 is K2_GetActorLocation) |

Run `npm run harness -- check-api`; add whatever else it reports (for example `FPlatformTime::Seconds` and `TWeakObjectPtr` already have rows; `FVector` may need one). Update `docs/native-readers.md`: Resource nodes row note "native reader built; scans 2,000 actors per tick, once per 30 minutes".

- [ ] **Step 8: Build, test, check**

Run: the `test-mod.ps1` command, then `npm run harness -- judge-mod-test <tmp>\mod-test --min 149`, then `check-api` and `check-provenance`.
Expected: **149** tests pass (145 + `NativeNodes.Golden`, `NativeNodes.Rules`, `FrmWorld.Native`, `Driver.NativeNodes`); the existing `FrmResourceNodes.*`, `FrmWorld.*` and `Driver.World*` tests still pass (they guard the refactors); `check-api` at `0 missing, 0 bad rows`; `check-provenance: 3 native readers, 0 problems`. Read the server log of the first `native-parity` run for the scan line: it is the cost evidence.

- [ ] **Step 9: The floor, then commit**

Propose the floor to the owner (do not change it unasked): in `.github/workflows/mod-test.yml`, `--min 142` becomes `--min 149`. Make the edit only if the owner has said so in this task's dispatch; otherwise leave it and say so in the PR.

```bash
git add -A mod api-sources.md docs/native-readers.md
git commit -m "Native resource nodes reader and the sliced scan (ADR-0058 S1, R4)"
```

---

### Task 4: The combined native parity run (`native-parity`)

**Files:**
- Create: `harness/src/frm-native.ts`, `harness/test/frm-native.test.ts`
- Create: `runs/native-parity/pass.ts`, `harness/test/native-parity-pass.test.ts`
- Create: `harness/src/core-runs/native-parity.ts`
- Modify: `harness/src/recorder.ts`, `harness/test/recorder.test.ts`, `harness/src/core-run.ts`, `harness/src/core-runs/index.ts`, `.github/workflows/core-run.yml`, `docs/runner.md`

**Interfaces:**
- Consumes: Task 2 and 3's config keys `playersSource`, `resourceNodesSource`; the existing `enrolPhase`, `startFrmPoller`, `parseGetPowerFull`, `pickBracket`, `startRecorder`, `sessionHash`, `PARITY_POWER_TOLERANCE` (`runs/parity/pass.ts`), `FRM_BRACKET_GAP_MS` (`runs/enrol/pass.ts`), and `ResourceNodesLayerDataSchema` (`harness/contract/src/map.ts`).
- Produces:
  - `RecorderOptions.keepWorldData?: string[]` → `RecordedCall.worldData?: unknown[]` (the posted `data` array of those layers only).
  - `CoreRunSpec.modConfig?: Partial<NativeSources>`, merged into every `ctx.writeConfig`; `captures["mod-config.json"]` records it. `NativeSources = { playersSource: "bridge" | "native"; resourceNodesSource: "bridge" | "native" }`; `NATIVE_SOURCES`.
  - Run id `native-parity`; captures `native-parity.json` and `readers.json` (`{ power, players, resourceNodes }`, each a `Verdict`).
  - `Pass.judgeReaders(c: NativeParity): ReaderVerdicts`, `Pass.evaluate`.

**What the run judges (Decision 3, row by row), each reader separately:**

| Reader | Oracle in the same minute | Pass condition (from the ADR, unchanged) |
| --- | --- | --- |
| R0 power | FRM `getPower`, read back to back (250 ms) by the harness; each mod sample bracketed by the last read before it and the first after, each within 1,000 ms (the enrol run's rule) | every field, per circuit group, within 3% of the bracket (`PARITY_POWER_TOLERANCE`); group ids exact; fuse equal to a neighbour; at least 288 compared rounds (`ceil(1800 / 5 x 0.8)`, the parity run's `NEED_PAIRS`) |
| R1 players | FRM `getPlayer` every 2 s, names hashed with `sessionHash`; each mod round bracketed within 5 s | the sorted (hash, online) lists equal on every round where FRM's two neighbours agree; at least 288 compared rounds; `available` true |
| R4 resource nodes | FRM `getResourceNode` every 60 s (and right after each mod post), mapped to the contract form by the harness's own TypeScript mapper | the mod's posted layer valid against the contract and equal, as a sorted list, to the FRM sample on at least one side of the post (before or after, within 120 s); every difference listed; at least one accepted post in the window |

A reader failing does not change another reader's verdict (`readers.json`); the run as a whole is red if any reader is, because the owner's switch PRs read `readers.json` and `result.json` together. Fields the world never exercises (a fuse that never trips, no batteries, no merged group, nobody online, no exploited node) are compared but reported in the reasons as `NOT EXERCISED`.

- [ ] **Step 1: Write the failing tests for the FRM side**

Create `harness/test/frm-native.test.ts`:

```ts
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { ResourceNodesLayerDataSchema } from "../contract/src/map";
import { canonNodes, countMergedGroups, mapFrmResourceNodes, mapNodeType, playerKeys } from "../src/frm-native";
import { sessionHash } from "../src/status";

const FIXTURES = new URL("../../mod/SatisDashAgent/Source/SatisDashCoreTests/Fixtures/", import.meta.url);
const load = (name: string) => JSON.parse(readFileSync(fileURLToPath(new URL(name, FIXTURES)), "utf8"));

describe("mapFrmResourceNodes", () => {
  it("reproduces the bridge's golden from the bridge's FRM capture (the harness oracle and the mod agree on the form)", () => {
    expect(mapFrmResourceNodes(load("frm-getResourceNode.json"))).toEqual(load("frm-getResourceNode.expected.json"));
  });

  it("produces a layer the pinned contract accepts", () => {
    const r = ResourceNodesLayerDataSchema.safeParse(mapFrmResourceNodes(load("frm-getResourceNode.json")));
    expect(r.success ? [] : r.error.issues).toEqual([]);
  });

  it("refuses a record that is not what the Node agent's schema accepts", () => {
    expect(() => mapFrmResourceNodes([{ Name: "x", Purity: "Pure", NodeType: "Node", Exploited: "no", location: { x: 0, y: 0 } }])).toThrow();
  });

  it("drops a node past 1,000,000 m and never yields -0", () => {
    const out = mapFrmResourceNodes([
      { Name: "far", Purity: "Pure", NodeType: "Node", Exploited: false, location: { x: 1e12, y: 0 } },
      { Name: "half", Purity: "Pure", NodeType: "Node", Exploited: false, location: { x: -50, y: 50 } },
    ]);
    expect(out).toHaveLength(1);
    expect(Object.is(out[0]!.x, 0)).toBe(true);
    expect(out[0]!.y).toBe(1);
  });
});

describe("mapNodeType", () => {
  it("names the three known kinds and camel-cases the rest on JavaScript's \\s", () => {
    expect(mapNodeType("Node")).toBe("node");
    expect(mapNodeType("Fracking Satellite")).toBe("frackingSatellite");
    expect(mapNodeType("Geyser")).toBe("geyser");
    expect(mapNodeType("Some New TYPE")).toBe("someNewType");
    expect(mapNodeType("some  new   TYPE")).toBe("someNewType");
  });
});

describe("canonNodes", () => {
  it("is order-independent", () => {
    const a = { type: "A", purity: "pure", nodeType: "node", x: 1, y: 2, exploited: false };
    const b = { ...a, x: 0 };
    expect(canonNodes([a, b])).toEqual(canonNodes([b, a]));
  });
});

describe("playerKeys", () => {
  it("keeps a hash and online, never the name, sorted", () => {
    const keys = playerKeys([{ Name: "Zed", Online: false, Location: { x: 1 } }, { Name: "Amy", Online: true }]);
    expect(keys).toEqual([`${sessionHash("Amy")}:true`, `${sessionHash("Zed")}:false`].sort());
    expect(keys.join()).not.toContain("Amy");
  });
  it("refuses a record with no Online", () => {
    expect(() => playerKeys([{ Name: "x" }])).toThrow();
  });
});

describe("countMergedGroups", () => {
  it("counts groups with more than one circuit", () => {
    expect(countMergedGroups([{ AssociatedCircuits: [0, 3] }, { AssociatedCircuits: [1] }])).toBe(1);
  });
});
```

Create `harness/src/frm-native.ts`:

```ts
// FRM's HTTP output as the native-parity run's oracle (ADR-0058 Decision 3): what the harness reads from the test server's
// FRM, and the forms the judge compares. No FRM source is read; the shapes are the bridge's own fixtures and captures.
import { z } from "zod";
import { sessionHash } from "./status";

const LIMIT_METRES = 1_000_000;
const MAX_NODES = 5000;

/** A resourceNodes item as the contract has it (map.ts ResourceNodeSchema). */
export interface CanonNode { type: string; purity: string; nodeType: string; x: number; y: number; exploited: boolean }

const FrmNodeSchema = z.object({
  Name: z.string(),
  Purity: z.string(),
  NodeType: z.string(),
  Exploited: z.boolean(),
  location: z.object({ x: z.number(), y: z.number() }),
});

/** Math.round(units / 100), with -0 written 0 as JSON.stringify would. */
const metres = (units: number): number => {
  const m = Math.round(units / 100);
  return m === 0 ? 0 : m;
};
const cut = (s: string, n: number) => (s.length > n ? s.slice(0, n) : s);

/** nodeType.ts mapNodeType: the three known kinds by name, anything else camel-cased on JavaScript's \s. */
export function mapNodeType(raw: string): string {
  if (raw === "Node") return "node";
  if (raw === "Fracking Satellite") return "frackingSatellite";
  if (raw === "Geyser") return "geyser";
  return raw
    .split(/\s+/)
    .filter((w) => w.length > 0)
    .map((w, i) => (i === 0 ? w.toLowerCase() : w[0]!.toUpperCase() + w.slice(1).toLowerCase()))
    .join("");
}

/** getResourceNode's body to the contract's items (the mapper the bridge's golden pins), in FRM's order. Throws on an invalid record. */
export function mapFrmResourceNodes(body: unknown): CanonNode[] {
  const out: CanonNode[] = [];
  for (const r of z.array(FrmNodeSchema).parse(body)) {
    const x = metres(r.location.x);
    const y = metres(r.location.y);
    if (Math.abs(x) > LIMIT_METRES || Math.abs(y) > LIMIT_METRES) continue;
    out.push({ type: cut(r.Name, 200), purity: cut(r.Purity.toLowerCase(), 40), nodeType: cut(mapNodeType(r.NodeType), 40), x, y, exploited: r.Exploited });
  }
  return out.slice(0, MAX_NODES);
}

/** One string per node, sorted: two layers are equal when these lists are. */
export function canonNodes(nodes: CanonNode[]): string[] {
  return nodes.map((n) => JSON.stringify([n.type, n.purity, n.nodeType, n.x, n.y, n.exploited])).sort();
}

const FrmPlayerSchema = z.object({ Name: z.string(), Online: z.boolean() });
/** getPlayer's body to "<hash of name>:<online>", sorted. Names are personal data (ADR-0029): only the hash leaves this function. */
export function playerKeys(body: unknown): string[] {
  return z.array(FrmPlayerSchema).parse(body).map((p) => `${sessionHash(p.Name)}:${p.Online}`).sort();
}

const GroupsSchema = z.array(z.object({ AssociatedCircuits: z.array(z.number()) }));
/** How many of getPower's groups hold more than one circuit (evidence on whether the world exercises the group merge). */
export function countMergedGroups(body: unknown): number {
  return GroupsSchema.parse(body).filter((g) => g.AssociatedCircuits.length > 1).length;
}

/** One successful read of an FRM endpoint and when it ran (ms since the epoch; the harness and the server share a clock). */
export interface Sample<T> { startedAt: number; endedAt: number; value: T }

/** Reads on a fixed pause until stopped; take() reads once now (a failed read counts and is not kept). */
export function startSampler<T>(
  read: () => Promise<T>,
  opts: { intervalMs: number; now: () => number; sleep: (ms: number) => Promise<void> },
): { samples: Sample<T>[]; counts(): { ok: number; failed: number }; take(): Promise<void>; stop(): void } {
  const samples: Sample<T>[] = [];
  let running = true;
  let ok = 0;
  let failed = 0;
  const take = async () => {
    const startedAt = opts.now();
    try {
      const value = await read();
      samples.push({ startedAt, endedAt: opts.now(), value });
      ok++;
    } catch {
      failed++;
    }
  };
  void (async () => {
    while (running) {
      await take();
      await opts.sleep(opts.intervalMs);
    }
  })();
  return { samples, counts: () => ({ ok, failed }), take, stop: () => void (running = false) };
}
```

- [ ] **Step 2: The recorder option**

Add to `harness/test/recorder.test.ts` a test next to the existing world-post test (follow its helpers: it starts an upstream, a recorder, posts, and reads `entries`): with `startRecorder(upstream, { keepWorldData: ["resourceNodes"] })`, a POST to `/agent/v1/world/resourceNodes` with `{"observedAt":"t","data":[{"type":"Iron Ore"}]}` records `entry.world = { layer: "resourceNodes", items: 1, bytes: <n> }` **and** `entry.worldData` equal to `[{ type: "Iron Ore" }]`; a POST to `/agent/v1/world/rails` with the same recorder keeps `world` but has no `worldData`; and a recorder started with no options keeps no `worldData` for either. Run it and see it fail.

In `harness/src/recorder.ts`: add to `RecordedCall`:

```ts
  /** The posted `data` array, kept only for the layers RecorderOptions.keepWorldData names (never for snapshots). */
  worldData?: unknown[];
```

add to `RecorderOptions`:

```ts
  /** World layers whose posted `data` array is kept in `worldData` (names a layer carries must not be personal data: resourceNodes carries resource names). */
  keepWorldData?: readonly string[];
```

and in `startRecorder` add `const keepWorld = new Set(opts.keepWorldData ?? []);` beside `trimFor`; in the world branch replace the `if (w !== undefined && Array.isArray(data)) entry.world = ...;` statement with:

```ts
          if (w !== undefined && Array.isArray(data)) {
            const layer = WORLD.exec(path)![1]!;
            entry.world = { layer, items: data.length, bytes: w.size };
            if (keepWorld.has(layer)) entry.worldData = data;
          }
```

Run: `npx vitest run test/recorder.test.ts --reporter=dot` → PASS.

- [ ] **Step 3: `modConfig` in the core run**

In `harness/src/core-run.ts`:

```ts
/** The sources the harness writes into the mod's config for a run (ADR-0058 Decision 4): the hidden per-reader override. */
export interface NativeSources { playersSource: "bridge" | "native"; resourceNodesSource: "bridge" | "native" }
export const NATIVE_SOURCES: NativeSources = { playersSource: "native", resourceNodesSource: "native" };
```

add `"native-parity"` to `CORE_RUN_IDS` after `"manual"` (the schema `z.enum(CORE_RUN_IDS)` picks it up; Task 5 adds `"tick-cost-native"`); in `CoreRunSpec` add

```ts
  /** Keys added to every config the run writes (the mod's per-reader source override, ADR-0058). Recorded in mod-config.json. */
  modConfig?: Partial<NativeSources>;
```

change `CoreContext.writeConfig`'s parameter comment/type to keep `values: { backendUrl: string; enrollmentCode: string }` (unchanged), and in the context built in `runCore` replace the `writeModConfig(...)` call with:

```ts
          const frmPort = deps.config.frmBaseUrl ? Number(new URL(deps.config.frmBaseUrl).port) : undefined;
          writeModConfig(deps.config.serverDir, MOD, { ...v, ...(spec.modConfig ?? {}), ...(frmPort ? { frmPort } : {}) });
```

(and keep the preceding comment). Where `captures` is created for the run, add after it: `if (spec.modConfig) captures["mod-config.json"] = { ...spec.modConfig };`. Add a test to `harness/test/core-run.test.ts` next to "writes frmPort into the mod config…": a spec with `modConfig: { playersSource: "native" }` whose drive calls `ctx.writeConfig(...)` writes a config file containing `"playersSource": "native"` (read it back with `readFileSync(modConfigPath(serverDir, MOD))` inside the drive, as the neighbouring test does) and the result's captures hold `mod-config.json`; and a spec without `modConfig` writes no `playersSource`. In `harness/src/core-runs/index.ts` register the spec (Step 7). Add to the native-parity test file (Step 4) a drift guard: the key names in `NATIVE_SOURCES` appear in `mod/.../SatisDashCore/Private/Config.cpp` as `TEXT("playersSource")` and `TEXT("resourceNodesSource")`.

- [ ] **Step 4: The judge and its tests**

Create `runs/native-parity/pass.ts`:

```ts
// The combined native-reader parity run for sprint S1 (ADR-0058, Decision 3 and the owner's answers of 2026-10-25): the mod,
// with its players and resource-node readers set to "native" (power is native already), runs against the test server for
// 30 minutes while the harness reads FRM's own endpoints as a black box in the same minutes. Each reader is judged on its
// own, against Decision 3's tolerances, which the owner approved as written; a reader that fails stays on the bridge and
// does not change another reader's verdict (readers.json carries the three verdicts; result.json is red if any is).
//
//  - R0 power: every field per circuit group within PARITY_POWER_TOLERANCE (3%, runs/parity/pass.ts) of FRM's getPower reads
//    just before and just after the mod's sample (the enrol run's bracket, FRM_BRACKET_GAP_MS); group ids and the fuse exact;
//    at least NEED_ROUNDS compared rounds. Fields the world never exercises are reported, not failed (open question 1 of
//    docs/plans/2026-10-25-native-readers-s1.md).
//  - R1 players: the list of (hash of name, online) equal to FRM's getPlayer on every round where FRM's samples either side
//    agree; at least NEED_ROUNDS compared rounds. Names never enter evidence (ADR-0029).
//  - R4 resource nodes: the layer the mod posted is valid against the pinned contract and equals, as a sorted list, FRM's
//    getResourceNode (mapped to the contract's form by frm-native.ts) at the sample before or after the post, within
//    NODES_GAP_MS; every difference is listed; at least one accepted post.
// Nothing here changes a pass condition of another run.
import type { CoreEvidence } from "../../harness/src/core-run";
import { ResourceNodesLayerDataSchema } from "../../harness/contract/src/map";
import type { FrmCircuitGroup } from "../../harness/src/frm";
import { canonNodes, type CanonNode } from "../../harness/src/frm-native";
import type { Verdict } from "../../harness/src/verdict";
import type { Circuit, Posted, ServerState } from "../parity/pass";
import { PARITY_POWER_TOLERANCE } from "../parity/pass";

export const requirements = ["native-parity", "R0-power", "R1-players", "R4-resource-nodes"];
export const WINDOW_MINUTES = 30;
const STATUS_SECONDS = 5; // FCadence::StatusSeconds
export const NEED_ROUNDS = Math.ceil(((WINDOW_MINUTES * 60) / STATUS_SECONDS) * 0.8);
/** Decision 3: power within 3%, the parity run's bar. */
export const POWER_TOLERANCE = PARITY_POWER_TOLERANCE;
export const PLAYERS_GAP_MS = 5_000;
export const NODES_GAP_MS = 120_000;
const SHOW = 10;

export interface FrmBracket<T> { startedAt: string; endedAt: string; value: T }
/** One accepted mod snapshot's power, with FRM's last read before it and first after it (null: none within the gap). */
export interface PowerRound { observedAt: string; circuits: Circuit[]; before: FrmCircuitGroup[] | null; after: FrmCircuitGroup[] | null }
/** One accepted mod snapshot's players part (names already hashed by the recorder); keys null when the part was absent. */
export interface PlayerRound { observedAt: string; available: boolean | null; keys: string[] | null }
export interface NodePost { at: string; status: number; data: unknown }
export interface NativeParity {
  windowStartedAt: string | null;
  windowEndedAt: string | null;
  accepted: number;
  unrecorded: number;
  refused: Posted[];
  state: ServerState | null;
  power: { rounds: PowerRound[]; frm: { ok: number; failed: number }; mergedGroupsMax: number | null };
  players: { rounds: PlayerRound[]; frmSamples: FrmBracket<string[]>[]; frm: { ok: number; failed: number } };
  nodes: { posts: NodePost[]; frmSamples: FrmBracket<string>[]; distinct: Record<string, CanonNode[]>; frm: { ok: number; failed: number } };
}
export interface ReaderVerdicts { power: Verdict; players: Verdict; resourceNodes: Verdict }

const ms = (iso: string) => Date.parse(iso);
const fmt = (n: number) => String(Math.round(n * 1000) / 1000);

/** The sample that ended by t and the one that started at or after t, each within gapMs. */
function around<T>(samples: FrmBracket<T>[], t: number, gapMs: number): { before: FrmBracket<T> | null; after: FrmBracket<T> | null } {
  const before = samples.filter((s) => ms(s.endedAt) <= t && t - ms(s.endedAt) <= gapMs).at(-1) ?? null;
  const after = samples.find((s) => ms(s.startedAt) >= t && ms(s.startedAt) - t <= gapMs) ?? null;
  return { before, after };
}

// ---------------------------------------------------------------------------------------------------------------- R0 power

const FIELD_PAIRS = [
  ["productionMW", "production"], ["consumptionMW", "consumption"], ["capacityMW", "capacity"], ["maxConsumptionMW", "maxConsumption"],
  ["batteryCapacityMWh", "batteryCapacity"], ["batteryPercent", "batteryPercent"], ["batteryDifferentialMW", "batteryDifferential"],
] as const satisfies ReadonlyArray<readonly [keyof Circuit, keyof FrmCircuitGroup]>;

const inBracket = (v: number, lo: number, hi: number) => {
  const tol = POWER_TOLERANCE * Math.max(Math.abs(lo), Math.abs(hi), 1);
  return lo - tol <= v && v <= hi + tol;
};
const idsOf = (ids: number[]) => [...ids].sort((a, b) => a - b).join(",");

export function judgePower(p: NativeParity["power"]): Verdict {
  const reasons: string[] = [];
  let compared = 0;
  let batteryRounds = 0;
  let fuseRounds = 0;
  let missing = 0;
  for (const r of p.rounds) {
    if (!r.before || !r.after) {
      missing++;
      if (missing <= SHOW) reasons.push(`${r.observedAt}: no FRM power read ${r.before ? "after" : "before"} the mod's sample within the gap`);
      continue;
    }
    const seen = new Map<number, Circuit>();
    for (const c of r.circuits) {
      if (seen.has(c.circuitGroupId)) reasons.push(`${r.observedAt}: the mod sent group ${c.circuitGroupId} twice`);
      seen.set(c.circuitGroupId, c);
    }
    if (seen.size === 0) continue;
    const modIds = idsOf([...seen.keys()]);
    // Only a neighbour whose group ids match brackets the values (the parity run's rule).
    const neighbours = [r.before, r.after].filter((g) => idsOf(g.map((x) => x.groupId)) === modIds);
    compared++;
    if (neighbours.length === 0) {
      reasons.push(`${r.observedAt}: circuit groups mod ${modIds} vs FRM ${idsOf(r.before.map((x) => x.groupId))} before, ${idsOf(r.after.map((x) => x.groupId))} after`);
      continue;
    }
    for (const [id, mod] of seen) {
      const frm = neighbours.map((g) => g.find((x) => x.groupId === id)!);
      for (const [modKey, frmKey] of FIELD_PAIRS) {
        const values = frm.map((g) => g[frmKey]);
        const lo = Math.min(...values);
        const hi = Math.max(...values);
        if (!inBracket(mod[modKey], lo, hi)) reasons.push(`${r.observedAt}: group ${id} ${modKey} mod ${fmt(mod[modKey])} vs FRM ${lo === hi ? fmt(lo) : `${fmt(lo)}..${fmt(hi)}`}`);
      }
      if (!frm.some((g) => g.fuseTriggered === mod.fuseTriggered)) reasons.push(`${r.observedAt}: group ${id} fuseTriggered mod ${mod.fuseTriggered} vs FRM ${frm[0]!.fuseTriggered}`);
      if (mod.batteryCapacityMWh > 0) batteryRounds++;
      if (mod.fuseTriggered) fuseRounds++;
    }
  }
  if (missing > SHOW) reasons.push(`... ${missing - SHOW} more rounds had no FRM neighbour`);
  if (compared < NEED_ROUNDS) reasons.push(`power compared in only ${compared} rounds, need ${NEED_ROUNDS}`);
  if (p.frm.failed > 0) reasons.push(`${p.frm.failed} of the harness's FRM getPower reads failed (${p.frm.ok} succeeded)`);
  if (reasons.length) return { pass: false, reasons: reasons.slice(0, 30) };
  const never = [
    batteryRounds === 0 ? "batteryCapacityMWh/batteryPercent/batteryDifferentialMW (no battery in any compared round)" : null,
    fuseRounds === 0 ? "fuseTriggered (never true)" : null,
    (p.mergedGroupsMax ?? 0) === 0 ? "the circuit-group merge (FRM never reported a group with more than one circuit)" : null,
  ].filter((x): x is string => x !== null);
  return {
    pass: true,
    reasons: [
      `${compared} rounds within ${POWER_TOLERANCE * 100}% of FRM's neighbouring reads; every compared field matches`,
      ...(never.length ? [`NOT EXERCISED by this world, compared only as zero/false/single: ${never.join("; ")}`] : []),
    ],
  };
}

// -------------------------------------------------------------------------------------------------------------- R1 players

export function judgePlayers(p: NativeParity["players"]): Verdict {
  const reasons: string[] = [];
  let compared = 0;
  let skipped = 0;
  let unpaired = 0;
  let onlineSeen = 0;
  let someoneListed = 0;
  for (const r of p.rounds) {
    if (r.keys === null || r.available !== true) {
      reasons.push(`${r.observedAt}: the mod's players part was ${r.keys === null ? "absent" : "not available"}`);
      continue;
    }
    const { before, after } = around(p.frmSamples, ms(r.observedAt), PLAYERS_GAP_MS);
    if (!before || !after) {
      unpaired++;
      continue;
    }
    if (before.value.join() !== after.value.join()) {
      skipped++; // somebody joined or left between FRM's two reads: no fair comparison
      continue;
    }
    compared++;
    if (before.value.length > 0) someoneListed++;
    if (before.value.some((k) => k.endsWith(":true"))) onlineSeen++;
    if (r.keys.join() !== before.value.join()) reasons.push(`${r.observedAt}: players mod [${r.keys.join(",")}] vs FRM [${before.value.join(",")}] (hash:online)`);
  }
  if (compared < NEED_ROUNDS) reasons.push(`players compared in only ${compared} rounds (${unpaired} had no FRM read within ${PLAYERS_GAP_MS} ms, ${skipped} fell across a join or leave), need ${NEED_ROUNDS}`);
  if (p.frm.failed > 0) reasons.push(`${p.frm.failed} of the harness's FRM getPlayer reads failed (${p.frm.ok} succeeded)`);
  if (reasons.length) return { pass: false, reasons: reasons.slice(0, 30) };
  const never = [someoneListed === 0 ? "any listed player (both sides empty)" : null, onlineSeen === 0 ? "any online player" : null].filter((x): x is string => x !== null);
  return {
    pass: true,
    reasons: [
      `${compared} rounds, the (hash, online) lists equal FRM's`,
      ...(never.length ? [`NOT EXERCISED by this world: ${never.join("; ")}`] : []),
    ],
  };
}

// ------------------------------------------------------------------------------------------------------ R4 resource nodes

const diff = (mod: string[], frm: string[]) => {
  const count = (xs: string[]) => xs.reduce((m, x) => m.set(x, (m.get(x) ?? 0) + 1), new Map<string, number>());
  const a = count(mod);
  const b = count(frm);
  const missing: string[] = [];
  const extra: string[] = [];
  for (const [k, n] of b) for (let i = a.get(k) ?? 0; i < n; i++) missing.push(k);
  for (const [k, n] of a) for (let i = b.get(k) ?? 0; i < n; i++) extra.push(k);
  return { missing, extra };
};

export function judgeNodes(n: NativeParity["nodes"]): Verdict {
  const reasons: string[] = [];
  let compared = 0;
  let exploited = 0;
  let items = 0;
  if (n.posts.length === 0) reasons.push("the mod posted no resourceNodes layer in the window");
  for (const post of n.posts) {
    const parsed = ResourceNodesLayerDataSchema.safeParse(post.data);
    if (!parsed.success) {
      reasons.push(`${post.at}: the posted layer is not valid against the pinned contract (${parsed.error.issues[0]?.message ?? "?"})`);
      continue;
    }
    if (parsed.data.length === 0) {
      reasons.push(`${post.at}: the posted layer is empty`);
      continue;
    }
    const { before, after } = around(n.frmSamples, ms(post.at), NODES_GAP_MS);
    if (!before && !after) {
      reasons.push(`${post.at}: no FRM getResourceNode sample within ${NODES_GAP_MS / 1000} s`);
      continue;
    }
    compared++;
    items = parsed.data.length;
    exploited = parsed.data.filter((x) => x.exploited).length;
    const mod = canonNodes(parsed.data as CanonNode[]);
    const sides = [before, after].filter((s): s is FrmBracket<string> => s !== null);
    const lists = sides.map((s) => canonNodes(n.distinct[s.value] ?? []));
    if (lists.some((l) => l.join("\n") === mod.join("\n"))) continue;
    const d = diff(mod, lists[0]!);
    reasons.push(`${post.at}: the layer differs from FRM's: ${d.missing.length} in FRM and not in the mod, ${d.extra.length} in the mod and not in FRM`);
    if (d.missing.length) reasons.push(`  in FRM only (first ${SHOW}): ${d.missing.slice(0, SHOW).join(" ")}`);
    if (d.extra.length) reasons.push(`  in the mod only (first ${SHOW}): ${d.extra.slice(0, SHOW).join(" ")}`);
  }
  if (n.frm.failed > 0 && n.frmSamples.length === 0) reasons.push("no FRM getResourceNode sample succeeded");
  if (reasons.length) return { pass: false, reasons: reasons.slice(0, 30) };
  return {
    pass: true,
    reasons: [
      `${compared} posted layer(s) valid against the contract and equal to FRM's (${items} items, ${exploited} exploited)`,
      ...(exploited === 0 ? ["NOT EXERCISED by this world: exploited true (no occupied node)"] : []),
    ],
  };
}
```

Then append the shared part to the same file:

```ts
// ------------------------------------------------------------------------------------------------------------- the run

/** The three verdicts, each computed from its own evidence only. */
export function judgeReaders(c: NativeParity): ReaderVerdicts {
  return { power: judgePower(c.power), players: judgePlayers(c.players), resourceNodes: judgeNodes(c.nodes) };
}

/** What must hold for any verdict to mean anything: the window, the mod enrolled, nothing refused or unreadable. */
function runReasons(c: NativeParity, sources: unknown): string[] {
  const out: string[] = [];
  if (!c.windowStartedAt || !c.windowEndedAt) out.push(`the ${WINDOW_MINUTES}-minute window was not recorded`);
  else {
    const minutes = (ms(c.windowEndedAt) - ms(c.windowStartedAt)) / 60_000;
    if (!(minutes >= WINDOW_MINUTES)) out.push(`the window ran ${minutes.toFixed(1)} minutes, need ${WINDOW_MINUTES}`);
  }
  const s = sources as { playersSource?: string; resourceNodesSource?: string } | undefined;
  if (s?.playersSource !== "native" || s?.resourceNodesSource !== "native") out.push("the run did not write playersSource and resourceNodesSource as native into the mod's config");
  if (!c.state) out.push("the mod's server state was not read");
  else {
    if (!c.state.codeConsumed) out.push("the mod's code was not consumed");
    if (!c.state.lastSeen) out.push("the backend never saw the mod's authenticated call");
  }
  if (c.refused.length) out.push(`the backend didn't accept ${c.refused.length} of the mod's posts (answers ${c.refused.map((p) => p.status).join(", ")})`);
  if (c.unrecorded) out.push(`${c.unrecorded} of the mod's accepted snapshots couldn't be read by the recorder`);
  return out;
}

export function evaluate(e: CoreEvidence): Verdict {
  const c = e.captures["native-parity.json"] as NativeParity | undefined;
  if (!c) return { pass: false, reasons: ["no native-parity capture"] };
  const v = judgeReaders(c);
  const run = runReasons(c, e.captures["mod-config.json"]);
  const names: Array<[string, Verdict]> = [["R0 power", v.power], ["R1 players", v.players], ["R4 resource nodes", v.resourceNodes]];
  const lines = names.flatMap(([name, x]) => x.reasons.map((r) => `${name} ${x.pass ? "PASS" : "FAIL"}: ${r}`));
  return { pass: run.length === 0 && names.every(([, x]) => x.pass), reasons: [...run, ...lines] };
}
```

Create `harness/test/native-parity-pass.test.ts`. The test builds a synthetic `NativeParity` and checks, one `it` each: (1) a clean capture passes and each reader line starts with `R0 power PASS`, `R1 players PASS`, `R4 resource nodes PASS`; (2) a power value 5% off fails only `power`, `players` and `resourceNodes` still pass in `judgeReaders`; (3) a players mismatch fails only `players`; (4) a node difference lists it as `in FRM only` / `in the mod only` and fails only `resourceNodes`; (5) fewer than `NEED_ROUNDS` compared power rounds fails with the count; (6) `reports fields the world never exercised`: a clean capture with no battery, no fuse, `mergedGroupsMax: 0`, nobody listed passes and the reasons contain `NOT EXERCISED` for power and players; (7) a run whose `mod-config.json` is missing the native sources is red even when all three readers pass; (8) the drift guard from Step 3. Use these builders:

```ts
import { readFileSync } from "node:fs";
import { describe, expect, it } from "vitest";
import { NATIVE_SOURCES } from "../src/core-run";
import type { CoreEvidence } from "../src/core-run";
import { canonNodes, type CanonNode } from "../src/frm-native";
import * as Pass from "../../runs/native-parity/pass";

const T0 = Date.parse("2026-10-26T12:00:00.000Z");
const iso = (ms: number) => new Date(ms).toISOString();
const group = (id: number, over: Partial<Record<string, number | boolean>> = {}) => ({
  groupId: id, production: 100, consumption: 80, capacity: 120, maxConsumption: 110, batteryCapacity: 0, batteryPercent: 0, batteryDifferential: 0, fuseTriggered: false, ...over,
});
const circuit = (id: number, over: Partial<Record<string, number | boolean>> = {}) => ({
  circuitGroupId: id, productionMW: 100, consumptionMW: 80, capacityMW: 120, maxConsumptionMW: 110, fuseTriggered: false, batteryCapacityMWh: 0, batteryPercent: 0, batteryDifferentialMW: 0, ...over,
});
const node = (x: number, over: Partial<CanonNode> = {}): CanonNode => ({ type: "Iron Ore", purity: "normal", nodeType: "node", x, y: 0, exploited: false, ...over });

function capture(): Pass.NativeParity {
  const rounds = Array.from({ length: Pass.NEED_ROUNDS + 5 }, (_, i) => iso(T0 + i * 5000));
  const nodes = [node(1), node(2), node(3, { exploited: true })];
  return {
    windowStartedAt: iso(T0), windowEndedAt: iso(T0 + 31 * 60_000), accepted: rounds.length, unrecorded: 0, refused: [],
    state: { codeConsumed: true, agentVersion: "mod/0.1.0", lastSeen: true, revoked: false },
    power: {
      rounds: rounds.map((observedAt) => ({ observedAt, circuits: [circuit(0), circuit(2, { batteryCapacityMWh: 10, batteryPercent: 50 })], before: [group(0), group(2, { batteryCapacity: 10, batteryPercent: 50 })], after: [group(0), group(2, { batteryCapacity: 10, batteryPercent: 50 })] })) as unknown as Pass.PowerRound[],
      frm: { ok: 100, failed: 0 }, mergedGroupsMax: 1,
    },
    players: {
      rounds: rounds.map((observedAt) => ({ observedAt, available: true, keys: ["a:true", "b:false"] })),
      frmSamples: rounds.flatMap((o) => [{ startedAt: iso(ms(o) - 2000), endedAt: iso(ms(o) - 1900), value: ["a:true", "b:false"] }, { startedAt: iso(ms(o) + 1000), endedAt: iso(ms(o) + 1100), value: ["a:true", "b:false"] }]),
      frm: { ok: 100, failed: 0 },
    },
    nodes: {
      posts: [{ at: iso(T0 + 8 * 60_000), status: 200, data: nodes }],
      frmSamples: [{ startedAt: iso(T0 + 7 * 60_000), endedAt: iso(T0 + 7 * 60_000 + 500), value: "h1" }, { startedAt: iso(T0 + 9 * 60_000), endedAt: iso(T0 + 9 * 60_000 + 500), value: "h1" }],
      distinct: { h1: nodes }, frm: { ok: 2, failed: 0 },
    },
  };
}
const ms = (s: string) => Date.parse(s);
const evidence = (c: Pass.NativeParity, sources: unknown = NATIVE_SOURCES): CoreEvidence =>
  ({ lines: [], errorLines: [], leaks: { code: false, secret: false, bearer: false }, captures: { "native-parity.json": c, "mod-config.json": sources } });
```

and the eight tests using them, for example:

```ts
describe("native-parity judge", () => {
  it("passes a clean capture and names each reader", () => {
    const v = Pass.evaluate(evidence(capture()));
    expect(v.pass).toBe(true);
    expect(v.reasons.some((r) => r.startsWith("R0 power PASS"))).toBe(true);
    expect(v.reasons.some((r) => r.startsWith("R1 players PASS"))).toBe(true);
    expect(v.reasons.some((r) => r.startsWith("R4 resource nodes PASS"))).toBe(true);
  });

  it("fails power alone when a value is 5% off (the other readers keep their verdicts)", () => {
    const c = capture();
    c.power.rounds[10]!.circuits[0]!.productionMW = 105; // FRM says 100 either side; 3% of 100 is 3
    const v = Pass.judgeReaders(c);
    expect(v.power.pass).toBe(false);
    expect(v.power.reasons.join("\n")).toMatch(/group 0 productionMW mod 105 vs FRM 100/);
    expect(v.players.pass).toBe(true);
    expect(v.resourceNodes.pass).toBe(true);
    expect(Pass.evaluate(evidence(c)).pass).toBe(false);
  });

  it("fails players alone on one differing round", () => {
    const c = capture();
    c.players.rounds[3]!.keys = ["a:true"];
    const v = Pass.judgeReaders(c);
    expect(v.players.pass).toBe(false);
    expect(v.power.pass && v.resourceNodes.pass).toBe(true);
  });

  it("lists node differences and fails resource nodes alone", () => {
    const c = capture();
    (c.nodes.posts[0]!.data as CanonNode[]).pop();
    c.nodes.posts[0]!.data = [...(c.nodes.posts[0]!.data as CanonNode[]), node(9)];
    const v = Pass.judgeReaders(c);
    expect(v.resourceNodes.pass).toBe(false);
    expect(v.resourceNodes.reasons.join("\n")).toContain("in FRM only");
    expect(v.resourceNodes.reasons.join("\n")).toContain("in the mod only");
    expect(v.power.pass && v.players.pass).toBe(true);
  });

  it("needs enough compared power rounds", () => {
    const c = capture();
    c.power.rounds = c.power.rounds.slice(0, 10);
    expect(Pass.judgeReaders(c).power.reasons.join("\n")).toMatch(/compared in only 10 rounds, need 288/);
  });

  it("reports fields the world never exercised, without failing", () => {
    const c = capture();
    for (const r of c.power.rounds) {
      r.circuits = [circuit(0)];
      r.before = [group(0)];
      r.after = [group(0)];
    }
    c.power.mergedGroupsMax = 0;
    for (const r of c.players.rounds) r.keys = [];
    c.players.frmSamples = c.players.frmSamples.map((s) => ({ ...s, value: [] }));
    const v = Pass.judgeReaders(c);
    expect(v.power.pass && v.players.pass).toBe(true);
    expect(v.power.reasons.join("\n")).toContain("NOT EXERCISED");
    expect(v.players.reasons.join("\n")).toContain("NOT EXERCISED");
  });

  it("is red when the run did not put the native sources in the mod's config, whatever the readers say", () => {
    expect(Pass.evaluate(evidence(capture(), { playersSource: "bridge", resourceNodesSource: "native" })).pass).toBe(false);
    expect(Pass.evaluate(evidence(capture(), null)).pass).toBe(false);
  });

  it("keeps the harness's key names equal to the mod's", () => {
    const cpp = readFileSync(new URL("../../mod/SatisDashAgent/Source/SatisDashCore/Private/Config.cpp", import.meta.url), "utf8");
    for (const key of Object.keys(NATIVE_SOURCES)) expect(cpp).toContain(`TEXT("${key}")`);
  });
});
```

Run: `npx vitest run test/native-parity-pass.test.ts test/frm-native.test.ts --reporter=dot` → PASS once the files exist (write the tests first and see them fail, then create `pass.ts` and `frm-native.ts`).

- [ ] **Step 5: The drive**

Create `harness/src/core-runs/native-parity.ts`:

```ts
import * as Pass from "../../../runs/native-parity/pass";
import { NATIVE_SOURCES, type CoreContext, type CoreRunSpec } from "../core-run";
import { parseGetPowerFull, pickBracket, startFrmPoller } from "../frm";
import { canonNodes, countMergedGroups, mapFrmResourceNodes, playerKeys, startSampler, type CanonNode } from "../frm-native";
import { startRecorder, type RecordedCall } from "../recorder";
import { createHash } from "node:crypto";
import { FRM_BRACKET_GAP_MS } from "../../../runs/enrol/pass";
import { accepted, enrolPhase } from "./enrol";

const MIN = 60_000;
const SECOND = 1000;
const POWER_POLL_MS = 250; // the enrol run's pause between getPower reads
const PLAYERS_POLL_MS = 2 * SECOND;
const NODES_POLL_MS = MIN;
const realSleep = (ms: number) => new Promise<void>((r) => setTimeout(r, ms));
const iso = (ms: number) => new Date(ms).toISOString();

/**
 * The combined native-reader parity run for sprint S1 (runs/native-parity/pass.ts says what is judged). The mod enrols with
 * playersSource and resourceNodesSource native (spec.modConfig); with the world running and auto-pause off, the harness
 * reads FRM's getPower (back to back), getPlayer (every 2 s) and getResourceNode (every minute, and at once after each mod
 * post) from the test server's FRM for 30 minutes, beside the mod's own posts. The harness drives no construction, so the
 * save is idle. Names are hashed before they are stored.
 */
export const nativeParity: CoreRunSpec = {
  requirements: Pass.requirements,
  backend: true,
  world: true,
  modConfig: NATIVE_SOURCES,
  evaluate: Pass.evaluate,
  preflight(deps) {
    if (!deps.config.frmBaseUrl) throw new Error("native-parity needs frmBaseUrl in the runner config: FRM is the oracle (docs/runner.md)");
  },
  async drive(ctx: CoreContext) {
    const frmBase = ctx.deps.config.frmBaseUrl!;
    const get = async (path: string, timeoutMs: number): Promise<unknown> => {
      const r = await fetch(new URL(path, frmBase), { signal: AbortSignal.timeout(timeoutMs) });
      if (!r.ok) throw new Error(`${path} answered ${r.status}`);
      return r.json();
    };
    const host = await ctx.deps.backendHost!();
    const recorder = await startRecorder(host.relayUrl, { keepWorldData: ["resourceNodes"] });
    const capture: Pass.NativeParity = {
      windowStartedAt: null, windowEndedAt: null, accepted: 0, unrecorded: 0, refused: [], state: null,
      power: { rounds: [], frm: { ok: 0, failed: 0 }, mergedGroupsMax: null },
      players: { rounds: [], frmSamples: [], frm: { ok: 0, failed: 0 } },
      nodes: { posts: [], frmSamples: [], distinct: {}, frm: { ok: 0, failed: 0 } },
    };
    let mergedMax = 0;
    const power = startFrmPoller(
      async () => {
        const body = await get("/getPower", 2000);
        mergedMax = Math.max(mergedMax, countMergedGroups(body));
        return parseGetPowerFull(body);
      },
      { now: Date.now, sleep: realSleep, intervalMs: POWER_POLL_MS },
    );
    const players = startSampler(async () => playerKeys(await get("/getPlayer", 3000)), { intervalMs: PLAYERS_POLL_MS, now: Date.now, sleep: realSleep });
    const nodes = startSampler(
      async () => {
        const mapped = mapFrmResourceNodes(await get("/getResourceNode", 15_000));
        const canon = canonNodes(mapped);
        return { hash: createHash("sha256").update(canon.join("\n")).digest("hex").slice(0, 16), mapped };
      },
      { intervalMs: NODES_POLL_MS, now: Date.now, sleep: realSleep },
    );
    try {
      // Samplers run from before the world is loaded: an FRM that is not up yet is a failed read, not a sample.
      await enrolPhase(ctx, host, recorder);
      await ctx.deps.loadWorld!();
      await ctx.deps.waitHealthy();
      ctx.rememberAutoPause(await ctx.deps.readAutoPause!());
      await ctx.deps.setAutoPause!(false); // the factory runs, so power moves (S3)
      await ctx.deps.sleep(MIN); // the world settles
      const from = Date.now();
      capture.windowStartedAt = iso(from);
      const firstOfWindow = accepted(recorder).length;
      let cursor = firstOfWindow;
      let postsSeen = 0;
      const waiting: RecordedCall[] = [];
      const observed = (e: RecordedCall) => Date.parse(String((e.snapshot as { observedAt?: unknown } | undefined)?.observedAt ?? ""));
      const settleRounds = (force: boolean) => {
        for (const e of [...waiting]) {
          const o = observed(e);
          const ready = !Number.isFinite(o) || force || power.reads.some((r) => r.startedAt >= o) || Date.now() - o > FRM_BRACKET_GAP_MS;
          if (!ready) continue;
          waiting.splice(waiting.indexOf(e), 1);
          const snap = e.snapshot as { observedAt?: string; power?: { circuits?: Pass.PowerRound["circuits"] } };
          if (!Number.isFinite(o)) continue;
          if (snap.power?.circuits) {
            const b = pickBracket(power.reads, o, FRM_BRACKET_GAP_MS);
            capture.power.rounds.push({ observedAt: snap.observedAt!, circuits: snap.power.circuits, before: b.before?.groups ?? null, after: b.after?.groups ?? null });
          }
        }
      };
      while (Date.now() - from < Pass.WINDOW_MINUTES * MIN) {
        await ctx.deps.sleep(SECOND);
        const all = accepted(recorder);
        while (cursor < all.length) {
          const e = all[cursor++]!;
          waiting.push(e);
          const snap = e.snapshot as { observedAt?: string; players?: { available?: boolean; players?: Array<{ name: string; online: boolean }> } } | undefined;
          if (snap?.observedAt) {
            capture.players.rounds.push({
              observedAt: snap.observedAt,
              available: typeof snap.players?.available === "boolean" ? snap.players.available : null,
              keys: Array.isArray(snap.players?.players) ? snap.players!.players!.map((p) => `${p.name}:${p.online}`).sort() : null,
            });
          }
        }
        settleRounds(false);
        // A node post is compared with an FRM sample taken right after it.
        const posts = recorder.entries.filter((e) => e.agent === "mod" && e.world?.layer === "resourceNodes" && e.status === 200);
        if (posts.length > postsSeen) {
          postsSeen = posts.length;
          await nodes.take();
        }
      }
      capture.windowEndedAt = iso(Date.now());
      await ctx.deps.sleep(2 * SECOND); // the last sample's FRM neighbour after it
      settleRounds(true);
      await ctx.sampleMemory("withModAfterRounds");
    } finally {
      power.stop();
      players.stop();
      nodes.stop();
      capture.power.frm = power.counts();
      capture.power.mergedGroupsMax = mergedMax;
      capture.players.frm = players.counts();
      capture.players.frmSamples = players.samples.map((s) => ({ startedAt: iso(s.startedAt), endedAt: iso(s.endedAt), value: s.value }));
      capture.nodes.frm = nodes.counts();
      for (const s of nodes.samples) capture.nodes.distinct[s.value.hash] ??= s.value.mapped as CanonNode[];
      capture.nodes.frmSamples = nodes.samples.map((s) => ({ startedAt: iso(s.startedAt), endedAt: iso(s.endedAt), value: s.value.hash }));
      const mod = recorder.entries.filter((e) => e.agent === "mod");
      const from = capture.windowStartedAt ? Date.parse(capture.windowStartedAt) : 0;
      const posts = mod.filter((e) => e.method === "POST" && e.path === "/agent/v1/snapshots");
      capture.accepted = posts.filter((e) => e.status === 200).length;
      capture.unrecorded = posts.filter((e) => e.status === 200 && e.snapshot === undefined).length;
      capture.refused = mod.filter((e) => e.method === "POST" && e.status !== 200 && Date.parse(e.at) >= from).map((e) => ({ at: e.at, status: e.status }));
      capture.nodes.posts = mod
        .filter((e) => e.world?.layer === "resourceNodes" && e.status === 200 && Date.parse(e.at) >= from)
        .map((e) => ({ at: e.at, status: e.status, data: e.worldData }));
      capture.state = await host.state("mod").catch(() => null);
      ctx.captures["native-parity.json"] = capture;
      ctx.captures["readers.json"] = Pass.judgeReaders(capture);
      ctx.captures["recorder.json"] = recorder.entries.map(({ snapshot: _, worldData: __, ...rest }) => rest);
      await recorder.close();
    }
  },
};
```

Note on the player keys: the recorder already replaces each snapshot's player `name` with `sessionHash(name)` (`hashPlayers` in `recorder.ts`), so `${p.name}:${p.online}` in the loop is `<hash>:<online>`, the form `playerKeys` produces on the FRM side. Run `npm run typecheck` and fix whatever it reports without changing the logic.

`ServerState` for `capture.state`: `host.state("mod")` returns the shape `parity.ts` stores (`codeConsumed`, `agentVersion`, `lastSeen`, `revoked`); the judge reads only the first and third.

- [ ] **Step 6: Register, document, dispatch notes**

`harness/src/core-runs/index.ts`: `import { nativeParity } from "./native-parity";` and add `"native-parity": nativeParity` after `manual`. `.github/workflows/core-run.yml`: add `native-parity` after `manual` in `options` and mention it in the header comment (needs the backend host and `frmBaseUrl`). `docs/runner.md`: after the `parity` paragraph add:

```markdown
- `native-parity` needs the backend host, the staged world and `frmBaseUrl`. It writes `playersSource` and `resourceNodesSource`
  as `native` into the mod's config (`modConfig`, recorded in `mod-config.json`), enrols the mod through the recorder, loads the
  world with auto-pause off and, for 30 minutes after a one-minute settle, reads the test server's FRM as a black box beside
  the mod: `getPower` back to back, `getPlayer` every 2 s (names hashed before they are stored), `getResourceNode` every
  minute and right after each mod post. It judges each reader on its own (`runs/native-parity/pass.ts`: power within 3% of
  FRM's reads either side of each mod sample; players equal; the resource-node layer equal and contract-valid) and writes
  the three verdicts to `readers.json`; the run is red if any reader is. It reports fields the world never exercised. It takes
  about 45 minutes plus the build. To exercise "online" players, join the test server with a game client during the window.
```

- [ ] **Step 7: Run everything and commit**

Run: `cd harness; npx vitest run --reporter=dot` and `npm run typecheck` and `npm run harness -- check-api`.
Expected: PASS (the `workflows.test.ts` "offers exactly the core runs" test passes only after `core-run.yml` and `CORE_RUNS` both list `native-parity` in the same order; `tick-cost-native` is added in Task 5, so for this task add the `CORE_RUN_IDS` entry for `"native-parity"` only and leave `"tick-cost-native"` to Task 5).

```bash
git add -A harness runs docs .github
git commit -m "Combined native parity run for S1: per-reader judgement against FRM (ADR-0058 Decision 3)"
```

---

### Task 5: The tick-cost run with the native readers on (`tick-cost-native`), and the sprint's docs

**Files:**
- Create: `runs/tick-cost-native/pass.ts`, `harness/src/core-runs/tick-cost-native.ts`, `harness/test/tick-cost-native-pass.test.ts`
- Modify: `harness/src/core-runs/tick-cost.ts`, `harness/src/core-run.ts`, `harness/src/core-runs/index.ts`, `.github/workflows/core-run.yml`, `docs/runner.md`, `docs/native-readers.md`

**Interfaces:**
- Consumes: Task 4's `CoreRunSpec.modConfig`, `NATIVE_SOURCES`, `mod-config.json`; the existing `tickCost` spec, `Pass.evaluate`, `TICK_WARMUP_MS`.
- Produces: run id `tick-cost-native`; capture `snapshot-parts.json` (added to the existing `tick-cost` run's evidence too: evidence only, no pass condition changes); `NativePass.evaluate`.

**What it judges:** the existing tick-cost limits, unchanged and imported (`runs/tick-cost/pass.ts`: C4 tick median at least 95% of the no-mod baseline, S12, same save, memory within 100 MB of the baseline and no more than 25 MB growth over 10 minutes), with the mod's players and resource-node readers native. In addition: both sources were native in the mod's config; at least one accepted snapshot in the window carried an available players part; and a `resourceNodes` world post was accepted inside the measured window (after the warmup), so the scan's cost is inside the measurement. The mod's other readers (factory, mapLive, rails) are still on the bridge in S1, as in the existing `tick-cost` run, which also writes `frmPort`. The scan's own cost (actors, steps, longest step, total) is in `result.json`'s `modLines` (the log line the reader writes). The ceiling for the longest step is the owner's to fix after this first measurement (ADR-0058 Decision 3 item 3); nothing here invents one.

- [ ] **Step 1: Write the failing test**

Create `harness/test/tick-cost-native-pass.test.ts`:

```ts
import { describe, expect, it } from "vitest";
import { NATIVE_SOURCES } from "../src/core-run";
import type { CoreEvidence } from "../src/core-run";
import { TICK_WARMUP_MS, type TickSeries } from "../src/ticks";
import * as Native from "../../runs/tick-cost-native/pass";
import * as Base from "../../runs/tick-cost/pass";

const FROM = Date.parse("2026-10-26T12:00:00.000Z");
const iso = (ms: number) => new Date(ms).toISOString();

// The same clean capture harness/test/tick-cost-pass.test.ts uses (that file's `capture()`), so Base.evaluate passes.
const MB = 1024 * 1024;
const at = (s: number) => new Date(FROM + s * 1000).toISOString();
const series = (values: number[]): TickSeries => ({
  warmup: [], samples: values.map((value, i) => ({ at: at(i * 10), value })), errors: 0, median: [...values].sort((a, b) => a - b)[Math.floor(values.length / 2)]!,
});
const mem = (mbs: number[]): Base.MemoryPoint[] => mbs.map((m, i) => ({ at: at(i * 30), workingSetBytes: m * MB }));
const RECORDER = "127.0.0.1:13105";
const baseCapture = (): Base.TickCost => ({
  baseline: series([22, 22, 22, 22]),
  withMod: series([22, 21.9, 22, 22]),
  outboundBaseline: ["203.0.113.7:443"],
  outboundWithMod: ["203.0.113.7:443", RECORDER],
  outboundSamples: { baseline: { ok: 21, failed: 0, dnsFailed: 0 }, withMod: { ok: 21, failed: 0, dnsFailed: 0 } },
  memoryBaseline: mem([6000, 6005, 6010]),
  memoryWithMod: mem([6040, 6050, 6060]),
  recorder: RECORDER,
  circuitGroups: 3,
  sessionBaseline: "a1b2c3d4e5f6",
  sessionWithMod: "a1b2c3d4e5f6",
  outboundNamesBaseline: { "203.0.113.7": ["api.example.com"] },
  outboundNamesWithMod: { "203.0.113.7": ["api.example.com"] },
});

function evidence(over: { sources?: unknown; parts?: unknown; posts?: unknown[] } = {}): CoreEvidence {
  return {
    lines: [], errorLines: [], leaks: { code: false, secret: false, bearer: false },
    captures: {
      "tick-cost.json": baseCapture(),
      "mod-config.json": "sources" in over ? over.sources : NATIVE_SOURCES,
      "snapshot-parts.json": over.parts ?? {
        measured: { from: iso(FROM), to: iso(FROM + TICK_WARMUP_MS + 10 * 60_000) },
        snapshots: [{ at: iso(FROM + TICK_WARMUP_MS + 60_000), playersAvailable: true }],
      },
      "recorder.json": over.posts ?? [{ at: iso(FROM + TICK_WARMUP_MS + 5 * 60_000), status: 200, agent: "mod", world: { layer: "resourceNodes", items: 600, bytes: 60000 } }],
    },
  };
}

describe("tick-cost-native judge", () => {
  it("passes when the existing limits pass and the native readers were live inside the measured window", () => {
    expect(Native.evaluate(evidence()).pass).toBe(true);
  });
  it("keeps the existing tick-cost verdict: a memory blow-up fails it", () => {
    const e = evidence();
    (e.captures["tick-cost.json"] as Base.TickCost).memoryWithMod = mem([6300, 6400, 6500]); // 490 MB above the baseline's end
    const v = Native.evaluate(e);
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toMatch(/memory/);
  });
  it("fails when the sources were not native", () => {
    expect(Native.evaluate(evidence({ sources: { playersSource: "bridge", resourceNodesSource: "native" } })).pass).toBe(false);
    expect(Native.evaluate(evidence({ sources: undefined })).pass).toBe(false);
  });
  it("fails when no snapshot in the window carried players", () => {
    const parts = { measured: { from: iso(FROM), to: iso(FROM + TICK_WARMUP_MS + 10 * 60_000) }, snapshots: [{ at: iso(FROM + TICK_WARMUP_MS + 60_000), playersAvailable: false }] };
    expect(Native.evaluate(evidence({ parts })).reasons.join("\n")).toMatch(/players/);
  });
  it("fails when the resourceNodes scan fell outside the measured window", () => {
    const early = [{ at: iso(FROM + 60_000), status: 200, agent: "mod", world: { layer: "resourceNodes", items: 600, bytes: 1 } }];
    const v = Native.evaluate(evidence({ posts: early }));
    expect(v.pass).toBe(false);
    expect(v.reasons.join("\n")).toMatch(/resourceNodes/);
  });
});
```

Run: `npx vitest run test/tick-cost-native-pass.test.ts --reporter=dot` → FAIL (no such file).

- [ ] **Step 2: The judge**

Create `runs/tick-cost-native/pass.ts`:

```ts
// The tick-cost run with sprint S1's native readers on (ADR-0058 Decision 3 item 3). The limits are the existing tick-cost
// run's, imported and unchanged (runs/tick-cost/pass.ts: C4, S12, same save, memory). Added, because "the reader was on" is
// part of the claim: the mod's config had playersSource and resourceNodesSource native; a snapshot in the measured window
// carried an available players part; and a resourceNodes world post was accepted inside the measured window after the warmup
// (so the actor scan ran while the ticks were measured). The scan's own cost is the mod's log line in result.json's modLines.
import type { CoreEvidence } from "../../harness/src/core-run";
import { TICK_WARMUP_MS } from "../../harness/src/ticks";
import type { Verdict } from "../../harness/src/verdict";
import * as Base from "../tick-cost/pass";

export const requirements = [...Base.requirements, "R0-R1-R4-tick-cost"];

interface Parts { measured?: { from: string; to: string }; snapshots?: Array<{ at: string; playersAvailable?: boolean }> }
interface Recorded { at: string; status: number; agent: string; world?: { layer: string; items: number; bytes: number } }

export function evaluate(e: CoreEvidence): Verdict {
  const base = Base.evaluate(e);
  const extra: string[] = [];
  const sources = e.captures["mod-config.json"] as { playersSource?: string; resourceNodesSource?: string } | undefined;
  if (sources?.playersSource !== "native" || sources?.resourceNodesSource !== "native") extra.push("the run did not write playersSource and resourceNodesSource as native into the mod's config");
  const parts = e.captures["snapshot-parts.json"] as Parts | undefined;
  const from = parts?.measured ? Date.parse(parts.measured.from) + TICK_WARMUP_MS : NaN;
  const to = parts?.measured ? Date.parse(parts.measured.to) : NaN;
  if (!Number.isFinite(from) || !Number.isFinite(to)) extra.push("the measured window was not recorded");
  const inWindow = (at: string) => Date.parse(at) >= from && Date.parse(at) <= to;
  const withPlayers = (parts?.snapshots ?? []).filter((s) => s.playersAvailable === true && inWindow(s.at)).length;
  if (withPlayers === 0) extra.push("no accepted snapshot in the measured window carried an available players part: the native players reader did not run");
  const recorded = (e.captures["recorder.json"] as Recorded[] | undefined) ?? [];
  const nodePosts = recorded.filter((r) => r.agent === "mod" && r.status === 200 && r.world?.layer === "resourceNodes" && inWindow(r.at));
  if (nodePosts.length === 0) extra.push("no resourceNodes world post was accepted inside the measured window: the native scan was not measured");
  if (extra.length === 0 && base.pass) {
    return { pass: true, reasons: [...base.reasons, `native readers live in the window: ${withPlayers} snapshots with players, ${nodePosts.length} resourceNodes post(s)`] };
  }
  return { pass: false, reasons: [...base.reasons.filter(() => !base.pass), ...extra] };
}
```

(When the base verdict passes, its reasons describe what passed, so they are kept only on success; on failure only the failing reasons are listed.)

- [ ] **Step 3: The spec and the evidence the judge reads**

In `harness/src/core-runs/tick-cost.ts`, make two small additions (neither changes a pass condition or what `Pass.evaluate` reads):

1. In `drive`, declare `let measured: { from: string; to: string } | null = null;` before the `try`, and around the measurement replace `const w = await measureWindow(ctx.deps, ctx.proc.pid, ctx.proc.exited);` with

```ts
      const measureFrom = ctx.deps.now().toISOString();
      const w = await measureWindow(ctx.deps, ctx.proc.pid, ctx.proc.exited);
      measured = { from: measureFrom, to: ctx.deps.now().toISOString() };
```

2. In the `finally`, before `ctx.captures["recorder.json"] = ...`:

```ts
      // Evidence on which parts the mod's snapshots carried (no body, no names): tick-cost-native's judge reads it.
      ctx.captures["snapshot-parts.json"] = {
        measured,
        snapshots: accepted(recorder).map((e) => ({
          at: e.at,
          playersAvailable: (e.snapshot as { players?: { available?: unknown } } | undefined)?.players?.available === true,
        })),
      };
```

Create `harness/src/core-runs/tick-cost-native.ts`:

```ts
import * as Pass from "../../../runs/tick-cost-native/pass";
import { NATIVE_SOURCES, type CoreRunSpec } from "../core-run";
import { tickCost } from "./tick-cost";

/**
 * The tick-cost run (baseline boot without the mod, then the mod's boot, each measured for the warmup plus 10 minutes) with
 * the players and resource-node readers set to native through the mod's config. Same drive and same limits; a different
 * judge (runs/tick-cost-native/pass.ts), which also requires the native readers to have been live in the window.
 */
export const tickCostNative: CoreRunSpec = {
  ...tickCost,
  requirements: Pass.requirements,
  modConfig: NATIVE_SOURCES,
  evaluate: Pass.evaluate,
};
```

In `core-run.ts` add `"tick-cost-native"` to `CORE_RUN_IDS` (if Task 4 added only `"native-parity"`); in `core-runs/index.ts` import and register `"tick-cost-native": tickCostNative` after `"native-parity"`; in `core-run.yml` add `tick-cost-native` after `native-parity` in `options` and in the header comment.

- [ ] **Step 4: Docs**

`docs/runner.md`, after the `native-parity` bullet:

```markdown
- `tick-cost-native` is `tick-cost` with the players and resource-node readers set to `native` in the mod's config. It boots
  once without the mod and once with it and measures each for the warmup plus 10 minutes, judged by the same limits (tick
  median at least 95% of the baseline's, S12, same save, memory within 100 MB and no more than 25 MB growth) plus proof that the
  native readers were live in the measured window (`runs/tick-cost-native/pass.ts`). The mod logs one line per resource-node
  scan (actors, steps, longest step, total, in `result.json`'s `modLines`), which is the scan's cost; the owner fixes a ceiling
  for the longest step after the first measurement. The mod's factory, mapLive and rails readers are still on the bridge, as in
  `tick-cost`. `snapshot-parts.json` (which parts each accepted snapshot carried; no bodies) is now written by `tick-cost` too.
```

`docs/native-readers.md`: set the Players and Resource nodes rows' "Notes" to name the two runs ("judged by `native-parity` and `tick-cost-native`; the default flips in a small switch PR once `readers.json` passes for that reader").

- [ ] **Step 5: Run everything**

Run: `cd harness; npx vitest run test/tick-cost-native-pass.test.ts --reporter=dot`, then `npx vitest run --reporter=dot`, `npm run typecheck`, `npm run harness -- check-api`, `npm run harness -- check-provenance`.
Expected: all PASS. The existing `tick-cost-pass.test.ts` and `tick-cost.test.ts` still pass (the drive only gained evidence; `Pass.evaluate` is untouched); `workflows.test.ts` passes with the run ids in the same order in `CORE_RUNS` and `core-run.yml`.

- [ ] **Step 6: Commit**

```bash
git add -A harness runs docs .github
git commit -m "Tick-cost run with the native readers on; sprint S1 docs (ADR-0058)"
```

---

## Self-review

**Spec coverage (ADR-0058, S1, and the owner's answers):**

| Requirement | Where |
| --- | --- |
| Build task 2: contributor note with rules 1-5 verbatim, PR-template checkbox, CI check that fails a native-reader directory without its provenance file; layout and file name decided; check in the harness and the CI workflow; no similarity job | Task 1 |
| Per-reader source switch (Decision 4), default `bridge`, hidden per-reader override documented as a stopgap, contract/endpoint/agent version unchanged | Task 2 (`ESource`, keys, `docs/native-readers.md`); no `powerSource` (no bridge path) |
| R0: rest of power | Verified complete in code (see "Lookups made"); proof and provenance in Tasks 1 and 4; no new reader code |
| R1: players natively behind the switch | Task 2 |
| R4: resource nodes natively, exactly the bridge's layer, into the existing world sender, game-thread cost stated | Task 3 (shared record writer; golden test; sliced scan; cost line) |
| One combined 30-minute parity run, same minute against FRM's endpoints, each reader judged separately, new files only | Task 4 |
| One tick-cost run with all native readers on, existing limits | Task 5 |
| Provenance files for the three readers; README | Tasks 1 (Power), 2 (Players), 3 (ResourceNodes); README line in Task 1; `docs/native-readers.md` |
| Switch PRs and the dash evidence PR after the runs | "Follow-ups" below, not tasks |
| `[NEEDS VERIFICATION]` from the survey become steps or questions | Offline players, enumeration, `GetActorLocation`, geyser path: looked up (table). Left to the parity run, as judged claims: offline-list equality, resource-name text, `IsOccupied` for satellites and geysers, actor-iteration completeness (608), group ids and per-circuit-versus-group stats (R0), `MaximumPowerConsumption` meaning. Open: fuse/battery/merge exercise (question 1) |

**Placeholder scan:** the only fill-in values are `Author:` and `Date:` in provenance files, which the executing session must set (Task 1's check rejects a malformed date); no "TBD".

**Type consistency:** `ESource`, `GetPlayersSource()`, `GetResourceNodesSource()` (Task 2) are used in Task 3 (`WorldSenderIsNative`) and Task 4/5 (`playersSource`, `resourceNodesSource` keys, guarded by the drift test); `FResourceNodeReading`, `FNativeWorldBody`, `BuildNativeResourceNodes`, `AppendResourceNodeRecord`, `NodeScanActorsPerStep`, `NodeScanRetrySeconds` are defined in Task 3 Step 2 and used in Steps 4-6 and the tests; `FakeGame` members (`Players`, `Nodes`, `bHasWorld`, `StepsToFinish`, `ScanBegins`) are defined where first used and reused by the driver tests; `NativeParity`, `PowerRound`, `judgeReaders`, `NEED_ROUNDS` are defined in Task 4's `pass.ts` and used by the drive and tests; `NATIVE_SOURCES`, `CoreRunSpec.modConfig` (Task 4) are used in Task 5.

**Review Focus:** each line names its test; the Game-module code (`ReadPlayers`, the scan) is not unit-testable in this suite (the suite links Core only), so its correctness is carried by the parity run (R1, R4) and the scan's log line; the plan says so.

## Owner decisions and open items

1. **Fields the test world never exercises (R0 and R1).** Decision 3 sets tolerances, not coverage. The judge compares everything that exists and reports `NOT EXERCISED` for a fuse that never trips, no batteries, no merged group, nobody online, no occupied node; it does not fail on them. Does the owner accept that for the switch, or want a save that exercises them (the harness cannot trip a fuse), or a join with a game client during the window?
2. **Red run, per-reader verdicts.** The run is red if any reader fails; `readers.json` carries the three verdicts and the switch PRs read it. The alternative (green run, verdicts only in the file) hides a failure in the summary.
3. **`--min` floor.** 142 now; Task 3 proposes 149 (a workflow-file limit, so the owner's call).
4. **PR split.** Task 1 as PR S1-A first; Tasks 2-5 as PR S1-B.
5. **`className`.** Not emitted by R4 (the bridge's golden does not carry it; the contract's field is optional). Adding it is a separate change to the golden and both sources.
6. **The survey's folder** in the dash repo for the `Survey:` lines (the file name is used; confirm the folder).
7. **CLAUDE.md** is untouched; a seventh rule pointing agents at `docs/clean-room.md` and the checkbox is the owner's to add.
8. **Judged by the run, not assumed:** offline players equal FRM's remembered list; `GetResourceName()` equals FRM's `Name` on the server's culture; `IsOccupied()` for satellites and geysers equals FRM's `Exploited` (31 exploited in the capture); the actor scan finds the same 608 nodes (459 + 118 + 31); the lowest circuit id speaks for a merged group.

## Follow-ups after the runs (not tasks)

- Owner dispatches `mod-test` on the branch, then `native-parity` and `tick-cost-native`; the owner rules on any listed differences and on the longest-step ceiling.
- Per passing reader, a small switch PR flips `DefaultPlayersSource` / `DefaultResourceNodesSource` to `Native` (a reader that missed stays on `bridge`, the rest switch), with the mod-claims row.
- One dash evidence PR: `raw-sources/mod-native-readers/` results and bundles, `wiki/mod-claims.md` rows, the survey note's folder.
- Sprint S2 (factory, trains and routes) starts from this plan's layout, switch and runs.
