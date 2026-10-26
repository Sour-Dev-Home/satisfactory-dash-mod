# Native readers, Sprint S2 (factory machines, production rates, trains, stations, train routes) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The mod reads the factory's machines (R2a), their production and ingredient rates (R2b), the live trains and stations (R3) and the train-routes world layer from the game itself, resource nodes also carry `className`, and one combined 30-minute `native-parity` run plus one `tick-cost-native` run, both on the owner's `Test2` save, judge every reader separately, with a hard 2 ms ceiling on any single native game-thread step.

**Architecture:**
- Same pattern as S1. A per-reader source switch (`factorySource`, `trainsSource`; `ESource`, default `bridge`) in the mod's config. Pure logic (mapping, derivations, caps, sorting, the exact JSON) lives in `SatisDashCore` under `Native/<Reader>/` and shares one record writer with the FRM parse, so both sources emit the same bytes. Game-thread code is small, in `GameReader.cpp`, and **time-sliced**: every step stops after about 1 ms of work (`FStepBudget`), so no step can approach 2 ms.
- R2a/R2b: a sliced pass over the buildable subsystem's list (cursor re-fetched each step), `Cast` to `AFGBuildableManufacturer`, one plain `FFactoryReading` per machine; the worker sorts, derives, writes the `factory` snapshot part. R2b's rate maths is a separate pure function with a one-constant fallback (`DefaultRatesMode`).
- R3: trains and stations are small lists read in time-bounded slices (mapLive part, same 30 s cadence and caps as today). The **train-routes layer has no bridge path** (the bridge never sent it), so it has no switch and is a new world layer `TrainRoutes` fed only by the game.
- Harness: `s2World` + `worldKey` stage `Test2` for the two S2 runs only; `native-parity` gains the new readers' verdicts in `readers.json`; `tick-cost-native` gains the S2 sources and the 2 ms ceiling.

**Tech Stack:** Unreal Engine 5.6.1-CSS, SML 3.12.0 (`harness/toolchain.json`), C++20, UE automation tests (`SatisDashCoreTests`), TypeScript harness (Node, zod 4, Vitest 5).

