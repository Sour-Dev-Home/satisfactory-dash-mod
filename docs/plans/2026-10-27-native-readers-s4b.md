# Native readers, Sprint S4 part B (extractors, generators, train station transfer rates, storage) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The mod reads the extractors (`factory.extractors`), the generators (`power.generators`), the train station transfer rate (`mapLive.stations[].transferPerMinute`) and the storage containers (the new `storage` snapshot part) from the game itself and sends them in the snapshot. One combined 30-minute `native-parity` run plus one `tick-cost-native` run on `Test3` judge each reader separately, with the 2 ms ceiling on every native game-thread step. Each reader has a `bridge`/`native` switch that stays `bridge` (= "not sent") until one switch PR after the runs pass. R7 (deleting the bridge) is not in this plan.

**Architecture:**
- Same pattern as S1 to S4-A. Pure logic (mapping, caps, sorting, the exact JSON) lives in `SatisDashCore` under `Native/<Reader>/`; game-thread code is small, in `GameReader.cpp`, and **time-sliced** by `FStepBudget` (target 1 ms a step, ceiling 2 ms).
- **One new sliced "Buildables read" for extractors, generators and storage** (R6, ruling B1): one cursor over `GetAllBuildablesRef()`, one `Cast` per wanted class per element, re-fetched per element, flags per reader (a reader whose source is `bridge` is not in the pass). It is driven by the `FDriver` exactly as `StepFactoryRead` is (begin / step / abandoned / end, `FStepBudget`), on the factory cadence.
- **Station rates ride the existing trains station walk** (`TrainsReadStation`, `GameReader.cpp` ~:586) and its mapLive part; no new pass.
- Hand-off to the snapshot: extractors are spliced into the factory part (`factory.extractors`), generators into the power part (`power.generators`), storage is a new part (`storage`). All three are held as finished JSON fragments (built on the worker) and attached to a snapshot as in ruling B3.
- Harness: `native-parity` gains four verdicts in `readers.json` (new judge `runs/native-parity/pass-s4b.ts`, new oracle `harness/src/frm-s4b.ts`, from the four FRM endpoints `s4-capture` already records); `tick-cost-native` gains the new step lines.

**Tech Stack:** Unreal Engine 5.6.1-CSS, SML 3.12.0 (`harness/toolchain.json`), C++20, UE automation tests (`SatisDashCoreTests`), TypeScript harness (Node, zod 4, Vitest 5).

