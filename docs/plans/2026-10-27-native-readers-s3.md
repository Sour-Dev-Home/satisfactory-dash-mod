# Native readers, Sprint S3 (rail tracks R5, conveyors R6: belts, lifts, pipes) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The mod reads the rail tracks (R5) and the conveyors (belts, lifts, pipes; R6) from the game itself and posts the `rails` and `conveyors` world layers; one combined 30-minute `native-parity` run plus one `tick-cost-native` run, both on the owner's `Test3` save, judge each reader separately, with the 2 ms ceiling on every native game-thread step. Each reader has a `bridge`/`native` switch that stays `bridge` until its own switch PR after the runs pass. R7 (deleting the bridge, the FRM keys, the FRM fixtures and the dependency) is not in this plan.

**Architecture:**
- Same pattern as S1/S2. Pure logic (sampling plan, mapping, the end-kind table, capacity, caps, sorting, the exact JSON) lives in `SatisDashCore` under `Native/<Reader>/`; game-thread code is small, in `GameReader.cpp`, and **time-sliced** by `FStepBudget` (about 1 ms a step).
- **One shared logistics pass** (survey ruling 1): when either layer is due, one sliced pass over `GetAllBuildablesRef()` collects weak pointers to the tracks, belts, lifts and pipelines wanted by the native layers, then reads them in later slices; the result (`FLogisticsSnapshot`) is split into one world body per layer, built on the worker through `FNativeWorldBuilder` and posted by the existing `FWorldSender` machinery. A layer whose source is `bridge` is not in the pass.
- `rails` keeps its FRM path (`getTrainRails`) while `railsSource` is `bridge`. `conveyors` has **no bridge path** (the bridge never sent it; Plan 3C was cancelled): `conveyorsSource = bridge` means "the layer is not sent", exactly today's behaviour; `native` sends it (Ruling M1).
- Harness: `sprint3World` stages `Test3`; `native-parity` gains the rails and conveyors verdicts in `readers.json`; `tick-cost-native` gains the `Logistics read` step line; a one-off `s3-census` run reads the tier numbers only the test server can give.

**Tech Stack:** Unreal Engine 5.6.1-CSS, SML 3.12.0 (`harness/toolchain.json`), C++20, UE automation tests (`SatisDashCoreTests`), TypeScript harness (Node, zod 4, Vitest 5).