**Spec:** ADR-0058 (satisfactory-dash, `docs-vault/wiki/decisions/0058-native-readers-remove-frm.md`: reader table, Decision 3 gates, risks, sprint table), the S2 header survey (`mod-native-readers-s2-headers-2026-10-26.md`, dash `docs-vault/raw-sources/`), the contract (`agent.ts`, `factory.ts`, `map.ts`, vendored in `harness/contract/src/`), and the S1 plan `docs/plans/2026-10-25-native-readers-s1.md` (the template; S1 shipped as PRs #53-#57). The owner's decisions of 2026-10-26 (scope, `Test2`, R2b tolerance and fallback, train status wording, 2 ms per step, runner economy) are built in below.

## Global Constraints

Everything in the S1 plan's Global Constraints still holds (pure Core, test names `SatisDash.Core.<Area>.<Case>`, unity-build prefixes for file-local helpers, case-sensitive protocol strings, no `check()` on outside data, logs only to `LogSatisDash` at `Display`/`Warning` and rate-limited, `check-api` at `0 missing, 0 bad rows`, `check-provenance` clean). Added or restated:

- **Clean room (CLAUDE.md rule 7, `docs/clean-room.md`).** Nobody opens FicsitRemoteMonitoring's source in any form or searches the web for it. FRM is a black box over HTTP: our recorded outputs (`Fixtures/`, the dash `captured-responses/`) and the live test server's endpoints. If FRM source is seen by accident: stop, tell the owner, record it in that reader's `PROVENANCE.md`.
- **No API from memory (rule 1).** Every game/SML/UE symbol comes from the "Lookups made" table below or from a header opened by the task, and gets its `api-sources.md` row (`path:line`) in the same task. `check-api` fails otherwise. **The header wins** over the survey; both are recorded in the row.
- **No weakened judges (rule 3).** No existing `runs/*/pass.ts` or `spikes/*/pass.ts` condition changes. New conditions are new code. The only owner-authorised additions are the 2 ms step ceiling and the S2 judgements. A failed spike stops the work and is reported with its evidence bundle.
- **Evidence is machine-written; loopback and the test server only** (rules 4, 5). Player, train and station names are player-chosen game text: evidence carries `sessionHash(name)` only (the recorder already does this for `mapLive` names).
- **Contract and goldens.** The contract does not move. The goldens move only where stated: Task 3 adds `className` to the resource-node golden (both sources), nothing else changes.
- **Memory and cost (owner: the mod stays light).** The native factory read keeps one plain struct per machine (about 0.3 KB for an average machine; the 20,000-machine cap is a worst case of a few MB, transient, freed when the worker has built the part) and never the 16 MB FRM body; the cursor holds no copy of the 100k+ buildable pointers. Trains and stations are copied once per read (dozens of pointers). Each read logs counts and times only.
- **The step ceiling (owner, 2026-10-26).** No single native game-thread step may take over 2 ms (`MaxNativeStepMs = 2.0`). Every sliced read stops at `NativeStepBudgetSeconds = 0.001` and logs its longest step; `tick-cost-native` fails if any logged longest step exceeds 2.0 ms or if a reader that must have run logged nothing. One-shot reads that are not sliced (status, auto-pause, players, power's circuit-id probe) are S0/S1 code outside this rule; the judge's reasons say so.
- **Test counts.** `mod-test.yml` has `--min 149` and 149 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` macros in `SatisDashCoreTests/Private` today. Running ledger: T2 +2 = 151, T3 +1 = 152, T4 +1 = 153, T5 +3 = 156, T6 +3 = 159, T7 +2 = 161, T8 +5 = 166, T9 +2 = 168. Each task's `judge-mod-test --min N` uses its ledger number; the workflow floor stays 149 until the owner raises it (proposed: 168; a higher floor cannot make a failing run pass).
- **Runs.** Executors never start runs. The owner dispatches `mod-test` on each PR branch, then **one** `native-parity` (30 min window) and **one** `tick-cost-native` for the whole sprint, on `Test2`. No other core run is added or repeated. Mod-test runs are batched: one per PR, plus one after any fix.
- Commits end with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. No personal data, no local paths in anything committed (including this plan).

## Lookups made for this plan (header wins)

From the S2 survey (Source/FactoryGame/Public unless noted; survey line numbers re-opened where marked **R**, re-opened by this plan). Each task still opens the header it uses before writing the call and adds the row.

| Need | Header, `path:line` | Notes |
| --- | --- | --- |
| Buildable list | `FGBuildableSubsystem.h:246` class; `:272` `Get(UWorld*)`; **R** `:359-360` `FORCEINLINE const TArray<AFGBuildable*>& GetAllBuildablesRef() const`, comment "can be invalidated by buildables being added/removed"; `:363` `GetBuildableCount(TSubclassOf<AFGBuildable>)` | Re-fetch the reference in every step and keep only an index across ticks (never a pointer into the array). `GetTypedBuildable` `:367/:371` scans all buildables in one call: not used (not sliceable). `mFactoryBuildings` `:640` is private. |
| Machine classes | `Buildables/FGBuildableFactory.h:27`; `Buildables/FGBuildableManufacturer.h:85`; `FGBuildableManufacturerVariablePower.h:11` | `Cast<AFGBuildableManufacturer>` also covers variable-power machines. Whether FRM also lists generators/miners/platforms is judged by the id-set check. |
| Identity | `UObject::GetName()` `Runtime/CoreUObject/Public/UObject/UObjectBaseUtility.h:429`; `GetClass()->GetName()` | Equality with FRM ids is the first parity check. |
| Display name | **R** `Buildables/FGBuildable.h:824` `FText mDisplayName` ("The human readable display name of this object"), in the `public:` block from `:813`; `:467` `SetBuildableDisplayName` (editor only) | Resolves survey item 10: read the public field from the actor (its class default if the instance copy is empty is the parity run's question; compare with FRM's `Name`, e.g. "Assembler"). |
| Recipe | `FGBuildableManufacturer.h:165` `GetCurrentRecipe()`; `FGRecipe.h:55` `GetRecipeName`, `:59/:63` static `GetIngredients/GetProducts`, `:75` `GetManufacturingDuration`; `ItemAmount.h:14` (`ItemClass :32`, `Amount :36`); `Resources/FGItemDescriptor.h:191` `GetItemName`, `:158` `GetForm`, `:18` `EResourceForm` | Null recipe maps to JSON null. |
| State flags | `FGBuildableFactory.h:151` `IsProducing`, `:184` `IsProductionPaused`, `:92` `HasPower`, `:189` `GetProductionIndicatorStatus` (`FGBuildable.h:50`, not used) | |
| Clock, productivity | `FGBuildableFactory.h:225` `GetCurrentPotential`, **R** `:211-213` `virtual float GetProductivity() const` ("A measure of how productive this factory is"), `:692-695` window comment (average over 300 to 600 s), `:710-712` `uint8 mCurrentProductivity` ("replicated compressed version ... 0-255 is 0-100 percent"), `:315` `GetCurrentProductionBoost` | The header does not say whether `GetProductivity()` is a 0-1 fraction. Handled as an evidence item, see "Survey open items". |
| Location | `GameFramework/Actor.h:2505` `GetActorLocation`, `:2511` `GetActorRotation` | cm to metres; yaw normalised to [0,360), as the bridge does. |
| Circuit group | `FGBuildableFactory.h:108` `GetPowerInfo`; `FGPowerInfoComponent.h:80` `GetPowerCircuit`; `FGCircuit.h:71` `GetCircuitGroupID` | -1 when any link is null. |
| Inventories | `FGBuildableManufacturer.h:157/:161` `GetInput/OutputInventory`; `FGInventoryComponent.h:443` `GetSizeLinear`, `:326` `GetStackFromIndex`, `:451` `GetInventoryStacks`, `:493` `GetSlotSize`; `FInventoryStack :95` (`NumItems :116`, `Item :112`, `GetItemClass :45`); `FGItemDescriptor.h:251` `GetStackSize`, `:258` `GetStackSizeConverted` (liquids /1000) | Only non-empty slots are read (FRM lists only those). Fluid scale: Amount and MaxAmount both /1000 for liquid/gas, in pure code. |
| Trains | `FGRailroadSubsystem.h:88`, `:117` `Get(UWorld*)`, `:174` `GetAllTrains`, `:207` `GetAllTrainStations`; `FGTrain.h:386`, `:460` `GetTrainName`, **R** `:64-70` `ETrainStatus { TS_Parked "Parked", TS_ManualDriving "Manual Driving", TS_SelfDriving "Self Driving", TS_Derailed "Derailed" }`, `:614` `GetTrainStatus`, `:511` `GetTimeTable`, `:519` `HasTimeTable`, `:523` `GetFirstVehicle`, `:527` `GetLastVehicle` | |
| Train position | **R** `FGVehicle.h:337-339` `virtual void GetVehicleLocationAndRotation(FVector&, FRotator&) const` with the comment "Returns the real location of the vehicle in regard to autopilot and proxy actor. This should be used instead of GetActorLocation/GetActorRotation"; `:347` `GetVehicleLocation()`; `FGRailroadVehicle.h:36` derives `AFGVehicle` | Resolves survey item 7 as far as headers go: use the first vehicle's `GetVehicleLocation()`. Which vehicle FRM reports is parity's question (fallback constant, Task 8). |
| Timetable | `FGRailroadTimeTable.h:32`, `:70` `GetStops`, `:15` `FTimeTableStop` (`Station :21`) | Stop names via the station identifier's name. |
| Stations | `FGTrainStationIdentifier.h:17`, `:84` `GetStation`, `:88` `GetStationName`; `Buildables/FGBuildableRailroadStation.h:16`, `:49` `GetStationIdentifier`, `:56` `GetStationOutputConnection`; `FGTrainPlatformConnection.h:26` (`GetConnectedTo :42`, `GetPlatformOwner :53`); `Buildables/FGBuildableTrainPlatform.h:29`, `:66` `GetConnectionInOppositeDirection` | Platform chain walk; the walk's order is ours, output sorted by id. |
| Cargo platform | `Buildables/FGBuildableTrainPlatformCargo.h:18`, **R** `:57-58` `GetIsInLoadMode()` ("whether this station should load or unload from vehicles"), `:37` `GetInventory` | The recorded station capture lists **cargo platforms** (`Build_TrainDockingStation_C_*`, FRM's name for freight platforms) with `LoadingMode` "Loading"/"Unloading" and inventory classes (current contents). That resolves survey item 9 for `items`: distinct classes currently in `GetInventory()`. |
| Wagon cargo | `FGFreightWagon.h:24` `GetFreightInventory :47`; `FTrainConsist :103` / `mConsistData :709` | Train `items` = distinct classes over the consist's freight wagons. |
| Node class | **R** `Resources/FGResourceNodeBase.h:144` `virtual TSubclassOf<UFGResourceDescriptor> GetResourceClass() const override`; `:146` override, `:150` original; `Resources/FGResourceDescriptor.h:13` | `GetResourceClass()->GetName()` is the recorded `Desc_OreIron_C` form. |
| Time | `FPlatformTime::Seconds` (already in `GameReader.cpp`'s scan) | Step budget. |

## Survey open items and how they are settled

| # | Item | Resolution in this plan |
| --- | --- | --- |
| 1 | Is FRM's id the actor `GetName()`? (machines, trains, stations, platforms) | Native ids are `GetName()`. The judge's first check, per reader, is **id-set equality** with FRM on the same minute (`readers.json` reasons list the first differences). If they differ the owner rules on a one-time id change (ADR risk row); nothing is matched silently. |
| 2 | Which classes does FRM's `getFactory` list? | Start from `AFGBuildableManufacturer` (Task 7). The judge lists FRM-only and mod-only ids with their `className`, so the owner sees at once whether generators/miners must be added. |
| 3 | Fluid slot units | Pure code scales liquid/gas amounts and capacities by 1/1000 (Task 5); `inputsFull`/`isBackedUp` use the same derivation as `FrmFactory.cpp`. Judged on the refinery in `Test2` (checklist). |
| 4 | `GetProductivity()` scale and standby value | Task 6: `ProductivityFullScale` constant (1.0 = fraction), rate maths clamped to 0..1; the Factory read log line carries `productivity min/max/mean` (counts only); the judge fails R2b with a clear reason if a raw value above 1.0001 appears (scale wrong), and prints the median mod/FRM ratio. A wrong scale is then a one-constant fix, or the fallback below. |
| 5 | Somersloop in `maxPerMinute` | Not applied (no capture has one). The Factory log line counts machines with `GetCurrentProductionBoost() > 1`; the judge reports `NOT EXERCISED` when the count is 0 and lists mismatching boosted machines when not. |
| 6 | Enumeration cost | Time-sliced cursor over `GetAllBuildablesRef()` (about 1 ms a step, class check per element); `GetBuildableCount` is logged beside the found count as advisory only (whether it counts subclasses is not in the header). Cost is measured by `tick-cost-native` on `Test2` against the 2 ms ceiling. A per-class cache is deferred unless that run shows the ceiling or total time is a problem. |
| 7 | Train position source | Header: first vehicle's `GetVehicleLocation()` (`FGVehicle.h:339/:347`). The recorded `getTrains` capture carries no per-vehicle location, so which vehicle FRM uses cannot be settled offline; the parity run decides, and `NativeTrainPositionVehicle` (Task 8) is a one-line fallback (first, last, or actor). A trains miss does not fail the other readers. |
| 8 | Non-"Self-Driving" status strings | Mapping is ours (Task 8): `TS_SelfDriving` "Self-Driving", `TS_Parked` "Parked", `TS_ManualDriving` "Manual", `TS_Derailed` "Derailed". Parity requires "Self-Driving" to match exactly; parked/manual/derailed mismatches are reported, not failed. |
| 9 | Platform `items`, ordering, station without platforms | Current contents of the cargo platform's inventory (recorded capture); a station with no cargo platform has `platforms: []`; platforms and item classes are sorted so the layer is deterministic. |
| 10 | Building display name | `mDisplayName` (FGBuildable.h:824), table above. |

## Review Focus

- **A world with no trains, no stations, no machines, or no recipe set:** `mapLive` with empty arrays, `factory` with `buildings: []`, unconfigured machines with `recipe: null` and empty rates. Expected: valid parts, never `available: false`, never a fall-back to FRM. Pinned in `NativeMapLive.Map`, `NativeFactory.Map`, `DriverNativeFactory`, `DriverNativeTrains`.
- **Buildables added or removed while a sliced read is in progress:** the list shrinks under the cursor, a machine is dismantled between slices. Expected: no crash, no duplicate id in the part (de-duplicated on the worker), cursor past the end ends the pass; weak pointers checked every step. Pinned in `NativeFactory.Rules` (duplicates, cap), `DriverNativeFactory` (a fake that shrinks mid-read).
- **More machines or trains than the contract holds:** 20,001 machines, 501 trains, a 250-character train name, 70 platforms, 70 item classes, 201 stops. Expected: caps and cuts exactly as the bridge's (`MaxBuildings`, `MapLiveMaxItems`, `ClampString`, `TRAIN_ROUTES_MAX_*`), counted, same bytes whatever the scan order. Pinned in `NativeFactory.Rules`, `NativeMapLive.Map`, `NativeTrainRoutes.Rules`.
- **A machine that is standby, newly built or has productivity 0, a fluid machine, a clock of 250%, a non-finite float:** rates 0 not NaN; fluid amounts in m3; no `-0`; non-finite values dropped to 0. Pinned in `NativeRates.Max`, `NativeRates.Current`.
- **A slice that would run long:** an element that is slow, a huge consist. Expected: the step ends after the element that crossed 1 ms; the next tick continues. Pinned in `StepBudget.Slices`.
- **A wrong source value, or one source native and the other bridge:** `factorySource`/`trainsSource` follow S1's rules (Broken config naming the key); with factory native and trains bridge, `getFactory` is never sent while `getTrains` still is, and the reverse. Pinned in `Config.Sources`, `Driver.NativeBridgeFlags`.
- **A reader the world never exercises:** parked/manual/derailed trains, Somersloop, an unconfigured machine, a station without platforms. Expected: compared where present, reported as `NOT EXERCISED`, not failed. Pinned in the new `runs/native-parity` tests.

## How the tests run

As in the S1 plan: UE tests via `pwsh scripts/test-mod.ps1 -Project <sml-project> -Engine <engine-root> -Out <tmp>\mod-test` then `cd harness; npm run harness -- judge-mod-test <tmp>\mod-test --min <ledger N>` (the executor's own paths, never committed; one build per task at most; Tasks 1-3 of PR S2-A and 4-9 of PR S2-B share the batch). `cd harness; npm run harness -- check-api` and `check-provenance` end every task that touches `mod/`. Harness: `npx vitest run <file> --reporter=dot` while iterating, the whole suite and `npm run typecheck` at the end of Tasks 1, 2, 10 and 11. Dispatching `mod-test` on the branch is the owner's (`gh workflow run mod-test.yml --ref <branch>`).

**PRs.** The work splits naturally in two and each half is independently reviewable:
- **PR S2-A (Tasks 1-3):** world staging for `Test2`, the 2 ms step ceiling (log format, `FStepBudget`, judge), and R4 `className`. It changes no factory or train behaviour and can merge before the readers exist.
- **PR S2-B (Tasks 4-11):** the switches, R2a, R2b, R3 and train routes, the parity judge and the tick-cost extension. One PR because the parity judge needs all readers and the readers share the switch, the bridge flags and the log lines.

## File structure

`mod/...` is `mod/SatisDashAgent/Source`.

| File | New/Modified | Responsibility |
| --- | --- | --- |
| `harness/src/config.ts`, `harness/src/run.ts`, `harness/src/core-run.ts`, `harness/src/core-runs/*.ts`, `docs/runner.md` | Modified (T1) | `s2World`, `worldKey`, the staged-save checklist |
| `mod/SatisDashCore/Public/SatisDash/StepBudget.h`, `Private/StepBudget.cpp` | New (T2) | `FStepBudget` (injectable clock), `NativeStepBudgetSeconds` |
| `harness/src/reader-lines.ts`, `runs/tick-cost-native/pass.ts`, tests | Modified (T2, T11) | Step-cost lines, the 2 ms ceiling |
| `mod/SatisDashCore/.../Native/ResourceNodes/*`, `FrmResourceNodes.*`, `GameReader.cpp`, `Fixtures/frm-getResourceNode.expected.json`, `harness/src/frm-native.ts` | Modified (T3) | `className` |
| `Config.h/.cpp`, `FrmBridge.h/.cpp`, `Driver.cpp`, `DriverFakes.h` | Modified (T4) | `factorySource`, `trainsSource`, bridge flags, `Readers:` line |
| `Native/Factory/*` (`NativeFactory.h/.cpp`, `PROVENANCE.md`), `FrmFactory.cpp/.h` | New/Modified (T5) | `FFactoryReading`, shared writer, `NativeFactoryPart` |
| `Native/Rates/*` | New (T6) | `NativeRates`, `ERatesMode` |
| `GameReader.h/.cpp`, `Ports.h`, `FrmBridge.*`, `Driver.cpp` | Modified (T7) | Sliced factory read, `SubmitNativeFactory` |
| `Native/Trains/*`, `Native/TrainRoutes/*` | New (T8) | Readings, status mapping, mapLive arrays, route records |
| `FrmWorld.*`, `WorldSender.*`, `GameReader.*`, `Ports.h` | Modified (T9) | Third world layer `TrainRoutes`, native mapLive read |
| `harness/src/frm-s2.ts`, `runs/native-parity/pass.ts`, `harness/src/core-runs/native-parity.ts`, `harness/src/recorder.ts` | New/Modified (T10) | Oracles, judge, drive |
| `docs/native-readers.md`, `api-sources.md`, `.github/workflows/core-run.yml` | Modified (T3-T11) | Status table, rows, workflow options |

---

## PR S2-A

### Task 1: Stage `Test2` for the two S2 runs, and the owner's save checklist

**Files:**
- Modify: `harness/src/config.ts` (`s2World: z.string().min(1).optional()`), `harness/src/run.ts` (`RunDeps.loadWorld?(key?: WorldKey)`; `realDeps` reads `config.s2World` when the key is `"s2"`, else `config.s3World`), `harness/src/core-run.ts` (`CoreRunSpec.worldKey?: WorldKey`, default `"s3"`; the baseline `loadWorld` call at the spec's key; preflight refusal), `harness/src/core-runs/native-parity.ts`, `harness/src/core-runs/tick-cost.ts` (+ `tick-cost-native.ts`): `loadWorld!(spec.worldKey)`
- Modify: `docs/runner.md` (new section "The S2 world (Test2)")
- Test: `harness/test/core-run.test.ts` (or the existing file that covers `needs s3World`), `harness/test/config.test.ts`

**Interfaces:**
- Produces: `type WorldKey = "s3" | "s2"` (exported from `harness/src/run.ts`); `CoreRunSpec.worldKey`; the refusal text `<id> needs s2World in the runner config`.

**How staging works today** (so the reviewer can check the change is minimal): `config.s3World` names a save file; `loadWorld(api, name, bytes)` (`harness/src/world.ts`) uploads it with `UploadSaveGame` and `LoadSaveGame: true`, each time a core run needs a game (`ctx.deps.loadWorld!()`), and the run records the session hash. Only `satis-runner` stages (`savesConflict`). Nothing else reads `s3World`, so `parity`, `enrol`, `tick-cost` and the rest keep calling `loadWorld()` with no key and get S3's save exactly as before.

- [ ] **Step 1: Failing tests.** In the core-run test file add: (a) a spec with `world: true, worldKey: "s2"` and deps without `s2World` is refused with `/needs s2World/`; (b) with the key, `loadWorld` is called with `"s2"` (a fake records its argument); (c) a spec with no `worldKey` calls it with `undefined`/`"s3"` (existing behaviour, assertion on the fake). In `config.test.ts`: `s2World` is accepted, optional, and an empty string is rejected.
- [ ] **Step 2: Run to see them fail** (`npx vitest run harness/test/core-run.test.ts --reporter=dot`).
- [ ] **Step 3: Implement** the types, the `realDeps` branch (`readFileSync(config.s2World!)`), the preflight refusal next to the existing `needs s3World` one (`core-run.ts:167-168`), and pass `spec.worldKey` at the baseline load (`core-run.ts:294`) and in the two drives. Do not touch `run.ts`'s spike path (S3/S6 keep `s3World`).
- [ ] **Step 4: Docs.** `docs/runner.md`: the runner config names `"s2World"` beside `"s3World"`; only `native-parity` and `tick-cost-native` use it; the staged file lives outside the repository; the same `satis-runner` rules apply. Add the owner's checklist verbatim:

> **Owner checklist: `Test2` must contain, before the S2 runs** (if something is missing, add it in the save and stage the new copy; nothing in the harness can build it):
> 1. Production machines of **several kinds** (at least assembler, constructor and one more recipe machine), some with a **non-100% clock**, at least one **unconfigured** machine, at least one machine in standby/paused (all are checked, not required to pass if absent: absent ones are reported NOT EXERCISED).
> 2. **At least one oil refinery** (or packager) running a **fluid** recipe, so liquid slots and rates are compared.
> 3. **Power**: at least one connected circuit with production and consumption.
> 4. **Trains running a timetable between at least two stations** (self-driving), each station with at least one cargo platform, wagons carrying items. A parked or manual train is welcome, not required.
> 5. The save loads on the test server (the same dedicated-server version as `harness/toolchain.json`) and is the drones-and-trucks save used for the 3B captures.
- [ ] **Step 5:** `npx vitest run --reporter=dot` and `npm run typecheck`. **Step 6:** commit `Stage the S2 world for the native runs (s2World, worldKey)`.

**Reviewer checks:** existing runs untouched (diff shows no change to `parity`, `enrol`, `tick-cost` behaviour; their tests unchanged and green); no path to a real save in committed text; the refusal names the key; no pass condition touched.

### Task 2: The 2 ms step ceiling: `FStepBudget`, the step-cost log lines, the judge

**Files:**
- Create: `mod/SatisDashCore/Public/SatisDash/StepBudget.h`, `Private/StepBudget.cpp`, `SatisDashCoreTests/Private/StepBudgetTest.cpp`
- Modify: `harness/src/reader-lines.ts`, `harness/test/reader-lines.test.ts` (create if absent), `runs/tick-cost-native/pass.ts`, `harness/test/tick-cost-native-pass.test.ts`

**Interfaces:**
- Produces (C++): 
```cpp
inline constexpr double NativeStepBudgetSeconds = 0.001;   // a sliced step stops after the element that crosses this
inline constexpr double MaxNativeStepMs = 2.0;             // owner, 2026-10-26: the ceiling tick-cost-native judges
class SATISDASHCORE_API FStepBudget
{
public:
	FStepBudget(TFunction<double()> InNow, double InBudgetSeconds = NativeStepBudgetSeconds);
	void Begin();                       // starts a step
	bool Spent() const;                 // true once the budget is used up (check after each element, never before the first)
	void End();                         // records this step's duration
	int32 Steps() const; double LongestMs() const; double TotalMs() const;
	void Reset();
};
```
- Produces (TS): `STEP_LINE_NAMES = ["Resource node scan","Factory read","Trains read","Train routes read"]`; `stepCosts(lines): StepCost[]` (`{ reader, longestStepMs, totalMs }`, one per matching line, parsed from `<Name> (game thread only): ... longest step X ms, total Y ms`); `MAX_NATIVE_STEP_MS = 2.0`; `stepProblems(lines, required: string[]): string[]` (a required reader with no line, or any `longestStepMs > 2.0`, becomes a reason naming the reader and the value).

The existing `scanCosts`/`describeScanCost`/`SCAN` regex keep their behaviour (S1 tests unchanged); `stepCosts` is additive.

- [ ] **Step 1: Failing UE test** `SatisDash.Core.StepBudget.Slices`: with a fake clock that advances 0.4 ms per call, `Spent()` is false after the first and second element and true after the third; `End()` then records `Steps()==1`, `LongestMs()` about 1.2; a second step of one element records `LongestMs()` as the maximum; `Reset()` zeroes all. `SatisDash.Core.StepBudget.AlwaysOneElement`: with a clock that jumps 5 ms on the first call, a step still does exactly one element (the loop is `do { work } while (!Spent())`, so progress is guaranteed), and `LongestMs()` is reported as 5 (it is the ceiling's job to fail it). Expected count 151.
- [ ] **Step 2: Failing Vitest** in `reader-lines.test.ts`: three sample lines (one per reader, longest 0.30, 1.70, 2.05 ms) parse to three `StepCost`; `stepProblems(lines, ["Factory read"])` is empty for 1.70, lists `Train routes read` at 2.05 ms (`2.05 ms exceeds the 2 ms ceiling`), and lists a required reader that has no line (`no "Trains read (game thread only):" line`). A line with `longest step 2.00 ms` passes (the ceiling is "over 2 ms").
- [ ] **Step 3: Implement** both. `FStepBudget` takes the clock as a `TFunction` so the game passes `FPlatformTime::Seconds` and the tests a fake (no UE time in Core tests).
- [ ] **Step 4:** In `tick-cost-native/pass.ts` add, after the existing `extra` checks, `extra.push(...stepProblems(e.lines, requiredStepReaders(sources)))` where `requiredStepReaders` returns `["Resource node scan"]` plus `"Factory read"` when `factorySource` is native, `"Trains read"` and `"Train routes read"` when `trainsSource` is native, from `captures["mod-config.json"]`. With S1's capture (no S2 keys) only the scan is required, so S1's stored evidence still rejudges identically. Add tests: S1-shaped evidence unchanged; S2-shaped evidence with a 2.4 ms Factory read fails with that reason; reasons on pass list the longest step per reader.
- [ ] **Step 5:** `check-api` (rows for `FPlatformTime::Seconds` exist; add none unless reported), `judge-mod-test --min 151`, Vitest. Commit.

**Reviewer checks:** `Spent()` never checked before the first element; the ceiling comparison is `> 2.0`; the existing `Base.evaluate` limits (tick median, memory, outbound) are imported unchanged; S1's `tick-cost-native` tests still pass untouched; no `result.json` edit.

### Task 3: R4 `className` on native resource nodes (S1 leftover 1)

**Files:**
- Modify: `mod/SatisDashCore/Public/SatisDash/Native/ResourceNodes/NativeResourceNodes.h` (`FString ClassName;` on `FResourceNodeReading`), `Private/Native/ResourceNodes/NativeResourceNodes.cpp`, `Public/SatisDash/FrmResourceNodes.h` + `Private/FrmResourceNodes.cpp` (the shared record writer `AppendResourceNodeRecord` writes `className` after `exploited`; the FRM parse reads FRM's `ClassName`; absent or empty means the field is omitted, as the contract's field is optional), `SatisDashGame/Private/GameReader.cpp` (node scan: `Node->GetResourceClass()` -> `GetName()`; null class gives empty), `Native/ResourceNodes/PROVENANCE.md` (add headers `Resources/FGResourceNodeBase.h:144`, `Resources/FGResourceDescriptor.h:13`, survey S2 and the capture name)
- Modify fixtures: `SatisDashCoreTests/Fixtures/frm-getResourceNode.expected.json` (every node gains `className` from the fixture's `ClassName`; see below)
- Modify: `SatisDashCoreTests/Private/NativeNodesTest.cpp`, `FrmResourceNodesTest.cpp`; `harness/src/frm-native.ts` (`CanonNode.className?`, `mapFrmResourceNodes` passes `ClassName`, `canonNodes` includes it), `harness/test/frm-native.test.ts`; `runs/native-parity/pass.ts` (R4 compares `className` too, via `canonNodes`); its test; `api-sources.md`

**Interfaces:** consumes S1's `FResourceNodeReading`, `AppendResourceNodeRecord`, `BuildNativeResourceNodes`; produces `FResourceNodeReading::ClassName`.

- [ ] **Step 1: Golden first.** Check that `frm-getResourceNode.json` carries `ClassName` per node (it is FRM's captured output: `Desc_OreIron_C` and similar). If a record lacks it, the expected file omits `className` for that record. Update the expected file by running the (failing) golden test once and reading the diff, never by hand-guessing: the only difference must be the new `className` lines.
- [ ] **Step 2: Failing tests.** `FrmResourceNodesTest` golden (existing test, updated expected); `NativeNodes.ClassName` (new, +1 = 152): readings with `ClassName` `Desc_OreIron_C` and one empty produce `"className":"Desc_OreIron_C"` and no key for the empty one; sorting stays by the S1 key then `className`; two nodes differing only by className are both kept. Vitest: `frm-native` golden equality (harness mapper == the same expected file), `canonNodes` differs when only `className` differs.
- [ ] **Step 3: Implement.** The record writer is shared, so FRM and native bytes stay identical by construction. Bridge mode now also emits `className`: one extra world repost per host on update, expected and harmless (hash changes once).
- [ ] **Step 4: Game side.** In the scan's node branch copy `const TSubclassOf<UFGResourceDescriptor> Class = Node->GetResourceClass();` and set `Reading.ClassName = Class ? Class->GetName() : FString()`. Add the `api-sources.md` rows (`AFGResourceNodeBase::GetResourceClass`, `UFGResourceDescriptor`, the include, with the `path:line` above and a note "H: returns the effective class; the override's behaviour is parity's question").
- [ ] **Step 5:** run `judge-mod-test --min 152`, `check-api`, `check-provenance`, Vitest; commit.

**Reviewer checks:** the golden diff is only `className`; no other golden moved; the parity judge compares `className` in the same canonical form (not a new weaker comparison); PROVENANCE.md lists the new headers and "FRM source was not opened"; `GetName()` not `GetPathName()`.

---

## PR S2-B

### Task 4: The switches `factorySource` and `trainsSource`, the bridge flags, the `Readers:` line

**Files:**
- Modify: `Config.h/.cpp` (`DefaultFactorySource`, `DefaultTrainsSource` = `Bridge`; `TOptional<ESource> FactorySource, TrainsSource`; `GetFactorySource()`, `GetTrainsSource()`; the same strict parse and round trip as S1's `playersSource`), `FrmBridge.h/.cpp` (`RequestPass(bool bFactoryDue, bool bPlayers = true, bool bFactoryFromFrm = true, bool bTrainsFromFrm = true)`: `getFactory` only when due **and** `bFactoryFromFrm`; `StartMapCycle()` only when due and `bTrainsFromFrm`), `Driver.cpp` (pass the flags from the config; the `Readers:` line becomes `Readers: players=%s resourceNodes=%s factory=%s trains=%s`), `DriverFakes.h`
- Modify tests: `ConfigTest.cpp` (`SatisDash.Core.Config.Sources` extended with both keys: bad values `"Native"`, `""`, `1`, `null` name the key; unset keys are not written back), new `DriverNativeTest.cpp` case `SatisDash.Core.Driver.NativeBridgeFlags` (+1 = 153): factory native + trains bridge sends no `/getFactory` but does send `/getTrains` and `/getTrainStation`; reversed; both native sends neither; players follow their own key; default config asks FRM for all.
- Modify: `harness/src/reader-lines.ts` (`READERS` regex accepts the S1 two-field form and the S2 four-field form; `ReaderSources` gains optional `factory`, `trains`; `factoryLogProblems`, `trainsLogProblems` mirror `playersLogProblems`), its test (S1 line still parses and yields `factory`/`trains` undefined)
- Modify: `harness/src/core-run.ts` (`NativeSources` gains `factorySource`, `trainsSource`; a new `NATIVE_SOURCES_S2` with all four native; S1's `NATIVE_SOURCES` unchanged so S1 evidence rejudges), `harness/src/mod-config.ts` drift test (the guard that keys written equal the mod's keys), `docs/native-readers.md` (two new rows, still `bridge`; the override keys documented as stopgaps)

**Interfaces:** produces `FAgentConfig::GetFactorySource()/GetTrainsSource()`, the bridge flags above, `NATIVE_SOURCES_S2`, `factoryLogProblems(lines)`, `trainsLogProblems(lines)`.

- [ ] Steps: failing tests (UE and Vitest), run, implement, run, `judge-mod-test --min 153`, `check-api`, commit.

**Reviewer checks:** an unset key means the default, and the default is `bridge`; with a source native, the bridge makes no request for that part (the existing `getTrains`-pair state machine is not left half-started); the bridge still runs for the other part; S1's `NATIVE_SOURCES` and its tests are untouched; the config error names the key.

### Task 5: R2a, the pure factory reading and the shared record writer

**Files:**
- Create: `Public/SatisDash/Native/Factory/NativeFactory.h`, `Private/Native/Factory/NativeFactory.cpp`, `Private/Native/Factory/PROVENANCE.md`, `SatisDashCoreTests/Private/NativeFactoryTest.cpp`
- Modify: `Public/SatisDash/FrmFactory.h`, `Private/FrmFactory.cpp` (extract `WriteBuilding` into a free function `AppendFactoryBuilding(Writer, const FFactoryBuildingModel&)` that both the FRM parse and the native path call; **bytes of the FRM path must not change**: the existing `FrmFactoryTest` goldens stay green untouched)

**Interfaces:**
- Produces (plain structs, all numbers in the units FRM's wording and the bridge use):
```cpp
struct FRateReading { FString Name; FString ClassName; double Current = 0; double Max = 0; double Percent = 0; };
struct FSlotReading { FString ClassName; double Amount = 0; double MaxAmount = 0; };   // already in FRM's scale (fluids in m3)
struct FFactoryReading {
	FString Id, Name, ClassName;          // GetName(), mDisplayName, GetClass()->GetName()
	TOptional<FString> Recipe;            // unset: no recipe
	bool bProducing = false, bPaused = false;
	TArray<FRateReading> Production, Ingredients;
	TArray<FSlotReading> Output; TOptional<TArray<FSlotReading>> Input;
	int64 CircuitGroupId = -1;
	bool bHasLocation = false; double XCm = 0, YCm = 0, ZCm = 0, YawDeg = 0;
	double ClockPercent = 0;              // 100 * potential; 0 = not reported
};
SATISDASHCORE_API FString NativeFactoryPart(TArray<FFactoryReading> Readings, int32& OutDropped);   // {"buildings":[...]}
```
`NativeFactoryPart` sorts by `Id` (ordinal), drops later duplicates of an id, keeps `MaxBuildings` (counting the rest as dropped), derives `isBackedUp` and `inputsFull` with the exact rules of `FrmFactory.cpp:468-486` (moved into one shared helper so both sources use it), converts location with the bridge's `FrmFactoryMetres` / `FrmFactoryYaw` (moved to the shared header), rounds `clockSpeedPercent` to 0.01, and writes through `AppendFactoryBuilding`.

- [ ] **Step 1: Failing tests (+3 = 156).**
  - `NativeFactory.Golden`: build readings equal to the records of `frm-getFactory-running.json` (the test writes them out explicitly, one machine per record of the fixture, including the unconfigured one from `frm-getFactory-norecipe.json`) and assert `NativeFactoryPart(...)` equals the same `*.expected.json` text the bridge test asserts. This proves identical bytes, the S1 pattern.
  - `NativeFactory.Map`: unconfigured machine (`Recipe` unset) gives `"recipe":null`, empty rates, `isBackedUp:false`, `inputsFull:false`; a paused machine is never backed up; a full output slot (`Amount >= MaxAmount > 0`) gives `isBackedUp:true`; `inputsFull` needs every ingredient's slot full and an `Input` list set; `CircuitGroupId` -1 is written as -1; a missing location omits `location`; yaw `-90` is written as `270`; `ClockPercent` 159.99999 is written `160`.
  - `NativeFactory.Rules`: 20,001 readings keep 20,000 and report 1 dropped; the same readings in a different order give identical text; a duplicate id keeps the first in sorted order; a 250-character name is cut as `ClampString` does; more than `MaxRatesPerBuilding` rates are cut at 16.
- [ ] **Step 2: Run to see them fail** (compile errors count). **Step 3: Implement.** **Step 4:** `judge-mod-test --min 156` (the existing `FrmFactory*` tests must still pass unchanged), `check-api`.
- [ ] **Step 5: PROVENANCE.md** (`Native/Factory/`): `Reader: Factory`; `Code:` the two new files plus `GameReader.cpp` (Task 7 adds it, so write the line then); `Headers:` the factory rows of the lookups table; `Survey: mod-native-readers-s2-headers-2026-10-26.md`; `Captures:` `frm-getFactory-2026-09-22-01-running-trimmed.json`, `frm-getFactory-2026-10-25-id-stability.md` (FRM HTTP output only); author, date; `FRM source was not opened`. Commit.

**Reviewer checks:** the FRM path's bytes unchanged (existing goldens untouched); one writer, not two copies; the derivation helper is shared, not re-implemented; `-0` and non-finite handled as the bridge does (`FrmFactoryMetres`); no `check()` on outside data; provenance has all fields.

### Task 6: R2b, the rate maths, and the fallback

**Files:**
- Create: `Public/SatisDash/Native/Rates/NativeRates.h`, `Private/Native/Rates/NativeRates.cpp`, `Private/Native/Rates/PROVENANCE.md`, `SatisDashCoreTests/Private/NativeRatesTest.cpp`

**Interfaces:**
```cpp
enum class ERatesMode : uint8 { Averaged, Instantaneous };
/** The switch PR for R2b flips nothing; the owner's fallback ruling (R2b ships max only) flips this one constant. */
inline constexpr ERatesMode DefaultRatesMode = ERatesMode::Averaged;
/** GetProductivity() full scale. 1.0 = a 0..1 fraction (header does not say; the parity run checks, Task 10). */
inline constexpr double ProductivityFullScale = 1.0;

struct FRecipeItemReading { FString Name; FString ClassName; double AmountPerCycle = 0; bool bFluid = false; };
struct FRateInput {
	TArray<FRecipeItemReading> Products, Ingredients;
	double CycleSeconds = 0;       // UFGRecipe::GetManufacturingDuration, no modifiers
	double Potential = 1.0;        // GetCurrentPotential
	double RawProductivity = 0;    // GetProductivity()
	bool bProducing = false;
};
SATISDASHCORE_API void NativeRates(const FRateInput& In, ERatesMode Mode, TArray<FRateReading>& OutProduction, TArray<FRateReading>& OutIngredients);
```
Rules (the owner's tolerance: `maxPerMinute` within rounding of FRM, current within 3%):
- `Max = (AmountPerCycle / (bFluid ? 1000 : 1)) * 60 / CycleSeconds * Potential`, 0 when `CycleSeconds <= 0`, non-finite, or negative; rounded to 4 decimals (float noise: potential 1.6f must give 8, not 8.0000001).
- `Averaged`: `Fraction = clamp(RawProductivity / ProductivityFullScale, 0, 1)`; `Current = Max * Fraction`; `Percent = 100 * Fraction`; the same fraction for products and ingredients (the capture shows the same percent for both).
- `Instantaneous` (the fallback, "max only plus an instantaneous flag"): `Fraction = bProducing ? 1 : 0`; `Current = Max * Fraction`; `Percent = 100 * Fraction`. `isProducing` is already in the part, so the planner has the flag.
- A Somersloop boost is not applied (see survey item 5).

- [ ] **Step 1: Failing tests (+3 = 159).** `NativeRates.Max`: with numbers from the recorded captures, not the code: a recipe of 5 items per 12 s... use the capture's own pairs, which the implementer reads from `frm-getFactory-2026-09-22-01-running-trimmed.json`: Stator at 100% clock gives `maxPerMinute` 5 and at 160% gives 8 (use the recipe amount and duration that reproduce 5 at 100%); the refinery's 60 crude in and 40 fuel out per minute (m3) from 60000/40000 litres per cycle amounts; a 250% clock; `CycleSeconds = 0` gives 0 not inf. `NativeRates.Current`: productivity `24.0/255.0` (the grid value in the capture, 9.4117%) times Max 60 gives `5.647`, percent `9.4118` within 1e-3; productivity above full scale clamps to 100%; negative or NaN gives 0. `NativeRates.Fallback`: `Instantaneous` gives Max when producing and 0 when not, for both lists, and `DefaultRatesMode` is `Averaged` (the test pins the default so a fallback flip is a visible diff).
- [ ] **Step 2:** run, see fail; **Step 3:** implement; **Step 4:** `judge-mod-test --min 159`; `check-provenance`; commit. PROVENANCE.md (`Reader: Rates`) lists `FGRecipe.h`, `FGBuildableFactory.h` productivity rows, `FGItemDescriptor.h` form rows, the two captures, and states the averaging window inference as "hypothesis tested by the parity run".

**Reviewer checks:** formula uses the recipe's base duration with the potential, not `GetProductionCycleTime()` twice; fluids /1000 only for `bFluid`; fallback is a mode, not a deleted code path; `ProductivityFullScale` has a comment pointing at the parity judge; no tolerance copied into the mod.

### Task 7: R2a/R2b, the game-side sliced read, the bridge hand-off and the driver

**Files:**
- Modify: `Public/SatisDash/Ports.h` (`IGameReader::BeginFactoryRead() -> bool`, `StepFactoryRead(FStepBudget&) -> TOptional<TArray<FFactoryReading>>`, defaults `false`/`{}`), `SatisDashGame/Private/GameReader.h/.cpp`, `Public/SatisDash/FrmBridge.h`, `Private/FrmBridge.cpp` (`bool SubmitNativeFactory(TArray<FFactoryReading>, double ReadAt)`: moves the readings into an `FFactoryJob` whose first worker task builds the part with `NativeFactoryPart` and sets `FactoryReady`, so `TakeFactory` is unchanged), `Private/Driver.cpp`, `DriverFakes.h`
- Tests: `DriverNativeTest.cpp` `SatisDash.Core.Driver.NativeFactory`, `FrmBridgeTest.cpp` `SatisDash.Core.FrmBridge.NativeFactory` (+2 = 161)
- Modify: `Native/Factory/PROVENANCE.md` (Code line for `GameReader.cpp`), `api-sources.md`, `docs/native-readers.md`

**Interfaces:** consumes `FFactoryReading`, `NativeRates`, `FStepBudget`; produces the port methods above and the log line `Factory read (game thread only): %d buildings in %d steps, %d machines, longest step %.2f ms, total %.2f ms, expected %d, boosted %d, productivity %.3f..%.3f` (counts, times and the raw productivity min/max only; one line per completed read, at `Display`, rate-limited to one per read).

Game side, in `GameReader.cpp` (all symbols from the lookups table; open each header and add its row before using it):
- `BeginFactoryRead`: `UWorld` and `AFGBuildableSubsystem::Get(World)`; null gives false. Reset cursor 0, results empty, weak world pointer stored, `FStepBudget` counters reset, `GetBuildableCount(AFGBuildableManufacturer::StaticClass())` stored as `Expected` (advisory).
- `StepFactoryRead(Budget)`: `Budget.Begin()`; `do { const TArray<AFGBuildable*>& All = Subsystem->GetAllBuildablesRef();` (re-fetched every element-batch, never kept across ticks) `if (Cursor >= All.Num()) { finished } ... Cast<AFGBuildableManufacturer>(All[Cursor++]); if (Machine && IsValid(Machine)) ReadOne(Machine) } while (!Budget.Spent());` then `Budget.End()`. Unset until the cursor passes the end, then the readings. If the world changed (weak pointer differs) the pass is abandoned and the caller treats it as unreadable.
- `ReadOne`: id/class via `GetName()`; `Name` from `mDisplayName.ToString()`; recipe from `GetCurrentRecipe()` and `UFGRecipe::GetRecipeName(...).ToString()` (unset when the class is null); `bProducing`, `bPaused`; `Potential` from `GetCurrentPotential()`; location/yaw from `GetActorLocation()`/`GetActorRotation()`; circuit group via `GetPowerInfo()` -> `GetPowerCircuit()` -> `GetCircuitGroupID()` with -1 on any null; `RecipeItemReading`s from `UFGRecipe::GetIngredients/GetProducts` with `UFGItemDescriptor::GetItemName(...)`, `ItemClass->GetName()`, `GetForm(...)` mapped to `bFluid` (liquid or gas); `CycleSeconds` from `UFGRecipe::GetManufacturingDuration`; `NativeRates(...)` then fills the rate arrays; slots: for each of `GetInputInventory()`/`GetOutputInventory()`, `GetSizeLinear()` and `GetStackFromIndex(i, Stack)`; keep only stacks with `NumItems > 0`; `Amount = NumItems`, `MaxAmount = GetStackSize(class)`, both /1000 when the item is fluid. Raw `GetProductivity()` min/max go to the log counters; `GetCurrentProductionBoost() > 1` increments `boosted`.
- Time check is the loop's `Spent()`; no per-element allocation beyond the reading struct.

Driver: when `GetFactorySource() == Native` and a factory pass is due and the game is running, call `BeginFactoryRead()` once (a read still in progress means this cadence tick is skipped, with one rate-limited note); every driver tick while a read is open call `StepFactoryRead(Budget)`; when it returns readings call `Bridge.SubmitNativeFactory(...)`. An unreadable pass (no world/subsystem) leaves the factory part out with one rate-limited warning and **never** asks FRM. Ready parts reach the snapshot through the unchanged `TakeFactory(Now, FactorySeconds)`.

- [ ] **Step 1: Failing tests.** `Driver.NativeFactory` with the fake game: a read that needs 3 steps is stepped once per tick and submitted after the third; the snapshot carries `"factory":{"buildings":[...` from the fake's readings; `/getFactory` is never sent; a fake that returns `BeginFactoryRead() == false` leaves the part out, writes no `available:false`, still sends no `/getFactory`; a read still open at the next due time is not restarted. `FrmBridge.NativeFactory`: `SubmitNativeFactory` makes `TakeFactory` return the part after the worker ran (inline fake worker), and refuses (false) while a factory job is already held.
- [ ] **Step 2-4:** run (fail), implement, run; `judge-mod-test --min 161`; `check-api`; `check-provenance`.
- [ ] **Step 5:** docs row; commit.

**Reviewer checks:** no pointer into `GetAllBuildablesRef()` kept between ticks; `Cast` result and every component null-checked; fluids scaled in pure code, not in the game code; no logging of ids, names or recipes; the log line carries counts only; `Native/Factory/PROVENANCE.md` and `api-sources.md` rows complete; the Game module code is not unit-testable here (suite links Core only) and the plan says so: its correctness is carried by the parity run.

### Task 8: R3, the pure trains, stations and train-routes mapping

**Files:**
- Create: `Public/SatisDash/Native/Trains/NativeTrains.h`, `Private/Native/Trains/NativeTrains.cpp`, `Private/Native/Trains/PROVENANCE.md`; `Public/SatisDash/Native/TrainRoutes/NativeTrainRoutes.h`, `Private/Native/TrainRoutes/NativeTrainRoutes.cpp`, `Private/Native/TrainRoutes/PROVENANCE.md`
- Create tests: `NativeMapLiveTest.cpp` (`NativeMapLive.Map`, `NativeMapLive.Status`), `NativeTrainRoutesTest.cpp` (`NativeTrainRoutes.Map`, `.Rules`, `.Golden`) (+5 = 166)
- Modify: `FrmMapLive.h/.cpp` and `FrmRecords.*` only to expose the record writers the FRM parse already uses (`AppendTrainRecord`, `AppendStationRecord`), so both sources emit identical bytes

**Interfaces:**
```cpp
enum class ETrainState : uint8 { Parked, Manual, SelfDriving, Derailed };
inline constexpr const TCHAR* NativeTrainStatusText(ETrainState S)  // "Parked", "Manual", "Self-Driving", "Derailed"
struct FTrainReading   { FString Id, Name; ETrainState State; double XCm = 0, YCm = 0; TArray<FString> Stops; TArray<FString> Items; };
struct FStationReading { FString Id, Name; double XCm = 0, YCm = 0; TArray<FPlatformReading> Platforms; };
struct FPlatformReading{ FString Id; bool bLoad = false; TArray<FString> Items; };
/** One ENUM constant: which vehicle gives a train's x,y. FirstVehicle until the parity run says otherwise. */
enum class ETrainPositionSource : uint8 { FirstVehicle, LastVehicle, Actor };
inline constexpr ETrainPositionSource NativeTrainPositionSource = ETrainPositionSource::FirstVehicle;
SATISDASHCORE_API bool NativeMapLiveJson(const TArray<FTrainReading>&, const TArray<FStationReading>&, FString& OutJson);   // {"trains":[...],"stations":[...]}; false = over MapLiveMaxBytes (leave the part out, as JoinMapLive)
SATISDASHCORE_API FNativeWorldBody BuildNativeTrainRoutes(const FString& ObservedAt, const TArray<FStationReading>&, const TArray<FTrainReading>&);   // the world body
```
Rules: positions to whole metres with `Math.round` semantics and out-of-range dropped, exactly as the bridge's trains/stations arrays (reuse its helpers); trains and stations sorted by id, first 500 each (`MapLiveMaxItems`), bytes capped by `MapLiveMaxBytes`; the status text mapping is ours (owner): `Self-Driving` must equal FRM's, the other three are our words. Train routes: records `{recordType:"station"|"train",...}` sorted (stations first by id, then trains by id), `platforms` sorted by id and cut at 64, `items` distinct, sorted, cut at 64, `stops` in timetable order cut at 200, `mode` is `"load"` or `"unload"`, whole layer cut at `TRAIN_ROUTES_MAX_ITEMS` (4,000), body capped by the existing `WorldLayerMaxBytes`; the same readings in any order give identical bytes.

- [ ] **Step 1 (offline analysis, allowed: our own captures).** Read the recorded `getTrainStation` and `getTrains` captures in the dash repo's `captured-responses/` and the two Fixtures; confirm the facts used here and write them in each PROVENANCE.md: platforms are the cargo platforms (`CargoInventory[].ID`, `LoadingMode` "Loading"/"Unloading", `Inventory[].ClassName` current contents); trains list `TimeTable[].StationName` and consist cargo in `Vehicles[]` inventories; train `location` exists and per-vehicle `location` is absent in the captures (so the position source cannot be settled offline). Pin the facts that become tests.
- [ ] **Step 2: Failing tests.** `NativeMapLive.Map`: readings equal to `frm-getTrains.json` and `frm-getTrainStation.json`'s first two stations/trains, minus the far-away train, give `frm-mapLive.expected.json` **except** the status words the owner changed (the fixture's synthetic "Parked"/"Manual" match ours, so the file is unchanged; assert equality with the existing golden); 501 trains keep 500; coordinates of `1e12` cm are dropped and counted; over `MapLiveMaxBytes` returns false. `NativeMapLive.Status`: the four mappings; `"Self-Driving"` is byte-equal to the captures' `Status`. `NativeTrainRoutes.Map`: one station with two platforms (one load, one unload), one train with stops `[A, B, A]` and items; deterministic bytes in either input order; empty platforms for a station without cargo platforms. `NativeTrainRoutes.Rules`: 65 platforms keep 64; 201 stops keep 200; duplicated and unsorted item classes become sorted and distinct; a name with trailing space is kept as is (captures show "Silica/Crstal Out "); `BuildNativeTrainRoutes` output validates the field set of `TrainRoutesLayerDataSchema`. `NativeTrainRoutes.Golden`: the expected JSON is written by hand from the capture's station and train records, in the test, and the implementer cross-checks it against the capture before committing (there is no bridge golden to copy: the bridge never sent this layer).
- [ ] **Step 3-4:** implement, run, `judge-mod-test --min 166`, `check-api`, `check-provenance`. Provenance files: `Reader: Trains` (mapLive trains and stations) and `Reader: TrainRoutes`, with their header rows and the captures named above. Commit.

**Reviewer checks:** the shared record writers (no second copy of the JSON layout); status mapping owned by us and "Self-Driving" pinned; sorting makes bytes order-independent; caps equal the contract's; hand-written golden is derived from the capture, not from the code under test; clean-room statement present in both provenance files.

### Task 9: R3, the game-side reads, the `TrainRoutes` world layer, and the driver

**Files:**
- Modify: `Ports.h` (`BeginTrainsRead() -> bool`, `StepTrainsRead(FStepBudget&) -> TOptional<FTrainsSnapshot>` where `FTrainsSnapshot { TArray<FTrainReading> Trains; TArray<FStationReading> Stations; }`; the same snapshot feeds both the mapLive part and the train-routes layer, read at most once per due time), `GameReader.h/.cpp`, `FrmWorld.h/.cpp` (`enum class EWorldLayer : uint8 { Rails, ResourceNodes, TrainRoutes }`, `WorldLayerCount = 3`; `FWorldLayerSpec::Endpoint` may be null for a native-only layer, `{ TEXT("trainRoutes"), nullptr, 180.0, 600.0 }`: first read 3 minutes after Running, then every 10 minutes (ADR 0055 table: 10 min), both ours; `FWorldRead` takes a builder `TUniqueFunction<FNativeWorldBody(const FString&)>` for any native layer, S1's node constructor becomes a thin wrapper around it; behaviour and bytes of nodes unchanged), `WorldSender.h/.cpp` (a layer whose endpoint is null is native-only: scanned, never requested from FRM; `WorldSenderIsNative` generalises to `(Layer, Context)` per layer: nodes by `resourceNodesSource`, train routes by `trainsSource`), `FrmBridge.*` (`SubmitNativeWorld` generalised as above; `LargeBusy`/`NextWorldJob` already iterate layers by count), `Driver.cpp`, `DriverFakes.h`
- Tests (+2 = 168): `DriverNativeTest.cpp` `SatisDash.Core.Driver.NativeTrains` (mapLive part built from the fake's readings, no `/getTrains` or `/getTrainStation` request, an unreadable read leaves the part out), `WorldSenderTest.cpp` `SatisDash.Core.WorldSender.TrainRoutes` (with `trainsSource` native the layer scans, submits and posts to `/agent/v1/world/trainRoutes` once, does not repost an unchanged body, never sends an FRM request, and with `trainsSource` bridge the layer is never scheduled)
- Modify: provenance Code lines, `api-sources.md`, `docs/native-readers.md`

Game side (headers in the lookups table, rows added): `BeginTrainsRead` gets `AFGRailroadSubsystem::Get(World)`, copies `GetAllTrains` and `GetAllTrainStations` into `TArray<TWeakObjectPtr<>>` (dozens), resets the budget; `StepTrainsRead` processes train by train then station by station under `do/while (!Budget.Spent())`: per train `GetName()`, `GetTrainName().ToString()`, `GetTrainStatus()` mapped to `ETrainState`, the position per `NativeTrainPositionSource` (default `GetFirstVehicle()` then `GetVehicleLocation()`), timetable stops (`HasTimeTable()`, `GetTimeTable()->GetStops(...)`, each stop's `Station` -> `GetStationName().ToString()`, null stations skipped), cargo classes from the consist's freight wagons' `GetFreightInventory()` (`Cast<AFGFreightWagon>` over `mConsistData.Vehicles`); per station the building via `GetStation()` (`GetName()` and `GetActorLocation()`), and the platform chain from `GetStationOutputConnection()` via `GetConnectedTo()`/`GetPlatformOwner()`/`GetConnectionInOppositeDirection()`, keeping `AFGBuildableTrainPlatformCargo` platforms (`GetName()`, `GetIsInLoadMode()`, distinct item classes of `GetInventory()` stacks); the walk is bounded (at most 256 hops, visited set) so a cyclic or broken chain ends. Log: `Trains read (game thread only): %d trains, %d stations in %d steps, longest step %.2f ms, total %.2f ms` and `Train routes read (game thread only): %d stations, %d trains in %d steps, longest step %.2f ms, total %.2f ms` (the second line is written when the world sender's scan of the layer ends).

Driver and sender: with `trainsSource` native, the mapLive part is built at the factory cadence from a `StepTrainsRead` pass and joined by `NativeMapLiveJson` (over-cap leaves the part out, one warning); the train-routes layer starts its own pass at its schedule, in the `Scanning` phase S1 added for nodes.

- [ ] **Step 1-4:** failing tests, implement, run, `judge-mod-test --min 168`, `check-api`, `check-provenance`. **Step 5:** commit.

**Reviewer checks:** the layer with a null endpoint can never reach `FFrmQueue`; resource-node bytes and tests unchanged; the platform walk is bounded and weak-pointer safe; names never logged; stops use station names exactly (no trimming); the third layer does not change rails' schedule or hashes.

### Task 10: The parity judge for S2 (`native-parity`)

**Files:**
- Create: `harness/src/frm-s2.ts`, `harness/test/frm-s2.test.ts`
- Modify: `harness/src/recorder.ts` + test (`keepSnapshotParts?: ("factory"|"mapLive")[]` keeps those parts of each accepted mod snapshot in memory on `RecordedCall.parts`, names already hashed; `world.data` kept for `trainRoutes` through the existing `keepWorldData`), `runs/native-parity/pass.ts` + `harness/test/native-parity-pass.test.ts`, `harness/src/core-runs/native-parity.ts`, `harness/src/core-runs/index.ts` (the run uses `worldKey: "s2"` and `NATIVE_SOURCES_S2`), `.github/workflows/core-run.yml` (no new options; `test/workflows.test.ts` stays green), `docs/runner.md`

**Interfaces:**
- `frm-s2.ts` (FRM's HTTP output as the oracle; shapes come from the bridge's own fixtures and captures): `mapFrmFactory(body): CanonMachine[]` (the FRM-to-contract rules of `FrmFactory.cpp`: unconfigured to null recipe, `isBackedUp`, `inputsFull`, metres, yaw, clock), `mapFrmTrains(body)`, `mapFrmStations(body)`, `mapFrmTrainRoutes(stationBody, trainsBody)`, `canon*` sort/hash helpers; names pass through `sessionHash`. Tests: `mapFrmFactory(load("frm-getFactory-running.json"))` equals the bridge's `frm-getFactory-running.expected.json` (and the norecipe pair); `mapFrmTrains`/`mapFrmStations` joined equal `frm-mapLive.expected.json` after hashing; `mapFrmTrainRoutes` of the fixtures validates against `TrainRoutesLayerDataSchema` and equals a hand-written expectation.
- `pass.ts` exports (all new): `judgeReaders(c): ReaderVerdicts` extended to `{ power, players, resourceNodes, factory, rates, trains, stations, trainRoutes }`. S1's three judges are called unchanged; their S1 tests stay untouched and green.

**What the run judges** (Decision 3 rows, owner decisions of 2026-10-26; each reader separately; a failing reader does not change another's verdict; the run is red if any is; `NOT EXERCISED` fields are reported, not failed). Oracle: FRM's own endpoints, read by the harness as a black box in the same minutes (`/getFactory` every 5 s, `/getTrains` every 2 s, `/getTrainStation` every 30 s, `/getTrainStation`+`/getTrains` pair for routes right after each mod routes post). A mod part carries no read time, so each accepted snapshot's part is compared with the FRM samples whose start falls in `[observedAt - 40 s, observedAt + 5 s]` (the window covers the 30 s part age and one status pass); that window is the "neighbours".

| Reader | Pass condition |
| --- | --- |
| `factory` (R2a) | at least 40 compared rounds (30 min / 30 s cadence x 0.8 + slack, the new-run floor, stated in the code); **id sets equal** to the union of FRM's ids in the window (differences listed with `className`); static fields (`name`, `className`, `recipe`, `circuitGroupId`, location within 1 m, `clockSpeedPercent` within 0.01) equal for every machine in every round; dynamic flags (`isProducing`, `isPaused`, `isBackedUp`, `inputsFull`) equal to a value FRM showed in the window for at least 99% of machine-rounds (owner question 1 below), mismatches listed (first 10) |
| `rates` (R2b) | `maxPerMinute` within 0.1% or 0.001 absolute of FRM's, every item of every machine, every round; `currentPerMinute` and `percent`: per item-class factory total within 3% of FRM's range over the window (`lo - 3%`..`hi + 3%`, the power bracket rule) and per building within 5% for at least 95% of buildings; reasons print the median mod/FRM ratio, the raw productivity range from the mod's `Factory read` line, and fail with an explicit "scale" reason when a raw value above 1.0001 was logged. **Fallback (owner):** if `maxPerMinute` matches but `current`/`percent` miss, the verdict is `fail` with the text `R2b: max matches, averaged current does not; fallback = set DefaultRatesMode to Instantaneous (Task 6) and the owner rules on the tolerance`, and the factory reader (R2a) is judged without the rate columns so one does not hide the other |
| `trains` (R3) | train id sets equal; `name` hash equal; `status` equal for self-driving trains (exactly "Self-Driving"); parked, manual, derailed mismatches reported as `NOT EXERCISED / reported`, not failed; position: a train whose two FRM neighbours are within 1 m of each other must be within 1 m; a moving train must be within `dist(before, after) + 5 m` of the nearer neighbour (this measured distance replaces the ADR's "speed x pair offset", which needs FRM's speed unit; owner question 2); at least 40 compared rounds; if every position misses by about one train length the reason says "position source: try NativeTrainPositionSource LastVehicle" |
| `stations` (R3) | station id sets equal, `name` hash equal, positions within 1 m, same round floor |
| `trainRoutes` | the mod's posted layer (valid against `TrainRoutesLayerDataSchema`) equals, as a sorted canonical list, the FRM-derived layer taken on at least one side of the post within 120 s (every difference listed); at least one accepted post in the window; fields never exercised (a station without platforms) reported |

- Evidence size: `native-parity.json` holds the distinct static lists once (by hash) and per round compact dynamic rows; a test asserts the capture for a 340-machine fixture world over 60 rounds stays under 8 MB.
- Both new readers are shown native by the mod's log: `factoryLogProblems`/`trainsLogProblems` (Task 4) plus a completed `Factory read`/`Trains read`/`Train routes read` line each. With a reader on the bridge the values would pass trivially, so these fail closed.

- [ ] **Step 1: Failing tests** for `frm-s2.ts` (golden equality as above), recorder (`keepSnapshotParts` keeps the parts, never writes them to `recorder.json`), and each judge: passes on equal data; fails on one missing machine id; fails on a `maxPerMinute` off by 0.2%; passes `current` at +2.9% and fails at +3.2%; the fallback text appears when only current misses; parked-train mismatch is reported and passes; "Self-Driving" mismatch fails; position rules (stationary, moving, long jump); `NOT EXERCISED` lines for absent Somersloop and absent parked trains; fewer than the round floor fails. **Step 2:** run, fail. **Step 3:** implement; keep `drive` in `native-parity.ts` as S1's, adding the three FRM samplers and `keepSnapshotParts: ["factory","mapLive"]`, `keepWorldData: ["resourceNodes","trainRoutes"]`. The window, the enrol timing and the 30-minute length are S1's; only the machine count of `Test2` changes the byte size (recorder `MAX_RECORDED` is 2 MB per snapshot: a snapshot over it counts as `unrecorded` and fails the run, which is correct and loud; if `Test2`'s factory part exceeds 2 MB, raise the recorder's cap in a new constant for this run only, with a test). **Step 4:** `npx vitest run --reporter=dot`, `npm run typecheck`. **Step 5:** commit.

**Reviewer checks:** S1's judges and tests byte-for-byte unchanged; thresholds are the ADR's/owner's (0.1%, 3%, 5%/95%, 1 m, exact ids/strings), anything new (round floor, 99%, 40 s window, the measured moving-train distance) is named in the code comment as new and in the owner questions; evidence has hashed names only; no `result.json` hand-edit; FRM is read only through its HTTP endpoints.

### Task 11: The tick-cost run with S2 on, the docs, the follow-up list

**Files:**
- Modify: `harness/src/core-runs/tick-cost-native.ts` (`modConfig: NATIVE_SOURCES_S2`, `worldKey: "s2"`), `runs/tick-cost-native/pass.ts` (requires `factorySource`/`trainsSource` native in `mod-config.json`, a `Factory read`, `Trains read` and `Train routes read` line each inside the measured window, and the Task 2 ceiling; the S1 conditions are kept), `harness/test/tick-cost-native-pass.test.ts`, `docs/native-readers.md` (status table rows for Factory, Rates, Trains, TrainRoutes, `bridge` until their switch PR; the step-ceiling rule; `Test2` note), `docs/runner.md`
- Test: S2-shaped evidence passes when everything is native and each longest step is 1.9 ms or less; fails for 2.1 ms on any reader, for a missing `Train routes read` line, for `factorySource` bridge in the captured config; S1-shaped evidence (no S2 keys) still passes as before.

- [ ] Steps: failing tests, implement, `npx vitest run --reporter=dot`, `npm run typecheck`, `check-api`, `check-provenance`, final `judge-mod-test --min 168`, commit.

**Reviewer checks:** `Base.evaluate` limits (tick median 95%, memory 100/25 MB, S12) imported and unchanged; the ceiling is `> 2.0 ms`; the second `tick-cost-native` run is the owner's single dispatch; `docs/native-readers.md` states honestly which readers are still `bridge`.

---

## End of sprint: the two runs and the follow-ups

After both PRs merge (or from the S2-B branch), the owner dispatches, in this order and once each: `mod-test` on the branch (floor 168 expected), then `native-parity` (30-minute window, about 45 minutes plus the build, backend host, `frmBaseUrl` and `s2World` in the runner config) and `tick-cost-native` (baseline boot then mod boot, on `Test2`). Nothing else is run.

**`readers.json` must show, per reader** (`pass: true`, reasons listing what was compared and what was NOT EXERCISED):
- `power`, `players`, `resourceNodes` (now including `className`): as S1, on `Test2`.
- `factory`: id sets equal, static fields exact, dynamic flags matching, N compared rounds.
- `rates`: `maxPerMinute` matching, current/percent within the stated tolerances, productivity range logged. **Fallback if not:** R2b ships with max plus the instantaneous flag (`DefaultRatesMode = Instantaneous`, one constant, tested in Task 6), `rates` stays a recorded failure, and the owner rules on the tolerance (ADR risk row).
- `trains`, `stations`: id sets equal, positions within the rules, "Self-Driving" exact; other statuses reported.
- `trainRoutes`: layer equal to the FRM-derived layer on one side of a post, or every difference listed.
`result.json` is red if any reader is; `tick-cost-native` `result.json` must pass with each logged longest step at or under 2 ms.

**Follow-ups (not tasks):**
- One small switch PR per passing reader: `DefaultFactorySource` (R2a with R2b's mode decision), `DefaultTrainsSource` (trains and stations together; the train-routes layer follows `trainsSource` and has no bridge path, so it turns on with that PR), each with its `mod-claims.md` row. A reader that missed stays `bridge`; the rest switch. (`DefaultPlayersSource`/`DefaultResourceNodesSource` already flipped in PRs #55 and #56.)
- If R2b missed: the owner's ruling PR flips `DefaultRatesMode` and records the tolerance decision.
- One dash evidence PR: `raw-sources/mod-native-readers/` results and bundles, `wiki/mod-claims.md` rows, the S2 survey note's folder.
- Raise the `mod-test.yml` floor to 168.
- Sprint S3 (rails R5, conveyors R6, then the removal release R7) starts from this plan's layout, switches and runs; the `trainRoutes` third world layer and the generalised native `FWorldRead` builder are its base for the next native layers.

## Self-review

**Spec coverage.** R2a (identity, recipe, state flags, location, clock, circuit group, inventory fullness): Tasks 5 and 7. R2b rates, tolerance and fallback: Tasks 6, 10, end-of-sprint. R3 trains and stations: Tasks 8, 9, 10. Train-routes layer: Tasks 8 (pure), 9 (layer), 10 (judge). `className` and its parity: Task 3 (and 10's canon form). 2 ms ceiling and its application to the sliced factory and train reads: Tasks 2, 7, 9, 11. `Test2` staging and checklist: Task 1. Train status wording: Task 8, judge in Task 10. One combined parity run and one tick-cost run, per-reader verdicts, switches default `bridge`, two PRs: structure and end of sprint. Survey open items 1-10: table above. Per-task reviewer checks and `api-sources.md`/PROVENANCE.md rows: each task.

**Placeholder scan.** The only fill-ins are `Author:`/`Date:` in provenance files (`check-provenance` rejects a malformed date). The hand-written train-routes golden (Task 8) is deliberately derived by the implementer from the recorded capture. The mod-side Game-module code is described by the exact symbols and sequence; its lines are written by the implementer against the open headers.

**Type consistency.** `FFactoryReading`, `FRateReading`, `FSlotReading` (Task 5) are used in Tasks 6 and 7; `ERatesMode`, `DefaultRatesMode`, `ProductivityFullScale` (Task 6) in Tasks 7 and 10; `FStepBudget` (Task 2) in Tasks 7 and 9; `FTrainReading`, `FStationReading`, `FPlatformReading`, `NativeTrainPositionSource`, `NativeMapLiveJson`, `BuildNativeTrainRoutes` (Task 8) in Tasks 9 and 10; `ESource`, `GetFactorySource()`, `GetTrainsSource()`, `NATIVE_SOURCES_S2` (Task 4) in Tasks 7, 9, 10, 11; log-line names in Task 2's `STEP_LINE_NAMES` equal the strings in Tasks 7 and 9.

**Review Focus coverage.** Each line names its test (above); the Game-module paths are covered by the parity and tick-cost runs, as in S1.

## Open questions for the owner

1. **Dynamic flags on a running factory.** ADR Decision 3 says booleans are exact "on an idle save", but `Test2` runs, so `isProducing`/`isBackedUp`/`inputsFull` can flip between FRM's read and the mod's. Task 10 requires each flag to equal a value FRM showed in the 45-second window for at least 99% of machine-rounds, static fields exact. Accept 99%, or want exact and a frozen save?
2. **Moving-train distance.** Task 10 replaces "speed x pair offset + 5 m" with "the distance FRM's own two neighbours moved + 5 m", because FRM's `ForwardSpeed` unit is not stated in any capture. Accept?
3. **`--min` floor** in `mod-test.yml`: 149 today; proposed 168 after S2-B (a workflow-file limit, the owner's call, as in S1).