**Spec:** ADR-0058; the S4-B header survey `docs/surveys/mod-native-readers-s4b-headers-2026-10-27.md` (commit afb23a2: its 10 questions and NEEDS VERIFICATION items); the four dash contracts in `packages/shared/src/agent.ts`, shipped one dash PR at a time (extractors = dash #782, then generators, station rates, storage), each vendored by its own task; the S4-A plan `docs/plans/2026-10-28-native-readers-s4a.md` (the template); the FRM black-box captures `frm-s4-captures-2026-10-27` (getExtractor, getGenerators, getTrainStation, getStorageInv) and the `s4-capture` core run (PR #67).

## The contracts (summary; the vendored copy is the truth)

Everything is optional and additive; the backend derives `state` and `unit` and drops them if sent.
1. **Extractors** `factory.extractors?: AgentFactoryBuilding[]`, max 5000 (`AGENT_INPUT_MAX_EXTRACTORS`); the building shape and bounds; `recipe` null, `ingredients` `[]` or absent, `production` = the resource with currentPerMinute / maxPerMinute / percent; raw flags `isBackedUp` (true only when the output slot is full), `isPaused`, `isProducing`. Separate from `buildings`.
2. **Generators** `power.generators?`, max 5000: id, name, className (<=200), location? (metres), circuitGroupId? (-1 unconnected), currentMW, capacityMW (at current clock), clockSpeedPercent?, isProducing, `fuel: {className,name,amount}|null` REQUIRED (null when empty/unread/geothermal, never omitted), `supplement?` (absent = not read). No waste inventory. Ids share the building id space.
3. **Station rates** on `mapLive.stations[]`: `transferPerMinute?`, `inflowPerMinute?`, `outflowPerMinute?`, finite >= 0, absent = not read (not 0). Solid items per minute over item freight platforms; fluid platforms excluded. Station `id` equals its trainRoutes record id.
4. **Storage** `storage?: { containers: [...] }`, max 2000 containers, max 64 items each; a data part (refused when `reachable: false`). Container: id, name, className, location?, slots, slotsUsed, `fluidCapacityM3?` (present ONLY on a fluid buffer, THE test for one), `items: [{className, name, amount, stackMax?: int|null}]` merged per class; fluid amount in m3 with stackMax null. Snapshot body cap 5 MB (`SNAPSHOT_MAX_BYTES`).

Exact field names come from the vendored contract; each reader's pure-builder task is written against it.

## Owner decisions and controller rulings, carried as rules

| # | Ruling | Status |
| --- | --- | --- |
| R1 | The survey's recommendations 1-5 and 7-10 are taken as written (extractor rate path, isBackedUp, generator MW path, geothermal reports what the power-info path gives, fuel/supplement amounts, item inflow/outflow omitted, fluid platform = has a pipe connection, depot uploader reported like a container, `stackMax` from `GetSlotSize`). | controller |
| R2 | Q6 station item transfer rate is read by UE reflection, read-only, on the game thread. First choice: the game's own private `UFUNCTION GetCurrentItemTransferRate` (`FGBuildableTrainPlatformCargo.h:180`, BlueprintPure, under `private:` :121) via `FindFunction` + `ProcessEvent`, the `UFunction` looked up once per class and cached. Fallback: `FindFProperty<FFloatProperty>` on `mSmoothedLoadRate` :363 / `mSmoothedUnloadRate` :366 chosen by `GetIsInLoadMode()` :57. Binding `mOnTransferRateUpdated` is rejected (per-platform lifecycle state). If the lookup fails (null `UFunction`/property), the station's rate is **absent, not 0**, and a rate-limited Warning is logged. | controller, provisional (owner queue) |
| R3 | Q7: `inflowPerMinute` / `outflowPerMinute` stay absent for item platforms (the header's getters are fluid-only m3/s). Only `transferPerMinute` is sent. | controller |
| R4 | Rate unit: the header states none. The mod sends per-minute values. The first parity run records the native raw value next to FRM's (evidence); the factor is the named constant `NativeStationRateToPerMinute` (assumed **1.0**, NEEDS VERIFICATION, ruling B6). If the run shows a different ratio the run is **failed (stop and report)**; the constant is then fixed in a ruling PR and the run repeated. The tolerance is never loosened. | controller |
| R5 | Source keys `extractorsSource`, `generatorsSource`, `stationRatesSource`, `storageSource`, each default `bridge` = "not sent" (like `mapMarkersSource`), flipped by one later switch PR after native-parity and tick-cost-native pass. The `Readers:` line grows accordingly. | controller |
| R6 | Extractors, generators and storage share ONE sliced buildable pass unless the factory read can carry them more cheaply. Cadence matches the factory part's. | controller; decided by B1, B2 below |
| R7 | New S4-B judges in `runs/native-parity` with FRM oracles from the capture endpoints, keyed like S4-A (ids compared, names hashed by the per-run HMAC). Tolerances stated up front per field (Task 18). Pass conditions are written before the runs and never weakened. | controller |
| R8 | Clean room: each reader directory `Native/<Reader>/` has a `PROVENANCE.md` (Survey: `mod-native-readers-s4b-headers-2026-10-27`); each PR carries the checkbox "FRM source was not opened". | controller |

### Rulings made by this plan (from reading the code), for the owner to confirm

- **B1. One separate Buildables pass, not an extension of `StepFactoryRead`.** `StepFactoryRead` (`GameReader.cpp` :412) casts to `AFGBuildableManufacturer`; extractors, generators and storage are different classes, so the factory read would need three more casts per element and three source flags inside a read that has its own tests and its own switch. A separate pass costs one more walk of the list (a re-fetch and a pointer read per element, inside the same 1 ms slices) and keeps each reader's flag independent: with only `storageSource` native the pass casts only to storage. Rejected alternative: fold into `StepFactoryRead` (saves one list walk, couples four readers to one switch).
- **B2. Cadence.** The factory part is read when `bFactoryDue` (`Driver.cpp` :268, `Cadence.FactorySeconds`, default 30 s, set by the backend), and the trains/mapLive read uses the same flag. The Buildables pass starts on the same `bFactoryDue` tick (skipped, not restarted, while the last one is still open, as the factory read is). Station rates ride the trains read, so the same cadence. Generators are read every 30 s even though the power part is built every `PowerSeconds` (5 s): see B3. First reads: at the first `bFactoryDue` (as the factory read), no 480 s delay.
- **B3. Attaching fragments.** `Game.ReadPower()` (:261) is synchronous and cheap, so the 5 s power part stays as is. The latest finished generators fragment is held by the driver and attached to every power part until it is older than `3 x FactorySeconds` (then generators are omitted = "not read", the `TakeFactory` max-age idea). The extractors fragment is spliced into the factory part only when that part is built from a native factory read of the same cadence tick or later; storage is attached to the next snapshot after it is built and then cleared (a data part is sent once per read, like the factory part). Whether the backend keeps the last generators when a power part omits them is a dash question (Open question 6).
- **B4. Extractor values** follow the survey recommendation Q1 (compute from per-cycle count, cycle time at current potential, purity, productivity) with the parity run as judge; the path may change to `GetExtractionPerMinute` for fluids after a failed run, in a ruling PR (never a weakened judge).
- **B5. Held memory.** At most 5000 extractor readings, 5000 generator readings, 2000 container readings with at most 64 merged items each (a small map per container), freed after the fragment is built. No game struct is kept across ticks; weak pointers are not needed because the cursor is an index and the list is re-fetched per element.
- **B6. Station rate evidence.** The first run sends `raw x 1.0`; the parity evidence stores the mod's posted value next to FRM's. The judge (Task 18) fails on a wrong factor (median ratio outside the band below), so a wrong unit is a failed run, not a loosened one.
- **B7. Names are player text** (building and station names, container names): never in logs or committed evidence; the harness hashes them before they enter a capture (M6 of S4-A).

## Global Constraints

Everything in the S1 to S4-A plans' Global Constraints still holds (pure Core, test names `SatisDash.Core.<Area>.<Case>`, unity-build prefixes for file-local helpers, case-sensitive protocol strings, no `check()` on outside data, logs only to `LogSatisDash` at `Display`/`Warning` and rate-limited, counts and times only in logs, `check-api` at `0 missing, 0 bad rows`, `check-provenance` clean). Added or restated:

- **Clean room (CLAUDE.md rule 7, `docs/clean-room.md`).** Nobody opens FicsitRemoteMonitoring's source or searches the web for it. FRM is a black box over HTTP: our recorded outputs and the live test server's endpoints (`/getExtractor`, `/getGenerators`, `/getTrainStation`, `/getStorageInv`). If FRM source is seen by accident: stop, tell the owner, record it in that reader's `PROVENANCE.md`.
- **No API from memory (rule 1).** Every game/SML/UE symbol comes from the Lookups table or a header the task opens, and gets its `api-sources.md` row (`path:line`) in the same task. **The header wins**; record both in the row's Source when they disagree. Paths are relative to `Source/FactoryGame/Public` unless marked `Engine/`.
- **No weakened judges (rule 3).** No existing `pass*.ts` condition changes. New conditions are new code (`pass-s4b.ts`). A failed reader stops the work and is reported with its evidence bundle.
- **Evidence is machine-written; loopback and the test server only** (rules 4, 5).
- **Contract.** Each vendoring task re-vendors at the merged dash commit for that contract. **No reader task starts before the dash reports its contract merged.** Existing goldens do not move; new goldens come from our recorded FRM output with synthetic names.
- **The step ceiling.** No native game-thread step over 2 ms (`MaxNativeStepMs = 2.0`); each read stops at `NativeStepBudgetSeconds = 0.001` after the element that crosses it.
- **Reflection (R2).** Read-only; `FindFunction`/`FindFProperty` results cached once per class in the game module, never per call; `ProcessEvent` only on the game thread with a valid, non-pending-kill object; a `BlueprintPure` function with no parameters but a float/int return is the only call made.
- **Test counts.** The floor is 205 (`mod-test.yml` `--min 205`, 205 macros, S4-A included). Running ledger: T3 +4 = 209, T4 +2 = 211, T7 +4 = 215, T8 +2 = 217, T11 +3 = 220, T14 +4 = 224, T15 +2 = 226. The floor and `harness/test/workflows.test.ts` rise in the task whose tests land.
- **Runs.** Executors never start runs. The owner dispatches `mod-test` on each PR branch, then **one** `native-parity` and **one** `tick-cost-native` on `Test3`.
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. No personal data, no local paths in anything committed.

## Lookups made for this plan (header wins)

From the survey (it opened the headers in the pinned checkout). Every task still opens the header it uses, checks the line, and adds the row.

| Need | Header, `path:line` (survey) | Notes |
| --- | --- | --- |
| Buildable list | `FGBuildableSubsystem.h:360` `GetAllBuildablesRef()` (row); `:363` `GetBuildableCount` (row, advisory) | Re-fetch per element. |
| Extractor | `Buildables/FGBuildableResourceExtractor.h:13` class (new row), `GetOutputInventory` :35, `GetNumExtractedItemsPerCycle` :39, `GetExtractionPerMinute` :47, `GetProductionCycleTime` :29, `CalcProductionCycleTimeForPotential` :31, `GetFlowRateSmoothed` :59, `GetMaxFlowRate` :63; base `FGBuildableResourceExtractorBase.h:65` (row), `GetExtractableResource` :103 | Cast target covers miners, water pump, oil pump (Blueprint: NEEDS VERIFICATION). |
| Resource | `Resources/FGExtractableResourceInterface.h:18`, `GetResourceClass` :41, `GetExtractionSpeedMultiplier` :56 | Null resource (unoccupied) handled. |
| Factory base | `FGBuildableFactory.h` `IsProducing` :151, `IsProductionPaused` :184, `GetProductivity` :213, `GetCurrentPotential` :225, `GetPowerInfo` :108 | Rows exist. |
| Inventory | `FGInventoryComponent.h` `GetSizeLinear` :443, `GetStackFromIndex` :326, `GetSlotSize` :493, `HasEnoughSpaceForItem` :433 (new row), `GetNumItems` :415 (new row) | |
| Generator | `Buildables/FGBuildableGenerator.h:60` (row), `GetPowerProductionCapacity` :87, `CalcPowerProductionCapacityForPotential` :95, `GetLoadPercentage` :83 | |
| Power info | `FGPowerInfoComponent.h` `GetBaseProduction` :137, `GetDynamicProductionCapacity` :148, `GetRegulatedDynamicProduction` :160 | New rows; MW assumed (NEEDS VERIFICATION). |
| Fuel generator | `FGBuildableGeneratorFuel.h:14`, `GetFuelInventory` :54, `GetCurrentFuelClass` :103, `GetSupplementalResourceClass` :111, `GetRequiresSupplementalResource` :74, `GetFuelInventoryIndex` :116, `GetSupplementalResourceInventoryIndex` :117 | Geothermal (`FGBuildableGeneratorGeoThermal.h:14`) is not a Fuel subclass: `fuel: null`. |
| Station cargo | `FGBuildableTrainPlatformCargo.h:18` (row), `GetIsInLoadMode` :57 (row), private `GetCurrentItemTransferRate` :180, `mSmoothedLoadRate` :363, `mSmoothedUnloadRate` :366, `mOnTransferRateUpdated` :198 (rejected) | R2. |
| Fluid platform test | `FGPipeConnectionComponent.h:177` `UFGPipeConnectionComponent` | Actor component query (`GetComponents<T>`); row for the UE call too. |
| Storage | `Buildables/FGBuildableStorage.h:13` (row), `GetStorageInventory` :30 (non-const); `FGCentralStorageContainer.h:15` (uploader, new row) | |
| Fluid buffer | `Buildables/FGBuildablePipeReservoir.h:43` (row), `GetFluidContent` :67, `GetFluidContentMax` :71, `GetFluidDescriptor` :87 | Content is an `FFluidBox`, not an inventory. |
| Item facts | `Resources/FGItemDescriptor.h` `GetStackSize` :251, `GetForm` :158 (row), `GetItemName` :191 (row) | |
| Existing walk | `GameReader.cpp` `TrainsReadStation` :586, `StepFactoryRead` :412, `FactoryReadSlots` | Same file; reuse the item/slot helpers. |

## Open questions

**NEEDS VERIFICATION carried from the survey, and how each is settled**

| Item | Settled by |
| --- | --- |
| V1. Extractor current/max semantics (clock, purity, fluid scaling) | Task 5 opens the headers and writes the formula; the parity run (Task 18 tolerances) judges; a failed run goes to a ruling PR (B4). |
| V2. Oil pump class (Blueprint on `AFGBuildableResourceExtractor`) | Parity id-set check: an FRM extractor missing from the mod set fails, naming the class. |
| V3. `isBackedUp` rule | Task 5 header check on `GetSlotSize`/`HasEnoughSpaceForItem`; judged against FRM's flag per sample. |
| V4. Generator currentMW / capacityMW units and potential-awareness | Task 9 header check; parity tolerances (Task 18). |
| V5. Geothermal capacity and fuel | Reported as the power-info path gives, `fuel: null`; the judge treats FRM's capacity 0 as report-only for geothermal. |
| V6. Station item-rate access and unit | R2 and R4/B6; Task 12 opens the header; parity run settles the factor. |
| V7. Item inflow/outflow | R3: omitted; nothing to verify. |
| V8. Fluid platform detection (pipe component) | Task 12; the parity run's station set (a fluid platform's rate must not be summed in). |
| V9. Industrial Fluid Buffer class | Task 16 header check; parity container id set (fluid buffers appear in FRM's `getStorageInv`). |
| V10. `GetFluidDescriptor` when empty | Task 16: an empty buffer sends `items: []`; the unit test pins it. |

**New questions found while planning (owner / dash):**
1. **B1 (separate pass) and B2 (cadence 30 s, first read at the first due tick, not 480 s).** Accept?
2. **Two reflection fallbacks.** If both the `UFunction` and the property lookups fail on `Test3`, the station rate stays absent for the whole run and the station verdict fails (never passes silently). Accept as the stop condition?
3. **The station rate factor** (`NativeStationRateToPerMinute = 1.0`) is a guess (R4); the judge band (Task 18) is NEW and provisional.
4. **Station id equals trainRoutes id** (contract 3): the existing `TrainsReadStation` id is `Building.GetName()`, which is also the trainRoutes id (survey), so no change is made; Task 12 adds a unit test that the two readers share one id helper.
5. **Parity timing noise.** Extractor productivity, generator output, fuel and container contents change between a mod post and an FRM sample, so Task 18 compares against the bracketing FRM samples and uses stated tolerances; fields that cannot be compared exactly are report-only (listed in Task 18). Confirm.
6. **Dash: does the backend clear `power.generators` or keep the previous list when a power part omits it?** B3 attaches the held list to every power part, which is safe either way, but if the backend would retain the last list, the 3x max-age rule is stricter than needed. Ask the dash.
7. **Snapshot size.** 5000 extractors + 5000 generators + 2000 containers x 64 items can pass the 5 MB cap (`SNAPSHOT_MAX_BYTES`) on a huge save. Task 15 measures the encoded size on `Test3` (110 containers) and logs it; the overflow rule (drop `storage` first, then `generators`, then `extractors`, with a rate-limited Warning, never a partial part) is a new ruling to confirm.
8. **Depot uploader (`Build_CentralStorage_C`)** reports as a 1-slot container (R1/survey 9); FRM does too. No pooled depot read.

## Review Focus

- **Nothing, or still loading:** zero buildables is unreadable; a finished pass with zero extractors sends `extractors: []` (a real empty list), a pass with no subsystem is abandoned and sends nothing. Pinned in the Buildables-pass sender tests.
- **A fluid buffer vs a container:** `fluidCapacityM3` present only on a fluid buffer; item `stackMax` null on fluid amounts. Pinned in `NativeStorage.Map`.
- **Required null:** generator `fuel` is always present (null, never omitted). Pinned in `NativeGenerators.Map`.
- **Absent is not zero:** a station with no rate read has no `transferPerMinute`. Pinned in `NativeTrains.StationRates`.
- **Bridge means not sent:** with the four keys unset nothing changes in any snapshot byte. Pinned in `Config.Sources`, `Driver.NativeBuildables`.
- **Reflection safety:** lookups cached, null handled, Warning rate-limited; carried by the runs (the Game module is not unit-testable here).

## How the tests run

As in S4-A: UE tests via `pwsh scripts/test-mod.ps1 -Project <sml-project> -Engine <engine-root> -Out <tmp>\mod-test` then `cd harness; npm run harness -- judge-mod-test <tmp>\mod-test --min <ledger N>` (the executor's own paths, never committed; one build per task at most). `check-api` and `check-provenance` end every task that touches `mod/`. Harness: `npx vitest run <file> --reporter=dot` while iterating; whole suite and `npm run typecheck` at the end of each harness task. The Game module is carried by the parity and tick-cost runs.

**PRs** (each independently reviewable; each carries "FRM source was not opened"; the dash contract each waits on is named):
- **PR S4B-1 (Tasks 1-5):** the four source keys (no contract needed), then extractors. Tasks 2-5 wait on dash #782.
- **PR S4B-2 (Tasks 6-9):** generators. Waits on the dash generators contract PR.
- **PR S4B-3 (Tasks 10-12):** station rates. Waits on the dash station-rates contract PR.
- **PR S4B-4 (Tasks 13-16):** storage. Waits on the dash storage contract PR.
- **PR S4B-5 (Tasks 17-21):** oracles, judges, parity drive, tick-cost, docs. Needs the four readers (can be written once the vendored contract has all four; Tasks 17-19 can be drafted earlier against recorded captures).

## File structure

`mod/...` is `mod/SatisDashAgent/Source`.

| File | New/Modified | Responsibility |
| --- | --- | --- |
| `mod/SatisDashCore/Public/SatisDash/Config.h`, `Private/Config.cpp`, `Private/Driver.cpp`, `harness/src/reader-lines.ts`, `core-run.ts` | Modified (T1) | Four source keys, 13-field `Readers:` line, `NATIVE_SOURCES_S4B` |
| `harness/contract/manifest.json`, `harness/contract/src/*.ts` | Modified (T2, T6, T10, T13) | Re-vendored contract, once per dash PR |
| `Native/Extractors/`, `Native/Generators/`, `Native/Storage/` (`.h/.cpp`, `PROVENANCE.md`, tests) | New (T3, T7, T14) | Pure mapping and exact JSON |
| `Public/SatisDash/Ports.h`, `Driver.h/.cpp`, `Snapshot.h`, `Private/Snapshot.cpp`, `FrmBridge.h/.cpp`, `DriverFakes.h`, `DriverNativeTest.cpp` | Modified (T4, T8, T15) | Buildables-pass port, driver, fragments, snapshot hand-off |
| `Native/Trains/NativeTrains.h/.cpp`, `NativeTrainsTest.cpp` | Modified (T11) | `FStationReading` rate, mapLive JSON |
| `SatisDashGame/Private/GameReader.h/.cpp` | Modified (T5, T9, T12, T16) | Game-side reads |
| `harness/src/frm-s4b.ts`, `runs/native-parity/pass-s4b.ts`, `pass.ts`, `harness/src/core-runs/native-parity.ts`, `tick-cost-native.ts`, `runs/tick-cost-native/pass.ts` | New/Modified (T17-T20) | Oracles, judges, drive, tick-cost |
| `docs/native-readers.md`, `docs/runner.md`, `api-sources.md`, `.github/workflows/mod-test.yml`, `harness/test/workflows.test.ts` | Modified (all) | Status, rows, floor |

---

## PR S4B-1

### Task 1: The four source keys, the `Readers:` line, `NATIVE_SOURCES_S4B` (no contract needed)

**Files:**
- Modify: `Config.h` (`DefaultExtractorsSource`, `DefaultGeneratorsSource`, `DefaultStationRatesSource`, `DefaultStorageSource`, all `ESource::Bridge`; `TOptional<ESource>` members and `GetExtractorsSource()` etc.; the `BadField` comment), `Config.cpp` (parse and write back exactly as `mapMarkersSource` via `ConfigReadSource`), `Driver.cpp` (`Readers:` line grows ` extractors=%s generators=%s stationRates=%s storage=%s` after `powerLines=`), `harness/src/reader-lines.ts` (`ReaderSources` gains four optional keys; the `READERS` regex gains an optional nested group, all four or none; four `*LogProblems`), `harness/src/core-run.ts` (`NativeSources` gains the keys; `export const NATIVE_SOURCES_S4B: NativeSources = { ...NATIVE_SOURCES_S4, extractorsSource: "native", generatorsSource: "native", stationRatesSource: "native", storageSource: "native" }`; earlier constants unchanged), `docs/native-readers.md` (four rows, `bridge`, "not sent")
- Test (existing cases extended, no new macro): `ConfigTest.cpp` (`SatisDash.Core.Config.Sources`), `DriverNativeTest.cpp` (the `Readers:` line case), `harness/test/reader-lines.test.ts`, `harness/test/mod-config.test.ts` (drift guard), `harness/test/native-parity-pass.test.ts` (a `NATIVE_SOURCES_S4B` equality case)

- [ ] **Step 1: Failing tests.** For each key: bad values `"Native"`, `""`, `1`, `null` make the config `Broken` naming the key; `"native"` round-trips; unset is not written back and the getter is `Bridge`. Driver: default config reads `... powerLines=<s4a default> extractors=bridge generators=bridge stationRates=bridge storage=bridge` (copy the exact prefix from the test). Vitest: the nine-field line still parses (new fields undefined); the thirteen-field line yields them; a partial extra group does not parse as thirteen; each `*LogProblems` says `has no <key>= field` / `not native` / empty.
- [ ] **Step 2-4:** run (fail), implement, `judge-mod-test --min 205`, Vitest, `check-api`. **Step 5:** commit `extractorsSource, generatorsSource, stationRatesSource, storageSource (default bridge) and the thirteen-field Readers line`.

**Test count:** 205 (cases extended). **Reviewer checks:** unset means bridge; S1 to S4-A constants and tests untouched (deep-equality); the older line shapes still parse.

### Task 2: Re-vendor the contract at dash #782 (extractors)

**Gate: do not start until the dash reports #782 merged.**

**Files:** `harness/contract/manifest.json`, `harness/contract/src/*.ts` (script-written), `docs/runner.md` (the pin line), test `harness/test/contract.test.ts`.

Vendoring is `npm run sync-contract -- --commit <40-hex dash sha>` from `harness/`, then `npm run harness -- verify-contract` (0 problems), as S4-A Task 1.

- [ ] **Step 1:** take the dash main commit that contains #782 (or a descendant: `gh api repos/Sour-Dev-Home/satisfactory-dash/compare/<782 merge sha>...<sha> --jq .status` is `ahead` or `identical`). **Step 2:** sync and verify. **Step 3: failing-first Vitest** (fails on the old copy): `AgentFactorySchema`/the factory part accepts `extractors` with one valid building (recipe null, `ingredients: []`, `production` with the three numbers, the three flags) and with the key absent, rejects 5001 extractors, and the vendored constant `AGENT_INPUT_MAX_EXTRACTORS` is 5000. Literals from the schema's own text only. **Step 4:** whole Vitest + typecheck. If an existing test or judge fails because a vendored schema changed: stop and report with the diff (rule 3). **Step 5:** commit `Re-vendor the dash contract at <short sha> (S4-B: extractors)`.

**Reviewer checks:** manifest commit is a dash main commit including #782; diff is only script-written files; no judge touched. Owner step: backend images from this pin or later before `native-parity`.

### Task 3: Extractors, the pure reading: mapping and exact JSON

**Files:** Create `Public/SatisDash/Native/Extractors/NativeExtractors.h`, `Private/Native/Extractors/NativeExtractors.cpp`, `Private/Native/Extractors/PROVENANCE.md`, `SatisDashCoreTests/Private/NativeExtractorsTest.cpp`. Modify `mod-test.yml` and `workflows.test.ts` (`--min 209`).

**Interfaces** (names for the vendored field set; confirm against the vendored `AgentFactoryBuilding`):
```cpp
inline constexpr int32 ExtractorsMaxItems = 5000;   // AGENT_INPUT_MAX_EXTRACTORS
struct FExtractorReading
{
	FString Id, Name, ClassName;
	bool bProducing = false, bPaused = false, bBackedUp = false;
	double ClockPercent = 100.0;
	double XCm = 0, YCm = 0, ZCm = 0, YawDeg = 0;
	int32 CircuitGroupId = -1;
	FString ResourceClass;            // empty = unoccupied / unknown (production omitted per the contract)
	double CurrentPerMinute = 0, MaxPerMinute = 0;   // m3 per minute for fluids, items per minute for solids (B4)
	bool bFluid = false;
};
/** The extractors list: JSON array text spliced into the factory part as "extractors". */
SATISDASHCORE_API FString NativeExtractorsFragment(TArray<FExtractorReading> Readings, int32& OutDropped, int32& OutOver);
```
Rules: same building bounds as `NativeFactoryPart` (reuse its shared helpers: id/name/className clamps, location to metres, clock, `percent = current/max` guarded for 0); `recipe` null, `ingredients` `[]`; `production` item with the class and name; sorted by id (tie by content), a repeated id keeps the first, more than 5000 keep 5000 and count `OutOver`; invalid numbers drop the reading and count; `isBackedUp` is passed through (true only when the reader said so).

- [ ] **Step 1: Failing tests (+4 = 209).** `NativeExtractors.Map` (a Mk1 miner with synthetic ids; production fields; percent 0 when max is 0; unoccupied -> production absent per contract); `NativeExtractors.Flags` (backed-up, paused, producing independent; backed-up never inferred in Core); `NativeExtractors.Rules` (empty -> `[]`; 5001 keep 5000, `OutOver == 1`; shuffled input same bytes; duplicate id keeps one; NaN rate drops); `NativeExtractors.Golden` (three extractors shaped like `frm-getExtractor-2026-10-27-test3-trimmed.json`, synthetic names, equal a hand-written string, validated by shape against the vendored schema in a Vitest next to it).
- [ ] **Step 2-4:** fail, implement, `judge-mod-test --min 209`, raise floor, `check-api`, `check-provenance`. **Step 5: PROVENANCE.md** (`Reader: Extractors`; `Code:` the new files, `GameReader.cpp`, `Driver.cpp`; `Headers:` the extractor rows; `Survey: mod-native-readers-s4b-headers-2026-10-27`; `Captures: frm-getExtractor-2026-10-27-test3-trimmed.json (FRM HTTP output only)`; `Facts:` the vendored contract; `FRM source was not opened`). Commit `Extractors: pure fragment builder (S4-B)`.

**Reviewer checks:** the building bounds equal `NativeFactoryPart`'s (shared helper, not copied); no name from a capture in tests.

### Task 4: The Buildables pass: port, driver, fragments (extractors flag only)

**Files:** Modify `Ports.h` (below), `Driver.h/.cpp`, `FrmBridge.h/.cpp` (the factory part splice, B3), `DriverFakes.h`, tests `DriverNativeTest.cpp` (`SatisDash.Core.Driver.NativeBuildables`), `FrmBridgeTest`-style case for the splice (`SatisDash.Core.FrmBridge.Extractors`) (+2 = 211); `mod-test.yml`, `workflows.test.ts` (`--min 211`); `harness/src/reader-lines.ts` (`STEP_LINE_NAMES` gains `"Buildables read"`); `docs/native-readers.md`.

**Interfaces:**
```cpp
// Ports.h
struct FBuildablesFlags { bool bExtractors = false, bGenerators = false, bStorage = false; };
struct FBuildablesCounts { int32 Elements = 0, Extractors = 0, Generators = 0, Containers = 0; };
struct FBuildablesSnapshot { TArray<FExtractorReading> Extractors; /* T8: Generators, T15: Containers */ FBuildablesCounts Counts; };
// IGameReader, defaults do nothing:
virtual bool BeginBuildablesRead(const FBuildablesFlags& Flags) { return false; }
virtual TOptional<FBuildablesSnapshot> StepBuildablesRead(FStepBudget& Budget) { return {}; }
virtual bool BuildablesReadAbandoned() const { return false; }
virtual void EndBuildablesRead() {}
```
Driver (beside `TickFactoryRead`): `TickBuildablesRead(Now)` and begin on `bFactoryDue` when any flag's source is native and `Status->bIsGameRunning`; a busy pass skips the tick with a rate-limited Display (no ids or names); source changed under a read drops it (`EndBuildablesRead`); leaving Running ends it. A finished read logs `Buildables read (game thread only): %d elements, %d extractors, %d generators, %d containers in %d steps, longest step %.2f ms, total %.2f ms` and hands the readings to the bridge worker, which builds the fragment (`NativeExtractorsFragment`) and holds it; `TakeFactory` splices `,"extractors":[...]` into the factory part JSON (before the closing brace) when the fragment is not older than `FactorySeconds * 1.5` and the factory part is produced; a held fragment is consumed once. With `extractorsSource` `bridge` nothing is begun and no byte of the factory part changes.

- [ ] **Step 1: Failing tests.** `Driver.NativeBuildables` (fake game reader gains `BuildablesStepsToFinish`, `Extractors`, abandoned and begin counters): nothing begins before the first due tick; Begin once per cadence tick, stepped once per tick; a second due tick while open is skipped; an abandoned read posts nothing and warns once; default config never begins; flipping the source to bridge under an open read calls End; leaving Running calls End; the log line has counts and times only. `FrmBridge.Extractors`: a fragment is spliced into the factory part, parses as JSON, other keys unchanged byte-for-byte; a stale fragment is not spliced; empty list is spliced as `[]`.
- [ ] **Step 2-4:** fail, implement, `judge-mod-test --min 211`, raise floor, Vitest for `STEP_LINE_NAMES` (a 2.05 ms line fails `stepProblems`), `check-api`. **Step 5:** commit `Buildables pass in the driver; extractors spliced into the factory part (S4-B)`.

**Reviewer checks:** factory part bytes identical with the flag off; budget reset when a read begins; no reading survives after the fragment exists; `FDriver` constructor order preserved.

### Task 5: Extractors, the game-side read

**Files:** Modify `GameReader.h/.cpp` (`BeginBuildablesRead`, `StepBuildablesRead`, `BuildablesReadAbandoned`, `EndBuildablesRead`: the pass skeleton, with the extractor branch), `PROVENANCE.md` (headers), `api-sources.md`, `docs/native-readers.md`.

Game side (open every header in the Lookups table first and add its row before the symbol; header wins): `Begin`: false when a read is open or no `AFGBuildableSubsystem`; store the weak world, the flags, reset the cursor and the lists. `Step`: `Budget.Begin(); do { re-fetch GetAllBuildablesRef(); if cursor at end finish; take element; if flag Extractors: Cast<AFGBuildableResourceExtractor>; IsValid; read id (`GetName()`), name (`mDisplayName`), class (`GetClass()->GetName()`), `IsProducing`, `IsProductionPaused`, clock `100 * GetCurrentPotential()`, location, circuit group id (the factory read's chain, null-checked), production resource through `GetExtractableResource()` then `GetResourceClass()` (null -> unoccupied), rates per ruling B4/V1 (formula written in the task from the headers, in per-minute units; fluids in m3), `bBackedUp` from the output inventory per V3 (`GetSlotSize`/`HasEnoughSpaceForItem`, true only when the slot is full), `bFluid` from the item form (`GetForm`) } while (!Budget.Spent()); Budget.End();`. World changed or subsystem gone: abandoned. A list cap of `2 * ExtractorsMaxItems`. The same cursor serves Tasks 9 and 16 (they add branches).

- [ ] **Step 1:** rows first (`check-api` `0 missing`). **Step 2:** implement. **Step 3:** `judge-mod-test --min 211`, `check-api`, `check-provenance`. **Step 4:** docs row ("Extractors | S4-B | `bridge` (default = not sent) | the Buildables pass, factory cadence"). Commit `Extractors: game-side sliced read (S4-B)`.

**Reviewer checks:** every symbol has a row; null resource and null inventory handled; no name or id in any log.

---

## PR S4B-2

### Task 6: Re-vendor the contract at the dash generators PR

**Gate: wait for the dash to report the generators contract merged.** As Task 2: sync at that commit; failing-first Vitest: the power part accepts `generators` (the full field set, `fuel: null`, `fuel` omitted is **rejected**, `supplement` absent accepted, `supplement: null` accepted), rejects 5001, rejects a `className` of 201 characters. Commit `Re-vendor the dash contract at <short sha> (S4-B: generators)`.

### Task 7: Generators, the pure reading

**Files:** Create `Native/Generators/NativeGenerators.h/.cpp`, `PROVENANCE.md`, `NativeGeneratorsTest.cpp`; floor `--min 215`.

**Interfaces:**
```cpp
inline constexpr int32 GeneratorsMaxItems = 5000;
struct FFuelReading { FString ClassName, Name; double Amount = 0; };   // amount in the contract's unit (items; fluids in m3)
struct FGeneratorReading
{
	FString Id, Name, ClassName;
	bool bHasLocation = false; double XCm = 0, YCm = 0, ZCm = 0;
	int32 CircuitGroupId = -1;
	double CurrentMW = 0, CapacityMW = 0, ClockPercent = 100;
	bool bProducing = false;
	TOptional<FFuelReading> Fuel;                 // unset -> "fuel":null (always written)
	bool bSupplementRead = false; TOptional<FFuelReading> Supplement;   // not read -> key absent; read and empty -> null
};
SATISDASHCORE_API FString NativeGeneratorsFragment(TArray<FGeneratorReading> Readings, int32& OutDropped, int32& OutOver);
```
Rules: key order as the vendored contract; `fuel` always present; `supplement` per `bSupplementRead`; negative or non-finite MW drops; className clamped to 200; sort by id, first of a repeated id; cap 5000.

- [ ] **Step 1: Failing tests (+4 = 215).** `NativeGenerators.Map` (coal with water supplement, fuel generator, biomass, nuclear without waste), `NativeGenerators.Fuel` (`fuel: null` present for geothermal and for empty; supplement absent vs null), `NativeGenerators.Rules` (cap, shuffle, duplicate, NaN), `NativeGenerators.Golden` (shapes of `frm-getGenerators-2026-10-27-test3-trimmed.json`, synthetic names). **Step 2-5** as Task 3; commit `Generators: pure fragment builder (S4-B)`.

### Task 8: The generators flag in the pass, attached to the power part

**Files:** Modify `Ports.h` (`FBuildablesSnapshot` gains `TArray<FGeneratorReading> Generators`), `Driver.cpp`, `FrmBridge.*`/`Snapshot.*` (per B3: the held generators fragment is spliced into the power part for every snapshot that carries `Power`, until older than `3 x FactorySeconds`), `DriverFakes.h`, tests `Driver.NativeBuildables` extended + `SatisDash.Core.Snapshot.Generators` (new), `Driver.NativeGenerators` (new) (+2 = 217); floor `--min 217`.

- [ ] **Step 1: Failing tests.** Generators fragment appears inside `power` on the next power part, then on later power parts until stale; after 3x the key is gone; with `generatorsSource` bridge the power part is byte-identical to today; the pass runs with only the generators flag (the extractor cast not performed: the fake counts casts); Snapshot JSON validates by shape. **Step 2-4:** fail, implement, `judge-mod-test --min 217`, floor, `check-api`. **Step 5:** commit `Generators flag in the Buildables pass, spliced into the power part (S4-B)`.

**Reviewer checks:** circuits array bytes unchanged; staleness rule has one named constant; no reading outlives the fragment.

### Task 9: Generators, the game-side read

**Files:** `GameReader.cpp` (the generators branch of `StepBuildablesRead`), `PROVENANCE.md`, `api-sources.md`, docs.

Branch: `Cast<AFGBuildableGenerator>`; id/name/class/location/circuit as extractors; `bProducing` `IsProducing()`; clock from `GetCurrentPotential()`; `CurrentMW` from the power-info path (`GetBaseProduction()` + `GetRegulatedDynamicProduction()`, V4: open the header, write the sum the comments support, units MW assumed); `CapacityMW` = `CalcPowerProductionCapacityForPotential(GetCurrentPotential())` (survey Q3; if `GetPowerProductionCapacity()` already follows the clock the header says so and the row records both); fuel: `Cast<AFGBuildableGeneratorFuel>` then `GetCurrentFuelClass()` and the count in `GetFuelInventory()` at `GetFuelInventoryIndex()` (fluids scaled by the factory read's litres-to-m3 helper), `Fuel` unset for geothermal / non-Fuel / empty; supplement only if `GetRequiresSupplementalResource()`. Waste is never read.

- [ ] **Step 1-4:** rows first, implement, `judge-mod-test --min 217`, `check-api`, `check-provenance`; commit `Generators: game-side sliced read (S4-B)`.

---

## PR S4B-3

### Task 10: Re-vendor the contract at the dash station-rates PR

**Gate: wait for the dash.** As Task 2; failing-first Vitest: `MapTrainStation` accepts `transferPerMinute`, `inflowPerMinute`, `outflowPerMinute` (finite >= 0), rejects a negative and `NaN`, accepts the keys absent. Commit `Re-vendor the dash contract at <short sha> (S4-B: station rates)`.

### Task 11: Station rate in the mapLive builder

**Files:** Modify `Native/Trains/NativeTrains.h/.cpp` (`FStationReading` gains `TOptional<double> TransferPerMinute;`; `NativeMapLiveJson` writes `transferPerMinute` after the existing station keys when set and finite and >= 0, rounded to 3 decimals by the shared number writer; never `inflowPerMinute`/`outflowPerMinute` (R3)); constant `inline constexpr double NativeStationRateToPerMinute = 1.0;` (R4/B6) in the same header with its comment; tests `NativeTrainsTest.cpp` (`SatisDash.Core.NativeTrains.StationRates`, `.StationRatesAbsent`, `.StationRateFactor`) (+3 = 220); floor `--min 220`.

- [ ] **Step 1: Failing tests.** A station with a rate prints it; with none prints no key (absent is not 0); a rate of 0 prints `0`; negative and NaN print no key; the key order of the existing station fields is unchanged (existing mapLive goldens do not move); `NativeStationRateToPerMinute == 1.0` is pinned so a change is a visible diff; ids unchanged. **Step 2-4:** fail, implement, `judge-mod-test --min 220`, floor, `check-api`. **Step 5:** commit `mapLive stations: transferPerMinute (S4-B)`. Provenance: the existing `Native/Trains/PROVENANCE.md` gains the station-rate lines and the survey.

### Task 12: Station rates, the game-side read (reflection)

**Files:** `GameReader.h/.cpp` (inside `TrainsReadStation`'s walk, flag `stationRatesSource` native), `PROVENANCE.md`, `api-sources.md`, docs.

Game side (open `FGBuildableTrainPlatformCargo.h` and `FGPipeConnectionComponent.h` first; rows for `FindFunction`, `ProcessEvent`, `FindFProperty`, `FFloatProperty::GetPropertyValue_InContainer`, `GetComponents<UFGPipeConnectionComponent>` from the engine headers with `path:line`; header wins):
- For each cargo platform found while walking the station: skip it if `GetComponents<UFGPipeConnectionComponent>` is non-empty (fluid platform, V8, R1); otherwise read the rate by R2: `UFunction* F = Cargo->GetClass()->FindFunctionByName("GetCurrentItemTransferRate")` looked up once per class and cached in a file-local `TMap<TWeakObjectPtr<UClass>, ...>`; if found, call via `ProcessEvent` with a stack parameter struct sized from the `UFunction` (`ParmsSize`), read the return property as float or int by its `FProperty` type; else fall back to `FindFProperty<FFloatProperty>` on `mSmoothedLoadRate` / `mSmoothedUnloadRate`, chosen by `GetIsInLoadMode()`. Multiply by `NativeStationRateToPerMinute`; sum over the station's item platforms; a station with no item platform, or whose every item platform failed the lookup, has no rate (`TransferPerMinute` unset). Count platforms read, skipped-as-fluid and lookup-failed in the trains read's log line (counts only); a lookup failure also logs a rate-limited Warning once per class (the class name is a game class, not player text).
- The station id and the trainRoutes id must come from the same helper (Open question 4): a Core test pins the shared function.

- [ ] **Step 1:** rows, `check-api` `0 missing`. **Step 2:** implement; `judge-mod-test --min 220`, `check-api`, `check-provenance`. **Step 3:** docs. Commit `Station rates: reflection read of the cargo platform transfer rate (S4-B)`.

**Reviewer checks:** lookups cached, never per call; `ProcessEvent` only on a valid object on the game thread; no write; absent (not 0) on failure; the Warning names a class, never a station.

---

## PR S4B-4

### Task 13: Re-vendor the contract at the dash storage PR

**Gate: wait for the dash.** As Task 2; failing-first Vitest: a snapshot with `storage.containers` validates; `fluidCapacityM3` optional; 2001 containers and 65 items are rejected; a body over `SNAPSHOT_MAX_BYTES` constant is exported as 5 MB; the part is refused when `reachable: false`. Commit `Re-vendor the dash contract at <short sha> (S4-B: storage)`.

### Task 14: Storage, the pure reading

**Files:** Create `Native/Storage/NativeStorage.h/.cpp`, `PROVENANCE.md`, `NativeStorageTest.cpp`; floor `--min 224`.

**Interfaces:**
```cpp
inline constexpr int32 StorageMaxContainers = 2000, StorageMaxItems = 64;
struct FStorageItemReading { FString ClassName, Name; double Amount = 0; TOptional<int32> StackMax; };   // fluids: m3, StackMax unset -> null
struct FContainerReading
{
	FString Id, Name, ClassName;
	bool bHasLocation = false; double XCm = 0, YCm = 0, ZCm = 0;
	int32 Slots = 0, SlotsUsed = 0;
	TOptional<double> FluidCapacityM3;      // set ONLY on a fluid buffer
	TArray<FStorageItemReading> Items;      // per stack, merged here per class
};
SATISDASHCORE_API FString NativeStorageFragment(TArray<FContainerReading> Readings, int32& OutDropped, int32& OutOver, int32& OutItemsCut);
```
Rules: the Core merges items per class (sum of amounts; `StackMax` of the first stack of that class), sorts items by class, cuts at 64 (`OutItemsCut` counts containers cut); `fluidCapacityM3` written only when set; fluid item `stackMax` `null`; `slotsUsed <= slots` clamp reported; ids sorted, first of a repeated id; cap 2000; the part is `{"containers":[...]}`.

- [ ] **Step 1: Failing tests (+4 = 224).** `NativeStorage.Map` (Mk1 box with Wire and Plastic, stackMax 500/200), `NativeStorage.Fluid` (fluid buffer: `fluidCapacityM3` present, one fluid item, `stackMax` null; an item container never has the key; empty buffer -> `items: []`), `NativeStorage.Rules` (merge of two stacks of one class, 65 classes cut to 64, 2001 containers cut, shuffle same bytes, duplicate id), `NativeStorage.Golden` (shapes of `frm-getStorageInv-2026-10-27-test3-trimmed.json`, synthetic names). **Step 2-5** as Task 3; commit `Storage: pure fragment builder (S4-B)`.

### Task 15: The storage flag and snapshot part

**Files:** Modify `Ports.h` (`FBuildablesSnapshot` gains `TArray<FContainerReading> Containers`), `Snapshot.h/.cpp` (`TOptional<FString> StorageJson` written as the top-level `"storage":{...}`; not written when the status read failed), `Driver.cpp`, `FrmBridge.*`, `DriverFakes.h`, tests `SatisDash.Core.Snapshot.Storage` and `SatisDash.Core.Driver.NativeStorage` (+2 = 226); floor `--min 226`.

Rules: storage is attached to the first snapshot after the fragment is built, then cleared (B3); the encoded size is checked against `SNAPSHOT_MAX_BYTES` (5 MB) and, if the assembled body would pass it, `storage` is dropped first, then `generators`, then `extractors` (Open question 7, rate-limited Warning with sizes, never a partial part); the encoded size of each fragment goes in the Buildables log line.

- [ ] **Step 1: Failing tests.** The part appears once, then not again until the next read; an old-shape snapshot is byte-identical; an over-cap assembled body drops storage then generators then extractors in order; JSON validates by shape; `storageSource` bridge never begins or logs. **Step 2-4:** fail, implement, `judge-mod-test --min 226`, floor, `check-api`. **Step 5:** commit `Storage flag in the Buildables pass and the storage snapshot part (S4-B)`.

### Task 16: Storage, the game-side read

**Files:** `GameReader.cpp` (the storage branch of `StepBuildablesRead`), `PROVENANCE.md`, `api-sources.md`, docs.

Branch: `Cast<AFGBuildableStorage>` (non-const pointer for `GetStorageInventory()`; this includes the depot uploader, R1/Open question 8); slots `GetSizeLinear()`, `slotsUsed` the count of stacks with items (`FactoryReadSlots`'s loop), per-stack item class/name/count and `StackMax` from `GetSlotSize` (V-survey 10), fluids none. Separately `Cast<AFGBuildablePipeReservoir>` (V9: open the header and the Blueprint note; if the Industrial Fluid Buffer is not that class, record it and the parity run will show the missing ids): `fluidCapacityM3 = GetFluidContentMax()`, one fluid item `GetFluidDescriptor()` (null when empty, V10) with `GetFluidContent()` m3, `StackMax` unset. Per-container merge happens in Core (Task 14); the game side hands one `FStorageItemReading` per stack, capped at `4 x StorageMaxItems` stacks per container.

- [ ] **Step 1-4:** rows first, implement, `judge-mod-test --min 226`, `check-api`, `check-provenance`; commit `Storage: game-side sliced read (S4-B)`.

---

## PR S4B-5

### Task 17: The FRM oracles for the four endpoints

**Files:** Create `harness/src/frm-s4b.ts`, `harness/test/frm-s4b.test.ts`.

```ts
export function mapFrmExtractors(body: unknown): FrmExtractor[]   // id, className, producing, paused, backedUp, resourceClass, current, max, percent, clockPercent, circuit?
export function mapFrmGenerators(body: unknown): FrmGenerator[]   // id, className, currentMW, capacityMW, clockPercent, producing, fuelClass|null, fuelAmount|null, supplementClass|null, supplementAmount|null, circuit?, nameHash
export function mapFrmStationRates(body: unknown): FrmStationRate[]   // station id, name hash, transfer (the station-level figure), per-platform sum, platform count
export function mapFrmStorage(body: unknown): FrmContainer[]   // id, className, slots, slotsUsed, fluidCapacity|null, items[] (class, amount, stackMax|null), nameHash
```
Every name goes through `sessionHash` (M6); the mapping reads FRM's JSON output only. Fixtures are synthetic bodies shaped like the four captures.

- [ ] **Step 1-5:** failing tests (percent/current/max copied; a missing slot gives null; no raw name in `JSON.stringify` of any output; a fluid buffer has `fluidCapacity`, a box does not; the station-level TransferRate equals the sum of its platforms in the fixture), implement, whole Vitest + typecheck; commit `FRM oracles for extractors, generators, station rates and storage (S4-B)`.

### Task 18: The S4-B judges

**Files:** Create `runs/native-parity/pass-s4b.ts`, `harness/test/native-parity-s4b-pass.test.ts`.

Evidence and result types follow `pass-s4.ts` (`judgeExtractors`, `judgeGenerators`, `judgeStations`, `judgeStorage`; each takes posted data (names already hashed), FRM samples bracketing each post by `S4B_GAP_MS = 120_000`, and returns `{pass, reasons, never}`). **Tolerances, fixed here before any run, never loosened:**

| Reader | Rule | Reason |
| --- | --- | --- |
| all | Schema-valid post (vendored contract); id sets equal against the nearest bracketing FRM samples (FRM-only and mod-only ids fail, first 10 listed); at least one accepted post; `frm.failed > 0` with no sample fails | ids are exact |
| extractors | `className`, `resourceClass`, `circuitGroupId` exact. `maxPerMinute` within `max(0.1/min, 1%)` of FRM's max in either bracketing sample (max includes purity and clock, which change slowly). `currentPerMinute` and `percent`: within 5 percentage points of either bracket or inside the closed range between the two brackets (productivity moves between reads). `isProducing`, `isPaused`, `isBackedUp`: equal to either bracketing sample; a mismatch against both fails (counted per field). An unoccupied extractor with no production passes if FRM also has none | state flips between samples; a rate formula wrong by more than the band is exactly what must fail |
| generators | `className`, `circuitGroupId`, `fuel` class, `supplement` class exact (null = null). `capacityMW` within `max(0.1 MW, 1%)` of either bracket. `currentMW` within `max(0.5 MW, 2%)` of capacity-scaled range between the two brackets (output follows demand). `isProducing` equal to either bracket. Fuel and supplement **amounts are report-only** (they burn continuously; a count of differences and the largest is quoted). Geothermal capacity: report-only when FRM says 0 (V5). `fuel` key present on every post (schema) | amounts change every second; class and capacity are stable |
| stations | Only stations in both sets with a rate are compared; id sets (stations with a rate) equal to FRM's stations that have at least one item platform; for each pair with FRM rate `f` and mod rate `m`: pass if `|m - f| <= max(0.5, 0.25 f)` against either bracket. **Factor check:** the median of `m/f` over stations with `f >= 1` must be within `[0.8, 1.25]`; otherwise the verdict fails with `unit factor suspect: median ratio R (NativeStationRateToPerMinute in NativeTrains.h)` and quotes the raw pairs (the first 10, ids and numbers only). A station whose rate is absent in the mod while FRM's is > 0 fails. `inflowPerMinute`/`outflowPerMinute` must be absent (R3). A run where no FRM station has f >= 1: `NOT EXERCISED: rate factor` and the id/absent rules only | smoothed values at different instants; the factor band catches a wrong unit (R4) |
| storage | `className`, `slots`, `fluidCapacityM3` presence and value (within 0.01 m3) exact per container. `slotsUsed` and each item amount: for a container whose two bracketing FRM samples are identical, the mod's `items` class set and amounts must equal them (fluid within 0.5% or 0.01 m3); for a container that changed between the brackets, report-only (counted). `stackMax` exact for items, null exactly on fluids. The depot uploader is compared like any container | contents change; stable containers are the only sound exact test |
| NOT EXERCISED (never fail) | `a fluid buffer`, `a fluid extractor (pump)`, `a nuclear generator`, `a geothermal generator`, `a generator with a supplement`, `an occupied-and-backed-up extractor`, `a rate over 1/min`, `a container over 64 classes` when absent from `Test3` | |

- [ ] **Step 1: Failing tests** (evidence in the test, synthetic hashes): per reader, identical passes; each stated tolerance passes at the edge and fails just past it; wrong factor (ratio 2) fails with the hint; an id only on one side fails; zero on both sides passes with a NOT EXERCISED line; an invalid post fails; no post fails; no FRM sample within 120 s fails; report-only lines never fail; no raw name in any reason string. **Step 2-5:** fail, implement, `npx vitest run test/native-parity-s4b-pass.test.ts --reporter=dot`, whole suite + typecheck; commit `The S4-B judges: extractors, generators, station rates, storage`.

**Reviewer checks:** thresholds are named constants with the table's reasons in comments and marked NEW/controller; S1 to S4-A judges untouched.

### Task 19: Wire the judges into `native-parity`

**Files:** Modify `runs/native-parity/pass.ts` (`NativeParity.s4b?`; `ReaderVerdicts` gains `extractors?`, `generators?`, `stationRates?`, `storage?`; `judgeReaders` adds them when `c.s4b` exists; `runReasons` requires the four keys `native` for an S4-B run; `evaluate` adds `R-extractors`, `R-generators`, `R-station-rates`, `R-storage` rows with the `*LogProblems` and a `Buildables read` line check; `requirements` gains the four; `export * from "./pass-s4b"`), `harness/src/core-runs/native-parity.ts` (`modConfig: NATIVE_SOURCES_S4B`; samplers `/getExtractor`, `/getGenerators`, `/getTrainStation`, `/getStorageInv` every 2 min with generous read timeouts (20 s) and a fresh sample after each accepted snapshot; the mod's posted fragments come from the recorded backend snapshot bodies (`factory.extractors`, `power.generators`, `storage`, `mapLive.stations[].transferPerMinute`); names replaced by hashes before the capture is written; `s4bEvidenceFrom`), `docs/runner.md`; tests `native-parity-s4b-drive.test.ts`, cases added to `native-parity-pass.test.ts`.

- [ ] **Step 1: Failing tests.** Samplers start/stop; a snapshot with each fragment triggers one extra `take()`; **a fake snapshot whose names are "Alpha"/"Beta" leaves no trace in the stringified capture**; an S4-A-shaped capture re-judges as before; an S4-B capture with the four keys native gives four verdicts; a missing `Buildables read` line fails the three buildable readers with the log problem; `storageSource: "bridge"` fails `runReasons`; one failing reader leaves the others' verdicts unchanged; the raw station pairs (mod posted next to FRM) are in the capture (R4 evidence). **Step 2-4:** fail, implement, whole suite + typecheck. **Step 5:** commit `native-parity: the S4-B samplers, evidence and verdicts (Test3)`.

### Task 20: `tick-cost-native` with S4-B on

**Files:** Modify `harness/src/core-runs/tick-cost-native.ts` (`modConfig: NATIVE_SOURCES_S4B`), `runs/tick-cost-native/pass.ts` (`requiredStepReaders` adds `"Buildables read"` when any of extractors/generators/storage is native; an S4-B config must have all four keys native; S1 to S4-A conditions kept; the station walk is covered by the existing `Trains read` line), `harness/test/tick-cost-native-pass.test.ts`.

- [ ] **Steps:** failing tests (S4-B evidence passes with every longest step <= 1.9 ms; fails a `Buildables read` at 2.1 ms; fails a missing line; fails `storageSource: bridge` in an S4-B config; S1 to S4-A evidence passes as before), implement, whole suite, typecheck, `check-api`, `check-provenance`, final `judge-mod-test --min 226`, commit `tick-cost-native: the Buildables pass`.

### Task 21: Docs and the follow-up list

**Files:** `docs/native-readers.md` (four rows `bridge` (default = not sent) with sprint S4-B; the keys in the override list; a "Where each reader stands (S4-B)" paragraph: all four still `bridge`; the Buildables pass cadence; the station rate factor and its status; ids of rebuilt buildables change), `docs/runner.md` (order of the sprint's runs; the backend image note), `api-sources.md` tidy. Tests: `check-api`, `check-provenance`, whole Vitest, typecheck. Commit `Docs: S4-B status, the new parts`.

---

## End of sprint: the runs and the follow-ups

The owner dispatches, in this order and once each:
1. `mod-test` on each PR branch (floors 211, 217, 220, 226).
2. Owner step: backend images from the last vendored pin or later.
3. `native-parity` on `Test3` (30-minute window; needs the backend host, `frmBaseUrl`, `sprint3World`). The first run records the station raw values next to FRM's (R4); a failed factor check stops the work and is reported with the evidence bundle.
4. `tick-cost-native` (baseline boot, then mod boot, on `Test3`).
Nothing else is run.

**`readers.json` must show per reader** `pass: true` with reasons listing what was compared and what is NOT EXERCISED: extractors (ids, class, resource, max, state flags, current within the stated band), generators (ids, class, fuel classes, capacity, current within band, amounts reported), station rates (ids, per-station tolerance, median ratio in the band, inflow/outflow absent), storage (ids, class, slots, fluid capacity, stable-container contents). `tick-cost-native` passes with `Buildables read` at or under 2 ms.

**Owner's run checklist.** `Test3` has miners and a pump (ideally a fluid extractor and an unoccupied one), generators of several kinds (coal with water, fuel, biomass, ideally nuclear and geothermal), two or more item freight platforms with trains running (and, if available, a fluid platform), item containers, a fluid buffer and the depot uploader; FRM running; the four dash contracts merged and the backend at that pin. Absent items are NOT EXERCISED.

**After S4-B (named, not planned):** one switch PR flipping the four defaults to `Native` (a reader that missed stays `bridge`) with `mod-claims.md` rows and a dash evidence PR; R7, the removal release, remains the owner's explicit word.

## Self-review

**Spec coverage.** Brief scope: extractors (T2-T5), generators (T6-T9), station rates (T10-T12), storage (T13-T16), source keys (T1), parity and tick-cost (T17-T20), docs (T21); rulings R1-R8 in the table; survey NEEDS VERIFICATION items in Open questions with a settling task each; cadence stated (B2); R6 decided (B1) with the reason.

**Placeholder scan.** Fill-ins: `Author:`/`Date:` in provenance files, commit hashes taken at execution, the Game-module code (symbols and sequence described; written against the open headers), the extractor and generator rate formulas (written from the headers in Tasks 5 and 9, judged by the stated tolerances), exact vendored field names (Tasks 3, 7, 14 follow the contract the gate tasks vendor).

**Type consistency.** `FBuildablesFlags/Snapshot/Counts` (T4) extended in T8, T15; `FExtractorReading` (T3) used in T4, T5; `FGeneratorReading` (T7) in T8, T9; `FContainerReading` (T14) in T15, T16; `NativeStationRateToPerMinute` (T11) in T12, T18; `NATIVE_SOURCES_S4B` (T1) in T19, T20; log name `Buildables read` (T4) in T19, T20.
