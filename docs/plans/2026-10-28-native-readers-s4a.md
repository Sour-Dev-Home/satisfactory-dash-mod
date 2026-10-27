# Native readers, Sprint S4 part A (map markers R8, drone routes R9, power lines R10; the status fields) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The mod reads the map markers (R8), the drone ports (R9) and the power lines (R10) from the game itself and posts the `mapMarkers`, `droneRoutes` and `powerLines` world layers; the drone ports carry the new `pairedId`. One combined 30-minute `native-parity` run plus one `tick-cost-native` run, both on `Test3`, judge each reader separately, with the 2 ms ceiling on every native game-thread step. Each reader has a `bridge`/`native` switch that stays `bridge` until one switch PR after the runs pass. A last task (Task 18) adds `gameReachable`, `lastGameReadAt` and `modVersion` to the snapshot body (both dash contract gates, #739 and #740, have merged; no gate remains). S4-B (extractors, generators, train transfer rates, storage) and R7 (deleting the bridge) are not in this plan.

**Architecture:**
- Same pattern as S1 to S3. Pure logic (mapping, caps, sorting, the exact JSON) lives in `SatisDashCore` under `Native/<Reader>/`; game-thread code is small, in `GameReader.cpp`, and **time-sliced** by `FStepBudget` (about 1 ms a step).
- **powerLines rides the S3 shared logistics pass** (survey Q7): one more flag on `BeginLogisticsRead`, one more `Cast` in the `Collect` phase, one more read phase, one more world body split off the `FLogisticsSnapshot`. A layer whose source is `bridge` is not in the pass.
- **mapMarkers and droneRoutes each get their own small sliced read** (Ruling R-c), driven by the sender exactly as `trainRoutes` is (`StepTrainRoutesScan`): a begin / step / abandoned / end port per layer, one `FStepBudget` per layer, one log line per read.
- None of the three has a bridge path (the bridge never sent them). Each has a source key like `conveyorsSource`: `bridge` (the default until the switch PR) = **the layer is not sent**, `native` = sent (Ruling R-b).
- Harness: `native-parity` gains the three verdicts in `readers.json` (a new judge file `runs/native-parity/pass-s4.ts`, a new oracle `harness/src/frm-s4.ts`); `tick-cost-native` gains the two new step lines. The contract is re-vendored first.

**Tech Stack:** Unreal Engine 5.6.1-CSS, SML 3.12.0 (`harness/toolchain.json`), C++20, UE automation tests (`SatisDashCoreTests`), TypeScript harness (Node, zod 4, Vitest 5).