**Spec:** ADR-0058 (satisfactory-dash, `docs-vault/wiki/decisions/0058-native-readers-remove-frm.md`: reader rows 9 and 10, Decisions 3 and 4, the sprint table), the S3 header survey (`mod-native-readers-s3-headers-2026-10-26.md`, dash `docs-vault/raw-sources/`, dash PR #715: its 12 open questions and 23 [NEEDS VERIFICATION] items), the contract (`harness/contract/src/map.ts`: `RailsLayerDataSchema` :72, `ConveyorEdgeSchema` :144, `ConveyorsLayerDataSchema` :164), the S2 plan `docs/plans/2026-10-26-native-readers-s2.md` (the template; S2 shipped as PRs #58-#60) and the owner's decisions of 2026-10-26 (below).

## Owner decisions (2026-10-26), carried as rules

1. **Scope:** S3 = R5 rails and R6 conveyors (belts, lifts, pipes), each with a `bridge`/`native` switch that defaults to `bridge` until its own switch PR after the runs pass (as S1/S2). **R7 is NOT in this plan:** it is a separate PR later that merges only on the owner's explicit word. It is named as the next step only (see "After S3").
2. **Rails pass rule:** the set of track ids equals FRM's (`getTrainRails`) in each compared layer, and for every track every point of the mod's polyline lies within 1 m of FRM's polyline for that track, and every FRM point within 1 m of the mod's (a symmetric 1 m distance, point-to-polyline).
3. **Conveyors pass rule:** the same edges as FRM (`getBelts`, `getLifts`, `getPipes`): `id`, `kind`, `from`, `to`, `fromKind`, `toKind`, `tier` and `capacityPerMinute` all exact; `at` within 1 m. Open ends and anything filtered out (hypertubes) are reported, not failed.
4. **The survey's 12 open questions:** every recommended answer is taken (rulings S1 to S12 below).
5. **NOT EXERCISED** cases are reported and never fail (standing owner rule). Readers are judged separately; a reader passes or fails on its own.
6. **One** combined `native-parity` run and **one** `tick-cost-native` run for the sprint, on the S3 test save `Test3` (from `Test2`, plus belts of two tiers, a splitter, a merger, a lift, pipes with a junction or pump, belts into and out of the two paired drone ports labelled `FUEL 5` and `TestLand1`, a battery belt into one port, and a train that may have crashed). A byte copy is `sprint3-test3.sav` on the runner. The runner config key is `sprint3World` (`s3World` is the old S3 power spike's), with a preflight like `s2World`'s; the owner edits the runner config (outside the repository) to add it.
7. The 2 ms per-step ceiling on game-thread reads and the time-sliced `FStepBudget` pattern apply to the new readers; reads over thousands of actors are sliced across ticks.
8. The `mod-test.yml` floor rises to the new test count when the plan's tests land (owner set this pattern in S2).
9. Items the survey says only the test server can settle (belt/lift `GetSpeed()` units, tier names, drone-port ids) become either a parity-run check or the one-off pre-check task (Task 10) that the owner approves. Test-server runs need the owner's OK and go through runner jobs (the owner's Windows account cannot load a save: the harness `savesConflict` guard). **No local test-server step is planned.**

## Global Constraints

Everything in the S1 and S2 plans' Global Constraints still holds (pure Core, test names `SatisDash.Core.<Area>.<Case>`, unity-build prefixes for file-local helpers, case-sensitive protocol strings, no `check()` on outside data, logs only to `LogSatisDash` at `Display`/`Warning` and rate-limited, `check-api` at `0 missing, 0 bad rows`, `check-provenance` clean). Added or restated:

- **Clean room (CLAUDE.md rule 7, `docs/clean-room.md`).** Nobody opens FicsitRemoteMonitoring's source in any form or searches the web for it. FRM is a black box over HTTP: our recorded outputs (`Fixtures/`, the dash `captured-responses/`) and the live test server's endpoints. If FRM source is seen by accident: stop, tell the owner, record it in that reader's `PROVENANCE.md`. Each new reader (`Native/Rails/`, `Native/Conveyors/`) gets its own `PROVENANCE.md` with the line `FRM source was not opened`; each PR description carries the checkbox "FRM source was not opened".
- **No API from memory (rule 1).** Every game/SML/UE symbol comes from the "Lookups made" table below or from a header the task opens, and gets its `api-sources.md` row (`path:line`) in the same task. **The header wins** over the survey: if they disagree, follow the header and record both in the row's Source. The pinned checkout is the SML starter project at `harness/toolchain.json`'s version; paths below are relative to `Source/FactoryGame/Public` unless marked `Engine/`.
- **No weakened judges (rule 3).** No existing `runs/*/pass.ts` or `spikes/*/pass.ts` condition changes. New conditions are new code (a new file `runs/native-parity/pass-s3.ts`; S1's and S2's judges and tests stay byte-for-byte). A failed spike or reader stops the work and is reported with its evidence bundle. The `sprint3` thresholds are the owner's (1 m, exact fields) and are named in the code.
- **Evidence is machine-written; loopback and the test server only** (rules 4, 5). Evidence carries ids, class names, tier display names and numbers. The drone-port oracle keeps **ids only**, never a port's player-typed label. Nobody hand-edits a `result.json` or a capture.
- **Contract and goldens.** The contract (`harness/contract/`) does not move; no existing golden moves. New goldens are written from our recorded FRM output and hand-checked against the capture.
- **Memory and cost (owner: the mod stays light).** The pass holds one weak pointer per track/belt/lift/pipeline (a few thousand), then plain readings (a belt about 0.15 KB, a track about 16 bytes a sampled point), moved into the worker's builder and freed after the body exists; never an FRM body. Logs carry counts and times only (plus game class names and tier display names in the one-off census lines).
- **The step ceiling.** No single native game-thread step may take over 2 ms (`MaxNativeStepMs = 2.0`); the pass stops at `NativeStepBudgetSeconds = 0.001` after the element that crosses it. The shared pass logs one `Logistics read (game thread only): ...` line; `tick-cost-native` fails if its longest step is over 2.0 ms or the line is missing while a logistics layer is native.
- **Test counts.** `mod-test.yml` has `--min 168` and 168 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` macros today. Running ledger: T3 +1 = 169, T4 +4 = 173, T5 +4 = 177, T7 +5 = 182, T8 +2 = 184. Each task's `judge-mod-test --min N` uses its ledger number; the workflow floor is raised in the task whose tests land (T3: 169, T5: 177, T8: 184; also `harness/test/workflows.test.ts`, which pins the number).
- **Runs.** Executors never start runs. The owner dispatches `mod-test` on each PR branch, then (once Tasks 1-15 are merged or on the S3-D branch) the one-off `s3-census` (Task 10, owner-approved), then **one** `native-parity` and **one** `tick-cost-native`, on `Test3`. No other core run is added or repeated. Mod-test runs are batched: one per PR, plus one after any fix.
- Commits end with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. No personal data, no local paths in anything committed (including this plan: the runner config and the save are named by key and file name only).

## Lookups made for this plan (header wins)

From the S3 survey. Rows marked **R** had their FactoryGame header lines re-checked in the pinned checkout by the planner and matched the survey; the Engine headers (spline component, actor and scene components) and the buildable subsystem were not re-opened. Each task still opens the header it uses before writing the call and adds the row.

| Need | Header, `path:line` | Notes |
| --- | --- | --- |
| Buildable list | `FGBuildableSubsystem.h:246` class, `:272` `Get(UWorld*)`, `:360` `GetAllBuildablesRef()` (can be invalidated by add/remove), `:363` `GetBuildableCount(TSubclassOf<AFGBuildable>)` | S2 facts. Re-fetch the reference every element; keep only an index across ticks. `GetTypedBuildable` `:371` is not sliceable: not used. |
| Track class | **R** `Buildables/FGBuildableRailroadTrack.h:99` `UCLASS( Abstract )`, `:100` class | Abstract: filter with `Cast<AFGBuildableRailroadTrack>`; `GetBuildableCount(...StaticClass())` is advisory only (survey line 27). |
| Track sampling | **R** `FGBuildableRailroadTrack.h:150` `GetLength()` (float, cm), `:156` `GetIsOwnedByPlatform()`, `:171` `GetWorldLocationAndDirectionAtDistance(float, FVector&, FVector&) const`, `:247` `GetSplineComponent()`; `Engine/Classes/Components/SplineComponent.h:284` class, `:763` `GetLocationAtDistanceAlongSpline(float, ESplineCoordinateSpace::Type)`, `:731` `GetSplineLength()`; `ESplineCoordinateSpace` `:35-41` (Local, World) | Default sampler is the spline component (survey table); the game's own sampler (`:171`) is the one-constant fallback `NativeRailSampler` (Ruling M2). |
| Conveyor classes | **R** `Buildables/FGBuildableConveyorBase.h:97` base, `:160` `GetSpeed()` (no unit in the header), `:163-164` `GetConnection0/1()`, `:327` `ITEM_SPACING = 120.0f`; `FGBuildableConveyorBelt.h:22`; `FGBuildableConveyorLift.h:72` | One `Cast<AFGBuildableConveyorBase>` gives belt and lift; the subclass tells which (`Cast<AFGBuildableConveyorBelt>`). |
| Pipe classes | **R** `Buildables/FGBuildablePipeBase.h:22`, `:68-69` `GetConnection0/1()` (-> `UFGPipeConnectionComponentBase*`); **R** `FGBuildablePipeline.h:32` class, `:136` `GetFlowLimit()` (m3/s); `FGBuildablePipeHyper.h:16` (also a `AFGBuildablePipeBase`) | Cast to `AFGBuildablePipeline` only: hypertubes are not pipes (S10). |
| Connections | **R** `FGFactoryConnectionComponent.h:96` `GetConnection()`, `:111` `IsConnected()`, `:141` `GetConnectorLocation(bool)`, `:180` `GetOuterBuildable()`; `FGPipeConnectionComponent.h:69` `GetConnection()`, `:94` `IsConnected()`; `Engine/Classes/Components/ActorComponent.h:491` `GetOwner()`; `Engine/Classes/Components/SceneComponent.h:1031` `GetComponentLocation()` | Belt/lift end: `Conn->GetConnection()->GetOuterBuildable()->GetName()`. Pipe end: `Conn->GetConnection()->GetOwner()->GetName()`. A null anywhere, or `IsConnected()` false, is an open end. |
| Tier name | **R** `Buildables/FGBuildable.h:824` `FText mDisplayName`, `public:` block from `:813`; `:467` `SetBuildableDisplayName` | Read from the actor (S2 did the same for machines). |
| Identity | `UObject::GetName()` `Engine/Runtime/CoreUObject/Public/UObject/UObjectBaseUtility.h:429`; `GetClass()->GetName()` | S2 facts. |
| Kind cross-check casts (S4) | `FGBuildableAttachmentSplitter.h:14`, `FGBuildableSplitterSmart.h:64`, `FGBuildableAttachmentMerger.h:14`, `FGBuildableMergerPriority.h:14`, `FGBuildablePipelineJunction.h:14`, `FGBuildablePipelinePump.h:36`, `FGBuildableStorage.h:13`, `FGBuildablePipeReservoir.h:43`, `FGBuildableDroneStation.h:37`, `FGBuildableTrainPlatformCargo.h:18`, `FGBuildableDockingStation.h:63`, `FGBuildableResourceExtractorBase.h:65`, `FGBuildableGenerator.h:60`, `FGBuildableResourceSink.h:14`, `FGBuildableSpaceElevator.h:51`, `FGBuildableManufacturer.h:85` | Survey table; Task 9 re-opens each and adds its row. Used only for a counts-only disagreement log, never for output. |
| Time | `FPlatformTime::Seconds` | Step budget; row exists. |

## Rulings

### The survey's 12 open questions (owner: take every recommended answer)

| # | Question | Ruling (the survey's recommendation) and its reason |
| --- | --- | --- |
| S1 | Shared cursor pass for rails and conveyors every 10 min? | **Yes.** One sliced pass over `GetAllBuildablesRef()` feeds both layers: the scan cost is linear in all buildables and both layers share the 10-minute cadence (Task 5; conveyors join in Task 8). |
| S2 | Rail sampling step and clamp? | **1 m by distance along the spline, collapse consecutive equal whole-metre points, clamp to 1,000 points per segment by raising the step for a longer segment; no simplification in the mod.** A one-metre step keeps curves round while the contract only needs metres (Task 4: `RailSampleStepCm = 100`, `RailSampleIntervals`). |
| S3 | Platform-owned tracks? | **Included**: they are tracks on the map; `Cast<AFGBuildableRailroadTrack>` takes them. The parity run compares the native count with FRM's on `Test3` and any gap is listed (the id-set check). |
| S4 | Where is the end kind decided? | **A class-name table in `SatisDashCore`** (the adapter's `classifyConveyorEnd` ported with its test vectors, Task 7), plus a counts-only check against `Cast`s on the test save (Task 9) that is reported, not failed. |
| S5 | Belt/lift capacity? | **Read the eight speeds on the test server first; if `GetSpeed() / 2` gives 60/120/270/480 use it (it follows future tiers), else the class table.** Both exist behind `NativeConveyorCapacityMode` (default `SpeedHalf`); the census run (Task 10) settles it before the parity run. |
| S6 | Pipes carry a capacity? | **Yes, `GetFlowLimit() * 60`** (m3/min); the contract allows it (`map.ts` :141) and the adapter still decides when to trust it. Mk.1 5 and Mk.2 10 are checked in the census. |
| S7 | `tier` from `mDisplayName.ToString()`? | **Yes**, accepted when it equals FRM's `Name`; if it differs, prefer the game's string and record the difference. The parity judge requires exact equality (owner's rule), so a difference is a visible failure for the owner to rule on. |
| S8 | `at` source? | **The end-0 connector location** (`GetConnectorLocation()`), parity tolerance one metre (not byte equality). |
| S9 | Drone-port id? | **The building's `GetName()`.** The parity run checks that every belt end of drone-port class appears in FRM's `getDroneStation` ids and that the direction is `to` = port receives, `from` = port delivers (needs no extra field). |
| S10 | Hypertubes in the layer? | **No.** Filter on `AFGBuildablePipeline` only; hypertubes seen in FRM's `getPipes` are counted as filtered in the parity evidence, never failed. |
| S11 | A connection set but whose outer buildable is null? | **An open end, plus a counter** in the read summary (`ownerless ends`). |
| S12 | Sort rails by id? | **Yes**, in the native reader (the layer hash is order-sensitive); this differs from the bridge's order, so the layer's content hash changes once when the source switches. |

### The survey's 23 [NEEDS VERIFICATION] items (survey line numbers) and where each is settled

| Survey line | Item | Settled by |
| --- | --- | --- |
| 27 | `GetBuildableCount(Track::StaticClass())` includes subclasses | Logged beside the found count as advisory only (Task 6); never used for output. |
| 30 | Platform-owned tracks in the buildable list, listed by FRM | Parity id-set check (Task 12); S3. |
| 47 | Game sampler (`:171`) equals the spline component | `NativeRailSampler` constant (Task 4/6); parity decides, fallback is one line. |
| 53, 141 | Track and conveyor ids equal `GetName()` | Parity id-set joins (Task 12). A mismatch is a visible fail, never matched silently. |
| 61 | Longest single track | `RailSampleIntervals` clamps at 999 intervals; the judge reports tracks over 999 m as NOT EXERCISED when absent. |
| 63 | Measured cost | `tick-cost-native` (Task 14) against the 2 ms ceiling. |
| 79 | FRM's `getPipes` lists hypertubes | Parity: FRM-only pipe records whose `ClassName` matches `/hyper/i` are reported as filtered (Task 12); the census lists the distinct FRM classes. |
| 84 | No lightweight conveyor variants | Parity id-set check; a missing belt shows as FRM-only. |
| 95 | Connection set, owner null | Open end plus counter (S11). |
| 120, 178 | `at` source matches FRM's `location0` | Parity: `at` within 1 m (Task 12); flip is a one-line change in Task 9 (spline start vs connector). |
| 127, 176 | Belt/lift `GetSpeed()` unit | Census run (Task 10) then parity capacity exactness; `NativeConveyorCapacityMode` fallback (S5). |
| 129, 176 | Pipe flow limit 5 and 10 | Census (Task 10), parity capacity exactness. |
| 132, 177 | Tier name equals FRM `Name` | Parity `tier` exactness; census prints the distinct display names. |
| 142 | Rebuilt belts get new native ids | Not judged (the harness builds nothing); noted in `docs/native-readers.md`. |
| 150 | Drone-port id form | Parity drone check (Task 12) and census (Task 10). |
| 154 | Drone-port connectors and directions | Parity reports edges into and out of ports; NOT EXERCISED when absent; the census counts both. |
| 164, 166, 172 | Mapping-table repeats (id, enumeration, id) | As the rows above. |

### Rulings made by this plan beyond the owner's decisions

- **M1. `conveyorsSource` has the meaning "the layer is sent or not".** `bridge` (default) = not sent, as today; `native` = sent. There is no FRM fallback to keep. The switch PR flips `DefaultConveyorsSource`.
- **M2. `NativeRailSampler`** (`SplineComponent` default, `GameTrackSampler` fallback) is a constant like S2's `DefaultRatesMode`.
- **M3. Geometry is compared against FRM's raw points.** For rails and for `at`, FRM's `x`/`y` (centimetres) become metres unrounded and are compared with the mod's posted whole-metre values. Comparing against FRM's own rounded values would allow 1.41 m of rounding alone, which the owner's 1 m cannot guarantee. FRM tracks that the contract cannot hold (fewer than 2 distinct whole-metre points) are not in either set and are reported.
- **M4. Capacity is compared after rounding both sides to 4 decimals** (float noise: a `float` times 60 is not exact in `double`); this is what "exact" means for a number. The native value is rounded the same way before it is written.
- **M5. A finished pass that saw zero buildables is unreadable (nothing posted, retried soon, FRM never asked)**; a pass with buildables and zero tracks or zero conveyors posts a real empty layer. A world that is still loading must not wipe the backend's good copy.
- **M6. A shared pass pulls the other native layer forward.** When one logistics layer is due, a native layer that is waiting joins the pass at once (at most about 100 s early on the first read). Layers are separately switched; a layer not native is never in the pass.
- **M7. Native first read is 480 s** (the resource-node rule) for rails and conveyors: rails' bridge first read stays 120 s. `FWorldLayerSpec::NativeFirstSeconds` (0 = same as `FirstSeconds`) applies only while the layer is native. This keeps the first post inside `tick-cost-native`'s measured span (3 to 13 minutes) and `native-parity`'s window, as S2 arranged for train routes.
- **M8. Edge rules follow the adapter** (`conveyorsMapper.ts`): `id` or an end id over 128 characters, an empty `tier`, or an `at` outside +/-1,000,000 m drops the edge (counted); `tier` is cut at 64; edges are sorted by `id` (code-unit order), a duplicate `id` keeps the first; at most 50,000 edges; a body over 2,000,000 bytes is not posted (the existing `WorldLayerMaxBytes` path), with the usual one warning.
- **M9. Kinds are decided from the end's id with the adapter's table, both in C++ and in the harness oracle**, so a kind disagreement can only come from an id disagreement. The cast-based check is a second opinion, counts only.
- **M10. The conveyors judge's reading of "open ends reported, not failed":** an edge with an open end on both sides is compared like any other (null equals null) and counted; if the mod and FRM disagree on whether an end is open, that is a `from`/`to` difference and fails (see Open questions, 2).

## Review Focus

- **A world with no tracks, no belts, or still loading:** zero buildables is unreadable (nothing posted, FRM never asked, retry soon); buildables but no tracks gives `rails: []`; no conveyors gives `conveyors: []`. Pinned in `WorldSender.Logistics` (abandoned read), `NativeRails.Rules`, `NativeConveyors.Rules`.
- **Buildables dismantled or rebuilt while the pass is open:** weak pointers checked every element, duplicates dropped on the worker, no pointer into `GetAllBuildablesRef()` kept across ticks. Pinned in `NativeRails.Rules` / `NativeConveyors.Rules` (duplicate ids, same bytes in any order) and the parity id-set checks.
- **A very long, zero-length or looped track:** 999 intervals maximum, 1 interval minimum, non-finite lengths, a closed loop (first point equals last) keeps its points. Pinned in `NativeRails.Plan`, `NativeRails.Rules`.
- **Open, ownerless and port ends; an over-long id; an empty tier:** open ends are `null` with a `null` kind; an ownerless end is open plus counted; an over-long id or an empty tier drops the edge, counted. Pinned in `NativeConveyors.Map`, `NativeConveyors.Rules`, `NativeConveyors.Golden`.
- **One layer native and the other not:** with rails native and conveyors off (the default), `getTrainRails` is never requested while the native pass runs for rails only; with rails on the bridge and conveyors native, FRM is asked for rails and the pass serves conveyors only; a source changed under an open pass drops it. Pinned in `WorldSender.LogisticsBridgeRails`, `WorldSender.ConveyorsOffUnlessNative`, `Config.Sources`.

## How the tests run

As in the S2 plan: UE tests via `pwsh scripts/test-mod.ps1 -Project <sml-project> -Engine <engine-root> -Out <tmp>\mod-test` then `cd harness; npm run harness -- judge-mod-test <tmp>\mod-test --min <ledger N>` (the executor's own paths, never committed; one build per task at most). `cd harness; npm run harness -- check-api` and `check-provenance` end every task that touches `mod/`. Harness: `npx vitest run <file> --reporter=dot` while iterating; the whole suite and `npm run typecheck` at the end of Tasks 1, 2, 10, 12-15. Dispatching `mod-test` is the owner's (`gh workflow run mod-test.yml --ref <branch>`). The Game module (`GameReader.cpp`) is not unit-testable here (the suite links Core only): its correctness is carried by the census, the parity and the tick-cost runs, and each game-side task says so.

**PRs** (each independently reviewable; each carries the checkbox "FRM source was not opened"):
- **PR S3-A (Tasks 1-3):** staging `Test3`, the `railsSource` switch and `Readers:` line, the layer table's `NativeFirstSeconds`. Changes no reader behaviour.
- **PR S3-B (Tasks 4-6):** R5 rails: pure builder, the shared logistics pass and its sender/driver wiring, the game-side read.
- **PR S3-C (Tasks 7-10):** R6 conveyors: pure mapping, the `conveyors` layer and `conveyorsSource`, the game-side read with the cast check and the census lines, the one-off `s3-census` run.
- **PR S3-D (Tasks 11-15):** the FRM oracle, the rails and conveyors judges, the parity drive, the tick-cost extension, docs and the follow-up list.

## File structure

`mod/...` is `mod/SatisDashAgent/Source`.

| File | New/Modified | Responsibility |
| --- | --- | --- |
| `harness/src/config.ts`, `run.ts`, `core-run.ts`, `docs/runner.md` | Modified (T1) | `sprint3World`, `WorldKey "sprint3"`, the Test3 checklist |
| `mod/SatisDashCore/Public/SatisDash/Config.h`, `Private/Config.cpp`, `Private/Driver.cpp`, `harness/src/reader-lines.ts`, `core-run.ts`, `mod-config.ts` guard | Modified (T2, T8) | `railsSource`, `conveyorsSource`, the `Readers:` line |
| `mod/SatisDashCore/Public/SatisDash/FrmWorld.h`, `Private/FrmWorld.cpp` | Modified (T3, T8) | `NativeFirstSeconds`, the `Conveyors` layer |
| `Native/Rails/NativeRails.h/.cpp`, `PROVENANCE.md`, `NativeRailsTest.cpp` | New (T4) | Sampling plan, mapping, exact JSON |
| `Public/SatisDash/Ports.h`, `WorldSender.h/.cpp`, `Driver.cpp`, `DriverFakes.h`, `WorldSenderTest.cpp`, `DriverNativeTest.cpp` | Modified (T5, T8) | `FLogisticsSnapshot`, the shared pass in the sender |
| `SatisDashGame/Private/GameReader.h/.cpp` | Modified (T6, T9) | The sliced game-side read |
| `Native/Conveyors/NativeConveyors.h/.cpp`, `PROVENANCE.md`, `NativeConveyorsTest.cpp` | New (T7) | End-kind table, capacity, mapping, exact JSON |
| `harness/src/core-runs/s3-census.ts`, `core-runs/index.ts`, `core-run.ts` (`CORE_RUN_IDS`), `.github/workflows/core-run.yml`, `runs/s3-census/pass.ts` | New/Modified (T10) | The one-off tier and drone-port census |
| `harness/src/frm-s3.ts`, `recorder.ts` | New/Modified (T11) | FRM oracles for rails, conveyors, drone ports |
| `runs/native-parity/pass-s3.ts`, `pass.ts`, `harness/src/core-runs/native-parity.ts` | New/Modified (T12, T13) | Judges, drive |
| `harness/src/core-runs/tick-cost-native.ts`, `runs/tick-cost-native/pass.ts` | Modified (T14) | Logistics step line, `Test3` |
| `docs/native-readers.md`, `api-sources.md`, `.github/workflows/mod-test.yml` | Modified (T3-T15) | Status table, rows, floor |

---

## PR S3-A

### Task 1: Stage `Test3`: `sprint3World`, the world key, the owner's save checklist

**Files:**
- Modify: `harness/src/config.ts` (next to `s2World`: `sprint3World: z.string().min(1).optional()`), `harness/src/run.ts` (`export type WorldKey = "s3" | "s2" | "sprint3"`; in `realDeps`, `loadWorld` picks `config.sprint3World` for `"sprint3"`, and the condition that builds `loadWorld` at `run.ts:444` includes `config.sprint3World`), `harness/src/core-run.ts` (the preflight at `core-run.ts:179` becomes a lookup `const WORLD_CONFIG_KEY = { s3: "s3World", s2: "s2World", sprint3: "sprint3World" } as const` and the refusal reads `<id> needs <key> in the runner config`; `worldKey` comment lists `"sprint3"`)
- Modify: `docs/runner.md` (new section "The Sprint 3 world (Test3)")
- Test: `harness/test/core-run.test.ts`, `harness/test/config.test.ts`, `harness/test/run.test.ts`

**Interfaces:** Produces `WorldKey` value `"sprint3"` and the refusal text `<id> needs sprint3World in the runner config`. No run uses the key yet (Tasks 13 and 14 switch `native-parity` and `tick-cost-native`), so this task changes no run.

- [ ] **Step 1: Failing tests.** `core-run.test.ts`: (a) a spec with `world: true, worldKey: "sprint3"` and deps without `sprint3World` is refused with `/needs sprint3World/`; (b) with it, the fake `loadWorld` is called with `"sprint3"`; (c) the existing `s2` and default (`"s3"`) assertions are unchanged. `config.test.ts`: `sprint3World` accepted, optional, and `""` rejected. `run.test.ts`: `realDeps` reads the file named by `sprint3World` for the `"sprint3"` key and the file named by `s2World` for `"s2"` (use the existing test's temp-file pattern).
- [ ] **Step 2:** `npx vitest run test/core-run.test.ts test/config.test.ts test/run.test.ts --reporter=dot` (from `harness/`): the new tests fail.
- [ ] **Step 3: Implement** as listed. Do not touch the spike path (S3/S6 keep `s3World`).
- [ ] **Step 4: Docs.** `docs/runner.md`: the runner config names `"sprint3World"` beside `"s2World"` and `"s3World"`; only `native-parity` and `tick-cost-native` use it, from Tasks 13 and 14; the staged file lives outside the repository; the same `satis-runner` rules apply. Add the owner's checklist:

> **Owner checklist: `Test3` must contain, before the S3 runs** (it is `Test2` plus the items below; if something is missing, add it in the save and stage the new copy; nothing in the harness can build it):
> 1. Everything the `Test2` checklist asks (machines of several kinds, a fluid recipe, power, self-driving trains between two stations with cargo platforms).
> 2. **Belts of two tiers** (for example Mk.1 and Mk.2), a **splitter**, a **merger**, and at least one **lift**.
> 3. **Pipes** with a **junction or a pump**.
> 4. **Belts into and out of the two paired drone ports** (labelled `FUEL 5` and `TestLand1` in the owner's save), and a **battery belt into one port**.
> 5. **Rail tracks**, including at least one curved track. A train that may have crashed is welcome, not required.
> 6. The save loads on the test server (the dedicated-server version of `harness/toolchain.json`). A byte copy is staged on the runner as `sprint3-test3.sav`; the owner adds `"sprint3World": "<path to that copy>"` to the runner config (outside the repository).
> Absent items are reported `NOT EXERCISED`, never failed.
- [ ] **Step 5:** whole Vitest suite and `npm run typecheck`. **Step 6:** commit `Stage the S3 world for the native runs (sprint3World, worldKey sprint3)`.

**Reviewer checks:** existing runs untouched (their tests unchanged and green); the refusal names the key; no path or save name beyond `sprint3World`/`sprint3-test3.sav` in committed text; no pass condition touched.

### Task 2: The `railsSource` switch, the `Readers:` line, `NATIVE_SOURCES_S3`

**Files:**
- Modify: `mod/SatisDashCore/Public/SatisDash/Config.h` (`inline constexpr ESource DefaultRailsSource = ESource::Bridge;` beside the others; `TOptional<ESource> RailsSource;`; `ESource GetRailsSource() const { return RailsSource.Get(DefaultRailsSource); }`; the `BadField` comment lists `"railsSource"`), `Private/Config.cpp` (parse and write back exactly as `trainsSource`: strict `"bridge"`/`"native"`, a bad value makes the config `Broken` with `BadField == "railsSource"`, an unset key is not written back), `Private/Driver.cpp` (`:70` `Readers:` line becomes `Readers: players=%s resourceNodes=%s factory=%s trains=%s rails=%s`)
- Modify: `harness/src/reader-lines.ts` (`ReaderSources.rails?: string`; the `READERS` regex gains an optional ` rails=(\S+)` group after `trains=`; `railsLogProblems(lines)` via the existing `partLogProblems`, whose `part` type widens to `"factory" | "trains" | "rails"`), `harness/src/core-run.ts` (`NativeSources.railsSource?`; `export const NATIVE_SOURCES_S3: NativeSources = { ...NATIVE_SOURCES_S2, railsSource: "native" }`; S1's and S2's constants unchanged), `harness/src/mod-config.ts` drift guard test file `harness/test/mod-config.test.ts` (keys written equal the mod's keys), `docs/native-readers.md` (a `Rails (R5)` row, source `bridge`; the override key list gains `"railsSource": "bridge"`)
- Test: `SatisDashCoreTests/Private/ConfigTest.cpp` (`SatisDash.Core.Config.Sources` extended), `DriverNativeTest.cpp` (the existing case that asserts the `Readers:` line), `harness/test/reader-lines.test.ts`, `harness/test/mod-config.test.ts`

**Interfaces:** Produces `FAgentConfig::GetRailsSource()`, `DefaultRailsSource`, `railsLogProblems`, `NATIVE_SOURCES_S3`, `ReaderSources.rails`. The test count stays 168 (existing cases are extended).

- [ ] **Step 1: Failing tests.** `Config.Sources`: for `railsSource`, the bad values `"Native"`, `""`, `1`, `null` make the config `Broken` naming the key; a valid file with `"railsSource":"native"` round-trips through `ConfigToJson`; an unset key is not written back; with no key `GetRailsSource()` is `Bridge`. Driver case: the line reads `Readers: players=native resourceNodes=native factory=native trains=native rails=bridge` for the default config. Vitest: the S1 two-field line and the S2 four-field line still parse (`rails` undefined); the five-field line yields `rails: "bridge"`; `railsLogProblems` of a four-field line says `has no rails= field`, of `rails=native` is empty, of `rails=bridge` says `not native`.
- [ ] **Step 2:** run (UE compile errors and Vitest fail). **Step 3: Implement.** **Step 4:** `judge-mod-test --min 168`, Vitest, `check-api`. **Step 5:** commit `railsSource switch (default bridge) and the five-field Readers line`.

**Reviewer checks:** an unset key means the default and the default is `bridge`; S1's and S2's constants and tests untouched; the config error names the key.

### Task 3: `FWorldLayerSpec::NativeFirstSeconds`

**Files:**
- Modify: `Public/SatisDash/FrmWorld.h` (`FWorldLayerSpec` gains `double NativeFirstSeconds;` after `IntervalSeconds`, comment: "While the layer is native: the first read this long after Running (times the jitter). 0 = same as FirstSeconds."), `Private/FrmWorld.cpp` (rows: `rails` `{ TEXT("rails"), &FrmGetTrainRails, 120.0, 600.0, 480.0 }`, `resourceNodes` `{ ..., 480.0, 1800.0, 0.0 }`, `trainRoutes` `{ ..., nullptr, 480.0, 600.0, 0.0 }`)
- Test: `SatisDashCoreTests/Private/FrmWorldTest.cpp` (new case `SatisDash.Core.FrmWorld.LayerTable`, +1 = 169)
- Modify: `.github/workflows/mod-test.yml` (`--min 169`), `harness/test/workflows.test.ts` (the pinned number and its text)

**Interfaces:** Produces the field; nothing reads it until Task 5.

- [ ] **Step 1: Failing test** `FrmWorld.LayerTable`: for each `EWorldLayer` assert `Name`, whether `Endpoint` is null, `FirstSeconds`, `IntervalSeconds`, `NativeFirstSeconds`: rails `"rails"`, non-null, 120, 600, 480; resourceNodes `"resourceNodes"`, non-null, 480, 1800, 0; trainRoutes `"trainRoutes"`, null, 480, 600, 0; `WorldLayerCount == 3`.
- [ ] **Step 2-4:** run (fail to compile), implement, `judge-mod-test --min 169`, raise the workflow floor and its test to 169, `check-api`. **Step 5:** commit.

**Reviewer checks:** every existing aggregate initialiser of `FWorldLayerSpec` has the fifth member; rails' bridge schedule is unchanged (120 s, 600 s); the floor change is exactly the ledger number.

---

## PR S3-B

### Task 4: R5, the pure rails reading: sampling plan, mapping, exact JSON

**Files:**
- Create: `Public/SatisDash/Native/Rails/NativeRails.h`, `Private/Native/Rails/NativeRails.cpp`, `Private/Native/Rails/PROVENANCE.md`, `SatisDashCoreTests/Private/NativeRailsTest.cpp`

**Interfaces** (produces; follow `NativeTrainRoutes.h` for layout and `FNativeWorldBody`):
```cpp
/** map.ts MAP_WORLD_MAX_POLYLINE_POINTS and MAP_WORLD_MAX_ITEMS (same values as FrmRails.h MaxRailPoints, MaxRailSegments). */
inline constexpr double RailSampleStepCm = 100.0;             // survey ruling S2: one sample a metre of track
enum class ERailSampler : uint8 { SplineComponent, GameTrackSampler };
/** Ruling M2: which game call gives a track's points. The parity run judges it; the fallback is this one constant. */
inline constexpr ERailSampler NativeRailSampler = ERailSampler::SplineComponent;
struct FRailPointCm { double XCm = 0; double YCm = 0; };      // world space, centimetres
struct FRailReading { FString Id; TArray<FRailPointCm> Points; };
/** Intervals to sample a track of LengthCm: ceil(Length / RailSampleStepCm), at least 1, at most MaxRailPoints - 1 (999); non-finite or <= 0 gives 1. */
SATISDASHCORE_API int32 RailSampleIntervals(double LengthCm);
/** Distance of sample Index (0..Intervals): Length * Index / Intervals; Index == Intervals returns Length exactly. */
SATISDASHCORE_API double RailSampleDistanceCm(double LengthCm, int32 Intervals, int32 Index);
/** The rails world body {"observedAt":...,"data":[{"id","points":[[x,y],...]}]}. */
SATISDASHCORE_API FNativeWorldBody BuildNativeRails(const FString& ObservedAt, const TArray<FRailReading>& Readings);
```
`BuildNativeRails` rules: points cm to whole metres with `ToWholeMetres` (`FrmMap.h`: JS `Math.round` semantics, `-0` printed `0`); a point outside +/-1,000,000 m (`InWholeMetreBounds`) is skipped, not the track (as `FRailsParse`); consecutive equal whole-metre points collapse; a track with fewer than 2 points left, or more than `MaxRailPoints` (1,000), or an empty id, is dropped and counted in `Dropped`; the id is cut with `ClampString` (200) and a duplicate id keeps the first after sorting by (id, then the point list compared pairwise) so the same readings in any order give the same bytes; tracks sorted by id (code-unit order); at most `MaxRailSegments` (5,000) kept, the rest counted in `Over`; `Items` is the kept count; layout, key order and number writing identical to `FRailsParse` (`{"id":"...","points":[[x,y],...]}`), written with `WorldBodyPrefix`.

- [ ] **Step 1: Failing tests (+4 = 173).**
  - `NativeRails.Plan`: `RailSampleIntervals`: 0 -> 1, -5 -> 1, NaN -> 1, +inf -> 1, 100 -> 1, 100.5 -> 2, 1236.9 -> 13, 9500 -> 95, 99900 -> 999, 100000 -> 999, 1500000 -> 999. `RailSampleDistanceCm(9500, 95, 0) == 0`, `(9500, 95, 95) == 9500`, `(9500, 95, 10)` within 1e-9 of 1000; for `Intervals` 999 and Length 1500000, index 999 returns exactly 1500000.
  - `NativeRails.Map`: a reading of one track from `Fixtures/frm-getTrainRails.json` track 1, points in cm exactly as the fixture's `SplineData` x,y: `(0,0) (10,10) (250,0) (250.4,0) (500,1.0e12) (1050,-50)` gives `[[0,0],[3,0],[11,0]]` (10,10 collapses into 0,0; 250 and 250.4 are 3; the 1e12 point is skipped; -50 cm prints `0` for y of the last point, never `-0`); `Items == 1`. Rounding table, each as its own reading with two far-apart points so none collapses: x `150` -> `2`, `-150` -> `-1`, `50` -> `1`, `-50` -> `0`, `249` -> `2`, `250` -> `3`. `ObservedAt` appears in the prefix. An empty reading list gives `"data":[]` (a valid empty layer) with `Items == 0`.
  - `NativeRails.Golden`: readings for all three tracks of `Fixtures/frm-getTrainRails.json` (track 1 as above; track 2 `(100,100),(120,120)` collapses to one point; track 3 has no points) give `Items == 1`, `Dropped == 2` and a data text equal to `Fixtures/frm-getTrainRails.expected.json`, parsed and compared as JSON the way `FrmRails.Golden` does.
  - `NativeRails.Rules`: 5,001 valid tracks keep 5,000 with `Over == 1`. The same 20 shuffled readings in a different order give identical `Json`. Two readings with the same id and different points give the same bytes in both input orders (the one sorting first by points wins). A 250-character id is cut to 200 characters. 1,000 distinct-metre points keep the track; 1,001 drop it (`Dropped == 1`). An empty id drops. A closed loop (first point equals last, 3 distinct points) keeps all.
- [ ] **Step 2: Run to see them fail** (compile errors count). **Step 3: Implement.** **Step 4:** `judge-mod-test --min 173`, `check-api`.
- [ ] **Step 5: PROVENANCE.md** in `Native/Rails/`: `Reader: Rails`; `Code:` the two new files, `mod/SatisDashAgent/Source/SatisDashGame/Private/GameReader.cpp` (written now; Task 6 adds the code) and `.../Private/WorldSender.cpp`; `Headers:` the track rows of the lookups table; `Survey: mod-native-readers-s3-headers-2026-10-26.md`; `Captures: frm-getTrainRails-2026-09-27-trimmed.json` (satisfactory-dash `captured-responses`; FRM HTTP output only; the fixture is its reduction); `Author:`, `Date:`; `FRM source was not opened`. Commit `Rails: pure sampling plan and layer builder (R5)`.

**Reviewer checks:** the FRM bridge path's bytes unchanged (`FrmRails*` tests untouched and green); constants equal `FrmRails.h`'s; sorting makes bytes order-independent; no `check()` on outside data; provenance complete.

### Task 5: The shared logistics pass: port, sender, driver (rails only)

**Files:**
- Modify: `Public/SatisDash/Ports.h`, `Public/SatisDash/WorldSender.h`, `Private/WorldSender.cpp`, `Private/Driver.cpp`, `SatisDashCoreTests/Private/DriverFakes.h`
- Tests: `WorldSenderTest.cpp` (`SatisDash.Core.WorldSender.Logistics`, `.LogisticsBridgeRails`, `.NativeFirstRead`), `DriverNativeTest.cpp` (`SatisDash.Core.Driver.NativeLogistics`) (+4 = 177)
- Modify: `.github/workflows/mod-test.yml` and `harness/test/workflows.test.ts` (`--min 177`), `harness/src/reader-lines.ts` (`STEP_LINE_NAMES` gains `"Logistics read"`; test), `docs/native-readers.md`

**Interfaces:**
```cpp
// Ports.h
struct FLogisticsCounts
{
	int32 Buildables = 0, Tracks = 0, Belts = 0, Lifts = 0, Pipes = 0;
	int32 OpenEnds = 0, OwnerlessEnds = 0, Dropped = 0;
	/** Advisory GetBuildableCount of the track class, -1 when not asked. */
	int32 ExpectedTracks = -1;
};
struct FLogisticsSnapshot
{
	TArray<FRailReading> Rails;          // empty when rails were not asked for
	FLogisticsCounts Counts;
};
// IGameReader, defaults do nothing:
virtual bool BeginLogisticsRead(bool bRails, bool bConveyors) { return false; }     // false: no world/subsystem, or a pass is open
virtual TOptional<FLogisticsSnapshot> StepLogisticsRead(FStepBudget& Budget) { return {}; }
virtual bool LogisticsReadAbandoned() const { return false; }  // true: no subsystem, world changed, or zero buildables (Ruling M5)
virtual void EndLogisticsRead() {}
virtual bool IsLogisticsReadOpen() const { return false; }
// WorldSender.h FWorldContext: FStepBudget* LogisticsBudget = nullptr;   // the sender resets it when a pass begins
```
(`bConveyors` is accepted now and ignored until Task 8; the parameter exists so Task 8 does not change the port.)

Sender behaviour (all in `FWorldSender`, new private members `bool bPassOpen; bool bPassRails; bool bPassConveyors;` and per layer `bool bFirstNativeDone`):
- `WorldSenderIsNative(Rails)` is true when `Context.Config->GetRailsSource() == Native` and a reader is present.
- **Native first read (Ruling M7).** In `Waiting`, when a native layer is due, `NativeFirstSeconds > 0` and `!bFirstNativeDone`: set `bFirstNativeDone = true`, `NextAt = StartedAt + JitteredSeconds(NativeFirstSeconds, random01Source())` (`StartedAt` is set in `Start`) and return if `Now < NextAt`. Thereafter the layer behaves as usual. A layer on the bridge never takes this branch.
- **Start of a pass.** In `Waiting`, a due native logistics layer with `bGameRunning`, no open pass (`!bPassOpen`) and `!NativeReaders->IsLogisticsReadOpen()` calls `BeginLogisticsRead(bRails, bConveyors)` where each flag is "that layer is native and its Phase is Waiting"; on true it resets `LogisticsBudget`, sets `bPassOpen` and the two flags, and puts every included layer in `Scanning`; on false the due layer waits `NodeScanRetrySeconds` (FRM is not asked). A pass-open or reader-busy layer returns and looks again next tick.
- **Stepping.** At the start of `Tick`, `if (bPassOpen) StepLogisticsPass(Context)`: if no included layer is still native, `EndLogisticsRead()`, close the pass and `ScheduleNext` the included layers; otherwise `StepLogisticsRead(*LogisticsBudget)`; unset: return. When it returns: if `LogisticsReadAbandoned()` log one rate-limited warning (`World layer logistics: the game's read was abandoned; nothing sent, will read again in %.0f s`), set both layers `Waiting` with `NextAt = Now + NodeScanRetrySeconds`; else log
  `Logistics read (game thread only): layers rails=%d conveyors=%d, %d buildables, %d tracks, %d belts, %d lifts, %d pipes, %d open ends, %d ownerless ends in %d steps, longest step %.2f ms, total %.2f ms` (counts and times only), then for each included layer call `Bridge.SubmitNativeWorld(Layer, Utc.ToIso8601(), FNativeWorldBuilder(...), L.DataHash)` (rails: `[Snapshot = ...](const FString& Observed) { return BuildNativeRails(Observed, Rails); }`; the rails array is moved into the closure) and set `Phase = Reading`, or `ScheduleNext` when refused. A layer in `Scanning` whose source changed to non-native drops out of the pass (`ScheduleNext`). In `TickLayer`, a logistics layer in `Scanning` does nothing (the pass is stepped once a tick, above).
- `Stop()` and leaving Running: `Driver` calls `EndLogisticsRead()` where it calls `EndTrainsRead()` today (grep `EndTrainsRead` in `Driver.cpp`), and the sender's `Stop` clears the pass flags.

`DriverFakes.h`: the fake game reader gains `int32 LogisticsStepsToFinish`, `TArray<FRailReading> LogisticsRails`, `FLogisticsCounts LogisticsCounts`, `bool bLogisticsAbandoned`, `bool bBeginLogisticsOk = true`, counters for Begin/End calls and the last flags passed.

- [ ] **Step 1: Failing tests.**
  - `WorldSender.Logistics` (railsSource native, conveyors flag absent): before 432 s after `Start` (jitter `Random01 = 0` gives 0.9 x 480) nothing is requested or begun; at 433 s `BeginLogisticsRead(true, false)` is called once; the fake needing 3 steps is stepped once per tick; after the third a `rails` body is submitted and posted to `/agent/v1/world/rails` once, with `"id"` values sorted; `/getTrainRails` is never requested; a second pass with the same readings does not repost (same data hash); an abandoned read posts nothing, warns once, retries after `NodeScanRetrySeconds`; a read with `Counts.Buildables == 0` reported abandoned by the fake behaves the same.
  - `WorldSender.LogisticsBridgeRails` (railsSource bridge): at the first due time (108 to 132 s) FRM is asked for `getTrainRails` exactly as before and `BeginLogisticsRead` is never called; flipping the config to native under an open pass drops it (`EndLogisticsRead` called, layer rescheduled).
  - `WorldSender.NativeFirstRead`: with railsSource native the rails layer is not begun at 133 s but at 433 s; resourceNodes and trainRoutes keep their 480 s; rails on the bridge is requested at its due time (120 s +/-10%).
  - `Driver.NativeLogistics`: through the whole driver with the fake game: the snapshot flow is unchanged, `Readers:` shows `rails=native` for `railsSource: "native"`, leaving Running calls `EndLogisticsRead`, and no FRM request for rails is sent while native.
- [ ] **Step 2: Run to see them fail.** **Step 3: Implement.** **Step 4:** `judge-mod-test --min 177`; raise the floor; Vitest for `STEP_LINE_NAMES` (a `Logistics read` sample line parses to a `StepCost`; 2.05 ms fails `stepProblems`); `check-api`. **Step 5:** commit `Shared logistics pass in the world sender (rails)`.

**Reviewer checks:** the layer with a null endpoint (trainRoutes) and nodes behave as before (their tests untouched); no pointer or reading survives after the builder runs; a bridge-rails host never enters the pass; the budget is reset when a pass begins; `StepLogisticsRead` is called once a tick; no FRM request while native; the warning and the log line carry no ids or names.

### Task 6: R5, the game-side sliced read

**Files:**
- Modify: `SatisDashGame/Private/GameReader.h/.cpp` (`BeginLogisticsRead`, `StepLogisticsRead`, `LogisticsReadAbandoned`, `EndLogisticsRead`, `IsLogisticsReadOpen`), `Private/Native/Rails/PROVENANCE.md` (headers), `api-sources.md`, `docs/native-readers.md`

**Interfaces:** consumes `FRailReading`, `RailSampleIntervals`, `RailSampleDistanceCm`, `NativeRailSampler`, `FStepBudget`, `FLogisticsSnapshot`; produces the game side of Task 5's port. Game module code is not unit-testable here; correctness is carried by the census and parity runs.

Game side, in `GameReader.cpp` (open every header in the lookups table first and add its `api-sources.md` row with `path:line` before using the symbol; header wins):
- `BeginLogisticsRead(bRails, bConveyors)`: false when a pass is open, there is no `UWorld`, or `AFGBuildableSubsystem::Get(World)` is null. Store the weak world, reset cursor 0 and phase `Collect`, clear the weak-pointer arrays and results, store `bRails`/`bConveyors`; if `bRails`, `ExpectedTracks = GetBuildableCount(AFGBuildableRailroadTrack::StaticClass())` (advisory, logged only).
- `StepLogisticsRead(Budget)`: `Budget.Begin()`; `do { ... } while (!Budget.Spent());` then `Budget.End()`; one element per iteration.
  - **Collect:** `const TArray<AFGBuildable*>& All = Subsystem->GetAllBuildablesRef();` (re-fetched every iteration, never kept); `if (Cursor >= All.Num())` move to phase `ReadRails`; otherwise `AFGBuildable* B = All[Cursor++]`, `++Counts.Buildables`, skip if `!IsValid(B)`; if `bRails`, `Cast<AFGBuildableRailroadTrack>(B)` pushes a `TWeakObjectPtr` to `Tracks`. (Task 9 adds the belt/lift/pipeline casts to this same iteration.)
  - **ReadRails:** one weak pointer per iteration (index into `Tracks`); skip if invalid; `Length = Track->GetLength()`; `N = RailSampleIntervals(Length)`; for `i` in `0..N`, `D = RailSampleDistanceCm(Length, N, i)`; with `ERailSampler::SplineComponent`: `Track->GetSplineComponent()` (null: skip the track, count in `Dropped`) then `GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World)`; with `GameTrackSampler`: `Track->GetWorldLocationAndDirectionAtDistance(D, Loc, Dir)`. Push `FRailPointCm{Loc.X, Loc.Y}`; `FRailReading.Id = Track->GetName()`. Open each signature in the header and use its exact parameter types; if `GetLocationAtDistanceAlongSpline` does not take the coordinate-space overload at `SplineComponent.h:763`, follow the header and record both in the row.
  - When the track index passes the end: Counts.Tracks = number read; if `Counts.Buildables == 0` mark abandoned (Ruling M5); return the snapshot (moved) and free every array.
- If the world changed (weak pointer differs) or the subsystem is gone mid-pass: abandoned, return an empty snapshot with `LogisticsReadAbandoned() == true`.
- Counts only in logs; the sender writes the line. No pointer into `GetAllBuildablesRef()` is stored; weak pointers are checked at every use.

- [ ] **Step 1:** write the `api-sources.md` rows (symbol, `path:line`, Source column with survey line and "H:" for header-checked notes) for each symbol above, the includes and any new module dependency (`module:Engine` is present already for the spline component; check). `check-api` must report `0 missing` before the code compiles.
- [ ] **Step 2:** implement; update `Native/Rails/PROVENANCE.md` `Headers:` lines (the rows above), keep `Code:` lines true (`check-provenance` verifies the files exist).
- [ ] **Step 3:** `judge-mod-test --min 177`, `check-api`, `check-provenance`; Vitest unchanged. **Step 4:** docs row ("Rails (R5) | S3 | `bridge` (default) | a sliced pass over the buildable list; native rails follow `railsSource`; first scan 480 s"). Commit `Rails: game-side sliced read (R5)`.

**Reviewer checks:** `Cast` result and spline component null-checked; no list reference kept across ticks; `N` at most 999 so a segment never exceeds 1,000 points; sampler constant honoured; a log line is written only by the sender; no ids in logs; rows complete.

---

## PR S3-C

### Task 7: R6, the pure conveyors reading: end kinds, capacity, mapping, exact JSON

**Files:**
- Create: `Public/SatisDash/Native/Conveyors/NativeConveyors.h`, `Private/Native/Conveyors/NativeConveyors.cpp`, `Private/Native/Conveyors/PROVENANCE.md`, `SatisDashCoreTests/Private/NativeConveyorsTest.cpp`, `SatisDashCoreTests/Fixtures/frm-getBelts.json`, `frm-getLifts.json`, `frm-getPipes.json` (the items of the three trimmed captures `frm-getBelts-2026-10-21-connections-trimmed.json`, `frm-getLifts-2026-10-21-trimmed.json`, `frm-getPipes-2026-10-21-connections-trimmed.json` in the satisfactory-dash `docs-vault/raw-sources/captured-responses/`: drop each file's leading prose header, keep the JSON array exactly)

**Interfaces** (produces):
```cpp
inline constexpr int32 ConveyorsMaxEdges = 50000;      // map.ts CONVEYORS_MAX_EDGES
inline constexpr int32 ConveyorMaxIdLength = 128;
inline constexpr int32 ConveyorMaxTierLength = 64;
enum class EConveyorKind : uint8 { Belt, Lift, Pipe };
enum class EConveyorNodeKind : uint8 { Machine, Splitter, Merger, Storage, Station, Extractor, Consumer, Junction, Conveyor, Other };
/** survey ruling S5: how a belt's or lift's items per minute is derived from GetSpeed(). The census run settles it; one constant flips it. */
enum class ECapacityMode : uint8 { SpeedHalf, ClassTable };
inline constexpr ECapacityMode NativeConveyorCapacityMode = ECapacityMode::SpeedHalf;
struct FConveyorReading
{
	FString Id;                 // GetName()
	EConveyorKind Kind = EConveyorKind::Belt;
	FString FromId, ToId;       // end 0 / end 1 owner GetName(); empty = open end
	FString Tier;               // mDisplayName.ToString()
	double RawRate = 0;         // belts/lifts: GetSpeed(); pipes: GetFlowLimit() (m3/s)
	double XCm = 0, YCm = 0;    // end 0 connector location, world space
};
SATISDASHCORE_API EConveyorNodeKind ClassifyConveyorEnd(const FString& Id);
SATISDASHCORE_API const TCHAR* ConveyorNodeKindText(EConveyorNodeKind Kind);      // "machine","splitter","merger","storage","station","extractor","consumer","junction","conveyor","other"
SATISDASHCORE_API TOptional<double> ConveyorCapacityPerMinute(EConveyorKind Kind, double RawRate, const FString& Tier, ECapacityMode Mode);
SATISDASHCORE_API FNativeWorldBody BuildNativeConveyors(const FString& ObservedAt, const TArray<FConveyorReading>& Readings);
```
**`ClassifyConveyorEnd` is the adapter's `classifyConveyorEnd` (satisfactory-dash `packages/game-adapter/src/map/conveyorsMapper.ts`) ported line for line.** Take the class part `Cls` by removing one trailing `_<digits>` from the id (`/_\d+$/`), then the first matching row wins, in this order:

| Order | Kind | Rule on `Cls` |
| --- | --- | --- |
| 1 | splitter | starts with `Build_ConveyorAttachmentSplitter` |
| 2 | merger | starts with `Build_ConveyorAttachmentMerger` |
| 3 | conveyor | case-insensitively starts with `Build_ConveyorBelt` or `Build_ConveyorLift`, or equals (case-insensitively) `Build_Pipeline_C` or `Build_PipelineMK2_C` |
| 4 | junction | starts with `Build_PipelineJunction` or `Build_PipelinePump` |
| 5 | extractor | starts with `Build_Miner`, `Build_WaterPump`, `Build_OilPump` or `Build_Frack` |
| 6 | consumer | starts with `Build_Generator`, `Build_ResourceSink` or `Build_SpaceElevator` |
| 7 | storage | starts with `Build_StorageContainer`, `Build_CentralStorage`, `Build_IndustrialTank`, `Build_PipeStorage` or `Build_FluidBuffer` |
| 8 | station | starts with `Build_TrainDockingStation`, `Build_TrainStation`, `Build_DroneStation` or `Build_TruckStation` |
| 9 | machine | starts with `Build_Constructor`, `Build_Assembler`, `Build_Manufacturer`, `Build_Smelter`, `Build_Foundry`, `Build_Packager`, `Build_OilRefinery`, `Build_Blender`, `Build_HadronCollider`, `Build_Converter` or `Build_QuantumEncoder` |
| 10 | other | anything else |

(Rows 4 to 9 are case-sensitive; row 3's alternatives for belt and lift are prefixes, the two pipeline names are whole-string.)

`ConveyorCapacityPerMinute`: belts and lifts with `SpeedHalf` give `RawRate / 2` (survey: `ITEM_SPACING` 120, so items per minute = speed x 60 / 120); with `ClassTable` the number after the last `Mk.` in `Tier` (`"Conveyor Belt Mk.3"` -> 3): 1 -> 60, 2 -> 120, 3 -> 270, 4 -> 480, any other or none -> unset (the captured tiers; the contract allows `null`); pipes give `RawRate * 60` in both modes. The result is rounded to 4 decimals; non-finite or negative gives unset (written `null`).

`BuildNativeConveyors` rules (Ruling M8): per reading, an empty or over-128-character `Id`, an over-128-character end id, an empty `Tier`, or an `at` outside +/-1,000,000 m (`ToWholeMetres` and `InWholeMetreBounds`) drops it (`Dropped`); an empty `FromId`/`ToId` is an open end written `"from":null,"fromKind":null` (resp. `to`); the tier is cut at 64; edges sorted by `Id` (code-unit order), a duplicate `Id` keeps the first after sorting by (id, from, to, tier); at most `ConveyorsMaxEdges` kept, the rest in `Over`. Key order per edge, exactly the adapter's object: `id, kind, from, fromKind, to, toKind, capacityPerMinute, tier, at` with `at` as `{"x":..,"y":..}`; `kind` is `"belt"`, `"lift"` or `"pipe"`; numbers use the same number writer the factory part uses for rates (integers print without a decimal point: `300`, not `300.0`); `capacityPerMinute` unset is `null`.

- [ ] **Step 1: Failing tests (+5 = 182).**
  - `NativeConveyors.Kind`: `ClassifyConveyorEnd` on these ids (class part from the captures and the table): `Build_ConveyorAttachmentSplitterSmart_C_12` splitter; `Build_ConveyorAttachmentMerger_C_5` merger; `Build_ConveyorBeltMk3_C_77` conveyor; `Build_ConveyorLiftMk2_C_3` conveyor; `Build_Pipeline_C_9` conveyor; `Build_PipelineMK2_C_9` conveyor; `Build_PipelineJunction_Cross_C_4` junction; `Build_PipelinePump_C_8` junction; `Build_MinerMk2_C_6` extractor; `Build_WaterPump_C_1` extractor; `Build_GeneratorCoal_C_2` consumer; `Build_ResourceSink_C_1` consumer; `Build_StorageContainerMk1_C_3` storage; `Build_IndustrialTank_C_1` storage; `Build_DroneStation_C_2146572476` station; `Build_TrainDockingStation_C_4` station; `Build_ConstructorMk1_C_10` machine; `Build_OilRefinery_C_11` machine; `Build_Foo_C_1` other; an id with no trailing number (`Build_Assembler_C`) machine; `build_conveyorbeltmk1_c_1` conveyor (case-insensitive row) but `build_miner_c_1` other (case-sensitive rows); a "SplitterSmartLift" style name whose class starts `Build_ConveyorAttachmentSplitterSmart` is splitter, never conveyor (order); empty id other. `ConveyorNodeKindText` returns the ten contract strings.
  - `NativeConveyors.Capacity`: `SpeedHalf`: belt `120` -> 60, `240` -> 120, `540` -> 270, `960` -> 480, lift `960` -> 480, `NaN` -> unset, `-2` -> unset; `ClassTable`: `"Conveyor Belt Mk.1"` -> 60, `"Conveyor Lift Mk.4"` -> 480, `"Conveyor Belt Mk.6"` -> unset, `"Pipeline"` -> unset; pipes: `5.0` -> 300, `10.0` -> 600, `4.99999` -> 299.9994 (rounded to 4 decimals), in both modes; `NativeConveyorCapacityMode == ECapacityMode::SpeedHalf` (the test pins the default so a flip is a visible diff).
  - `NativeConveyors.Map`: one reading per case, written to the exact JSON: a miner-fed belt (`FromId` `Build_MinerMk1_C_7`, `ToId` `Build_ConstructorMk1_C_8`, tier `Conveyor Belt Mk.1`, speed 120, end-0 position `XCm = -5555497.7`, `YCm = -12860636.0`) gives `"from":"Build_MinerMk1_C_7","fromKind":"extractor","to":"Build_ConstructorMk1_C_8","toKind":"machine","capacityPerMinute":60,"tier":"Conveyor Belt Mk.1","at":{"x":-55555,"y":-128606}`; an open end gives `"to":null,"toKind":null`; a pipe with `FromId`, tier `Pipeline Mk.1`, flow 5 gives `"kind":"pipe"` and `300`; capacity unset gives `null`; `at` rounding follows `ToWholeMetres` (x 150 cm -> 2, -150 -> -1, never `-0`).
  - `NativeConveyors.Rules`: an empty list gives `"data":[]`, `Items == 0`; 50,001 valid edges keep 50,000, `Over == 1`; the same readings in a different order give identical `Json`; a duplicate id keeps one; a 129-character id, a 129-character end id, an empty tier and `at` of `1.0e12` cm each drop (`Dropped` counts each); a 65-character tier is cut to 64.
  - `NativeConveyors.Golden`: from the three fixtures, build readings by hand in the test (one per fixture record, ids, `ConnectedTo0/1` as end ids, `Name` as tier, `ItemsPerMinute` x 2 as `RawRate` for belts and lifts, `Speed` as `RawRate` for pipes, `location0` as `at`), sort-insensitive, and assert `BuildNativeConveyors` data equals the adapter's output for the same records, written out by the implementer into the test as literals from the capture and cross-checked against the dash repo's `conveyorsMapper.test.ts` expectations. The capture's open-end belt and the pipe out of a water extractor are in the set.
- [ ] **Step 2: Run to see them fail.** **Step 3: Implement.** **Step 4:** `judge-mod-test --min 182`, `check-api`, `check-provenance`.
- [ ] **Step 5: PROVENANCE.md** (`Reader: Conveyors`): `Code:` the two new files plus `GameReader.cpp` and `WorldSender.cpp`; `Headers:` the conveyor rows; `Survey:` the S3 note; `Captures:` the three trimmed captures and `frm-getDroneStation-2026-10-25-unpaired-trimmed.json`; `Facts:` the end-kind table is the adapter's (our own code, not FRM's) and the capture-derived tier numbers (60/120/270/480, pipe 5 and 10 m3/s). Commit `Conveyors: pure end kinds, capacity and layer builder (R6)`.

**Reviewer checks:** the table equals `classifyConveyorEnd` row for row and in order; key order equals the adapter's; the golden is derived from the captures, not from the code under test; no tolerance or FRM string copied; provenance complete.

### Task 8: The `conveyors` layer and `conveyorsSource`: config, layer row, the pass takes conveyors

**Files:**
- Modify: `Config.h/.cpp` (`DefaultConveyorsSource = ESource::Bridge`, `ConveyorsSource`, `GetConveyorsSource()`, key `"conveyorsSource"`, same strict parse; `BadField` comment), `Driver.cpp` (`Readers:` line gains ` conveyors=%s` after `rails=%s`), `Public/SatisDash/FrmWorld.h` (`enum class EWorldLayer : uint8 { Rails, ResourceNodes, TrainRoutes, Conveyors }`, `WorldLayerCount = 4`; the comment "Plan 3C adds Conveyors" becomes "Conveyors is native-only"), `Private/FrmWorld.cpp` (row `{ TEXT("conveyors"), nullptr, 480.0, 600.0, 0.0 }`; `FrmWorldMakeParse` already returns null for it), `Private/WorldSender.cpp` (`WorldSenderIsNative(Conveyors)` = `GetConveyorsSource() == Native`; the pass passes `bConveyors`; the conveyors builder `[Conveyors](const FString& O){ return BuildNativeConveyors(O, Conveyors); }`; the log line's `conveyors=` flag), `Ports.h` (`FLogisticsSnapshot` gains `TArray<FConveyorReading> Conveyors;` and `FLogisticsCounts` the fields below), `FrmBridge.*` only if a per-layer array is sized by a literal 3 (grep for `WorldLayerCount` and `[3]`; it should use the constant), `DriverFakes.h`
- Tests: `WorldSenderTest.cpp` `SatisDash.Core.WorldSender.LogisticsBoth`, `.ConveyorsOffUnlessNative` (+2 = 184); extend `Config.Sources` and the `Readers:` Driver case (no new macro)
- Modify: `harness/src/reader-lines.ts` (`ReaderSources.conveyors?`, regex gains an optional ` conveyors=(\S+)` after `rails=`, `conveyorsLogProblems`), `harness/src/core-run.ts` (`NATIVE_SOURCES_S3` gains `conveyorsSource: "native"`, `NativeSources.conveyorsSource?`), `mod-config` drift test, `docs/native-readers.md`, `.github/workflows/mod-test.yml` and `harness/test/workflows.test.ts` (`--min 184`)

`FLogisticsCounts` gains (all zero when the layer was not asked): `int32 KindChecked = 0, KindDiffer = 0; TArray<FString> DifferClasses; int32 EndsIntoPorts = 0, EndsOutOfPorts = 0; TArray<FTierCount> Tiers;` with `struct FTierCount { EConveyorKind Kind; FString Name; double RawRate; int32 Count; }`. The sender logs, once per pass and only when `bConveyors`, `Conveyor kinds: %d ends checked, %d differ from the class table (first classes: %s)` and `Conveyor drone ports: %d edges end at a port, %d start at one`; and, only on the first conveyors pass after `Start`, one `Conveyor tier census: %s | %s | speed %.3f | count %d` per `Tiers` entry (at most 16). These are game class names and tier display names (game data), no ids.

- [ ] **Step 1: Failing tests.**
  - `WorldSender.LogisticsBoth` (both sources native): when rails is due (433 s) `BeginLogisticsRead(true, true)` is called, conveyors goes `Scanning` at once though its own `NextAt` is later (Ruling M6); one finished snapshot posts `/world/rails` and `/world/conveyors` once each; the conveyors body is sorted by id and equals `BuildNativeConveyors` of the fake's readings; both layers `Reading` then `Pending`; no FRM request for any layer; a refused `SubmitNativeWorld` for one layer reschedules only that layer.
  - `WorldSender.ConveyorsOffUnlessNative`: with the default config (`conveyorsSource` unset = bridge) the conveyors layer is never scheduled (nothing begun, nothing posted, no request, no log) while rails (native) posts; with `railsSource` bridge and `conveyorsSource` native, FRM is asked for `getTrainRails` and `BeginLogisticsRead(false, true)` serves conveyors alone; an over-cap body (a fake with edges whose body passes `WorldLayerMaxBytes`) is not posted and warns once.
  - `Config.Sources`: `conveyorsSource` bad values `"Native"`, `""`, `1`, `null` name the key; unset not written back; default `Bridge`. Driver: line reads `... rails=bridge conveyors=bridge` by default.
  - Vitest: five-field line still parses (`conveyors` undefined); six-field yields it; `conveyorsLogProblems` mirrors `railsLogProblems`; `NATIVE_SOURCES_S3` has both keys; S1's and S2's constants unchanged (assert deep equality to their old values).
- [ ] **Step 2-4:** run (fail), implement, `judge-mod-test --min 184`, raise floor, Vitest, `check-api`. **Step 5:** commit `The conveyors layer and conveyorsSource (default bridge)`.

**Reviewer checks:** `conveyorsSource` default `bridge` means no scheduling and no log; no FRM request exists for the layer; rails' bridge path and nodes/trainRoutes bytes and tests unchanged; arrays sized by `WorldLayerCount` everywhere; the existing layer-table test from Task 3 is updated for the fourth row (`WorldLayerCount == 4`, name `conveyors`, null endpoint, 480, 600, 0).

### Task 9: R6, the game-side sliced read, the cast cross-check and the census counters

**Files:**
- Modify: `SatisDashGame/Private/GameReader.h/.cpp`, `Private/Native/Conveyors/PROVENANCE.md`, `api-sources.md`, `docs/native-readers.md`

Game side, in `GameReader.cpp` (headers from the lookups table opened first, rows added first; header wins):
- `Collect` (the same iteration as rails, Task 6): when `bConveyors`, `Cast<AFGBuildableConveyorBase>(B)` -> weak pointer to `Belts`/`Lifts` (subclass test with `Cast<AFGBuildableConveyorBelt>` / `Cast<AFGBuildableConveyorLift>`), else `Cast<AFGBuildablePipeline>(B)` -> `Pipes` (never `AFGBuildablePipeBase`: hypertubes). One element per iteration; no more than the casts.
- `ReadConveyors` phase (after `ReadRails`; one weak pointer per iteration):
  - Belt/lift: `C0 = Edge->GetConnection0()`, `C1 = Edge->GetConnection1()`; end id: `Conn != nullptr && Conn->IsConnected()`, `Other = Conn->GetConnection()`, `Owner = Other ? Other->GetOuterBuildable() : nullptr`; `Owner` null while `IsConnected()` is true counts an **ownerless end** and is open; otherwise `Owner->GetName()`. `at`: `C0->GetConnectorLocation()` (Ruling S8; null `C0` drops the edge, counted). `RawRate = Edge->GetSpeed()`. `Tier = Edge->mDisplayName.ToString()` read through the public field exactly as the factory reader reads a machine's.
  - Pipe: `P0 = Pipe->GetConnection0()`, `P1 = Pipe->GetConnection1()` (`UFGPipeConnectionComponentBase`); end: `Other = P->GetConnection()` (null or `!P->IsConnected()` -> open); owner `Other->GetOwner()` (null counts ownerless); `at`: `P0->GetComponentLocation()`; `RawRate = Pipe->GetFlowLimit()`.
  - Counts: `Belts`, `Lifts`, `Pipes`, `OpenEnds` (each open end), `OwnerlessEnds`, `Dropped`.
  - **Kind check (Ruling S4):** for each connected end, `ClassifyConveyorEnd(OwnerName)` vs `KindByCast(Owner)`, a file-local function returning an `EConveyorNodeKind` from `Cast`s in the table's order (splitter: `AFGBuildableAttachmentSplitter`, `AFGBuildableSplitterSmart`; merger: `AFGBuildableAttachmentMerger`, `AFGBuildableMergerPriority`; conveyor: `AFGBuildableConveyorBase`, `AFGBuildablePipeline`; junction: `AFGBuildablePipelineJunction`, `AFGBuildablePipelinePump`; extractor: `AFGBuildableResourceExtractorBase`; consumer: `AFGBuildableGenerator`, `AFGBuildableResourceSink`, `AFGBuildableSpaceElevator`; storage: `AFGBuildableStorage`, `AFGBuildablePipeReservoir`; station: `AFGBuildableDroneStation`, `AFGBuildableTrainPlatformCargo`, `AFGBuildableDockingStation`; machine: `AFGBuildableManufacturer`; else other). `KindChecked++`; a difference does `KindDiffer++` and adds `Owner->GetClass()->GetName()` to `DifferClasses` if not present and fewer than 8 are held. It never changes the output.
  - **Ports and census:** an end whose owner class name (`GetClass()->GetName()`) starts with `Build_DroneStation` counts `EndsIntoPorts` when it is end 1 (`to`) and `EndsOutOfPorts` when end 0 (`from`). `Tiers` accumulates (kind, display name, raw rate, count) by exact (kind, name) pair, at most 16 entries (extra ignored).
- Snapshot assembly: edges pushed as `FConveyorReading` (end ids as above; `Kind` from the cast); counts filled; abandoned rules as Task 6 (zero buildables). Free everything on return.
- [ ] **Step 1:** rows first (`check-api` `0 missing`). **Step 2:** implement. **Step 3:** `judge-mod-test --min 184`, `check-api`, `check-provenance`, Vitest. **Step 4:** `PROVENANCE.md` `Headers:` lines; docs row ("Conveyors (R6) | S3 | `bridge` (default = layer not sent)"). Commit `Conveyors: game-side sliced read, kind cross-check and census counters (R6)`.

**Reviewer checks:** hypertubes cannot enter (cast to `AFGBuildablePipeline`); every pointer null-checked; `GetConnection()` results null-checked at each hop; no id, name or label in any log except game class names and tier display names in the census/kind lines; the kind check cannot change output; `Tiers` and `DifferClasses` bounded; no pointer kept across ticks.

### Task 10: The one-off census run (pre-check; the owner approves each dispatch)

**Why.** The belt/lift `GetSpeed()` unit, the tier display names, the pipe flow limits and the drone-port id form can only be read from the running game (survey lines 127, 129, 132, 150, 154). This is the cheap way to learn them before the 45-minute parity run: a boot of `Test3` with the logistics layers native, waiting only for the first pass. The owner approves the dispatch; the run is read-only and judges nothing about FRM.

**Files:**
- Create: `harness/src/core-runs/s3-census.ts`, `runs/s3-census/pass.ts`, `harness/test/s3-census-pass.test.ts`
- Modify: `harness/src/core-run.ts` (`CORE_RUN_IDS` gains `"s3-census"`), `harness/src/core-runs/index.ts`, `.github/workflows/core-run.yml` (options list; `harness/test/workflows.test.ts` stays green), `docs/runner.md`, `harness/src/reader-lines.ts` (`censusLines(lines)`)

**Interfaces:** `censusLines(lines): { tiers: Array<{ kind: string; name: string; speed: number; count: number }>; kinds: { checked: number; differ: number; classes: string[] } | null; ports: { into: number; outOf: number } | null }` parsed from the three line families of Task 8 (`Conveyor tier census: <kind> | <name> | speed <x> | count <n>`, `Conveyor kinds: ...`, `Conveyor drone ports: ...`). `s3CensusRun: CoreRunSpec` with `world: true, worldKey: "sprint3", backend: true, modConfig: NATIVE_SOURCES_S3`, a drive that loads `Test3`, enrols as `tick-cost-native` does, waits up to 14 minutes for a `Logistics read (game thread only):` line (the first pass lands about 8 minutes after Running), then 20 more seconds, and writes `census.json` (the parsed `censusLines` plus the `Logistics read` line's counts). `pass.ts`: pass = a `Logistics read` line with `layers rails=1 conveyors=1` and at least one census line; every value is printed in `reasons`, none is judged. Reasons also print, per belt/lift tier whose name ends `Mk.1` to `Mk.4`, `speed / 2` next to the expected 60/120/270/480 and `match`/`NO MATCH`; per pipe tier `flow x 60` next to 300/600 for `Mk.1`/`Mk.2`.

- [ ] **Step 1: Failing Vitest** `s3-census-pass.test.ts`: sample log lines give the parsed census; `Conveyor Belt Mk.1` with speed 120 prints `60 match`; speed 240 for `Mk.1` prints `NO MATCH`; a log without the `Logistics read` line fails with that reason; a log with the line but `conveyors=0` fails; unparsable census lines are ignored, not fatal. **Step 2:** fail; **Step 3:** implement; **Step 4:** whole Vitest + typecheck. **Step 5:** commit `s3-census: a one-off read of the tier numbers and drone-port ends`.

**After it runs (the owner's, not a task):** the owner reads `census.json`. If every belt/lift tier prints `match`, nothing changes. If not, a small ruling PR sets `NativeConveyorCapacityMode = ECapacityMode::ClassTable` (one constant, test `NativeConveyors.Capacity` pins the new default). Tier names and the drone-port ends are compared by the parity run.

**Reviewer checks:** the run judges no FRM data and needs no `frmBaseUrl`; the workflow option is added and the workflow test passes; the census prints game class names only in `reasons` and `census.json`; the 14-minute wait is bounded.

---

## PR S3-D

### Task 11: The FRM oracles for rails, conveyors and drone ports

**Files:**
- Create: `harness/src/frm-s3.ts`, `harness/test/frm-s3.test.ts`
- Modify: `harness/src/recorder.ts` + `harness/test/recorder.test.ts` (nothing new is needed if `keepWorldData` already takes any layer name: add a test that `["rails","conveyors"]` keeps both; the comment above `keepWorldData` is updated to say rails and conveyors carry only ids, class names and tier names)

**Interfaces** (produces; shapes come from the captures and `frm-s2.ts`'s style; FRM is read only through its HTTP output):
```ts
export interface FrmRail { id: string; points: Array<[number, number]>; representable: boolean }   // metres, rounded to 0.01 (M3)
export function mapFrmRails(body: unknown): FrmRail[]                  // ID, SplineData[].x/y (cm) / 100; representable = >= 2 distinct consecutive whole-metre (JS Math.round) points inside +/-1,000,000
export const canonRails = (rails: FrmRail[]): string[]                  // sorted "id|x,y;x,y" lines, deterministic
export type FrmKind = "belt" | "lift" | "pipe";
export type NodeKind = "machine"|"splitter"|"merger"|"storage"|"station"|"extractor"|"consumer"|"junction"|"conveyor"|"other";
export interface FrmEdge { id: string; kind: FrmKind; from: string | null; fromKind: NodeKind | null; to: string | null; toKind: NodeKind | null; capacityPerMinute: number | null; tier: string; atX: number; atY: number }   // atX/atY metres, rounded to 0.01, NOT whole metres (M3)
export interface FrmConveyorSample { edges: FrmEdge[]; filtered: Array<{ id: string; className: string }> }
export function classifyConveyorEnd(id: string): NodeKind              // the adapter's function, ported (the table in Task 7)
export function mapFrmConveyors(belts: unknown, lifts: unknown, pipes: unknown): FrmConveyorSample
export const canonEdges = (s: FrmConveyorSample): string[]
export function mapFrmDronePorts(body: unknown): string[]              // ID of every record; never the label or name
export const round4 = (n: number): number                               // Math.round(n * 1e4) / 1e4, the shared "exact" for numbers (M4)
```
`mapFrmConveyors` follows the adapter: `from`/`to` are `null` when `ConnectedTo0/1` is `""`; kinds by `classifyConveyorEnd`; capacity = `ItemsPerMinute` (belts, lifts) or `Speed * 60` (pipes), `null` when absent, non-finite or negative, else `round4`; `tier = Name.slice(0, 64)`; a pipe record whose `ClassName` matches `/hyper/i` goes to `filtered` (reported) and not to `edges`; a record without both `ConnectedTo` fields or with an id or end id over 128 characters is dropped as the adapter does and counted in a returned `dropped` number (add `dropped: number` to `FrmConveyorSample`).

- [ ] **Step 1: Failing tests.** Fixtures: the three conveyor fixtures of Task 7 (read from `mod/.../Fixtures` the way `frm-s2.test.ts` loads `frm-getFactory-running.json`), `frm-getTrainRails.json` and a one-record drone-station body written in the test from `frm-getDroneStation-2026-10-25-unpaired-trimmed.json`'s shape (`ID` only needed). `mapFrmRails(frm-getTrainRails.json)`: track 1 yields points `[[0,0],[0.1,0.1],[2.5,0],[2.5,0],[5,10000000000],[10.5,-0.5]]` (centimetres divided by 100 and rounded to 0.01; 250.4 cm gives 2.5), and `representable === true` (the whole-metre points `0,0 / 3,0 / 11,0` are at least two distinct points inside the bounds; the out-of-bounds point does not count); track 2 `representable === false` (one distinct metre); track 3 `representable === false`. `mapFrmConveyors`: the miner-fed belt gives `from` of the miner id and `fromKind` `extractor`, `capacityPerMinute` equal to the capture's `ItemsPerMinute`, `tier` equal to its `Name`; the open-end belt gives `to: null, toKind: null`; the pipe gives `capacityPerMinute === round4(Speed * 60)`; a synthetic pipe record with `ClassName` `Build_PipeHyperExample_C` (a made-up value for the test) is in `filtered`; `classifyConveyorEnd` returns the same ten answers as the Task 7 `NativeConveyors.Kind` vectors (copy the vectors; this pins the TypeScript port to the C++ one). `mapFrmDronePorts` returns ids only (a record with a `Name` field of `FUEL 5` never leaks it: assert the output equals `["Build_DroneStation_C_2146572476"]`-style ids and contains no label).
- [ ] **Step 2-4:** run (fail), implement, run, whole Vitest + `npm run typecheck`. **Step 5:** commit `FRM oracles for rails, conveyors and drone ports (S3)`.

**Reviewer checks:** the table equals Task 7's; no label or player text in any output; geometry kept unrounded to 0.01 m (M3); FRM read only through HTTP output.

### Task 12: The S3 judges: rails and conveyors

**Files:**
- Create: `runs/native-parity/pass-s3.ts`, `harness/test/native-parity-s3-pass.test.ts`

**Interfaces:**
```ts
export const S3_RAILS_MAX_M = 1;            // owner, 2026-10-26: symmetric point-to-polyline distance
export const S3_AT_MAX_M = 1;               // owner: conveyor `at` within 1 m
export const S3_GAP_MS = 120_000;           // a post is compared with an FRM sample taken within this of it (nodes' and routes' rule)
export const S3_SHOW = 10;
export const S3_CURVE_M = 0.5;              // new: a track is "curved" when a point is this far from its end-to-end chord (NOT EXERCISED reporting only)
export const S3_LONG_TRACK_M = 999;         // new: reporting only, the point clamp
export interface Counts { reads: number; failed: number }
export interface Bracket<T> { startedAt: string; endedAt: string; value: T }
export interface RailsEvidence { posts: Array<{ at: string; status: number; data: unknown }>; frmSamples: Array<Bracket<string>>; distinct: Record<string, FrmRail[]>; frm: Counts }
export interface ConveyorsEvidence { posts: Array<{ at: string; status: number; data: unknown }>; frmSamples: Array<Bracket<string>>; distinct: Record<string, FrmConveyorSample>; frm: Counts; drone: { ids: string[]; frm: Counts } }
export interface S3Evidence { rails: RailsEvidence; conveyors: ConveyorsEvidence }
export interface S3Result { pass: boolean; reasons: string[]; never: string[] }
export function distToPolylineM(p: [number, number], poly: Array<[number, number]>): number   // min distance to any segment (clamped projection); a zero-length segment is the distance to its point
export function judgeRails(r: RailsEvidence): S3Result
export function judgeConveyors(c: ConveyorsEvidence, kindCheck?: { checked: number; differ: number; classes: string[] } | null): S3Result
```
**`judgeRails`** (owner's rails rule). For each accepted post: validate `data` with `RailsLayerDataSchema` (`harness/contract/src/map`); find the FRM sample before (ended within `S3_GAP_MS`) and after (started within it) as `judgeRoutes` does; none: a reason. The post passes against **either** side (as nodes and routes do), judged as: (1) **id sets equal**, FRM ids taken from `representable` tracks only; differences listed first `S3_SHOW` each way, FRM tracks that are not representable listed as reported (not failed); (2) for every common id, **every mod point** (`[x,y]` metres, integers) has `distToPolylineM(point, frmPoints) <= S3_RAILS_MAX_M + 1e-9` **and every FRM point** has `distToPolylineM(point, modPoints) <= S3_RAILS_MAX_M + 1e-9` (symmetric); failures listed with the id, the direction and the distance to 2 decimals (first `S3_SHOW`); (3) reasons print tracks compared, the maximum distance seen each way, the post count. At least one accepted post; `frm.failed > 0` with no sample fails. NOT EXERCISED (reported, never failing): `a curved track` when no FRM track has a point farther than `S3_CURVE_M` from its chord; `a track over 999 m (the point clamp)`; `a track the contract cannot hold` when none was reported.

**`judgeConveyors`** (owner's conveyors rule). Per accepted post: validate with `ConveyorsLayerDataSchema`; the same sample pairing; **id sets equal** between the mod's edges and `sample.edges` (`filtered` hypertubes excluded from FRM and listed as reported with the count); for every common id: `kind`, `from`, `to`, `fromKind`, `toKind`, `tier` strictly equal; `capacityPerMinute` equal after `round4` on both sides (`null` equals `null`); `at`: `Math.hypot(mod.at.x - frm.atX, mod.at.y - frm.atY) <= S3_AT_MAX_M + 1e-9`; each mismatch listed (first `S3_SHOW`) as `id field mod=<v> frm=<v>`. **Open ends** (an edge with `from` or `to` null) are counted and listed in the reasons (`N edges with an open end, agreed with FRM`); a disagreement is a `from`/`to` mismatch (Ruling M10). **Drone ports:** every end id whose class part starts with `Build_DroneStation` must be in `drone.ids` (else a failing reason naming the id); edges with such a `to` and such a `from` are counted; the kind-check line, when given, is reported (`kind table agrees with casts on X of Y ends; differing classes: ...`), never failed. NOT EXERCISED: any of `splitter`, `merger`, `junction`, `station` (as an end kind), `storage`, `extractor`, `consumer`, `machine` not seen among the mod's end kinds; `a lift edge`; `a pipe edge`; `an open end`; `two belt tiers` when fewer than two distinct belt `tier` values; `a belt into a drone port`; `a belt out of a drone port`; `a hypertube filtered by FRM` when `filtered` is empty. At least one accepted post; the FRM-failed rule as rails.

- [ ] **Step 1: Failing tests** (`native-parity-s3-pass.test.ts`; evidence built in the test):
  - `distToPolylineM`: point `(5,1)` to `[(0,0),(10,0)]` = 1; `(12,0)` to the same = 2; zero-length segment; a point on a vertex = 0.
  - `judgeRails`: identical polylines pass. FRM `[(0,0),(10,0)]` (cm 0..1000 -> metres) with mod `[[0,1],[10,1]]` **passes** (distance exactly 1); mod `[[0,2],[10,2]]` **fails**; mod `[[0,0],[5,0]]` **fails** on the FRM point `(10,0)` (the symmetric direction); a track id only in FRM fails, only in the mod fails; an FRM track with two points in the same metre (not representable) is reported and does not fail; an invalid post (a point as a string) fails; no post fails; a post with no FRM sample within 120 s fails; the other side of the pair rescues a post that differs from the first; NOT EXERCISED lines for no curved track and no track over 999 m (a 3-point FRM track bent 0.6 m from its chord removes the first line).
  - `judgeConveyors`: identical pass; `capacityPerMinute` 300 vs 300.0001 fails, 300 vs 300.00001 passes (4 decimals); `kind`, `tier`, `fromKind`, `to` each differing fails with its field named; `at` off by `(1,0)` passes and by `(1,1)` fails; an FRM-only pipe record in `filtered` is reported, not failed, while an FRM-only belt fails; an open end on both sides passes and is counted; open in the mod and connected in FRM fails; a mod end `Build_DroneStation_C_1` absent from `drone.ids` fails, present passes; no port edge gives both NOT EXERCISED lines; one tier gives `two belt tiers`; the kind-check line prints when given.
- [ ] **Step 2:** fail; **Step 3:** implement; **Step 4:** `npx vitest run test/native-parity-s3-pass.test.ts --reporter=dot`, then the whole suite and typecheck. **Step 5:** commit `The S3 judges: rails and conveyors`.

**Reviewer checks:** thresholds are the owner's (1 m both ways, exact fields, `at` 1 m), anything new (the 120 s gap reuse, `round4`, the representable rule, the 0.5 m and 999 m report-only constants) is named in a code comment as new and appears in Open questions; S1 and S2 judges and tests untouched; no field compared with tolerance except `at` and the geometry.

### Task 13: Wire the judges into `native-parity`: evidence, drive, verdicts, the log problems

**Files:**
- Modify: `runs/native-parity/pass.ts` (`NativeParity.s3?: S3Evidence`; `ReaderVerdicts` gains `rails?: Verdict; conveyors?: Verdict`; `judgeReaders` adds them when `c.s3` exists; `expectsS3(c, sources)` true when `c.s3` exists or `railsSource`/`conveyorsSource` is in the config; `runReasons` requires both keys `native` for an S3 run and the evidence present; `evaluate` adds `["R5 rails","rails",...]` and `["R6 conveyors","conveyors",...]` with `withProblems(..., railsLogProblems + logisticsLineProblems("rails"))` and the conveyors equivalent; `requirements` gains `"R5-rails"`, `"R6-conveyors"`; `export * from "./pass-s3"`), `harness/src/reader-lines.ts` (`logisticsLayers(lines)`, `logisticsLineProblems(lines, layer)`: needs a `Logistics read (game thread only):` line with `layers ... <layer>=1`; `kindCheck(lines)` parsed from `Conveyor kinds: ...`), `harness/src/core-runs/native-parity.ts` (`worldKey: "sprint3"`, `modConfig: NATIVE_SOURCES_S3`, `loadWorld!("sprint3")`; `keepWorldData: ["resourceNodes","trainRoutes","rails","conveyors"]`; three new samplers; capture `s3`), `harness/test/native-parity-pass.test.ts` (S3 cases), `docs/runner.md`
- Test: `harness/test/native-parity-s3-drive.test.ts` (like `native-parity-s2-drive.test.ts`)

**Drive (additions to the existing `drive`, the S1/S2 parts unchanged).** FRM is read as a black box through `get(...)`: `rails` sampler `getTrainRails` (timeout 15 s, `intervalMs 120_000`, value `{ hash, mapped }` with `hash = sha256(canonRails(mapped).join("\n")).slice(0,16)`); `conveyors` sampler three reads back to back (`/getBelts`, `/getLifts`, `/getPipes`, 20 s each, `intervalMs 120_000`, hash of `canonEdges`); `drone` sampler `/getDroneStation` (5 s, `intervalMs 60_000`) keeping ids only, and the union of ids over the run goes to `drone.ids`. The samplers start with the other FRM samplers (before the mod enrols) so a post from the first minutes has FRM neighbours on both sides. In the window loop, as for nodes: when a new accepted `rails` or `conveyors` post is seen (`recorder.entries` with `e.world?.layer === "rails"`/`"conveyors"` and status 200) take a fresh sample at once (`await rails.take()`), so the judge always has a sample within the gap. Posts for the evidence are those since the **start of enrolment**, not the window start, because the native first read (480 s +/-10% after Running) may land before the window opens: `capture.s3.rails.posts` filters `Date.parse(e.at) >= enrolledAt`. `capture.s3.*.distinct[hash] ??= mapped`. At the end `ctx.captures["readers.json"] = Pass.judgeReaders(capture, [])` as today (the final verdicts replace it in `evaluate`).

- [ ] **Step 1: Failing tests.** Drive test with the existing fake `get` and a fake recorder: the three S3 samplers are started and stopped; an accepted `rails` post triggers one extra `take()`; `capture.s3` is present and holds `distinct` once per hash; `drone.ids` holds ids only (a fake body with a `Name` of `FUEL 5` leaves no trace in the capture JSON: stringify and search). Pass tests: an S2-shaped capture (no `s3`, config without the new keys) re-judges exactly as before (reuse an existing fixture); an S3-shaped capture with both new keys native and a passing rails/conveyors evidence gives `rails`/`conveyors` verdicts in `readers.json`; a missing `Logistics read` line fails both with the log problem; `railsSource: "bridge"` in the config fails `runReasons`; rails failing does not change the conveyors verdict and the reverse.
- [ ] **Step 2-4:** run (fail), implement, run, the whole suite and typecheck. **Step 5:** commit `native-parity: the S3 samplers, evidence and verdicts (Test3)`.

**Reviewer checks:** S1 and S2 judges and tests byte-for-byte unchanged and green; S2-shaped stored evidence re-judges identically; evidence has ids, class names and tier names only (no port label); no `result.json` hand edit; FRM is read only through its HTTP endpoints; the 30-minute window, enrolment timing and `WINDOW_MINUTES` are the existing ones.

### Task 14: `tick-cost-native` with S3 on

**Files:**
- Modify: `harness/src/core-runs/tick-cost-native.ts` (`modConfig: NATIVE_SOURCES_S3`, `worldKey: "sprint3"`, `driveTickCost(ctx, "world-first", "sprint3")`; update `driveTickCost`'s key type and the doc comment), `harness/src/core-runs/tick-cost.ts` if its key type needs the third value, `runs/tick-cost-native/pass.ts` (`requiredStepReaders` adds `"Logistics read"` when `railsSource` or `conveyorsSource` is `native` in `captures["mod-config.json"]`; a `Logistics read` line must fall inside the measured window when so (the S2 window rule for the other lines); the S1 and S2 conditions are kept), `harness/test/tick-cost-native-pass.test.ts`, `harness/test/tick-cost-native-drive.test.ts`
- Test cases: S3-shaped evidence passes when everything is native and each longest step is 1.9 ms or less; fails for a `Logistics read` at 2.1 ms; fails for a missing `Logistics read` line when `railsSource` is native; fails when `conveyorsSource` is `bridge` in the captured config and the run is judged as an S3 run (a config with `railsSource` native and `conveyorsSource` bridge is accepted only as a rails-only run: the test asserts the required reader is still `Logistics read`); S1-shaped and S2-shaped evidence pass as before (assert against the existing fixtures).

- [ ] **Step 1-5:** failing tests, implement, `npx vitest run --reporter=dot`, `npm run typecheck`, `check-api`, `check-provenance`, final `judge-mod-test --min 184`, commit `tick-cost-native: the Logistics step line and Test3`.

**Reviewer checks:** `Base.evaluate` limits (tick median 95%, memory 100/25 MB, S12) imported unchanged; the ceiling is `> 2.0 ms`; S1/S2 evidence re-judges unchanged; the single `tick-cost-native` dispatch is the owner's.

### Task 15: Docs, the status table, the follow-up list

**Files:**
- Modify: `docs/native-readers.md` (rows Rails (R5) and Conveyors (R6) say `bridge` (default; conveyors: layer not sent) with their sprint; the keys `"railsSource"` and `"conveyorsSource"` in the override list; a "Where each reader stands (S3, after Task 15)" paragraph stating honestly that both are still `bridge` and that FRM is still needed for rails until the switch PR, and for nothing else once Conveyors is native; the shared pass and the pulled-forward rule; the `Test3` note; "ids of rebuilt belts change (survey line 142)"; the first native post of each layer changes its content hash once), `docs/runner.md` (the order of the sprint's runs), `api-sources.md` tidy, `mod-claims`-style notes are the dash repo's, not here
- Test: none beyond `check-api`, `check-provenance`, the whole Vitest suite and typecheck

- [ ] **Step 1:** write the docs. **Step 2:** all checks. **Step 3:** commit `Docs: S3 status, the shared pass, the Test3 note`.

**Reviewer checks:** the table says `bridge` for both readers until their own switch PR; no unproven claim; no path or private name.

---

## End of sprint: the runs and the follow-ups

After PRs S3-A to S3-D merge (or from the S3-D branch), the owner dispatches, in this order and once each:
1. `mod-test` on each PR branch (floors 169, 177, 184).
2. `s3-census` (Task 10; about 15 minutes; **the owner's approval**). The owner reads `census.json` and, only if a belt/lift tier prints `NO MATCH`, a one-constant ruling PR sets `NativeConveyorCapacityMode = ClassTable` before the next step.
3. `native-parity` (30-minute window, about 50 minutes plus the build; needs the backend host, `frmBaseUrl` and `sprint3World` in the runner config).
4. `tick-cost-native` (baseline boot then mod boot, on `Test3`).
Nothing else is run.

**`readers.json` must show, per reader** (`pass: true`, reasons listing what was compared and what was NOT EXERCISED):
- `power`, `players`, `resourceNodes`, `factory`, `rates`, `trains`, `stations`, `trainRoutes`: as S1/S2, now on `Test3`. A miss there is a finding for that reader's own owner ruling; it does not change the rails or conveyors verdicts.
- `rails`: id sets equal; every mod point within 1 m of FRM's polyline and every FRM point within 1 m of the mod's, for every common track; unrepresentable FRM tracks listed.
- `conveyors`: the same edges; `id`, `kind`, `from`, `to`, `fromKind`, `toKind`, `tier`, `capacityPerMinute` exact; `at` within 1 m; open ends and hypertubes reported; every drone-port end in FRM's port ids; the kind-check line reported.
`result.json` is red if any reader is; `tick-cost-native`'s `result.json` must pass with `Logistics read` at or under 2 ms.

**Owner's run checklist (before dispatch).** `Test3` staged as `sprint3-test3.sav` and `sprint3World` in the runner config (Task 1's checklist); FRM running on the test install; the backend host started in the owner's own session; the S3-A to S3-D branches' `mod-test` green; the census read.

**After S3 (named, not planned):**
- One small switch PR per passing reader: `DefaultRailsSource` and `DefaultConveyorsSource` (each flips `Bridge` to `Native`; a reader that missed stays `bridge`), with a `mod-claims.md` row each; and a dash evidence PR (`raw-sources/mod-native-readers/` results, the S3 note's folder).
- **R7, the removal release (ADR-0058 Decision 4)** is a separate PR that deletes the bridge, `frmPort`/`frmToken`, the per-reader overrides, the FRM fixtures and the dependency declaration. It merges **only on the owner's explicit word** after R0-R6 have passed, one 24-hour soak and the full-source similarity check. It is not in this plan.
- A `skipped: "tooManyEdges"` conveyors body for a layer over the cap (the contract has the alternative body) is not built here; an over-cap layer is simply not posted.

## Self-review

**Spec coverage.** Owner decision 1 (scope, switches, R7 excluded): Tasks 2, 3, 8, "After S3". 2 (rails rule): Tasks 11, 12 (`distToPolylineM`, symmetric, M3). 3 (conveyors rule): Tasks 7, 11, 12 (exact fields, `at` 1 m, open ends and hypertubes reported). 4 (12 rulings): the Rulings table. 5 (NOT EXERCISED): Tasks 12, 13. 6 (one run each, `Test3`, `sprint3World`): Tasks 1, 13, 14. 7 (2 ms, slicing): Tasks 5, 6, 9, 14. 8 (floor): Tasks 3, 5, 8. 9 (test-server-only facts): Task 10, Task 12, the 23-item table. Survey open questions 1-12: S1-S12; NEEDS VERIFICATION 1-23: the second table.

**Placeholder scan.** The only fill-ins are `Author:`/`Date:` in the two provenance files (`check-provenance` rejects a malformed date) and the Game-module code, which is described by exact symbols and sequence and written by the implementer against the open headers (as S2 did). Task 7's golden literals are written by the implementer from the captures and cross-checked, not from the code under test.

**Type consistency.** `FRailReading`, `FRailPointCm`, `RailSampleIntervals`, `RailSampleDistanceCm`, `BuildNativeRails`, `NativeRailSampler` (Task 4) are used in Tasks 5 and 6; `FLogisticsSnapshot`, `FLogisticsCounts`, `BeginLogisticsRead`, `StepLogisticsRead`, `LogisticsReadAbandoned`, `EndLogisticsRead`, `IsLogisticsReadOpen`, `FWorldContext::LogisticsBudget` (Task 5) in Tasks 6, 8, 9; `FConveyorReading`, `EConveyorKind`, `ClassifyConveyorEnd`, `ConveyorCapacityPerMinute`, `BuildNativeConveyors`, `NativeConveyorCapacityMode` (Task 7) in Tasks 8 and 9; `NATIVE_SOURCES_S3` (Task 2, completed in Task 8) in Tasks 10, 13, 14; `FrmRail`, `FrmEdge`, `FrmConveyorSample`, `classifyConveyorEnd`, `round4` (Task 11) in Task 12; log line names `Logistics read`, `Conveyor kinds`, `Conveyor drone ports`, `Conveyor tier census` (Task 8) in Tasks 10, 13, 14.

**Review Focus coverage.** Each line names its test above; the Game-module paths are covered by the census, parity and tick-cost runs, as in S1/S2.

## Open questions for the owner

1. **Geometry against FRM's raw points (Ruling M3).** The rails rule and `at` are judged against FRM's unrounded positions, not against FRM's own whole-metre values: two rounded polylines can differ by 1.41 m from rounding alone. Accept, or judge the contract form (stricter, may fail on rounding)?
2. **Open ends (Ruling M10).** I read "open ends are reported, not failed" as: an open end that both sources agree on is a normal edge, counted and listed; a disagreement (one source open, the other connected) is a `from`/`to` mismatch and fails. If you meant a disagreement should also only be reported, say so and Task 12 loses one comparison.
3. **`conveyorsSource = bridge` means "layer not sent" (Ruling M1),** because the bridge never sent conveyors. The switch PR makes it send. Accept?
4. **The shared pass pulls the other native layer forward (Ruling M6)** by up to about 100 s on the first read; the layers are still switched separately. Accept?
5. **Native first read 480 s for rails (Ruling M7)** while the bridge keeps 120 s, so the first native post is inside `tick-cost-native`'s measured span.
6. **New numbers not set by you:** `S3_GAP_MS` 120 s (the nodes/routes rule reused), `round4` for capacity, the report-only 0.5 m "curved" and 999 m "long track" lines.
7. **The census run (Task 10)** needs your approval to dispatch; it is read-only and about 15 minutes.
8. **`--min` floor:** 168 today; 169, 177, 184 as the PRs land (workflow-file limits, your call, as in S2).