**Spec:** ADR-0058 (satisfactory-dash `docs-vault/wiki/decisions/0058-native-readers-remove-frm.md`: the reader rows, the sprint table), the S4-A header survey (`mod-native-readers-s4a-headers-2026-10-27.md`, dash `docs-vault/raw-sources/`, dash PR #741: its 11 open questions and the NEEDS VERIFICATION items), the contract (dash `packages/shared/src/map.ts`: `KNOWN_MAP_WORLD_LAYERS` :41, `DronePortSchema` :216, `MapMarkerSchema` :252, `PowerLineSchema` :290; vendored by Task 1), the dash mappers `packages/game-adapter/src/map/{mapMarkersMapper,droneRoutesMapper,powerLinesMapper}.ts` (our own code; the spec of every derived field), `agent/src/worldReader.ts` (the three layers' intervals: all 600 s), the S3 plan `docs/plans/2026-10-27-native-readers-s3.md` (the template) and the owner's scope of 2026-10-27.

## Owner decisions and controller rulings, carried as rules

1. **Scope (owner, 2026-10-27):** three new native world layers R8 mapMarkers, R9 droneRoutes, R10 powerLines against the dash contracts; plus the status fields `gameReachable`, `lastGameReadAt`, `modVersion`. Neither is gated any more (controller updates): `pairedId` merged as dash #739 and is part of R9; the status fields merged as dash #740 (dash main `a5dd42c` includes both).
2. **R-a. The survey's 11 recommendations are taken as written** (see the Rulings table), with the deviations listed in Open questions.
3. **R-b. One source key per layer:** `mapMarkersSource`, `droneRoutesSource`, `powerLinesSource`, defaults `bridge` = layer not sent; `native` = sent. One switch PR after the parity pass flips all three (as S2 and S3 did). `NATIVE_SOURCES_S4` = `NATIVE_SOURCES_S3` plus the three keys native.
4. **R-c. Small sliced reads for markers and drone ports** (1 ms budget, 2 ms ceiling, index-only state, `IsValid` and weak pointers), logged as `<Name> read (game thread only): ...` so `tick-cost-native` judges them. Interval 600 s and tier "world" from the dash `worldReader.ts` (`MAP_MARKERS_INTERVAL_MS`, `DRONE_ROUTES_INTERVAL_MS`, `POWER_LINES_INTERVAL_MS` = 600,000); first native read 480 s +/-10% like S3 (`NativeFirstSeconds`).
5. **R-d. Parity judges (provisional, owner to confirm):** mapMarkers: id set equal, every contract field exactly equal after the dash mapper's derivations (x/y whole metres: a one-metre difference at a .5 m boundary is allowed and reported); droneRoutes: id set equal, name / paired / input / output / fuel exactly equal, x/y within 1 m of FRM's unrounded value, and (controller rule) **when FRM's `paired` name resolves to exactly one FRM port, the mod's `pairedId` must equal that port's id, otherwise `pairedId` is reported only**; powerLines: id set equal, each end within 1 m, in index order or swapped (report which). NOT EXERCISED lines never fail. A layer with zero items on both sides passes with a NOT EXERCISED line.
6. **R-e. Test world `Test3` (`sprint3`).** The `s4-capture` run (PR #67) reports how many markers, drone stations and wires it has: Task 13 step 1 records them; no number is invented here.
7. **R-f. Re-vendor the contract first** (Task 1), at a dash main commit that includes #739 and #740 (`a5dd42cf3395582dce7adfbc6b1ebbfe52676b90` or a descendant). One re-vendor serves the whole plan.
8. **Names are player text.** The readers never log a marker or port name; no committed evidence carries one (Task 15 hashes them before they enter a capture). The reader logs carry counts and times only.
9. **Process (carried from S1 to S3):** NOT EXERCISED is reported and never fails; readers are judged separately; the `mod-test.yml` floor rises with the tests; executors never start runs (the owner dispatches).

## Global Constraints

Everything in the S1 to S3 plans' Global Constraints still holds (pure Core, test names `SatisDash.Core.<Area>.<Case>`, unity-build prefixes for file-local helpers, case-sensitive protocol strings, no `check()` on outside data, logs only to `LogSatisDash` at `Display`/`Warning` and rate-limited, `check-api` at `0 missing, 0 bad rows`, `check-provenance` clean). Added or restated:

- **Clean room (CLAUDE.md rule 7, `docs/clean-room.md`).** Nobody opens FicsitRemoteMonitoring's source or searches the web for it. FRM is a black box over HTTP: our recorded outputs and the live test server's endpoints (`/getMapMarkers`, `/getDroneStation`, `/getCables`). If FRM source is seen by accident: stop, tell the owner, record it in that reader's `PROVENANCE.md`. Each new reader (`Native/MapMarkers/`, `Native/DroneRoutes/`, `Native/PowerLines/`) gets its own `PROVENANCE.md` with `FRM source was not opened`; each PR description carries the checkbox "FRM source was not opened".
- **No API from memory (rule 1).** Every game/SML/UE symbol comes from the Lookups table or from a header the task opens, and gets its `api-sources.md` row (`path:line`) in the same task. **The header wins** over the survey: record both in the row's Source when they disagree. Paths are relative to `Source/FactoryGame/Public` unless marked `Engine/`.
- **No weakened judges (rule 3).** No existing `runs/*/pass.ts`, `pass-s2.ts`, `pass-s3.ts` or `spikes/*/pass.ts` condition changes; their tests stay byte-for-byte. New conditions are new code (`runs/native-parity/pass-s4.ts`). A failed reader stops the work and is reported with its evidence bundle. The thresholds (exact fields, 1 m) are the owner's R-d and named in code.
- **Evidence is machine-written; loopback and the test server only** (rules 4, 5). Evidence carries ids, class names, counts, numbers and **hashes** of names.
- **Contract.** After Task 1 the vendored contract does not move. No existing golden moves; new goldens are written from our recorded FRM output (synthetic names, see Open questions) and hand-checked.
- **Memory and cost (the mod stays light).** Markers: at most 250 readings (the game's own cap, `FGMapManager.h:299`), a few hundred bytes each, freed after the body exists. Drone ports: a weak pointer per station, readings freed after the body. Wires: one weak pointer per wire (843 on the captured save, 20,000 cap), then 40 bytes a reading. Never an FRM body.
- **The step ceiling.** No single native game-thread step may exceed 2 ms (`MaxNativeStepMs = 2.0`); each read stops at `NativeStepBudgetSeconds = 0.001` after the element that crosses it. `tick-cost-native` fails if a longest step is over 2.0 ms or a required line is missing.
- **Test counts.** `mod-test.yml` has `--min 186` and 186 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` macros today (counted on this branch). Running ledger: T4 +4 = 190, T5 +2 = 192, T7 +4 = 196, T8 +1 = 197, T10 +4 = 201, T11 +2 = 203, T18 +2 = 205. The floor and `harness/test/workflows.test.ts` (which pins the number) rise in the task whose tests land.
- **Runs.** Executors never start runs. The owner dispatches `mod-test` on each PR branch, then **one** `native-parity` and **one** `tick-cost-native` on `Test3`. No other core run is added.
- Commits end with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. No personal data, no local paths in anything committed (including this plan).

## Lookups made for this plan (header wins)

From the S4-A survey. The survey's line numbers were **not** re-opened by the planner in the pinned checkout (it was not available to this plan); every task opens the header it uses, checks the line, and adds the row. Where a task's reading disagrees, the header wins and both go in the row.

| Need | Header, `path:line` (survey) | Notes |
| --- | --- | --- |
| Marker manager | `FGMapManager.h:70` class, `:95` `Get(UWorld*)`, `:118` `GetMapMarkers(TArray<FMapMarker>&)` (public, non-const, returns a copy), `:111` `GetNumMapMarkers`, `:299` `mMaxNumMapMarkers = 250` | One slice, no cursor; null-check the manager. |
| Marker struct | `FGMapMarker.h:13` `FMapMarker`: `MarkerGUID` :37, `HasValidID()` :71, `Location` :40 (`FVector_NetQuantize`, cm), `Name` :43, `MapMarkerType` :49, `IconID` :52, `Color` :55 (`FLinearColor`) | Never read: `CategoryName`, `Scale`, `CompassViewDistance`, `LastEditedBy`, `CreatedByPlayerID` (PII-adjacent). |
| Kind enum | `FGActorRepresentation.h:14` `ERepresentationType`, `:32` `RT_MapMarker`, `:33` `RT_Stamp` | Anything but `RT_Stamp` is "marker" (the mapper's `markerKind`). |
| Guid text | `Engine/Core/Public/Misc/Guid.h:329` `ToString(EGuidFormats)`, default `Digits` :39 | Upper-case it explicitly (Q2). |
| Colour | `Engine/Core/Public/Math/Color.h:47` `FLinearColor`, `:381` `ToFColor(bool bSRGB)`, `:379` `ToFColorSRGB`, `:706` `FColor::ToHex()` (RRGGBBAA) | One constant `NativeMarkerColorSRGB` (Q1). |
| Drone subsystem | `FGDroneSubsystem.h:61` class, `:68` `Get(UWorld*)`, `:81` `GetAllStations()` -> `const TArray<AFGDroneStationInfo*>&` | Q5. Re-fetch per element; keep an index. |
| Drone info | `FGDroneStationInfo.h:159` class, `:190` `GetBuildingTag_Implementation` (field `mBuildingTag` :411; interface `FGBuildingTagInterface.h:46`), `:201` `GetPairedStation()`, `:252` `GetStation()` (server-only), `:208` `GetStationLocation()` | Call the interface wrapper or `_Implementation`; open the header first. |
| Drone buildable | `Buildables/FGBuildableDroneStation.h:37` class, `:145` `GetInputInventory`, `:149` `GetOutputInventory`, `:153` `GetFuelInventory` | The header's accessor comments and field comments disagree about input/output; the capture agrees with the fields (see Rulings, V9). |
| Inventory | `FGInventoryComponent.h:158` class, `:443` `GetSizeLinear`, `:326` `GetStackFromIndex`, `:104` `HasItems`, `:45` `GetItemClass`; `GetInventoryStacks` (already used for train cargo, `api-sources.md`) | Reuse the file-local item helper `TrainsReadItems` pattern; class name = `GetName()` with `_C` suffix. |
| Wire | `Buildables/FGBuildableWire.h:33` class (`UCLASS( Abstract )` :32), `:61` `GetConnection(int32)`, `:65` `GetConnectionLocation(int32)` (cached, replicated), `:57` `GetLength()` | Filter with `Cast<AFGBuildableWire>`; `FGBuildableSubsystem.h:363` `GetBuildableCount(AFGBuildableWire::StaticClass())` is advisory only. |
| Buildable list | `FGBuildableSubsystem.h:272` `Get(UWorld*)`, `:360` `GetAllBuildablesRef()` | S2/S3 facts: re-fetch every element. |
| Identity | `UObject::GetName()` `Engine/.../UObjectBaseUtility.h:429` | S2 facts. |
| Version | `SatisDash::AgentVersion()` (`Version.h:11`, `Version.cpp`): "mod/" + the .uplugin `VersionName` | Rows exist (`api-sources.md`). No new symbol. |
| Time | `FPlatformTime::Seconds` | Row exists. |

## Rulings

### The survey's 11 open questions (R-a: take every recommended answer)

| # | Question | Ruling and its reason | Task |
| --- | --- | --- | --- |
| Q1 | Marker colour `ToFColor(true)` or `(false)`? | **One constant `NativeMarkerColorSRGB` (default `true`, a guess the run settles).** The pure function takes the flag, tests pin both modes by round trip. The parity run reads the owner's green marker and the captured ones against FRM's `ColorSlot`; if it fails, the fix is one line. | 4, 14 |
| Q2 | Marker id case? | **Upper-case the Guid digits** so the id equals FRM's (the id join is exact). | 4, 6 |
| Q3 | Drone `name`: tag or representation text? | **The building tag**; parity compares name hashes with FRM's; fallback to the representation text only if they differ. | 8, 9 |
| Q4 | `pairedId` beside `paired`? | **Yes, both.** `pairedId` is the partner's id, null if unpaired, if the partner's buildable is gone, or if the partner is not in the kept layer. Judge rule: R-d (unique name match). | 7, 14 |
| Q5 | Drone enumeration: subsystem list or buildables? | **`AFGDroneSubsystem::GetAllStations()`**; fallback only if the list is empty on the test server (then a ruling PR switches to the buildable cast; not built here). | 9 |
| Q6 | Wires in `GetAllBuildablesRef()`? | **Assume yes; fail loudly.** A rate-limited Warning when the cast count is 0 or differs from the advisory `GetBuildableCount` by more than 5 percent, plus the parity id-set check against FRM's count. `GetWires` on the circuit connection is the fallback and needs its own survey (not built). | 12, 14 |
| Q7 | Share the geometry pass? | **Yes**: powerLines is a third flag in the S3 logistics pass. Markers and drone are separate small reads. | 11 |
| Q8 | Wire end order and null ends? | **Keep index order; drop a wire with a null end** (R-a), count it in `dropped`. | 12 |
| Q9 | `modVersion` shape? | **`SatisDash::AgentVersion()` as is** ("mod/0.1.0", 9 chars, inside the contract's 1 to 64). | 18 |
| Q10 | Marker cap? | **Unchanged**: the contract's 1,000 (`MapMarkersMaxItems`); the game caps at 250. | 4 |
| Q11 | Privacy? | **No name in any log or committed evidence**; names compared inside the harness as hashes. | 4, 7, 15 |

### The survey's NEEDS VERIFICATION items and where each is settled

| Item | Settled by |
| --- | --- |
| V1. Stamps count toward the 250 cap | Not needed (cap is far below the contract's); counts logged. |
| V2. Marker Guid text case | Q2: upper-cased in the reader; unit test pins it. |
| V3. `GetMapMarkers` complete right after load on a dedicated server | Parity id-set check against FRM's markers (Task 14); a short list shows as "FRM only". |
| V4. `FVector_NetQuantize` stored value | Parity x/y rule (one-metre allowance at .5 m). |
| V5. Colour sRGB or plain | Q1. |
| V6. A drone info whose `mStation` is null | The reader skips it and counts `Dropped`; shown in the read line. |
| V7. Drone id (`GetName()` of the buildable) equals FRM's `ID` | Parity id-set join (Task 14); a mismatch is a visible fail. |
| V8. Name = building tag | Q3 (name-hash equality). |
| V9. Drone input/output direction | The judge reports "input/output swapped" when the sets differ but equal swapped; the reader swaps in one line if so (Task 9 note). |
| V10. Position: actor location or info location | Parity x/y within 1 m; Task 9 uses the actor; flip is one line. |
| V11. Wire end location frame (world) | Parity "each end within 1 m" (world vs FRM). |
| V12. Wires in the buildable list | Q6. |
| V13. Wire id equals FRM `ID` | Parity id-set join. |
| V14. Wire end order (0 vs 1) | The judge reports index order or swapped; swapped is allowed (R-d) but counted. |
| V15. Zero cached location on a live wire | The reader drops only null ends (R-a); the judge's id-set shows any difference. |
| Status shape (Q9) | The contract (dash #740): fields on the snapshot body beside `reachable`; Task 18. |

### Rulings made by this plan beyond the owner's decisions

- **M1. Small reads are driven by the sender like `trainRoutes`.** The sender calls the layer's begin / step / abandoned / end; a layer whose source is `bridge` is never scheduled and nothing is logged. A finished read with zero items is a real empty layer and is posted (an empty marker list is real); `Abandoned` (no manager or subsystem, world changed) posts nothing and retries after `NodeScanRetrySeconds`.
- **M2. Markers, ports, wires are sorted by id** (code-unit order), a duplicate id keeps the first after sorting by (id, content), as the dash mappers do, so the same readings in any order give the same bytes.
- **M3. Marker name rule = the dash `markerName`:** replace a lone surrogate with U+FFFD, trim with JavaScript's whitespace set (not UE's), cut to 60 UTF-16 units, never splitting a pair. Done in `SatisDashCore`, vectors in tests.
- **M4. A marker with no valid Guid (`HasValidID()` false) is dropped and counted**, not given a derived id (that needs sha256, which Core has no row for); see Open questions.
- **M5. Drone ports follow the mapper's drops:** an id or name over 200 characters drops the port (counted); item classes are distinct, sorted, empty or over-200 skipped, cut at 64 (`TRAIN_ROUTES_MAX_ITEM_CLASSES`).
- **M6. Names and ids in the harness captures:** every name is replaced by `sessionHash(name)` before it reaches `native-parity.json`; ids stay (game object names).
- **M7. The shared pass pulls the other native layer forward (S3 M6) and now also powerLines.**

## Review Focus

- **No markers, no ports, no wires, or still loading:** zero buildables is unreadable (logistics, M5 of S3); no markers or no ports posts a real empty layer; no manager or subsystem is abandoned. Pinned in `NativeMapMarkers.Rules`, `WorldSender.MapMarkers`, `WorldSender.DroneRoutes`, `WorldSender.LogisticsPowerLines`.
- **Player text:** a name with an emoji at the 60-unit cut, a lone surrogate, leading NBSP; none reaches a log. Pinned in `NativeMapMarkers.Map`; the log-line tests assert no id or name.
- **Stale links:** a port whose partner is gone, a partner outside the kept layer, an unpaired port. Pinned in `NativeDroneRoutes.Map`.
- **Dismantled wires or a null end mid-pass:** weak pointers checked every element, null-end wires dropped and counted. Pinned in `NativePowerLines.Rules`; game side carried by the runs.
- **One layer native and the others not:** with only powerLines native the pass serves it alone; with rails native and powerLines bridge the pass is S3's. Pinned in `WorldSender.LogisticsPowerLines`, `Config.Sources`.

## How the tests run

As in S3: UE tests via `pwsh scripts/test-mod.ps1 -Project <sml-project> -Engine <engine-root> -Out <tmp>\mod-test` then `cd harness; npm run harness -- judge-mod-test <tmp>\mod-test --min <ledger N>` (the executor's own paths, never committed; one build per task at most). `check-api` and `check-provenance` end every task that touches `mod/`. Harness: `npx vitest run <file> --reporter=dot` while iterating; the whole suite and `npm run typecheck` at the end of Tasks 1, 13 to 17. Dispatching `mod-test` is the owner's. The Game module (`GameReader.cpp`) is not unit-testable here: its correctness is carried by the parity and tick-cost runs, and each game-side task says so.

**PRs** (each independently reviewable; each carries the checkbox "FRM source was not opened"):
- **PR S4A-1 (Tasks 1-3):** re-vendor the contract, the three source keys and the `Readers:` line, the three layer rows. Changes no reader behaviour.
- **PR S4A-2 (Tasks 4-6):** R8 mapMarkers: pure builder, the sender's small-read path and the port, the game-side read.
- **PR S4A-3 (Tasks 7-9):** R9 droneRoutes: pure builder (with `pairedId`), sender and port, the game-side read.
- **PR S4A-4 (Tasks 10-12):** R10 powerLines: pure builder, the third pass flag, the game-side read with the cast-count check.
- **PR S4A-5 (Tasks 13-17):** the FRM oracle and counts, the S4 judges, the parity drive and verdicts, tick-cost, docs.
- **PR S4A-6 (Task 18):** the status fields (top-level snapshot fields, contract already vendored by Task 1).

## File structure

`mod/...` is `mod/SatisDashAgent/Source`.

| File | New/Modified | Responsibility |
| --- | --- | --- |
| `harness/contract/manifest.json`, `harness/contract/src/*.ts` | Modified (T1) | Re-vendored contract (dash `a5dd42c` or later) |
| `mod/SatisDashCore/Public/SatisDash/Config.h`, `Private/Config.cpp`, `Private/Driver.cpp`, `harness/src/reader-lines.ts`, `core-run.ts`, `harness/test/mod-config.test.ts` | Modified (T2) | The three source keys, the nine-field `Readers:` line, `NATIVE_SOURCES_S4` |
| `Public/SatisDash/FrmWorld.h`, `Private/FrmWorld.cpp` | Modified (T3) | Three layer rows, `WorldLayerCount = 7` |
| `Native/MapMarkers/NativeMapMarkers.h/.cpp`, `PROVENANCE.md`, `NativeMapMarkersTest.cpp` | New (T4) | Name rule, colour, mapping, exact JSON |
| `Public/SatisDash/Ports.h`, `WorldSender.h/.cpp`, `Driver.h/.cpp`, `DriverFakes.h`, `WorldSenderTest.cpp`, `DriverNativeTest.cpp` | Modified (T5, T8, T11) | Small-read ports, sender path, the third pass flag |
| `SatisDashGame/Private/GameReader.h/.cpp` | Modified (T6, T9, T12) | The game-side reads |
| `Native/DroneRoutes/NativeDroneRoutes.h/.cpp`, `PROVENANCE.md`, `NativeDroneRoutesTest.cpp` | New (T7) | Mapping, `pairedId`, exact JSON |
| `Native/PowerLines/NativePowerLines.h/.cpp`, `PROVENANCE.md`, `NativePowerLinesTest.cpp` | New (T10) | Mapping, exact JSON |
| `harness/src/frm-s4.ts`, `harness/test/frm-s4.test.ts` | New (T13) | FRM oracles (names hashed) |
| `runs/native-parity/pass-s4.ts`, `pass.ts`, `harness/src/core-runs/native-parity.ts` | New/Modified (T14, T15) | Judges, drive, verdicts |
| `harness/src/core-runs/tick-cost-native.ts`, `runs/tick-cost-native/pass.ts` | Modified (T16) | The two step lines, `NATIVE_SOURCES_S4` |
| `docs/native-readers.md`, `docs/runner.md`, `api-sources.md`, `.github/workflows/mod-test.yml`, `harness/test/workflows.test.ts` | Modified (T2-T18) | Status table, rows, floor |
| `Public/SatisDash/Snapshot.h`, `Private/Snapshot.cpp`, `Private/Driver.cpp`, `SnapshotTest.cpp` | Modified (T18) | The status fields |

---

## PR S4A-1

### Task 1: Re-vendor the contract at dash main (includes #739)

**Files:**
- Modify: `harness/contract/manifest.json`, `harness/contract/src/*.ts` (written by the script), `docs/runner.md` (one line: the pin), any plan/provenance text that quotes the old pin only if it states "vendored at" as current (do not rewrite history in old plans).
- Test: existing `harness/test/contract.test.ts`, `harness/test/backend.test.ts`.

**How vendoring is done (read from the code):** `harness/scripts/sync-contract.ts` (`npm run sync-contract -- --commit <40-hex dash sha>`, from `harness/`) calls `vendorContract` (`harness/src/contract.ts`): it fetches `packages/shared/src/agent.ts` and every same-folder module it imports (following `from "./x"`), through `gh api repos/Sour-Dev-Home/satisfactory-dash/contents/...?ref=<sha>`, replaces `harness/contract/src/`, and writes `manifest.json` with the commit and each file's SHA-256. `npm run harness -- verify-contract` checks it. `contractPin()` (`harness/src/backend.ts`) reads the commit; core runs refuse a backend image that does not descend from it.

- [ ] **Step 1:** Take the dash commit: the head of dash main at execution time, which must be `a5dd42cf3395582dce7adfbc6b1ebbfe52676b90` (it contains #739 `pairedId` and #740 the status fields) or a descendant (`gh api repos/Sour-Dev-Home/satisfactory-dash/compare/a5dd42c...<sha> --jq .status` is `ahead` or `identical`).
- [ ] **Step 2:** From `harness/`: `npm run sync-contract -- --commit <sha>`, then `npm run harness -- verify-contract` (0 problems).
- [ ] **Step 3: Failing-first check.** Before and after, a small Vitest in `harness/test/contract.test.ts`: the vendored `map.ts` exports `MapMarkersLayerDataSchema`, `DroneRoutesLayerDataSchema`, `PowerLinesLayerDataSchema`, `KNOWN_MAP_WORLD_LAYERS` containing all seven names, `DronePortSchema` accepts `pairedId: null` and a string and omits it, and `SnapshotRequestSchema` accepts a minimal valid body with and without the three top-level fields `gameReachable`, `lastGameReadAt`, `modVersion` (and rejects `modVersion: ""` and a 65-character one); use only literals from the schemas' own descriptions, no FRM value. It fails on the old copy.
- [ ] **Step 4:** whole Vitest suite and `npm run typecheck`. **If any existing test or judge fails because a vendored schema changed, stop and report it with the diff (rule 3); do not edit a judge.** Step 5: commit `Re-vendor the dash contract at <short sha> (S4-A: mapMarkers, droneRoutes, powerLines, pairedId)`.

**Owner step (not a task):** the runner's `APP_IMAGE`/`OPS_IMAGE` must be built from the new pin or a descendant before `native-parity` (the image guard in `core-run.ts` refuses otherwise).

**Test count:** 186 UE tests unchanged. **Reviewer checks:** `manifest.json` commit is a dash main commit including #739; the diff is only the files the script writes; no `harness/src` judge touched; no path or personal data.

### Task 2: The three source keys, the `Readers:` line, `NATIVE_SOURCES_S4`

**Files:**
- Modify: `mod/SatisDashCore/Public/SatisDash/Config.h` (`DefaultMapMarkersSource`, `DefaultDroneRoutesSource`, `DefaultPowerLinesSource` all `ESource::Bridge`; `TOptional<ESource> MapMarkersSource, DroneRoutesSource, PowerLinesSource;` and `GetMapMarkersSource()`, `GetDroneRoutesSource()`, `GetPowerLinesSource()`; the `BadField` comment lists the keys), `Private/Config.cpp` (parse and write back exactly as `conveyorsSource` via `ConfigReadSource`; unset not written back; a bad value makes the config `Broken` with `BadField` the key), `Private/Driver.cpp` (the `Readers:` line becomes `... conveyors=%s mapMarkers=%s droneRoutes=%s powerLines=%s`)
- Modify: `harness/src/reader-lines.ts` (`ReaderSources` gains `mapMarkers?`, `droneRoutes?`, `powerLines?`; the `READERS` regex gains an optional nested group ` mapMarkers=(\S+) droneRoutes=(\S+) powerLines=(\S+)` after `conveyors=`, all three or none; `partLogProblems` part type widens; `mapMarkersLogProblems`, `droneRoutesLogProblems`, `powerLinesLogProblems`), `harness/src/core-run.ts` (`NativeSources` gains the three optional keys; `export const NATIVE_SOURCES_S4: NativeSources = { ...NATIVE_SOURCES_S3, mapMarkersSource: "native", droneRoutesSource: "native", powerLinesSource: "native" }`; S1 to S3 constants unchanged), `docs/native-readers.md` (three rows, source `bridge`, "layer not sent"; the override list gains the keys)
- Test (existing cases extended, no new macro): `SatisDashCoreTests/Private/ConfigTest.cpp` (`SatisDash.Core.Config.Sources`), `DriverNativeTest.cpp` (the case asserting the `Readers:` line), `harness/test/reader-lines.test.ts`, `harness/test/mod-config.test.ts` (the drift guard: keys the harness writes equal keys the mod reads), `harness/test/native-parity-pass.test.ts` (a `NATIVE_SOURCES_S4` equality case next to the S3 one)

**Interfaces:** Produces `FAgentConfig::GetMapMarkersSource()`, `GetDroneRoutesSource()`, `GetPowerLinesSource()`, the `Default*` constants, `NATIVE_SOURCES_S4`, the three `*LogProblems`.

- [ ] **Step 1: Failing tests.** `Config.Sources`: for each new key the bad values `"Native"`, `""`, `1`, `null` make the config `Broken` naming the key; `"native"` round-trips through `ConfigToJson`; an unset key is not written back; with no key the getter is `Bridge`. Driver case: default config reads `Readers: ... conveyors=native mapMarkers=bridge droneRoutes=bridge powerLines=bridge` (rails and conveyors default native since the S3 switch; copy the exact existing prefix from the test). Vitest: the six-field line still parses (new fields undefined); the nine-field line yields them; a partial extra group (one of three) does not parse as nine fields; `mapMarkersLogProblems` of a six-field line says `has no mapMarkers= field`, of `mapMarkers=native` is empty, of `bridge` says `not native`.
- [ ] **Step 2:** run (compile errors and Vitest fail). **Step 3: Implement.** **Step 4:** `judge-mod-test --min 186`, Vitest, `check-api`. **Step 5:** commit `mapMarkersSource, droneRoutesSource, powerLinesSource (default bridge) and the nine-field Readers line`.

**Test count:** 186 (cases extended). **Reviewer checks:** unset means bridge; S1 to S3 constants and tests untouched (assert deep equality to their old values); the config error names the key; the older line shapes still parse.

### Task 3: Three layer rows and `WorldLayerCount = 7`

**Files:**
- Modify: `Public/SatisDash/FrmWorld.h` (`enum class EWorldLayer : uint8 { Rails, ResourceNodes, TrainRoutes, Conveyors, MapMarkers, DroneRoutes, PowerLines }`, `WorldLayerCount = 7`; the comment "Conveyors is native-only" extends to the three), `Private/FrmWorld.cpp` (rows `{ TEXT("mapMarkers"), nullptr, 480.0, 600.0, 0.0 }`, `{ TEXT("droneRoutes"), nullptr, 480.0, 600.0, 0.0 }`, `{ TEXT("powerLines"), nullptr, 480.0, 600.0, 0.0 }`; null endpoint, `FirstSeconds` 480 so no `NativeFirstSeconds` is needed, interval 600 from the dash `worldReader.ts`), `Private/WorldSender.cpp` (`WorldSenderIsNative` switch gains the three cases via the new getters; nothing else yet: a native layer with no scheduler branch must not be reachable, so each new case returns `false` until Tasks 5, 8, 11 turn it on, with a comment saying so)
- Test: `FrmWorldTest.cpp` (`SatisDash.Core.FrmWorld.LayerTable` extended: seven rows; `WorldLayerCount == 7`), `WorldSenderTest.cpp` existing cases still green (arrays are sized by `WorldLayerCount`).

- [ ] **Step 1: Failing test:** the extended `LayerTable`. **Step 2-4:** run, implement, grep for any array sized by a literal 4 (`WorldLayerCount`, `[4]`; `FrmBridge.*`), `judge-mod-test --min 186`, `check-api`. **Step 5:** commit `Three world-layer rows (mapMarkers, droneRoutes, powerLines)`.

**Test count:** 186. **Reviewer checks:** rails, nodes, trainRoutes, conveyors rows unchanged; no layer starts reading yet; `trainRoutes`' and the others' schedules untouched.

---

## PR S4A-2

### Task 4: R8, the pure markers reading: name rule, colour, mapping, exact JSON

**Files:**
- Create: `Public/SatisDash/Native/MapMarkers/NativeMapMarkers.h`, `Private/Native/MapMarkers/NativeMapMarkers.cpp`, `Private/Native/MapMarkers/PROVENANCE.md`, `SatisDashCoreTests/Private/NativeMapMarkersTest.cpp`
- Modify: `.github/workflows/mod-test.yml` and `harness/test/workflows.test.ts` (`--min 190`), `api-sources.md` (the `FLinearColor` / `FColor` rows)

**Interfaces** (produces; layout follows `NativeRails.h`, `FNativeWorldBody`):
```cpp
inline constexpr int32 MapMarkersMaxItems = 1000;      // map.ts MAP_MARKERS_MAX_ITEMS (Q10)
inline constexpr int32 MapMarkerNameMaxChars = 60;      // map.ts MAP_MARKER_NAME_MAX_CHARS
inline constexpr int32 MapMarkerMaxIcon = 1000000;
/** Q1: which conversion turns the game's FLinearColor into the contract's #rrggbb. Parity decides; one constant flips it. */
inline constexpr bool NativeMarkerColorSRGB = true;
enum class EMapMarkerKind : uint8 { Marker, Stamp };
struct FMapMarkerReading
{
	FString Id;                // Guid digits, upper case (Q2); empty = no valid Guid (M4)
	FString Name;              // raw, as the game gives it
	EMapMarkerKind Kind = EMapMarkerKind::Marker;
	int32 Icon = 0;
	FLinearColor Color = FLinearColor::White;
	double XCm = 0, YCm = 0;
};
/** The dash markerName: lone surrogate -> U+FFFD, JavaScript trim, cut to 60 UTF-16 units without splitting a pair (M3). */
SATISDASHCORE_API FString MapMarkerName(const FString& Raw);
/** "#rrggbb", lower case: Color.ToFColor(bSRGB) then the first six of ToHex (alpha dropped); NaN or non-finite channels give "#ffffff". */
SATISDASHCORE_API FString MapMarkerColorHex(const FLinearColor& Color, bool bSRGB);
/** The mapMarkers world body {"observedAt":...,"data":[{"id","name","kind","icon","color","x","y"}]}. */
SATISDASHCORE_API FNativeWorldBody BuildNativeMapMarkers(const FString& ObservedAt, const TArray<FMapMarkerReading>& Readings);
```
`BuildNativeMapMarkers` rules (the dash `mapMarkersMapper.ts`, ported): key order `id, name, kind, icon, color, x, y` (the contract's); x/y via `ToWholeMetres` (JS `Math.round`, `-0` printed `0`); a marker outside +/-1,000,000 m (`InWholeMetreBounds`), with an icon outside 0..1,000,000, or an empty id, is dropped and counted in `Dropped`; id cut with `ClampString` (200); name through `MapMarkerName`; kind `stamp` / `marker`; sorted by (id, then the item's JSON text) in code-unit order, a repeated id keeps the first; more than 1,000 keep 1,000 and count `Over`; `Items` is the kept count; written with `WorldBodyPrefix`.

- [ ] **Step 1: Failing tests (+4 = 190).**
  - `NativeMapMarkers.Name`: `MapMarkerName`: `"  a "` -> `a`; leading U+00A0 and U+FEFF and U+2028 trimmed (JS whitespace), a tab and newline trimmed; a 61-character ASCII name cut to 60; 59 ASCII characters then an emoji (a surrogate pair) cut to 59 (the pair is dropped whole); 60 units ending exactly on a pair kept; a lone high surrogate in the middle -> U+FFFD; empty -> empty.
  - `NativeMapMarkers.Color`: round trip by construction, no hard-coded float: for `FColor(0x6E,0xA3,0x7B)`, `FLinearColor::FromSRGBColor(c)` through `MapMarkerColorHex(..., true)` is `#6ea37b`; `FLinearColor(c)` (plain division) through `(..., false)` is `#6ea37b`; white and black are the same in both modes; the two modes differ for the green; alpha never appears; a NaN channel gives `#ffffff`; `NativeMarkerColorSRGB == true` pins the default so a flip is a visible diff.
  - `NativeMapMarkers.Map`: one reading per case, exact JSON: a marker (`RT_MapMarker`) at `XCm = -1246855.859375`, `YCm = 303000` -> `x` `-12469`, `y` `3030`; a stamp -> `"kind":"stamp"`; rounding table x `150` -> `2`, `-150` -> `-1`, `50` -> `1`, `-50` -> `0`; an icon of 2,000,000 drops; id `3FABC3BA...` style (32 upper-case hex, synthetic) is kept as given; a 250-character id cut to 200. Names in the tests are synthetic (`"Alpha"`, `"Iron outpost"`), never taken from a capture.
  - `NativeMapMarkers.Rules`: an empty list gives `"data":[]`, `Items == 0`; 1,001 valid markers keep 1,000, `Over == 1`; the same 20 shuffled readings in any order give identical `Json`; two readings with the same id and different content give the same bytes in both input orders; an empty id drops (`Dropped == 1`); a golden: three markers built from the shape of `frm-getMapMarkers-2026-09-27-full.json` (dash `captured-responses`, FRM HTTP output only; synthetic names) equal a hand-written expected string, parsed and compared as JSON.
- [ ] **Step 2: Run to see them fail.** **Step 3: Implement.** **Step 4:** `judge-mod-test --min 190`, raise the workflow floor and its pinned test to 190, `check-api`, `check-provenance`.
- [ ] **Step 5: PROVENANCE.md** in `Native/MapMarkers/`: `Reader: MapMarkers`; `Code:` the two new files, `.../SatisDashGame/Private/GameReader.cpp`, `.../SatisDashCore/Private/WorldSender.cpp`; `Headers:` the marker rows of the Lookups table; `Survey: mod-native-readers-s4a-headers-2026-10-27.md`; `Captures: frm-getMapMarkers-2026-09-27-full.json (satisfactory-dash captured-responses; FRM HTTP output only; the fixture is synthetic in its shape)`; `Facts:` the contract (`MapMarkerSchema`, `MAP_MARKERS_MAX_ITEMS`) and the dash `mapMarkersMapper.ts` (our own code) are the rules; no field name is taken from FRM source; `Author:`, `Date:`; `FRM source was not opened`. Commit `MapMarkers: pure name, colour and layer builder (R8)`.

**Test count after:** 190. **Reviewer checks:** every rule equals `mapMarkersMapper.ts` line for line; key order equals the contract's; no name from a capture in any test; no `check()` on outside data; provenance complete.

### Task 5: The small-read path in the sender and the markers port (mapMarkers only)

**Files:**
- Modify: `Public/SatisDash/Ports.h` (the markers port and `FMapMarkersSnapshot`), `Public/SatisDash/WorldSender.h` (`FWorldContext` gains `FStepBudget* MarkersBudget = nullptr;` and, for Task 8, `FStepBudget* DronesBudget = nullptr;` appended after `LogisticsBudget` so the existing positional initialiser still compiles), `Private/WorldSender.cpp`, `Public/SatisDash/Driver.h` (`FStepBudget MarkersBudget, DronesBudget;` beside `RoutesBudget`), `Private/Driver.cpp` (the context initialiser passes them; where `EndTrainsRead()` / `EndLogisticsRead()` are called on leaving Running, also `EndMapMarkersRead()`), `SatisDashCoreTests/Private/DriverFakes.h`
- Tests: `WorldSenderTest.cpp` (`SatisDash.Core.WorldSender.MapMarkers`), `DriverNativeTest.cpp` (`SatisDash.Core.Driver.NativeMapMarkers`) (+2 = 192)
- Modify: `.github/workflows/mod-test.yml`, `harness/test/workflows.test.ts` (`--min 192`), `harness/src/reader-lines.ts` (`STEP_LINE_NAMES` gains `"Map markers read"` and `"Drone routes read"` so Task 8 needs no second edit; test), `docs/native-readers.md`

**Interfaces:**
```cpp
// Ports.h
struct FMapMarkersCounts { int32 Read = 0, Dropped = 0; };      // new
struct FMapMarkersSnapshot { TArray<FMapMarkerReading> Markers; FMapMarkersCounts Counts; };   // new
// IGameReader, defaults do nothing:
virtual bool BeginMapMarkersRead() { return false; }   // false: no world / no manager, or a read is open
virtual TOptional<FMapMarkersSnapshot> StepMapMarkersRead(FStepBudget& Budget) { return {}; }
virtual bool MapMarkersReadAbandoned() const { return false; }
virtual void EndMapMarkersRead() {}
virtual bool IsMapMarkersReadOpen() const { return false; }
```
Sender behaviour (new private `StepSmallLayerScan(EWorldLayer, const FWorldContext&)`, one switch on the layer; Task 8 adds the DroneRoutes case): `WorldSenderIsNative(MapMarkers)` is true when `GetMapMarkersSource() == Native` and a reader is present (Task 3's `false` becomes the real test). In `Waiting`, a due native layer with `bGameRunning` and the reader not busy calls `BeginMapMarkersRead()`, resets `MarkersBudget`, goes `Scanning`; on false it waits `NodeScanRetrySeconds`. In `Scanning`: source changed -> `EndMapMarkersRead()` and `ScheduleNext`; else `StepMapMarkersRead(*MarkersBudget)`; unset: return; abandoned: one rate-limited Warning (`World layer mapMarkers: the game's read was abandoned; nothing sent, will read again in %.0f s`, no names), `Waiting`, `NextAt = Now + NodeScanRetrySeconds`; else log `Map markers read (game thread only): %d markers, %d dropped in %d steps, longest step %.2f ms, total %.2f ms` (counts and times only) and `Bridge.SubmitNativeWorld(MapMarkers, Utc.ToIso8601(), FNativeWorldBuilder([Snapshot = MoveTemp(Found->Markers)](const FString& O){ return BuildNativeMapMarkers(O, Snapshot); }), L.DataHash)` -> `Reading`, or `ScheduleNext` when refused. A finished read with zero markers is posted (M1). The first read is at `FirstSeconds` 480 s +/-10% (no `NativeFirstSeconds` needed).

`DriverFakes.h`: the fake game reader gains `int32 MarkersStepsToFinish`, `TArray<FMapMarkerReading> Markers`, `FMapMarkersCounts MarkersCounts`, `bool bMarkersAbandoned`, `bool bBeginMarkersOk = true`, Begin/End counters.

- [ ] **Step 1: Failing tests.**
  - `WorldSender.MapMarkers` (`mapMarkersSource` native): nothing is begun before 432 s after `Start` (`Random01 = 0` gives 0.9 x 480) and `BeginMapMarkersRead` is called once at 433 s; the fake needing 3 steps is stepped once per tick; a `mapMarkers` body is posted to `/agent/v1/world/mapMarkers` once with `"id"` values sorted; FRM is never requested; a second read with the same readings does not repost (same data hash); an abandoned read posts nothing, warns once, retries after `NodeScanRetrySeconds`; an empty finished read posts `"data":[]`; with the default config (key unset = bridge) the layer is never begun, posted, requested or logged; flipping the source to bridge under an open read drops it (`EndMapMarkersRead` called, layer rescheduled).
  - `Driver.NativeMapMarkers`: through the whole driver with the fake game: snapshots unaffected, `Readers:` shows `mapMarkers=native`, leaving Running calls `EndMapMarkersRead`.
- [ ] **Step 2-4:** run (fail), implement, `judge-mod-test --min 192`, raise the floor, Vitest for `STEP_LINE_NAMES` (a `Map markers read` sample line parses; 2.05 ms fails `stepProblems`), `check-api`. **Step 5:** commit `Small sliced reads in the world sender (mapMarkers)`.

**Test count after:** 192. **Reviewer checks:** the other layers' behaviour and tests unchanged; no pointer or reading survives after the builder runs; the budget is reset when a read begins; the warning and the log carry no id or name; `FWorldContext` initialiser order preserved.

### Task 6: R8, the game-side sliced markers read

**Files:**
- Modify: `SatisDashGame/Private/GameReader.h/.cpp` (`BeginMapMarkersRead`, `StepMapMarkersRead`, `MapMarkersReadAbandoned`, `EndMapMarkersRead`, `IsMapMarkersReadOpen`), `Private/Native/MapMarkers/PROVENANCE.md` (headers), `api-sources.md`, `docs/native-readers.md`

**Interfaces:** consumes `FMapMarkerReading`, `FMapMarkersSnapshot`, `FStepBudget`; produces the game side of Task 5's port. Not unit-testable; carried by the parity and tick-cost runs.

Game side (open every header in the Lookups table first and add its `api-sources.md` row with `path:line` before using the symbol; header wins):
- `BeginMapMarkersRead()`: false when a read is open, there is no `UWorld`, or `AFGMapManager::Get(World)` is null. Store the weak world, reset the cursor, copy the markers once: `AFGMapManager* Manager = ...; TArray<FMapMarker> Copy; Manager->GetMapMarkers(Copy);` (at most 250; the manager call is public and non-const, so the pointer is non-const) into a member `TArray<FMapMarker> MarkersCopy` (this copy is the only game struct held across ticks: 250 small structs; the `FString`s are freed on end).
- `StepMapMarkersRead(Budget)`: `Budget.Begin()`; `do { ... } while (!Budget.Spent());` then `Budget.End()`; one marker per iteration from the copy: `Reading.Id = Marker.HasValidID() ? Marker.MarkerGUID.ToString(EGuidFormats::Digits).ToUpper() : FString()`, `Name = Marker.Name`, `Kind = Marker.MapMarkerType == ERepresentationType::RT_Stamp ? Stamp : Marker`, `Icon = Marker.IconID`, `Color = Marker.Color`, `XCm/YCm = Marker.Location.X/Y`. Never read the other fields. When the cursor passes the end: `Counts.Read` = markers read; return the snapshot (moved), free the copy. World changed or manager gone: abandoned, empty snapshot with `MapMarkersReadAbandoned() == true`.
- Counts only in logs; the sender writes the line. The Dropped count comes from the builder, not here (`FMapMarkersCounts.Dropped` is set to the number with `HasValidID() == false`).

- [ ] **Step 1:** rows first (`check-api` `0 missing`), including the includes and any module dependency. **Step 2:** implement; `PROVENANCE.md` `Headers:` lines; keep `Code:` lines true. **Step 3:** `judge-mod-test --min 192`, `check-api`, `check-provenance`. **Step 4:** docs row ("Map markers (R8) | S4-A | `bridge` (default = layer not sent) | one small sliced read; first read 480 s"). Commit `MapMarkers: game-side sliced read (R8)`.

**Test count after:** 192. **Reviewer checks:** manager null-checked; no PII field read (`LastEditedBy`, `CreatedByPlayerID`); the Guid upper-cased; no name or id in any log; rows complete.

---

## PR S4A-3

### Task 7: R9, the pure drone-routes reading: mapping, `pairedId`, exact JSON

**Files:**
- Create: `Public/SatisDash/Native/DroneRoutes/NativeDroneRoutes.h`, `Private/Native/DroneRoutes/NativeDroneRoutes.cpp`, `Private/Native/DroneRoutes/PROVENANCE.md`, `SatisDashCoreTests/Private/NativeDroneRoutesTest.cpp`
- Modify: `.github/workflows/mod-test.yml`, `harness/test/workflows.test.ts` (`--min 196`)

**Interfaces** (produces):
```cpp
inline constexpr int32 DroneRoutesMaxItems = 2000;       // map.ts DRONE_ROUTES_MAX_ITEMS
inline constexpr int32 DroneRoutesMaxItemClasses = 64;   // map.ts TRAIN_ROUTES_MAX_ITEM_CLASSES (the item-class cap the port schema reuses)
struct FDronePortReading
{
	FString Id;                      // buildable GetName()
	FString Name;                    // the building tag, as the game gives it
	bool bPaired = false;
	FString PairedName;              // the partner's tag; meaningful when bPaired
	FString PairedId;                // the partner buildable's GetName(); empty = unresolved
	TArray<FString> Input, Output, Fuel;   // item class names, any order, duplicates allowed
	double XCm = 0, YCm = 0;
};
SATISDASHCORE_API FNativeWorldBody BuildNativeDroneRoutes(const FString& ObservedAt, const TArray<FDronePortReading>& Readings);
```
Rules (the dash `droneRoutesMapper.ts`, ported, plus `pairedId`): key order `id, name, paired, pairedId, input, output, fuel, x, y` (`pairedId` is written always, `null` when unresolved: the contract allows an explicit null, "unpaired or can't be resolved to exactly one port"); `paired` is `null` unless `bPaired`, the empty name, or over 200 characters; **`pairedId` is the partner's id only if that id is the id of another port that is kept in this same layer, else `null`** (so it can never name a dropped or missing port); an id or name over 200 characters, or a position outside +/-1,000,000 m, drops the port (`Dropped`); item classes distinct, sorted in code-unit order, empty or over-200 skipped, cut at 64; x/y whole metres via `ToWholeMetres`; ports sorted by (id, content), a repeated id keeps the first; over 2,000 keep 2,000 and count `Over`; written with `WorldBodyPrefix`.

- [ ] **Step 1: Failing tests (+4 = 196).**
  - `NativeDroneRoutes.Map`: from the shape of `frm-getDroneStation-2026-10-25-paired-*-trimmed.json` and `...-unpaired-trimmed.json` (synthetic names): a home port with 3 `Desc_Battery_C` in `Input` -> `"input":["Desc_Battery_C"]` (deduplicated), a partner with empty input and `Output` holding a battery, an unpaired port -> `"paired":null,"pairedId":null`; the two paired ports carry each other's id in `pairedId` and each other's name in `paired`; `x` rounding table as for markers.
  - `NativeDroneRoutes.PairedId`: the partner's id not in the layer (partner dropped for a 201-character name) -> `pairedId:null` while `paired` keeps the name; a port paired to itself -> `pairedId` equals its own id (allowed, reported only by the parity judge); `bPaired` true with an empty `PairedName` -> `paired:null`.
  - `NativeDroneRoutes.Rules`: empty list gives `"data":[]`; 2,001 ports keep 2,000, `Over == 1`; shuffled input gives identical `Json`; duplicate id keeps one; 65 distinct classes cut to 64; an item class of 201 characters skipped; id of 201 characters, name of 201 characters, `x` of `1.0e12` cm each drop and count.
  - `NativeDroneRoutes.Golden`: three ports (paired pair plus an unpaired) equal the hand-written expected text, parsed and compared as JSON, and **validated by shape against `droneRoutesSampleWithPairedId`** (dash `packages/shared/fixtures/map.ts`; the test copies that fixture's shape, not its values).
- [ ] **Step 2-4:** run (fail), implement, `judge-mod-test --min 196`, raise floor, `check-api`, `check-provenance`. **Step 5: PROVENANCE.md** (`Reader: DroneRoutes`; `Captures:` the three drone captures; `Facts:` the contract (`DronePortSchema`, map.ts :216, with #739's `pairedId`) and `droneRoutesMapper.ts`). Commit `DroneRoutes: pure layer builder with pairedId (R9)`.

**Test count after:** 196. **Reviewer checks:** `pairedId` can never name a port outside the layer; key order; `paired` is the name (unchanged contract field); no capture name in tests; provenance complete.

### Task 8: The drone-routes port and the sender's second small layer

**Files:**
- Modify: `Ports.h` (drone port and `FDroneRoutesSnapshot`), `WorldSender.cpp` (`StepSmallLayerScan` gains the `DroneRoutes` case, `WorldSenderIsNative(DroneRoutes)` real), `Driver.cpp` (`EndDroneRoutesRead()` on leaving Running), `DriverFakes.h`
- Tests: `WorldSenderTest.cpp` (`SatisDash.Core.WorldSender.DroneRoutes`, +1 = 197); extend `Driver.NativeMapMarkers`' sibling assertions with the `droneRoutes=native` line (no new macro)
- Modify: floor `--min 197` (workflow and its test), `docs/native-readers.md`

**Interfaces:**
```cpp
struct FDroneRoutesCounts { int32 Read = 0, Dropped = 0; };       // Dropped: stations skipped game-side (null buildable, V6)
struct FDroneRoutesSnapshot { TArray<FDronePortReading> Ports; FDroneRoutesCounts Counts; };
virtual bool BeginDroneRoutesRead() { return false; }
virtual TOptional<FDroneRoutesSnapshot> StepDroneRoutesRead(FStepBudget& Budget) { return {}; }
virtual bool DroneRoutesReadAbandoned() const { return false; }
virtual void EndDroneRoutesRead() {}
virtual bool IsDroneRoutesReadOpen() const { return false; }
```
Log line: `Drone routes read (game thread only): %d ports, %d dropped in %d steps, longest step %.2f ms, total %.2f ms`.

- [ ] **Step 1: Failing test** `WorldSender.DroneRoutes`: the same sequence as `WorldSender.MapMarkers` (432/433 s, one step a tick, sorted body posted to `/world/droneRoutes`, no repost on the same hash, abandoned retry, unset source = never scheduled), plus: both small layers native read independently (two begins, two budgets, no interference) and each posts once. **Step 2-4:** fail, implement, `judge-mod-test --min 197`, raise floor, `check-api`. **Step 5:** commit `Small sliced read in the world sender (droneRoutes)`.

**Test count after:** 197. **Reviewer checks:** the markers path and its tests untouched; the two budgets separate; nothing posted for a bridge source.

### Task 9: R9, the game-side sliced drone read

**Files:**
- Modify: `GameReader.h/.cpp`, `Private/Native/DroneRoutes/PROVENANCE.md`, `api-sources.md`, `docs/native-readers.md`

Game side (headers first; header wins):
- `BeginDroneRoutesRead()`: false when a read is open, no `UWorld`, or `AFGDroneSubsystem::Get(World)` is null. Store the weak world, cursor 0.
- `StepDroneRoutesRead(Budget)`: one station per iteration. Re-fetch `Subsystem->GetAllStations()` every iteration (never kept), take `Info = Stations[Cursor++]`; `!IsValid(Info)` skip. `AFGBuildableDroneStation* Station = Info->GetStation();` null: `++Counts.Dropped`, skip (V6). `Reading.Id = Station->GetName()`; `Reading.Name = <building tag via the interface wrapper or GetBuildingTag_Implementation, as the header requires>.ToString()` (Q3); `Paired = Info->GetPairedStation()`: valid -> `bPaired = true`, `PairedName` = its tag, `PairedId` = `Paired->GetStation()` valid ? its `GetName()` : empty; `Input/Output/Fuel` from `GetInputInventory()/GetOutputInventory()/GetFuelInventory()` through the file-local item helper (`TrainsReadItems`' pattern: distinct class names of non-empty stacks; a second helper name with the unity-build prefix `Drone`, or reuse the existing one if it is in the same anonymous namespace); `XCm/YCm = Station->GetActorLocation()` (V10; the info's `GetStationLocation()` is the one-line flip). V9: if the parity judge later reports input/output swapped, swap the two getters here and record both readings in the `api-sources.md` row. Finish: `Counts.Read` = ports read; abandoned on world/subsystem change.
- Names and ids only in the snapshot; nothing logged here.

- [ ] **Step 1:** rows first. **Step 2:** implement; `PROVENANCE.md` headers. **Step 3:** `judge-mod-test --min 197`, `check-api`, `check-provenance`. **Step 4:** docs row. Commit `DroneRoutes: game-side sliced read (R9)`.

**Test count after:** 197. **Reviewer checks:** every pointer null-checked at each hop (info, station, partner info, partner station, inventory); inventory amounts never read into the reading; no name in a log; the interface call matches the header (`BlueprintNativeEvent`).

---

## PR S4A-4

### Task 10: R10, the pure power-lines reading

**Files:**
- Create: `Public/SatisDash/Native/PowerLines/NativePowerLines.h`, `Private/Native/PowerLines/NativePowerLines.cpp`, `Private/Native/PowerLines/PROVENANCE.md`, `SatisDashCoreTests/Private/NativePowerLinesTest.cpp`
- Modify: floor `--min 201` (workflow and its test)

**Interfaces** (produces):
```cpp
inline constexpr int32 PowerLinesMaxItems = 20000;       // map.ts POWER_LINES_MAX_ITEMS
struct FPowerLineReading { FString Id; double X0Cm = 0, Y0Cm = 0, X1Cm = 0, Y1Cm = 0; };   // wire GetName(); both ends, world cm
SATISDASHCORE_API FNativeWorldBody BuildNativePowerLines(const FString& ObservedAt, const TArray<FPowerLineReading>& Readings);
```
Rules (`powerLinesMapper.ts`): item `{"id":"...","points":[[x0,y0],[x1,y1]]}` in index order; whole metres via `ToWholeMetres`; any of the four outside +/-1,000,000 m, or an empty id, drops the line (`Dropped`); two ends that round to the same metre are **kept**; id cut with `ClampString` (200); sorted by (id, then the points text), a repeated id keeps the first; over 20,000 keep 20,000, `Over` counts the rest; `WorldBodyPrefix`.

- [ ] **Step 1: Failing tests (+4 = 201).** `NativePowerLines.Map`: `Build_PowerLine_C_2147223283`-style ids (synthetic numbers), ends `(-1246855.8, 303000.2) (-1250000, 303100)` -> `[[-12469,3030],[-12500,3031]]`; rounding table; `-50` cm prints `0`, never `-0`; equal-metre ends kept. `NativePowerLines.Rules`: empty list `"data":[]`; 20,001 keep 20,000 with `Over == 1`; shuffled input identical `Json`; duplicate id keeps one; empty id and an out-of-bounds end each drop and count; a 250-character id cut to 200. `NativePowerLines.Golden`: three lines shaped like `frm-getCables-2026-09-27-trimmed.json` equal a hand-written expected text. `NativePowerLines.Bytes`: 20,000 lines of 30-character ids stay under `WorldLayerMaxBytes` (the layer is not capped by bytes first).
- [ ] **Step 2-4:** run, implement, `judge-mod-test --min 201`, raise floor, `check-api`, `check-provenance`. **Step 5: PROVENANCE.md** (`Reader: PowerLines`; `Captures: frm-getCables-2026-09-27-trimmed.json`; the contract and `powerLinesMapper.ts` are the rules). Commit `PowerLines: pure layer builder (R10)`.

**Test count after:** 201. **Reviewer checks:** equals `powerLinesMapper.ts`; index order kept; sorting order-independent; provenance complete.

### Task 11: The third flag on the shared logistics pass: port, sender, driver

**Files:**
- Modify: `Ports.h` (`BeginLogisticsRead(bool bRails, bool bConveyors, bool bPowerLines)`; `FLogisticsSnapshot` gains `TArray<FPowerLineReading> PowerLines;`; `FLogisticsCounts` gains `int32 Wires = 0; int32 ExpectedWires = -1;`), `WorldSender.h` (`bool bPassPowerLines`), `WorldSender.cpp` (`WorldSenderIsLogistics` includes PowerLines; `WorldSenderIsNative(PowerLines)` real; `TickLayer` computes `bPowerLines` like the other two and sets `Scanning`; `StepLogisticsPass` handles a third layer: drops out if its source changes, abandoned handling, submit through `BuildNativePowerLines`; every `bPassOpen = bPassRails = bPassConveyors = false` becomes a three-way reset), `Driver.cpp` (no change beyond the existing `EndLogisticsRead()` calls), `DriverFakes.h` (fake gains `LogisticsPowerLines`, the last third flag), `GameReader.h` (override signature; the body in Task 12)
- Update every existing caller and test of `BeginLogisticsRead(a, b)` to the three-argument form (grep `BeginLogisticsRead`; pass `false`).
- The `Logistics read` line becomes `Logistics read (game thread only): layers rails=%d conveyors=%d powerLines=%d, %d buildables, ..., wires %d, expected wires %d` (the counts appended before `longest step` is not allowed: the step regex needs `longest step X ms, total Y ms` at the end, so the new fields are inserted **before** `in %d steps`); `harness/src/reader-lines.ts` (`LOGISTICS` and `LOGISTICS_LAYERS` regexes accept the `powerLines=` group, optional so S3 lines still parse; `logisticsLayers` returns `powerLines` too; `logisticsLineProblems` layer type gains `"powerLines"`; `logisticsRead` gains `wires`)
- Tests: `WorldSenderTest.cpp` (`SatisDash.Core.WorldSender.LogisticsPowerLines`), `DriverNativeTest.cpp` (`SatisDash.Core.Driver.NativeLogisticsPowerLines`) (+2 = 203); Vitest `reader-lines.test.ts`
- Modify: floor `--min 203`, `docs/native-readers.md`

- [ ] **Step 1: Failing tests.** `WorldSender.LogisticsPowerLines`: (a) only `powerLinesSource` native (rails and conveyors bridge): at 433 s `BeginLogisticsRead(false, false, true)` is called once; one finished snapshot posts `/world/powerLines` once, sorted by id; rails is asked of FRM as before (`getTrainRails`), conveyors not sent; (b) all three native: the first due layer pulls the others forward (S3 M6), `BeginLogisticsRead(true, true, true)`, one snapshot posts all three bodies once each, each with its own data hash; a refused submit for one layer reschedules only that layer; (c) powerLines source changed to bridge under an open pass drops only that layer; (d) abandoned read: all three layers wait `NodeScanRetrySeconds`, one warning per layer, nothing posted; (e) the default config (key unset) never begins a pass for powerLines. `Driver.NativeLogisticsPowerLines`: `Readers:` shows `powerLines=native`, leaving Running ends the pass, no FRM request for powerLines. Vitest: S3's `Logistics read ... layers rails=1 conveyors=1, ...` line (no `powerLines=`) still parses; the new line yields `powerLines: 1`; `logisticsLineProblems(lines, "powerLines")` of an S3-shaped line says the pass did not serve it; a line with the new `wires` field still ends with the step cost and parses as a `StepCost`.
- [ ] **Step 2-4:** run (fail), implement, `judge-mod-test --min 203`, raise floor, Vitest, `check-api`. **Step 5:** commit `The shared logistics pass serves powerLines`.

**Test count after:** 203. **Reviewer checks:** S3's tests only changed by the extra `false` argument; a pass without powerLines is byte-identical in behaviour; `StepLogisticsRead` once a tick; the step-cost regex still matches the new line; the log line carries counts only.

### Task 12: R10, the game-side wires in the pass, with the cast-count check

**Files:**
- Modify: `GameReader.h/.cpp` (phase `ReadPowerLines` after `ReadConveyors`; `TArray<TWeakObjectPtr<AFGBuildableWire>> LogisticsWires;` `bool bLogisticsPowerLines;` reset in `ResetLogisticsRead`), `Private/Native/PowerLines/PROVENANCE.md`, `api-sources.md`, `docs/native-readers.md`, and `WorldSender.cpp` (the Warning below)

Game side (headers first):
- `BeginLogisticsRead(bRails, bConveyors, bPowerLines)`: stores the flag; if `bPowerLines`, `ExpectedWires = Subsystem->GetBuildableCount(AFGBuildableWire::StaticClass())` (advisory; the class is `Abstract`, so subclass inclusion is not assumed, as for tracks).
- `Collect` (the same iteration): `if (bLogisticsPowerLines) if (AFGBuildableWire* Wire = Cast<AFGBuildableWire>(Building))` push a weak pointer, capped at `PowerLinesMaxItems + 1` (the extra only into `Counts.Over`). The phase after `Collect` is the first of `ReadRails`, `ReadConveyors`, `ReadPowerLines` that is asked for; `ReadConveyors` ends by moving to `ReadPowerLines` (or finishing when not asked).
- `ReadPowerLines`: one weak pointer per iteration; skip if invalid; `Conn0 = Wire->GetConnection(0)`, `Conn1 = Wire->GetConnection(1)`; **either null/invalid: `++Counts.Dropped`, skip** (R-a, Q8); else `L0 = Wire->GetConnectionLocation(0)`, `L1 = Wire->GetConnectionLocation(1)`, `Reading.Id = Wire->GetName()`, push; `++Counts.Wires`. Optional sanity (counts only): `Wire->GetLength()` vs the distance of the ends, difference over 100 cm counted in a `LengthMismatch` count (log field, not in the contract).
- The abandoned rule is the pass's (zero buildables).
- **Cast-count check (Q6, loud):** in `StepLogisticsPass` after a finished pass with `bWasPowerLines`: if `Counts.Wires == 0` or `|Wires + Dropped - ExpectedWires| > max(5, 5% of ExpectedWires)` (with `ExpectedWires >= 0`), write one rate-limited `Warning` (`World layer powerLines: %d wires read, %d dropped, the subsystem counts %d; check that wires are in the buildable list`, counts only). It never changes the output; the parity id-set is the judge.

- [ ] **Step 1:** rows first. **Step 2:** implement; update `PROVENANCE.md` `Headers:`. **Step 3:** `judge-mod-test --min 203`, `check-api`, `check-provenance`, Vitest unchanged. **Step 4:** docs row ("Power lines (R10) | S4-A | `bridge` (default = layer not sent) | rides the logistics pass"). Commit `PowerLines: game-side wires in the logistics pass (R10)`.

**Test count after:** 203 (the warning's threshold function is tested in `WorldSender.LogisticsPowerLines` by extending it with a fake whose `Wires` and `ExpectedWires` differ: no new macro). **Reviewer checks:** `Cast` result and both connections null-checked; hypertube-style subclasses irrelevant; no list reference kept across ticks; the check never alters output; no id in logs.

---

## PR S4A-5

### Task 13: The FRM oracles, and the Test3 counts

**Files:**
- Create: `harness/src/frm-s4.ts`, `harness/test/frm-s4.test.ts`
- Modify: `harness/src/recorder.ts` + `harness/test/recorder.test.ts` (comment above `keepWorldData`: markers and drone ports carry player text, so they may be kept **in memory** only because `publicEntries` drops `worldData`, and the drive hashes names before anything is captured; a test that `["mapMarkers","droneRoutes","powerLines"]` are kept)

- [ ] **Step 1: Record the counts (R-e).** Read the `s4-capture` run's result for `/getMapMarkers`, `/getDroneStation`, `/getCables` (`runs/s4-capture/pass.ts` prints `N records in the first`); write the three numbers into the PR description and the `docs/native-readers.md` Test3 note (counts only). If the run has not been dispatched, stop and ask the owner; do not invent numbers.

**Interfaces** (produces; shapes from the dash mappers' raw schemas and our captures; FRM is read only through its HTTP output):
```ts
export interface FrmMarker { id: string; nameHash: string; kind: "stamp" | "marker"; icon: number; color: string; xM: number; yM: number }   // xM,yM unrounded metres, 0.01
export function markerName(raw: string | undefined): string               // the dash markerName, ported (M3)
export function mapFrmMarkers(body: unknown): FrmMarker[]                  // nameHash = sessionHash(markerName(Name)); color = markerColor(ColorSlot)
export interface FrmDronePort { id: string; nameHash: string; pairedHash: string | null; input: string[]; output: string[]; fuel: string[]; xM: number; yM: number; pairedIds: string[] }
export function mapFrmDroneFull(body: unknown): FrmDronePort[]             // pairedIds = ids of FRM ports whose Name equals PairedStation ("None" -> []); sets sorted, distinct, as itemClasses
export interface FrmWire { id: string; ends: [[number, number], [number, number]] }   // location0/1 cm / 100, 0.01
export function mapFrmWires(body: unknown): FrmWire[]
export const canonMarkers / canonDrone / canonWires = (...) => string[]   // sorted deterministic lines (hash)
```
`mapFrmDroneFull` resolves `pairedIds` over the whole FRM body (names never leave the function: only hashes and ids). `mapFrmDronePorts` (S3, ids only) is untouched.

- [ ] **Step 2: Failing tests** (fixtures: synthetic bodies in the test, shaped like the captures; names synthetic): `markerName` vectors equal Task 4's `NativeMapMarkers.Name` vectors (pins the TS to the C++); `mapFrmMarkers`: `ColorSlot` `6EA37BFF` -> `#6ea37b`, a missing slot -> `#ffffff`, `MapMarkerType` `RT_Stamp` -> stamp, any other -> marker, `location` 12468.55859375 cm style -> unrounded metres; no raw name appears in `JSON.stringify` of the output (assert); `mapFrmDroneFull`: `PairedStation` `"None"` -> `pairedHash null`, `pairedIds []`; a name matching two ports -> `pairedIds` of length 2; matching one -> that id; inventories to sorted distinct classes; `mapFrmWires` ends divided by 100. **Step 3-4:** fail, implement, whole Vitest + typecheck. **Step 5:** commit `FRM oracles for markers, drone ports and wires (S4-A)`.

**Test count:** UE 203 unchanged. **Reviewer checks:** no raw name in any returned value; `markerName` equals the C++ rule; FRM read only through HTTP output; S3 oracles untouched.

### Task 14: The S4 judges

**Files:**
- Create: `runs/native-parity/pass-s4.ts`, `harness/test/native-parity-s4-pass.test.ts`

**Interfaces** (follows `pass-s3.ts`):
```ts
export const S4_GAP_MS = 120_000;        // NEW for S4: reuses S3_GAP_MS / the routes rule (Open questions)
export const S4_XY_MAX_M = 1;            // owner R-d: drone x/y and wire ends within 1 m
export const S4_SHOW = 10;
export interface MarkersEvidence { posts: Array<{ at: string; status: number; data: unknown }>; frmSamples: Array<Bracket<string>>; distinct: Record<string, FrmMarker[]>; frm: Counts }
export interface DroneEvidence { posts: ...; frmSamples: ...; distinct: Record<string, FrmDronePort[]>; frm: Counts }
export interface WiresEvidence { posts: ...; frmSamples: ...; distinct: Record<string, FrmWire[]>; frm: Counts }
export interface S4Evidence { markers: MarkersEvidence; drone: DroneEvidence; wires: WiresEvidence }
export interface S4Result { pass: boolean; reasons: string[]; never: string[] }
export function judgeMarkers(e: MarkersEvidence): S4Result
export function judgeDrone(e: DroneEvidence): S4Result
export function judgeWires(e: WiresEvidence, log?: { dropped: number } | null): S4Result
```
Posted data in the evidence has names already replaced by hashes (Task 15), so the judge compares hashes.

**`judgeMarkers`.** Per accepted post: validate with `MapMarkersLayerDataSchema` (hashed names must still satisfy it: a name hash is a string of at most 60 characters); pair with FRM samples before/after (S4_GAP_MS) as `judgeRoutes` does; **id sets equal** (exact; first `S4_SHOW` each way listed); for each common id: `kind`, `icon`, `color`, `nameHash` exactly equal; `x`,`y`: equal to `Math.round(frm.xM)` exact passes; else if `|mod - frm.xM| <= 1` and the FRM fraction is within 0.01 of .5 it is **allowed and counted** (`N one-metre rounding differences at .5 m`); anything else fails with `id x mod=<v> frm=<v>`. A colour mismatch reason adds `(NativeMarkerColorSRGB decides this: Task 4)`. At least one accepted post; `frm.failed > 0` with no sample fails. NOT EXERCISED: `a stamp`, `a marker`, `a non-white colour`, `a name the dash rule changes (trim or cut)`, `a marker at a .5 m boundary` when absent; a layer with zero items on both sides passes with `NOT EXERCISED: any marker (both sides empty)`.

**`judgeDrone`.** Same pairing and schema (`DroneRoutesLayerDataSchema`, `pairedId` accepted); id sets equal; `nameHash`, `paired` (hash or null), and the `input`, `output`, `fuel` sets exactly equal (sorted arrays); `x`,`y` within `S4_XY_MAX_M` of `frm.xM/yM`; **pairedId rule (controller):** if FRM's `pairedIds.length === 1`, the mod's `pairedId` must equal it (a mismatch fails); if the mod says `pairedId: null` while FRM resolves exactly one port that is also in the mod's id set, that fails; otherwise (FRM ambiguous, unpaired, or the field absent) `pairedId` is **reported only** (`N ports: pairedId reported, not judged (ambiguous or unpaired in FRM)`). A mod `pairedId` naming an id not in the layer fails (the builder promises it cannot). When the input/output sets differ but equal each other swapped on every differing port, the reason adds `input/output look swapped (V9: swap the getters in Task 9)`. NOT EXERCISED: `a paired port`, `an unpaired port`, `cargo in an input inventory`, `cargo in an output inventory`, `fuel`, `two ports sharing a name (pairing ambiguous)`.

**`judgeWires`.** Same pairing and schema (`PowerLinesLayerDataSchema`); id sets equal (FRM-only ids are failures; the log's `dropped` count, when given, is quoted in the reason so a null-end wire is visible); for each common id each end within `S4_XY_MAX_M` of FRM's, **in index order, or both swapped** (swapped allowed and counted, `N wires with swapped ends`); a wire passes in neither order fails with the larger of the two sums. NOT EXERCISED: `a wire` when both sides empty, `a wire with swapped ends`.

- [ ] **Step 1: Failing tests** (`native-parity-s4-pass.test.ts`; evidence built in the test with synthetic hashes): markers: identical passes; a one-metre difference at a .5 boundary passes and is counted, at a non-boundary fails; a colour, icon, kind or name-hash difference fails with the field named; an id only in FRM fails, only in the mod fails; zero on both sides passes with NOT EXERCISED; an invalid post (icon as a string) fails; no post fails; no FRM sample within 120 s fails; the other side of the pair rescues. Drone: identical passes; x off by (1,0) passes, (1,1) fails; an input set differing fails, swapped input/output fails with the swap hint; `pairedId` equal to the unique FRM match passes, a different id fails, `null` with a unique FRM match fails, an ambiguous FRM name reports only, an unpaired port with `pairedId: null` passes; a `pairedId` outside the layer fails. Wires: identical passes; ends swapped pass and count; one end 1.5 m off fails; a wire id only in FRM fails with the `dropped` count quoted. **Step 2:** fail; **Step 3:** implement; **Step 4:** `npx vitest run test/native-parity-s4-pass.test.ts --reporter=dot`, then whole suite and typecheck. **Step 5:** commit `The S4 judges: markers, drone routes, power lines`.

**Reviewer checks:** thresholds are the owner's R-d and named in code; `S4_GAP_MS` and the .5 m allowance and the `pairedId` rule are marked NEW/controller in a comment and appear in Open questions; S1 to S3 judges untouched; no raw name in any reason string.

### Task 15: Wire the judges into `native-parity`: evidence, drive, verdicts

**Files:**
- Modify: `runs/native-parity/pass.ts` (`NativeParity.s4?: S4Evidence`; `ReaderVerdicts` gains `mapMarkers?`, `droneRoutes?`, `powerLines?`; `judgeReaders` adds them when `c.s4` exists; `expectsS4(c, sources)` true when `c.s4` exists or any of the three keys is in the config; `runReasons` requires the three keys `native` for an S4 run and the evidence present; `evaluate` adds `["R8 mapMarkers","mapMarkers", withProblems(..., mapMarkersLogProblems + readLineProblems("Map markers read"))]`, `["R9 droneRoutes", ...]`, `["R10 powerLines", ... powerLinesLogProblems + logisticsLineProblems("powerLines")]`; `requirements` gains `"R8-map-markers"`, `"R9-drone-routes"`, `"R10-power-lines"`; `export * from "./pass-s4"`), `harness/src/core-runs/native-parity.ts` (`modConfig: NATIVE_SOURCES_S4`; `keepWorldData` gains the three layers; `startS4Samplers(get, clock)` beside `startS3Samplers`: `/getMapMarkers` (timeout 10 s, every 2 min), `/getDroneStation` (10 s, every 2 min, full mapping), `/getCables` (20 s, every 2 min), each `{ hash, mapped }`; `s4LayerPosts` and `takeAfterNewS4Posts` like the S3 pair, taking a fresh sample right after each accepted post; `s4EvidenceFrom`), `docs/runner.md`
- Test: `harness/test/native-parity-s4-drive.test.ts` (like the S3 drive test), cases added to `harness/test/native-parity-pass.test.ts`

**Drive additions (S1 to S3 parts unchanged).** The S4 samplers start with the others (before the mod enrols). The posts kept for evidence are those since the start of enrolment (the native first read can land before the window). **Before a post enters the capture, every `name` and `paired` string is replaced by `sessionHash(markerName/…)`** (`s4LayerPosts` maps the kept `worldData`; M6), so `native-parity.json` carries ids, numbers, class names and hashes only. At the end `capture.s4 = s4EvidenceFrom(...)`.

- [ ] **Step 1: Failing tests.** Drive: the three samplers are started and stopped; an accepted `mapMarkers`/`droneRoutes`/`powerLines` post triggers one extra `take()` of that sampler; `capture.s4` is present with `distinct` once per hash; **a fake post whose names are `"Alpha"` and `"Beta"` leaves no trace in the stringified capture** (search the whole JSON); an S3-shaped capture (no `s4`, config without the new keys) re-judges exactly as before (reuse an existing fixture); an S4-shaped capture with all three keys native and passing evidence gives three verdicts in `readers.json`; a missing `Map markers read` line fails markers with the log problem; `powerLinesSource: "bridge"` fails `runReasons`; one failing reader leaves the other two verdicts unchanged. **Step 2-4:** fail, implement, whole suite and typecheck. **Step 5:** commit `native-parity: the S4 samplers, evidence and verdicts (Test3)`.

**Reviewer checks:** S1 to S3 judges and tests byte-for-byte; stored S3-shaped evidence re-judges identically; no name in `native-parity.json` or `recorder.json`; the 30-minute window and enrolment timing are the existing ones; FRM read only over HTTP.

### Task 16: `tick-cost-native` with S4 on

**Files:**
- Modify: `harness/src/core-runs/tick-cost-native.ts` (`modConfig: NATIVE_SOURCES_S4`), `runs/tick-cost-native/pass.ts` (`S2Sources` type gains the three keys; `requiredStepReaders` adds `"Map markers read"` when `mapMarkersSource` is native, `"Drone routes read"` when `droneRoutesSource` is native, and `"Logistics read"` when `powerLinesSource` is native as before; an S4-shaped config must have all three keys native, as S3's rule does for `conveyorsSource`; S1 to S3 conditions kept), `harness/test/tick-cost-native-pass.test.ts`
- The measured span (3 to 13 minutes) contains the first reads (432 to 528 s): assert in a test that a `Map markers read` line outside the span is not counted (the S2 window rule).

- [ ] **Step 1-5:** failing tests (S4-shaped evidence passes when all lines present and each longest step is 1.9 ms or less; fails for a `Drone routes read` at 2.1 ms; fails for a missing `Map markers read` line when the key is native; fails `powerLinesSource: bridge` in an S4-shaped config; S1, S2 and S3-shaped evidence pass as before against the existing fixtures), implement, whole suite, typecheck, `check-api`, `check-provenance`, final `judge-mod-test --min 203`, commit `tick-cost-native: the markers and drone steps and the powerLines pass`.

**Reviewer checks:** `Base.evaluate` limits (tick median 95%, memory 100/25 MB, S12) imported unchanged; the ceiling is `> 2.0 ms`; the single `tick-cost-native` dispatch is the owner's.

### Task 17: Docs and the follow-up list

**Files:** `docs/native-readers.md` (rows R8, R9, R10 say `bridge` (default = layer not sent) with their sprint; the keys in the override list; a "Where each reader stands (S4-A, after Task 17)" paragraph: all three are still `bridge`, the layers are not sent by default, FRM is not involved in them either way; the pass pulls layers forward; the `Test3` note with Task 13's counts; "ids of rebuilt wires/ports change"; the first native post of each layer changes its content hash once; `droneRoutes` posts carry `pairedId`), `docs/runner.md` (order of the sprint's runs; the image-guard note: rebuild the backend image from the new pin), `api-sources.md` tidy. Test: `check-api`, `check-provenance`, whole Vitest, typecheck. Commit `Docs: S4-A status, the new layers, the Test3 counts`.

**Reviewer checks:** the table says `bridge` for all three until the switch PR; no unproven claim; no path, private name or player text.

---

## PR S4A-6

### Task 18: `gameReachable`, `lastGameReadAt`, `modVersion` on the snapshot body

**No gate:** dash #740 is merged and Task 1 vendored it. Step 1 only confirms that.

**What the contract says (dash `packages/shared/src/agent.ts` at main, `SnapshotRequestSchema`, read by the planner):** three OPTIONAL fields at the **top level** of the snapshot, beside `reachable`, `paused` and `settings`, **not inside `status`** (the schema's refine requires `status` to be absent when `reachable` is false): `gameReachable` (boolean), `lastGameReadAt` (`IsoTimeSchema`: ISO datetime with an offset), `modVersion` (string, 1 to 64 characters, `SENDER_VERSION_MAX = 64`). No rule ties `gameReachable` to `reachable`. `SnapshotRequestSchema` is a `z.strictObject`, so an older backend **rejects** a body that carries any of them. `AgentVersion()` returns "mod/0.1.0" (9 characters), which fits 1 to 64; the contract says only "the sender's version", so the shape is an Open question.

**Files:**
- Modify: `Public/SatisDash/Snapshot.h` (`FSnapshot` gains `TOptional<bool> bGameReachable; TOptional<FDateTime> LastGameReadAt; FString ModVersion;`), `Private/Snapshot.cpp` (`SnapshotToJson` writes `gameReachable`, `lastGameReadAt` (UTC, `ToIso8601()`, only when set) and `modVersion` (only when non-empty, clamped to 64 with `ClampStringTo`) at the top level beside `reachable`, never inside `status`), `Private/Driver.cpp` (`TickSample`: `Snapshot.ModVersion = AgentVersion();`, `Snapshot.bGameReachable = Snapshot.Status.IsSet();`, a member `TOptional<FDateTime> LastGameReadAt` set to `Clock.UtcNow()` whenever `ReadStatus()` succeeded and written into `Snapshot.LastGameReadAt` when set; reset on `Running` start), `SatisDashCoreTests/Private/SnapshotTest.cpp` (extend), `DriverTest.cpp` (one new case), `docs/native-readers.md`, `.github/workflows/mod-test.yml`, `harness/test/workflows.test.ts` (`--min 205`)
- Test: `SatisDash.Core.Snapshot.ReachabilityFields` (new, +1), `SatisDash.Core.Driver.ReachabilityFields` (new, +1) = 205; the existing snapshot cases stay green (a snapshot without the fields is byte-identical to today's).

**Interfaces:** the mod's `reachable` stays always `true` (the mod never sends false); `gameReachable` is the new truth about whether the game was read this pass.

- [ ] **Step 1: Confirm** `harness/contract/src/agent.ts` has the three fields and `SENDER_VERSION_MAX` (Task 1's vendor); if the manifest commit predates #740, re-run Task 1's sync first. The mod sends all three on **every** snapshot (`gameReachable` true when the last game-thread read succeeded, `lastGameReadAt` the time of that read, `modVersion` = `AgentVersion()`).
- [ ] **Step 2: Failing tests.** `Snapshot.ReachabilityFields`: with the three set, the JSON has `gameReachable:true`, `lastGameReadAt` equal to `2026-10-27T12:00:00.000Z`-style UTC text, `modVersion:"mod/0.1.0"`, and validates by shape (the test reads the keys, not the schema); with none set the JSON equals the pre-change output exactly; `modVersion` of 70 characters is cut to 64; an empty `ModVersion` writes no key; `gameReachable:false` is written when the status read failed. `Driver.ReachabilityFields`: a pass where `ReadStatus()` fails posts `gameReachable:false` and a `lastGameReadAt` from the last good read (or none before the first); a good pass sets both; the value of `modVersion` equals `AgentVersion()`.
- [ ] **Step 3-4:** run (fail), implement, `judge-mod-test --min 205`, raise the floor, harness whole suite (the vendored schema now accepts the fields: add a Vitest that a mod snapshot body with the three fields validates against `SnapshotRequestSchema`), `check-api`. **Step 5:** commit `gameReachable, lastGameReadAt and modVersion on the snapshot (S4-A, R7)`.

**Release order (see Open questions 6):** the dash backend must be deployed with #740 **before** a mod build that sends these fields reaches any host, because an older backend answers 4xx to every snapshot. The mod's `.uplugin` version is not bumped here.

**Test count after:** 205. **Reviewer checks:** an old-shape snapshot is byte-identical; `reachable` unchanged; no new symbol (the version string is the existing `AgentVersion()`); time is UTC.

---

## End of sprint: the runs and the follow-ups

After PRs S4A-1 to S4A-5 merge (or from the S4A-5 branch), the owner dispatches, in this order and once each:
1. `mod-test` on each PR branch (floors 190, 192, 197, 203; 205 for S4A-6).
2. Owner step: the backend images built from the Task 1 pin or later.
3. `native-parity` (30-minute window, about 50 minutes plus the build; needs the backend host, `frmBaseUrl` and `sprint3World`).
4. `tick-cost-native` (baseline boot then mod boot, on `Test3`).
Nothing else is run.

**`readers.json` must show, per reader** (`pass: true`, reasons listing what was compared and what is NOT EXERCISED):
- S1 to S3 readers: as before, now with the S4 layers also native; a miss there is a finding for that reader's own ruling and does not change the three new verdicts.
- `mapMarkers`: id sets equal; kind, icon, colour, name hash exact; x/y exact or the allowed .5 m case, reported.
- `droneRoutes`: id sets equal; name, paired, input, output, fuel exact; x/y within 1 m; `pairedId` equal to the unique FRM match, else reported.
- `powerLines`: id sets equal; each end within 1 m in index order or swapped, swapped counted.
`result.json` is red if any reader is; `tick-cost-native`'s `result.json` must pass with `Map markers read`, `Drone routes read` and `Logistics read` at or under 2 ms.

**Owner's run checklist (before dispatch).** `Test3` staged (`sprint3World`); it contains at least one marker (ideally one stamp, one coloured marker such as the green one, one long or emoji name), a **paired** drone port pair (the two paired ports are there from S3, in the owner's test saves) plus an **unpaired** port, cargo in one input inventory, and power lines (843 on the captured save); FRM running on the test install; the backend host started in the owner's session with images at the new pin; the S4A-1 to S4A-5 branches' `mod-test` green. Absent items are reported NOT EXERCISED, never failed.

**After S4-A (named, not planned):**
- One switch PR for all three: `DefaultMapMarkersSource`, `DefaultDroneRoutesSource`, `DefaultPowerLinesSource` (each flips `Bridge` to `Native`; a reader that missed stays `bridge`), with `mod-claims.md` rows, and a dash evidence PR (`raw-sources/mod-native-readers/`).
- S4-B (extractors, generators, train transfer rates, storage) is a separate plan.
- R7, the removal release, remains the owner's explicit word.

## Self-review

**Spec coverage.** Brief scope: R8, R9, R10 (Tasks 4-6, 7-9, 10-12), source keys and switch (2, 3, After S4-A), `pairedId` in R9 (7, 14), the status fields gated (18), contract re-vendor first (1), parity judges per R-d (14, 15), tick-cost (16), Test3 counts (13), privacy (4, 13, 15). Survey Q1-Q11: Rulings table; the NEEDS VERIFICATION items: second table (15 rows, each with a settling task).

**Placeholder scan.** The only fill-ins are `Author:`/`Date:` in the three provenance files, the Game-module code (described by exact symbols and sequence; written by the implementer against the open headers, as S2 and S3 did), and the Task 1 / Task 18 commit hashes (taken at execution). Golden literals are written by the implementer from the captures' shapes with synthetic names.

**Type consistency.** `FMapMarkerReading`, `MapMarkerName`, `MapMarkerColorHex`, `BuildNativeMapMarkers`, `NativeMarkerColorSRGB` (Task 4) are used in Tasks 5, 6; `FMapMarkersSnapshot`, the five markers port functions (Task 5) in Task 6; `FDronePortReading`, `BuildNativeDroneRoutes` (Task 7) in Tasks 8, 9; `FPowerLineReading`, `BuildNativePowerLines` (Task 10) in Tasks 11, 12; the three-argument `BeginLogisticsRead` (Task 11) in Task 12; `NATIVE_SOURCES_S4` (Task 2) in Tasks 15, 16; `FrmMarker`/`FrmDronePort`/`FrmWire`, `markerName` (Task 13) in Tasks 14, 15; the log names `Map markers read`, `Drone routes read`, `Logistics read ... powerLines=` (Tasks 5, 8, 11) in Tasks 15, 16; `STEP_LINE_NAMES` gets both new names in Task 5.

**Review Focus coverage.** Each line names its test above; the Game-module paths are covered by the parity and tick-cost runs, as in S1 to S3.

## Open questions for the owner

**Provisional controller rulings (confirm or change):**
1. **R-a:** the survey's 11 recommendations are taken as written (colour switch decided by parity, upper-case Guid, name = building tag, `pairedId` beside `paired`, drone stations from `GetAllStations`, wires from the buildable list with a loud cast-count check, powerLines on the shared pass, drop null-end wires and count them, `modVersion` = `AgentVersion()`, caps unchanged, no player text in logs or committed evidence).
2. **R-b:** one source key per layer, `bridge` (default) = layer not sent; one switch PR for all three; `NATIVE_SOURCES_S4`.
3. **R-c:** markers and drone ports are small sliced reads with their own log lines; 600 s interval from the dash `worldReader.ts`; first native read 480 s +/-10%.
4. **R-d:** the three parity rules, including the controller's `pairedId` rule (unique FRM name match must equal the mod's id; otherwise reported only).
5. **R-e/R-f:** `Test3`, the `s4-capture` counts recorded not invented, contract re-vendored first at a dash main commit including #739 and #740 (`a5dd42c`).

**Found while planning:**
6. **The status fields break old backends.** `SnapshotRequestSchema` is a strict object, so a backend without #740 rejects every snapshot that carries `gameReachable`, `lastGameReadAt` or `modVersion`. The dash backend must deploy before any mod build with Task 18 reaches a host. Accept this order, or gate the fields behind a config key?
7. **`gameReachable` meaning.** The mod never sends `reachable: false`; per the coordinator, `gameReachable` = the last game-thread read succeeded, `lastGameReadAt` = the time of that read; I implement "the status read of this pass". Right?
8. **`modVersion` shape.** The contract only says "the sender's version, 1 to 64 characters". I send `AgentVersion()` = "mod/0.1.0" (fits), which duplicates `agentVersion`. Keep, or send the bare "0.1.0"?
9. **Marker with no valid Guid is dropped and counted** (M4), not given the mapper's derived id (needs sha256, no Core row). It should never fire with saved Guids; accept?
10. **`NativeMarkerColorSRGB` defaults to `true`, a guess;** the parity run on the owner's green marker settles it (one-line fix after a failed run).
11. **Fixtures use synthetic names,** not the captured ones (player text). Accept?
12. **New numbers not set by you:** `S4_GAP_MS` 120 s (the S3 rule reused), the .5 m rounding allowance for marker x/y, the 5 percent / 5 wire threshold of the cast-count warning, the S4 FRM sampler interval of 90 s (`S4_SAMPLER_MS`, plus a fresh sample after each accepted post), and the sampler read timeouts of 10 s (markers), 10 s (drone ports) and 20 s (cables).
13. **The pass log line gains `powerLines=` and `wires` fields;** S3 evidence still parses (optional group).
14. **Owner step:** the backend images must be rebuilt from the new contract pin before `native-parity`, or the image guard refuses the run.
15. **A port named "None":** FRM's sentinel makes the oracle read it as unpaired while the mod sends the real name; a one-port edge case reported, not failed. Accept?

**Added during execution (the judges' review, 2026-10-28):**
16. **Id-set exemptions** (reported, never failed). Two cases:
    - when both sides are exactly at the layer cap, the ids that sort after the earlier of the two last ids;
    - clamped mod ids, but no more of them than FRM's derived ids;
    - FRM-derived ids (an empty or over-long FRM id; the native reader drops or clamps them), dropped from the FRM-only set however many there are, and reported;
    - FRM's repeated ids, reported only (the native readers keep the first).

    Any other id difference fails. Accept?
17. **`pairedId` tightened.**
    - An unpaired FRM port needs the mod's `pairedId` to be null or absent.
    - An ambiguous FRM name allows null, absent, or one of FRM's candidate ids.
    - A unique FRM partner must equal the mod's id.

    Accept?
18. **`S4_MARKER_BOUNDARY_TOL_M` = 0.01 m:** a marker whose FRM value sits within 0.01 m of a .5 m rounding boundary may round one metre apart without failing. Accept?
19. **Drone x/y** is a straight-line distance ≤ 1 m (not per axis). **Power lines** take the better of the two end orders.
20. **Drone cargo is report-only (final review I-1).** A difference in a port's input, output or fuel item-class set is reported (a count and the first three), never failed, because the drones fly during the run; this mirrors the owner's 2026-10-27 trainRoutes cargo ruling. An input/output swap (the sets equal only when swapped) and every other field (ids, name hash, `paired`, `pairedId`, position) still fail. NEW (controller, provisional). Confirm?
21. **Per-tick total cost (final review M-1).** Up to six ~1 ms native reads can land in one tick, first reads all near 480 s; tick-cost-native measures it; stagger only if it shows. Accept?
