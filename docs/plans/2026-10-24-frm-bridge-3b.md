# FRM bridge, Plan 3B: mapLive and the world sender Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The mod sends `mapLive` (trains and stations) in its snapshots, and sends the `rails` and `resourceNodes` world layers to `POST /agent/v1/world/:layer`, exactly as the Node agent does, with no hitch on the host: every large body is hashed, parsed and gzipped a slice at a time on the worker, and an unchanged world layer costs only its download.

**Architecture:** Everything new is pure logic in `SatisDashCore`:
- `FFrmArrayParse`: one base class for "an FRM array of objects, read a slice at a time, written straight to compact JSON text". `FTrainsParse`, `FStationsParse`, `FRailsParse` and `FResourceNodesParse` are its four users. Each record is validated, mapped and written as it is read; nothing is kept but the output.
- `FFrmBridge` grows two things: a mapLive cycle (two reads, joined into one part) and a world slot per layer (read, hash, parse, outcome).
- `FWorldRead`: the world job. Stage 1 hashes the raw body (BLAKE3, 1 MiB a slice); when it equals the last settled raw hash the job ends there. Stage 2 parses; stage 3 hashes the mapped data, applies the 2,000,000-byte rule and gzips.
- `FWorldSender`: the schedule (boot offsets, cadences, jitter), the per-layer hashes, the posts, the retries, the 401.

`SatisDashGame` changes only `FTaskWorker` (a deferred-note fix). The driver attaches mapLive to the snapshot, routes any snapshot that carries it to the worker for encoding, and ticks the world sender.

**Tech Stack:** Unreal Engine 5.6.1-CSS, SML 3.12.0 (`harness/toolchain.json`), C++20, UE automation tests, and the TypeScript harness (Node, zod 4, Vitest 5).

**Spec:** `docs/specs/2026-10-23-frm-bridge-design.md`. This plan covers its "Order of work" steps 3 (`mapLive`) and 4 (the world sender with `rails` and `resourceNodes`). The Node agent it matches is satisfactory-dash `agent/` and `packages/game-adapter`; the mappers it ports (`map/railsMapper.ts`, `resourceNodesMapper.ts`, `trainsMapper.ts`, `stationsMapper.ts`, `coordinates.ts`, `nodeType.ts`, `sampler.ts`, `worldReader.ts`) are identical at the runner's checkout and at the contract pin `699aa6c1` (checked by diff). The contract was re-pinned in Plan 3A Task 1 and has `mapLive`, the three world layers and `WorldIngestResponseSchema`.

## Global Constraints

Everything in Plan 3A's Global Constraints still holds (pure Core, test naming, unity-build prefixes, case-sensitive protocol strings, no `check()` on outside data, logs only to `LogSatisDash` at `Display` or `Warning` and rate-limited, no FRM value in a log, the game thread never parses an FRM body, one FRM request in flight, one worker task at a time, `check-api` at `0 missing, 0 bad rows`, no local paths or personal data in committed files). Added or changed for 3B:

- **The game thread never hashes, parses or gzips an FRM body.** It may only start requests, move a reply's bytes into a job, take a finished part or outcome, join two small mapLive arrays (at most 256,000 bytes, one `Reserve` and three appends), and build and send one world request.
- **Large and small reads:**
  - A read with `bLarge` never starts while a large body is held (the queue enforces it, Task 1).
  - `getTrains` and `getTrainStation` are small live reads that are *not* `bMayPassLarge`: they wait behind a large parse. Only `getPlayer` passes one (spec "Load spreading").
  - `getTrainRails` and `getResourceNode` are large, in the world lane.
- **Caps (spec, `sampler.ts`, `map.ts`):**
  - mapLive: 500 trains and 500 stations kept (after bad coordinates are dropped, as the Node agent counts them); the whole part is left out when its JSON passes 256,000 UTF-8 bytes; every string cut to 200.
  - rails: 5,000 segments of 2 to 1,000 points; resource nodes: 5,000.
  - A world body is measured as the Node agent measures it: the whole `{"observedAt":...,"data":[...]}` in UTF-8 bytes against 2,000,000, before gzip.
  - Response caps and timeouts: 4 MB for `getTrains`, `getTrainStation`, `getTrainRails`, `getResourceNode`; 5 s for the two train reads and 15 s for the two large ones (the Node agent uses 5 s for every FRM read, `connection.ts:47`; the longer value is deliberate for the large bodies).
  - Parsing: 2,000 records per worker slice (`FrmSliceRecords`); raw-body hashing: 1 MiB per slice.
- **Bit-exact Node parity for coordinates:** `toWholeMetres` is `Math.round(x / 100)`. The division goes through a volatile divisor (UE builds with `/fp:fast`, see `FrmFactoryMetres`), and `Math.round` is `JsRound`, not `FMath::RoundToDouble`. Both are pinned by tests.
- **Output is written by hand, not through `TJsonWriter`.** `TJsonStringWriter` buffers every byte in a `TArray<uint8>` and copies it into the `FString` one character at a time in `Close()` (`JsonWriter.h:640-670`), which doubles the memory of a large part. The 3B parsers append to one `FString` with `Json::AppendString` and `FString::AppendInt`. (3A's `FFactoryParse` still uses the writer: see "Left for 3C".)
- **Hashes are BLAKE3** (`Hash/Blake3.h`; `FBlake3Hash` is 32 bytes, `operator==` compares them). The spec says SHA-256, but a hash never leaves the process and Core has no SHA-256 class. A hash of the mapped data covers exactly the `[...]` text of `data`, never `observedAt`.
- **A world layer's settled raw hash** (`RawHash`) is set only when the layer needs nothing more from that body: the backend accepted it, or its mapped data equals the last accepted data, or it was over the 2,000,000-byte cap. It is never set by a failed read, an invalid body, or a refused post, so those are tried again at the next cycle, as the Node agent does.
- Pass conditions (`runs/*/pass.ts`) and every existing limit are never changed without the owner's ruling (CLAUDE.md rule 3). Task 4 asks for one; the `--min` floor in `mod-test.yml` is raised only as the owner decides (Task 4, Task 10).
- Commits end with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.

## Review Focus

- **A train or station with a bad coordinate, a half-metre, or too many of them:**
  - examples: `x` of `1e12`; `150` and `-150` cm (rounds to 2 and -1); 600 valid trains; an invalid record after the 500th;
  - expected: the bad one is dropped and not counted against the 500; halves go toward +infinity; the first 500 are kept; an invalid record anywhere refuses the whole read, as zod does.
  - Pinned in `SatisDash.Core.FrmMap.Metres`, `SatisDash.Core.FrmMapLive.Rules`, `SatisDash.Core.FrmMapLive.Caps`, `SatisDash.Core.FrmMapLive.Golden`.
- **mapLive over 256,000 bytes, or half of it missing:**
  - expected: the whole part is left out with one warning; a failed or invalid read of either kind leaves it out; status, power, players and the factory keep flowing.
  - Pinned in `SatisDash.Core.FrmMapLive.Join`, `SatisDash.Core.FrmBridge.MapLiveCap`, `SatisDash.Core.FrmBridge.MapLive`, `SatisDash.Core.Driver.FrmMapLiveDown`.
- **A snapshot that carries mapLive is never serialised or gzipped on the game thread:**
  - Pinned in `SatisDash.Core.Driver.FrmMapLive`, `SatisDash.Core.Snapshot.MapLive`.
- **An unchanged world layer:**
  - expected: the first read after start always posts; the same raw body costs one hash step, no parse and no post; a changed raw body with the same mapped data parses but does not post; a changed layer posts.
  - Pinned in `SatisDash.Core.FrmWorld.Read`, `SatisDash.Core.FrmWorld.Slices`, `SatisDash.Core.WorldSender.Send`.
- **The schedule:**
  - expected: rails first read at 120 s and resource nodes at 480 s after the agent starts Running, each times a jitter in [0.9, 1.1]; then 600 s and 1,800 s times the same jitter, counted from the end of a cycle; nothing is read while the agent is not Running.
  - Pinned in `SatisDash.Core.WorldSender.Schedule`, `SatisDash.Core.Driver.World`, `SatisDash.Core.Driver.WorldStops`.
- **A world post that fails:**
  - expected: a transient failure or 429 retries the same body (no new FRM read) after `max(Retry-After, backoff)`; another 4xx gives up until the next cycle and is not remembered; a 401 halts the agent; a held body is dropped when the agent stops.
  - Pinned in `SatisDash.Core.WorldSender.Retry`, `.Refused`, `.Halt`, `SatisDash.Core.Driver.WorldHalt`.
- **World posts never delay snapshots:**
  - expected: a post starts only when no snapshot is queued or in flight, and never two at once.
  - Pinned in `SatisDash.Core.WorldSender.Behind`.
- **Hostile world bodies:**
  - examples: 1,001 points in a segment; 5,001 segments; a rail with `"SplineData": null`; `1e999`; a 1.1 MB body (two hash slices); trailing bytes; `[[[[...` 10,000 deep;
  - expected: refused or capped without a crash; the layer is left out or trimmed as the Node agent plus the contract would.
  - Pinned in `SatisDash.Core.FrmRails.Rules`, `.Caps`, `SatisDash.Core.FrmResourceNodes.Rules`, `SatisDash.Core.FrmWorld.Slices`.
- **Only getPlayer passes a large parse, and live goes before world even when the world read was queued first:**
  - Pinned in `SatisDash.Core.FrmQueue.LargeRule`, `SatisDash.Core.FrmQueue.Lanes` (rewritten in Task 1), `SatisDash.Core.FrmQueue.WorldEndpoints`, `SatisDash.Core.FrmBridge.World`.
- **The agent leaves Running mid-read:**
  - expected: no world read is used afterwards, no held body is posted, no reply from the old run changes the new run.
  - Pinned in `SatisDash.Core.FrmBridge.WorldStop`, `SatisDash.Core.Driver.WorldStops`.

## Not in 3B, and what 3B leaves for 3C

**Plan 3C** (written after 3B lands): `conveyors`, and the extended `parity` and `tick-cost` runs and `frm-down`, with the security review items. What 3B leaves for it, so that 3C starts from a short list:

1. **Conveyors.** `getBelts`, `getLifts`, `getPipes` (16 MB caps on belts and pipes), read one after another; the 50,000-edge `skipped: "tooManyEdges"` body and the `skipped: "unsupportedFrm"` body (`{"observedAt":...,"skipped":"..."}`, posted once per streak, replacing the backend's copy); the Node rule that after an accepted skip the layer's `sentHash` is cleared so the next good read posts even if equal. 3B's hooks: `EWorldLayer` and the `WorldLayer()` table take a third row (first read 300 s, every 600 s); `FWorldSender` keeps `RawHash`/`DataHash` per layer and posts a held `FEncodedBody`; `FWorldRead` is the place a multi-endpoint read would hash and join.
2. **In-game proof of the world sender.** The existing `enrol`, `revoke` and `command` runs are not built to last until the 8-minute resource-node read (check their length in a bundle before relying on them), and no judge reads a world post. 3B-2 records world posts as evidence (layer, items, bytes) but judges nothing; `parity` ("each world layer once") is the in-game check. If the owner wants proof before 3B-2 merges, that needs a long-running variant (owner's decision, see findings).
3. **`tick-cost` and `frm-down` limits.** The pass numbers are the owner's ruling. Measure around the largest reads: `getResourceNode` runs on the game thread inside FRM, so if its read hitches the fix is a longer cadence; and peak memory around the factory encode (3A final review, Minor #3 and its recommendation 3).
4. **3A's `FFactoryParse` memory.** It writes through `TJsonStringWriter`, which holds the part twice at `Close()` (see Global Constraints). At the 20,000-building cap that is tens of MB of transient memory on the worker. 3C's `tick-cost` memory evidence will show it; the fix, if the owner wants one, is the hand-written output 3B uses (`FFrmArrayParse` style), pinned by the existing `FrmFactory.Golden`.
5. **Rate limit with three layers.** The backend allows 20 world posts per 10 minutes per credential. With two layers each retrying at the 60 s backoff ceiling, that is exactly 20; with three it is 30. Retries honour `Retry-After`, so a 429 spaces them, but 3C should decide whether a held layer's backoff ceiling needs a floor.
6. **Deferred 3A minor findings 3B does not touch** (`3a-deferred` triage): ReplyTest whitespace; constant "oversized" log key; `SetLimit` ternary style; ConfigTest gaps; `IsHeaderSafe` allowing characters above 0x7F; no RED evidence on Task 3; untested token-streak reset; the reply lambda touching `this` before the generation check; no re-entrant or Code 0 handler test on the queue; vacuous `1e999` marker; unset-`TOptional` dereference in a test; depth-in-skip and element-257 tests; `Step(<=0)` loops; missing-field cases in the factory tests; `EncodeSnapshot` replacing silently; stop test split; `FrmEncodeAutoPause` separation; an old held job blocking the new run (bounded by one slice); no driver-level token-line test; destructor-only wait comments; `frmBaseUrl` without a port; URL parsed per write; `hashPlayers` scope.

## Owner-facing findings from the lookups for this plan

1. **Ruling needed: the enrol judge rejects `mapLive`.**
   - `runs/enrol/pass.ts:37` (`SNAPSHOT_KEYS`) allows `players` and `factory` since the owner's 2026-10-24 ruling, and rejects `mapLive`, as the owner said it would until 3B. Every 30 s snapshot after PR 3B-1 carries `mapLive` once FRM answers, so S13 would fail every world round.
   - Task 4 has the exact diff as a step marked **OWNER RULING REQUIRED**. It must not be made, and PR 3B-1 must not merge, before the owner rules first-hand.
2. **Judgement call to confirm: evidence would carry train and station names.** They are labels a player typed. The 2026-09-27 captures say none held a player name, but the recorder stores snapshot bodies in public evidence. Task 4 hashes `mapLive` names in the recorder, as it hashes player names and the session name. It is not a pass condition and is easy to reverse.
3. **The Node agent measures and trims less than the contract does.** Where the contract (`map.ts`) and the spec's table are stricter than the Node agent's code, the mod follows the contract, so the backend never refuses a layer the Node agent would have sent whole:
   - the Node agent does not cut world-layer strings (a 201-character id makes the backend refuse the whole layer); the mod cuts to 200 (`purity` and `nodeType` to 40, the contract's bound);
   - the Node agent does not cap a rail at 1,000 points or the layer at 5,000 items; the mod drops a segment with more than 1,000 points (a partial line would mislead) and keeps the first 5,000 items. Both are counted and logged once per streak;
   - none of these appear in a real world (the captured one has 65 rails and 608 nodes).
4. **Deliberate differences from the Node agent's behaviour** (all in the spec): the first world read comes 2 and 8 minutes after start, not at once; a world post waits for the snapshot queue to be idle; hashes are BLAKE3.
5. **`FFactoryParse` doubles its output in memory** (Left for 3C, item 4). Not caused by 3B; found while reading `JsonWriter.h` for 3B's output.
6. **`mod-test.yml` still runs `--min 80`.** 3A left the floor where it was while the suite grew to 104. Task 4 and Task 10 propose raising it to each PR's final count (120, then 142). A higher floor cannot make a run pass that would have failed, but it is a limit in a workflow file, so it is the owner's call.

## How the tests run

As in Plan 3A:

- Each task's test step runs locally:
  ```powershell
  pwsh scripts/test-mod.ps1 -Project <sml-project> -Engine <engine-root> -Out <tmp>\mod-test
  cd harness; npm run harness -- judge-mod-test <tmp>\mod-test --min <N>
  ```
  `<sml-project>` holds `FactoryGame.uproject` and `<engine-root>` holds `Engine\`. Both are the executor's own and are never committed. `<N>` is the task's expected test count; **104 on main today**, and each task states its new count.
- `check-api`: `cd harness; npm run harness -- check-api` ends every task at `0 missing, 0 bad rows`. The rows a task lists were looked up for this plan in the installed 5.6.1 engine on 2026-10-24 (line numbers below); anything else `check-api` reports is looked up in the pinned headers and added in the same task.
- The harness tests: `cd harness; npx vitest run <file> --reporter=dot`, then `npx vitest run --reporter=dot` and `npm run typecheck` once at the end of each PR.
- Before each PR merges, the owner dispatches `mod-test` on the branch (`gh workflow run mod-test.yml --ref <branch>`) and approves its `ue-runner` deployment, then the core runs `enrol,revoke,command` through `run-backend.ps1`. PR 3B-1 runs them only after the owner's Task 4 ruling.

**PRs:** Tasks 1-4 are **PR 3B-1** (spec step 3, `mapLive`); Tasks 5-10 are **PR 3B-2** (spec step 4, the world sender). Test counts: 104 now; 107 after Task 1; 115 after Task 2; 120 after Task 3 (PR 3B-1 done); 123 after Task 5; 129 after Task 6; 133 after Task 7; 139 after Task 8; 142 after Task 9 and Task 10 (PR 3B-2 done).

## File structure

`mod/...` is `mod/SatisDashAgent/Source`.

| File | New/Modified | Responsibility |
| --- | --- | --- |
| `mod/.../SatisDashCore/Public/SatisDash/FrmQueue.h`, `Private/FrmQueue.cpp` | Modified (T1) | A large read never starts while a large body is held |
| `mod/.../SatisDashGame/Private/TaskWorker.h` | Modified (T1) | A second launch waits for the first |
| `mod/.../SatisDashCore/Public/SatisDash/FrmBridge.h`, `Private/FrmBridge.cpp` | Modified (T1, T3, T7) | Part age from the read; the mapLive cycle; the world slots |
| `mod/.../SatisDashCore/Private/Snapshot.cpp`, `Public/SatisDash/Snapshot.h` | Modified (T1, T3, T6) | Splice guard and table; `MapLiveJson`; `ClampStringTo` |
| `mod/.../SatisDashCore/Public/SatisDash/JsonUtil.h`, `Private/JsonUtil.cpp` | Modified (T2) | `Json::AppendString` |
| `mod/.../SatisDashCore/Public/SatisDash/FrmMap.h`, `Private/FrmMap.cpp` | New (T2) | `JsRound`, `ToWholeMetres`, `InWholeMetreBounds`, `ReadFrmLocation` |
| `mod/.../SatisDashCore/Public/SatisDash/FrmRecords.h`, `Private/FrmRecords.cpp` | New (T2, T6) | `FFrmArrayParse`, `WorldBodyPrefix` |
| `mod/.../SatisDashCore/Public/SatisDash/FrmMapLive.h`, `Private/FrmMapLive.cpp` | New (T2) | `FTrainsParse`, `FStationsParse`, `JoinMapLive` |
| `mod/.../SatisDashCore/Public/SatisDash/Frm.h` | Modified (T3, T5) | Four endpoints |
| `mod/.../SatisDashCore/Public/SatisDash/Requests.h`, `Private/Requests.cpp`, `Responses.h`, `Private/Responses.cpp` | Modified (T5) | `BuildWorldRequest`, `ParseWorldResponse` |
| `mod/.../SatisDashCore/Public/SatisDash/FrmRails.h`, `Private/FrmRails.cpp` | New (T6) | `FRailsParse` |
| `mod/.../SatisDashCore/Public/SatisDash/FrmResourceNodes.h`, `Private/FrmResourceNodes.cpp` | New (T6) | `FResourceNodesParse`, `MapNodeType` |
| `mod/.../SatisDashCore/Public/SatisDash/FrmWorld.h`, `Private/FrmWorld.cpp` | New (T7) | `EWorldLayer`, the layer table, `FWorldOutcome`, `FWorldRead` |
| `mod/.../SatisDashCore/Public/SatisDash/WorldSender.h`, `Private/WorldSender.cpp` | New (T8) | `FWorldSender` |
| `mod/.../SatisDashCore/Public/SatisDash/Driver.h`, `Private/Driver.cpp` | Modified (T3, T9) | mapLive attached and routed to the worker; the world sender ticked |
| `mod/.../SatisDashCoreTests/Private/*Test.cpp` | New/Modified | Tests |
| `mod/.../SatisDashCoreTests/Fixtures/frm-getTrains.json`, `frm-getTrainStation.json`, `frm-mapLive.expected.json` | New (T2) | mapLive fixtures |
| `mod/.../SatisDashCoreTests/Fixtures/frm-getTrainRails.json`, `frm-getResourceNode.json` and their `.expected.json` | New (T6) | World fixtures |
| `harness/scripts/frm-golden.ts` | Modified (T4, T6) | `mapLive`, `rails`, `resourceNodes` kinds |
| `harness/src/recorder.ts`, `harness/test/recorder.test.ts` | Modified (T4, T10) | `mapLive` names hashed; world posts recorded |
| `harness/test/snapshot-fixtures.test.ts`, `harness/test/enrol-pass.test.ts` | Modified (T4, T6) | Fixtures against the contract; the S13 test |
| `runs/enrol/pass.ts` | Modified **only after the owner's ruling** (T4) | S13 allows `mapLive` |
| `.github/workflows/mod-test.yml` | Modified **if the owner agrees** (T4, T10) | `--min` |
| `api-sources.md`, `README.md` | Modified | Rows; what the mod sends |

---

## PR 3B-1: mapLive (spec step 3)

### Task 1: Follow-ups from Plan 3A's reviews that 3B builds on

Four small fixes, one commit, so the queue and the bridge are right before 3B adds to them. Each is a deferred item from the 3A reviews that 3B touches.

**Files:**
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Private/FrmQueue.cpp` (`Pump`)
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Frm.h` (the `bLarge` comment)
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/FrmBridge.h`, `Private/FrmBridge.cpp` (part age)
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Private/Snapshot.cpp` (`SnapshotToJson` splice)
- Modify: `mod/SatisDashAgent/Source/SatisDashGame/Private/TaskWorker.h`
- Test: `FrmQueueTest.cpp`, `FrmBridgeTest.cpp`, `SnapshotTest.cpp` (all under `SatisDashCoreTests/Private/`)

**Interfaces:**
- Produces: `FFrmQueue::Pump` blocks a `bLarge` read while `bLargeBusy`, even one also marked `bMayPassLarge`. A ready part's age is measured from the FRM reply, not from the harvest. `SnapshotToJson` splices its finished-JSON parts from a table (Task 3 adds a row) and never corrupts a snapshot whose root is not the condensed writer's shape. `FTaskWorker::Launch` never orphans a running task.

- [ ] **Step 1: Write the failing tests**

`FrmQueueTest.cpp`: add after the existing `FrmQueueTestWorld` constant

```cpp
	// Marked both large and passable: a mistake in an endpoint row must not let a second large body in.
	constexpr FFrmEndpoint FrmQueueTestBoth{ TEXT("getTestBoth"), EFrmLane::Live, true, true, FrmSmallCapBytes, 5.f };
```

and, at the end of the file:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmQueueLargeRuleTest, "SatisDash.Core.FrmQueue.LargeRule", SatisDash::Test::Flags)

bool FFrmQueueLargeRuleTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FFakeTransport Net;
	FFrmQueue FrmLanes{ Net };
	FrmLanes.Enqueue(FrmQueueRead(FrmQueueTestBoth, nullptr));
	FrmLanes.Pump(8080, FString(), true);
	TestEqual(TEXT("a large read never starts while a large body is held, even if marked passable"), Net.Open.Num(), 0);
	FrmLanes.Pump(8080, FString(), false);
	TestEqual(TEXT("and starts once the parse is done"), Net.pending(TEXT("/getTestBoth")), 1);
	return true;
}
```

Also make the existing lane-order test able to fail (3A deferred: "lane-order test that can't fail"): in `FFrmQueueLanesTest::RunTest`, replace everything after `TestEqual(TEXT("then goes"), Net.pending(TEXT("/getTestWorld")), 1);` (that is, from `FrmLanes.Enqueue(FrmQueueRead(FrmGetPlayer, &Codes));` through `return true;`) with this, which answers the world read, then queues the world read *before* the live one:

```cpp
	Net.answer(TEXT("/getTestWorld"), SatisDash::Test::Reply(200, TEXT("[]")));
	// The world read is queued first and the live read second: the live lane must still go first.
	FrmLanes.Enqueue(FrmQueueRead(FrmQueueTestWorld, &Codes));
	FrmLanes.Enqueue(FrmQueueRead(FrmGetPlayer, &Codes));
	FrmLanes.Pump(8080, FString(), false);
	TestEqual(TEXT("live before world, though the world read was queued first"), Net.pending(TEXT("/getPlayer")), 1);
	TestEqual(TEXT("world still queued"), FrmLanes.Queued(), 1);
	return true;
```

(the old block enqueued the player before the world read, so a queue that ignored lanes passed it.)

`FrmBridgeTest.cpp`, after `FFrmBridgeFactoryTest`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmBridgeReadAgeTest, "SatisDash.Core.FrmBridge.ReadAge", SatisDash::Test::Flags)

bool FFrmBridgeReadAgeTest::RunTest(const FString& Parameters)
{
	FFrmBridgeFixture F;
	F.Worker.bHold = true;
	F.Bridge.RequestPass(false);
	F.tick();
	F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, FrmBridgeOnePlayer));
	F.tick();      // launches the parse; the fake worker holds it
	F.tick(40);    // 20 s go by while the worker is busy
	F.Worker.release();
	F.tick();      // harvested now
	TestFalse(TEXT("a part is as old as the FRM read, not as old as its harvest"), F.Bridge.TakePlayers(F.T, 10).IsSet());
	return true;
}
```

`SnapshotTest.cpp`, at the end:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnapshotSpliceTest, "SatisDash.Core.Snapshot.Splice", SatisDash::Test::Flags)

bool FSnapshotSpliceTest::RunTest(const FString& Parameters)
{
	// SnapshotToJson splices finished part JSON before the root's closing brace, which is only right for the condensed
	// writer's shape. Pin that shape here so a change in Json::Write fails loudly.
	const TSharedRef<FJsonObject> One = MakeShared<FJsonObject>();
	One->SetNumberField(TEXT("a"), 1);
	SatisDash::Test::SameText(*this, TEXT("Json::Write is condensed and ends with the root's brace"), SatisDash::Json::Write(One), TEXT("{\"a\":1}"));

	FSnapshot Snapshot = Base();
	Snapshot.PlayersJson = TEXT("{\"available\":false,\"players\":[]}");
	Snapshot.FactoryJson = TEXT("{\"buildings\":[]}");
	const FString Json = SnapshotToJson(Snapshot);
	const TSharedPtr<FJsonObject> Parsed = SatisDash::Json::ParseObject(Json);
	TestTrue(TEXT("valid JSON with both parts spliced in"), Parsed.IsValid() && Parsed->HasField(TEXT("players")) && Parsed->HasField(TEXT("factory")));
	TestTrue(TEXT("and the root's own fields kept"), Parsed.IsValid() && Parsed->HasField(TEXT("reachable")));
	TestTrue(TEXT("players before factory"), Json.Find(TEXT("\"players\":")) < Json.Find(TEXT("\"factory\":")));
	return true;
}
```

- [ ] **Step 2: Run them to see them fail**

Run the mod tests (How the tests run) with `--min 107`.
Expected: `FrmQueue.LargeRule` fails (the read starts while busy), `FrmBridge.ReadAge` fails (the part is fresh because its age starts at the harvest), `FrmQueue.Lanes` still passes (and now can fail), `Snapshot.Splice` passes (it pins today's behaviour).

- [ ] **Step 3: Implement**

`FrmQueue.cpp`, in `Pump`, replace

```cpp
			if (bLargeBusy && !(*Lane)[I].Endpoint->bMayPassLarge)
			{
				continue;
			}
```

with

```cpp
			const FFrmEndpoint& Endpoint = *(*Lane)[I].Endpoint;
			// A large body is held (parsing): only a small read marked bMayPassLarge (getPlayer) may start. A large read never
			// may, even if a table row marked it both: at most one large raw body is held (spec "Load spreading").
			if (bLargeBusy && (!Endpoint.bMayPassLarge || Endpoint.bLarge))
			{
				continue;
			}
```

`Frm.h`: change the `bLarge` comment to `/** A large body: hashed and parsed in slices on the worker, and at most one held at a time; FFrmQueue never starts a large read while one is held (spec "Memory"). */`.

`FrmBridge.h`: add `double ReadAt = 0;` to `FPlayersJob` and `FFactoryJob` (after `bRan`), and change `FReady`'s comment to `/** At: when FRM answered (the read), so a part ages from the data, not from its parse. */`. Keep `LastNow`.

`FrmBridge.cpp`:
- in `RequestPass`'s getPlayer handler, after `Players->Body = MoveTemp(Reply.Body);` add `Players->ReadAt = LastNow;`;
- in the getFactory handler, after `Factory = MakeShared<FFactoryJob>(MoveTemp(Reply.Body));` add `Factory->ReadAt = LastNow;`;
- in `Harvest`, use the job's time: `PlayersReady = FReady{ MoveTemp(Players->Json.GetValue()), Players->ReadAt };` and `FactoryReady = FReady{ Factory->Parse.TakeJson(), Factory->ReadAt };`.

(`LastNow` is set at the top of `Tick`, so a reply is stamped at most one tick, 0.5 s, late: it was an unused field before.)

`Snapshot.cpp`, replace the block from `FString Json = SatisDash::Json::Write(Root);` to the end of the function with:

```cpp
	FString Json = SatisDash::Json::Write(Root);
	// Parts that arrive as finished JSON, spliced in this order before the root's closing brace.
	struct FPart { const TCHAR* Key; const TOptional<FString>* Value; };
	const FPart Parts[] = {
		{ TEXT(",\"players\":"), &Snapshot.PlayersJson },
		{ TEXT(",\"factory\":"), &Snapshot.FactoryJson },
	};
	constexpr int32 KeySlack = 16; // each key (",\"players\":") is shorter than this
	int32 Extra = 0;
	for (const FPart& Part : Parts)
	{
		Extra += Part.Value->IsSet() ? KeySlack + Part.Value->GetValue().Len() : 0;
	}
	if (Extra == 0)
	{
		return Json;
	}
	// The root always has fields, and the condensed writer ends it with '}' (pinned by Snapshot.Splice). Anything else
	// would be corrupted by the splice: send the snapshot without the parts instead.
	if (!Json.EndsWith(TEXT("}"), ESearchCase::CaseSensitive))
	{
		return Json;
	}
	Json.LeftChopInline(1);
	Json.Reserve(Json.Len() + 1 + Extra); // one allocation for the whole result, each piece appended on its own
	for (const FPart& Part : Parts)
	{
		if (Part.Value->IsSet())
		{
			Json += Part.Key;
			Json += Part.Value->GetValue();
		}
	}
	Json.AppendChar(TEXT('}'));
	return Json;
}
```

`TaskWorker.h`, replace `Launch` with (IWorker's contract is "only while IsIdle()"; this keeps a violation from orphaning a running task that the destructor would then not wait for):

```cpp
		virtual void Launch(FWorkFn Work) override
		{
			Wait(); // a no-op unless a caller broke the contract (Launch only while IsIdle()): never orphan a running task
			Current = UE::Tasks::Launch(TEXT("SatisDashFrm"), MoveTemp(Work), UE::Tasks::ETaskPriority::BackgroundLow);
		}
```

- [ ] **Step 4: Run the tests**

Expected: `judge-mod-test --min 107` passes. `check-api`: `FString::EndsWith` may be reported; look it up in `UnrealString.h.inl` and add its row. `SatisDashGame` has no automation test: its compile is checked by the plugin build `test-mod.ps1` runs (and `mod-test` in CI).

- [ ] **Step 5: Commit**

```bash
git add mod/SatisDashAgent/Source api-sources.md
git -c core.safecrlf=false commit -m "Queue never starts a second large read, parts age from the FRM read, splice guarded, launch never orphans a task (FRM bridge 3B Task 1)" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

### Task 2: Coordinates, the sliced array reader, trains and stations

**Files:**
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/JsonUtil.h`, `Private/JsonUtil.cpp`
- Create: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/FrmMap.h`, `Private/FrmMap.cpp`
- Create: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/FrmRecords.h`, `Private/FrmRecords.cpp`
- Create: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/FrmMapLive.h`, `Private/FrmMapLive.cpp`
- Create: `mod/SatisDashAgent/Source/SatisDashCoreTests/Fixtures/frm-getTrains.json`, `frm-getTrainStation.json`, `frm-mapLive.expected.json`
- Test: `JsonUtilTest.cpp`, new `FrmMapTest.cpp`, new `FrmMapLiveTest.cpp`
- Modify: `api-sources.md`

**Interfaces:**
- Consumes: `FTokenReader` (`JsonTokens.h`: `Next`, `SkipValue`, `Name`, `String`, `Number`, `Boolean`, `AtCleanEnd`), `ClampString` (`Snapshot.h`).
- Produces:
  - `Json::AppendString(FString& Out, const FString& Value)`: appends `Value` as a quoted, escaped JSON string (`"`, `\`, `\n`, `\r`, `\t`, `\b`, `\f`, other controls below 0x20 as `\u00xx`; everything else as is).
  - `double JsRound(double)`, `double ToWholeMetres(double)`, `bool InWholeMetreBounds(double)`, `bool ReadFrmLocation(FTokenReader&, EJsonNotation Started, double& OutX, double& OutY)`.
  - `FFrmArrayParse` (`Step`, `TakeJson`, `PeekJson`, `DataFrom`, `DataEnd`, `Utf8Bytes`, `Kept`, `Dropped`, `Over`), with `ReadRecord(bool bRoom)` and `BeginRecord()` for subclasses; `FrmSliceRecords = 2000`.
  - `FTrainsParse(TArray<uint8>)`, `FStationsParse(TArray<uint8>)` (each yields the bare JSON array), `TOptional<FString> JoinMapLive(TrainsArray, TrainsBytes, StationsArray, StationsBytes)`, `MapLiveMaxItems = 500`, `MapLiveMaxBytes = 256000`, `MapLiveWrapperBytes = 23`.

The Node agent's rules being ported (`getTrains`/`getTrainStation` through `rawSchemas.ts`, `trainsMapper.ts`, `stationsMapper.ts`, `coordinates.ts`, then `sampler.ts` `conformMapLive`):
- A body is valid only if it is an array of objects that all have a string `ID` of at least 1 character, a string `Name`, (trains) a string `Status`, and a `location` with finite numbers `x`, `y`, `z` (and, if present, a finite `rotation`). One bad record refuses the whole body; unknown fields are skipped.
- A valid record whose rounded `x` or `y` (metres) is not finite or is past +/-1,000,000 is dropped and does not count towards the 500.
- The first 500 trains and 500 stations are kept; strings are cut to 200.

- [ ] **Step 1: The fixtures**

`Fixtures/frm-getTrains.json` (neutral names; the extra fields are the shape of the 2026-09-27 capture, and the third train is far out of bounds):

```json
[
{"ID":"BP_Train_C_1","Name":"Test Train A","ClassName":"BP_Train_C","location":{"x":-102587.95567750931,"y":-147300,"z":4400.01318359375,"rotation":270,"pitch":0},"TotalMass":499300,"PayloadMass":139300,"ForwardSpeed":14.6462,"Status":"Self-Driving","TimeTable":[{"StationName":"Test Station A"},{"StationName":"Test Station B"}],"Cars":[{"ID":"BP_Car_1","Name":"Locomotive","location":{"x":1,"y":2,"z":3},"Inventory":[{"Name":"Iron Plate","Amount":10}]}]},
{"ID":"BP_Train_C_2","Name":"Test Train B","ClassName":"BP_Train_C","location":{"x":150,"y":-150,"z":0},"Status":"Parked"},
{"ID":"BP_Train_C_3","Name":"Far Away","ClassName":"BP_Train_C","location":{"x":1.0e12,"y":0,"z":0},"Status":"Manual"}
]
```

`Fixtures/frm-getTrainStation.json`:

```json
[
{"ID":"Build_TrainStation_C_1","Name":"Test Station A","ClassName":"Build_TrainStation_C","location":{"x":-100050,"y":-147350,"z":4400,"rotation":90},"CargoInventory":[],"PowerInfo":{"CircuitGroupID":1,"PowerConsumed":0}},
{"ID":"Build_TrainStation_C_2","Name":"Test Station B","location":{"x":5049,"y":-5051,"z":0}}
]
```

`Fixtures/frm-mapLive.expected.json` is written by the Node agent's own code in Step 2 of Task 4 (the golden script). Until then write what it must contain, worked out by hand (halves go toward +infinity: -1000.5 is -1000, -1473.5 is -1473, 1.5 is 2, -1.5 is -1; the third train is dropped):

```json
{
  "trains": [
    { "id": "BP_Train_C_1", "name": "Test Train A", "x": -1026, "y": -1473, "status": "Self-Driving" },
    { "id": "BP_Train_C_2", "name": "Test Train B", "x": 2, "y": -1, "status": "Parked" }
  ],
  "stations": [
    { "id": "Build_TrainStation_C_1", "name": "Test Station A", "x": -1000, "y": -1473 },
    { "id": "Build_TrainStation_C_2", "name": "Test Station B", "x": 50, "y": -51 }
  ]
}
```

- [ ] **Step 2: Write the failing tests**

`JsonUtilTest.cpp`, at the end:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJsonAppendStringTest, "SatisDash.Core.Json.AppendString", SatisDash::Test::Flags)

bool FJsonAppendStringTest::RunTest(const FString& Parameters)
{
	auto Quote = [](const FString& Value) { FString Out; SatisDash::Json::AppendString(Out, Value); return Out; };
	SatisDash::Test::SameText(*this, TEXT("empty"), Quote(FString()), TEXT("\"\""));
	SatisDash::Test::SameText(*this, TEXT("plain"), Quote(TEXT("Iron Ore")), TEXT("\"Iron Ore\""));
	SatisDash::Test::SameText(*this, TEXT("quote and backslash"), Quote(TEXT("a\"b\\c")), TEXT("\"a\\\"b\\\\c\""));
	SatisDash::Test::SameText(*this, TEXT("named controls"), Quote(TEXT("\n\r\t\b\f")), TEXT("\"\\n\\r\\t\\b\\f\""));
	SatisDash::Test::SameText(*this, TEXT("other controls as \\u00xx, lower case"), Quote(FString::Printf(TEXT("%c%c"), TCHAR(0x01), TCHAR(0x1F))), TEXT("\"\\u0001\\u001f\""));
	const FString Accented = FString::Printf(TEXT("Z%cge"), TCHAR(0x00FC));
	SatisDash::Test::SameText(*this, TEXT("non-ASCII passes through"), Quote(Accented), TEXT("\"") + Accented + TEXT("\""));
	// Round trip through the engine's parser: what is written reads back as the same text.
	const FString Nasty = FString::Printf(TEXT("a\"\\/\n%cz"), TCHAR(0x07));
	const TSharedPtr<FJsonObject> Back = SatisDash::Json::ParseObject(TEXT("{\"v\":") + Quote(Nasty) + TEXT("}"));
	SatisDash::Test::SameText(*this, TEXT("round trip"), Back.IsValid() ? Back->GetStringField(TEXT("v")) : FString(), Nasty);
	return true;
}
```

`FrmMapTest.cpp`:

```cpp
#include "SatisDash/FrmMap.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	/** Reads one location value; false when ReadFrmLocation refused it or anything follows it. */
	bool FrmMapLocation(const TCHAR* Json, double& X, double& Y)
	{
		const TArray<uint8> Bytes = SatisDash::Test::Utf8(Json);
		FTokenReader Reader{ Bytes };
		EJsonNotation N;
		return Reader.Next(N) && ReadFrmLocation(Reader, N, X, Y) && Reader.AtCleanEnd();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmMapMetresTest, "SatisDash.Core.FrmMap.Metres", SatisDash::Test::Flags)

bool FFrmMapMetresTest::RunTest(const FString& Parameters)
{
	// JavaScript's Math.round: a half goes toward +infinity.
	TestEqual(TEXT("2.5 is 3"), JsRound(2.5), 3.0);
	TestEqual(TEXT("-2.5 is -2"), JsRound(-2.5), -2.0);
	TestEqual(TEXT("-1.5 is -1"), JsRound(-1.5), -1.0);
	TestEqual(TEXT("0.5 is 1"), JsRound(0.5), 1.0);
	TestEqual(TEXT("-0.5 is 0"), JsRound(-0.5), 0.0);
	TestEqual(TEXT("-0.4 is 0"), JsRound(-0.4), 0.0);
	TestEqual(TEXT("0.49999999999999994 is 0 (floor(x + 0.5), which FMath::RoundToDouble is, gets this one wrong)"), JsRound(0.49999999999999994), 0.0);
	TestEqual(TEXT("a whole number stays"), JsRound(7.0), 7.0);
	TestEqual(TEXT("a huge value stays"), JsRound(1e300), 1e300);
	TestTrue(TEXT("NaN stays NaN"), FMath::IsNaN(JsRound(std::numeric_limits<double>::quiet_NaN())));

	// toWholeMetres: Math.round(x / 100).
	TestEqual(TEXT("150 cm is 2 m"), ToWholeMetres(150.0), 2.0);
	TestEqual(TEXT("-150 cm is -1 m"), ToWholeMetres(-150.0), -1.0);
	TestEqual(TEXT("-100050 cm is -1000 m"), ToWholeMetres(-100050.0), -1000.0);
	TestEqual(TEXT("-102587.95567750931 cm is -1026 m"), ToWholeMetres(-102587.95567750931), -1026.0);
	TestEqual(TEXT("49 cm is 0 m"), ToWholeMetres(49.0), 0.0);

	// inWholeMetreBounds: finite and within +/-1,000,000.
	TestTrue(TEXT("0"), InWholeMetreBounds(0.0));
	TestTrue(TEXT("the bound itself"), InWholeMetreBounds(1000000.0) && InWholeMetreBounds(-1000000.0));
	TestFalse(TEXT("one past it"), InWholeMetreBounds(1000001.0) || InWholeMetreBounds(-1000001.0));
	TestFalse(TEXT("infinity"), InWholeMetreBounds(std::numeric_limits<double>::infinity()));
	TestFalse(TEXT("NaN"), InWholeMetreBounds(std::numeric_limits<double>::quiet_NaN()));
	TestFalse(TEXT("1e12 cm is out"), InWholeMetreBounds(ToWholeMetres(1e12)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmMapLocationTest, "SatisDash.Core.FrmMap.Location", SatisDash::Test::Flags)

bool FFrmMapLocationTest::RunTest(const FString& Parameters)
{
	double X = 0;
	double Y = 0;
	TestTrue(TEXT("x, y and z"), FrmMapLocation(TEXT("{\"x\":1,\"y\":2,\"z\":3}"), X, Y));
	TestTrue(TEXT("x and y are read"), X == 1.0 && Y == 2.0);
	TestTrue(TEXT("rotation, pitch and unknown nested fields are fine"), FrmMapLocation(TEXT("{\"x\":-4.5,\"y\":6,\"z\":0,\"rotation\":270,\"pitch\":0,\"extra\":{\"a\":[1,{\"b\":null}]}}"), X, Y));
	TestTrue(TEXT("the values"), X == -4.5 && Y == 6.0);
	TestTrue(TEXT("a repeated key: the last one wins, as JSON.parse does"), FrmMapLocation(TEXT("{\"x\":1,\"x\":5,\"y\":2,\"z\":3}"), X, Y) && X == 5.0);
	// Each refused, as zod's RawFrmLocationSchema refuses it.
	for (const TCHAR* Bad : {
		TEXT("{\"x\":1,\"y\":2}"),                        // z is required
		TEXT("{\"y\":2,\"z\":3}"),
		TEXT("{\"x\":\"1\",\"y\":2,\"z\":3}"),            // a string
		TEXT("{\"x\":1,\"y\":null,\"z\":3}"),
		TEXT("{\"x\":1e999,\"y\":2,\"z\":3}"),            // not finite
		TEXT("{\"x\":1,\"y\":2,\"z\":3,\"rotation\":null}"),
		TEXT("{\"x\":1,\"y\":2,\"z\":3,\"rotation\":1e999}"),
		TEXT("[1,2,3]"), TEXT("5"), TEXT("{\"x\":1,\"y\":2,\"z\":3} 1") })
	{
		TestFalse(FString::Printf(TEXT("refused: %s"), Bad), FrmMapLocation(Bad, X, Y));
	}
	return true;
}
```

`FrmMapLiveTest.cpp`:

```cpp
#include "SatisDash/FrmMapLive.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	/** What one parse produced; bOk is false when the body was refused. */
	struct FFrmMapLiveRun
	{
		bool bOk = false;
		FString Json;
		int32 Kept = 0;
		int32 Dropped = 0;
		int32 Over = 0;
		int32 Steps = 0;
		int64 Bytes = 0;
	};

	template <typename TParse>
	FFrmMapLiveRun FrmMapLiveOf(const FString& Json, int32 Slice = FrmSliceRecords)
	{
		TParse Parse{ SatisDash::Test::Utf8(Json) };
		FFrmMapLiveRun Run;
		FFrmArrayParse::EStep Step = FFrmArrayParse::EStep::More;
		while (Step == FFrmArrayParse::EStep::More)
		{
			Step = Parse.Step(Slice);
			++Run.Steps;
		}
		Run.bOk = Step == FFrmArrayParse::EStep::Done;
		Run.Kept = Parse.Kept();
		Run.Dropped = Parse.Dropped();
		Run.Over = Parse.Over();
		Run.Bytes = Parse.Utf8Bytes();
		if (Run.bOk)
		{
			Run.Json = Parse.TakeJson();
		}
		return Run;
	}

	/** One valid train; Extra is appended inside the object (a JSON fragment such as ",\"Status\":1"), and a repeated key wins, as in JSON.parse. */
	FString FrmMapLiveTrainWith(const TCHAR* Extra)
	{
		return FString::Printf(TEXT("[{\"ID\":\"T\",\"Name\":\"n\",\"Status\":\"s\",\"location\":{\"x\":150,\"y\":-150,\"z\":0}%s}]"), Extra);
	}

	FString FrmMapLiveStationWith(const TCHAR* Extra)
	{
		return FString::Printf(TEXT("[{\"ID\":\"S\",\"Name\":\"n\",\"location\":{\"x\":150,\"y\":-150,\"z\":0}%s}]"), Extra);
	}

	/** Count valid trains T0.. with x = 100 m apart. */
	FString FrmMapLiveTrains(int32 Count)
	{
		FString Json = TEXT("[");
		for (int32 I = 0; I < Count; ++I)
		{
			Json += FString::Printf(TEXT("%s{\"ID\":\"T%d\",\"Name\":\"n%d\",\"Status\":\"s\",\"location\":{\"x\":%d,\"y\":0,\"z\":0}}"), I > 0 ? TEXT(",") : TEXT(""), I, I, I * 10000);
		}
		return Json + TEXT("]");
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmMapLiveRulesTest, "SatisDash.Core.FrmMapLive.Rules", SatisDash::Test::Flags)

bool FFrmMapLiveRulesTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::SameText(*this, TEXT("a train: half-metres go toward +infinity, fields in the Node agent's order"),
		FrmMapLiveOf<FTrainsParse>(FrmMapLiveTrainWith(TEXT(""))).Json, TEXT("[{\"id\":\"T\",\"name\":\"n\",\"x\":2,\"y\":-1,\"status\":\"s\"}]"));
	SatisDash::Test::SameText(*this, TEXT("a station"),
		FrmMapLiveOf<FStationsParse>(FrmMapLiveStationWith(TEXT(""))).Json, TEXT("[{\"id\":\"S\",\"name\":\"n\",\"x\":2,\"y\":-1}]"));
	SatisDash::Test::SameText(*this, TEXT("no trains is valid"), FrmMapLiveOf<FTrainsParse>(TEXT("[]")).Json, TEXT("[]"));

	// Tolerated: fields the map doesn't use are skipped, however they are shaped.
	for (const TCHAR* Extra : { TEXT(",\"Cars\":[{\"a\":[1,2,{\"b\":null}]}]"), TEXT(",\"TimeTable\":[]"), TEXT(",\"ClassName\":\"c\",\"TotalMass\":1.5e3") })
	{
		TestTrue(FString::Printf(TEXT("tolerated: %s"), Extra), FrmMapLiveOf<FTrainsParse>(FrmMapLiveTrainWith(Extra)).bOk);
	}
	// Refused as zod's schemas refuse them: the whole body is left out. A repeated key wins, so the appended one decides.
	for (const TCHAR* Bad : { TEXT(",\"ID\":\"\""), TEXT(",\"ID\":1"), TEXT(",\"Name\":null"),
		TEXT(",\"location\":{\"x\":1,\"y\":2}"), TEXT(",\"location\":{\"x\":1e999,\"y\":2,\"z\":0}"), TEXT(",\"location\":null") })
	{
		TestFalse(FString::Printf(TEXT("train refused: %s"), Bad), FrmMapLiveOf<FTrainsParse>(FrmMapLiveTrainWith(Bad)).bOk);
		TestFalse(FString::Printf(TEXT("station refused: %s"), Bad), FrmMapLiveOf<FStationsParse>(FrmMapLiveStationWith(Bad)).bOk);
	}
	TestFalse(TEXT("a train with a numeric Status"), FrmMapLiveOf<FTrainsParse>(FrmMapLiveTrainWith(TEXT(",\"Status\":1"))).bOk);
	TestFalse(TEXT("a train without Status"), FrmMapLiveOf<FTrainsParse>(TEXT("[{\"ID\":\"T\",\"Name\":\"n\",\"location\":{\"x\":1,\"y\":2,\"z\":3}}]")).bOk);
	TestTrue(TEXT("a station needs no Status (and skips one)"), FrmMapLiveOf<FStationsParse>(FrmMapLiveStationWith(TEXT(",\"Status\":1"))).bOk);
	// Hostile bodies: refused without a crash.
	TArray<FString> Hostile;
	Hostile.Add(FString());
	Hostile.Add(TEXT("{}"));
	Hostile.Add(TEXT("[1]"));
	Hostile.Add(TEXT("[{}]"));
	Hostile.Add(TEXT("[] []"));
	Hostile.Add(TEXT("[]x"));
	Hostile.Add(TEXT("[{\"ID\":\"T\""));
	Hostile.Add(FString::ChrN(10000, TEXT('[')) + FString::ChrN(10000, TEXT(']')));
	for (int32 I = 0; I < Hostile.Num(); ++I)
	{
		TestFalse(FString::Printf(TEXT("hostile body %d is refused"), I), FrmMapLiveOf<FTrainsParse>(Hostile[I]).bOk);
	}

	// Strings: cut to 200 characters, escaped, non-ASCII kept.
	const FString Long = FString::ChrN(201, TEXT('a'));
	TestTrue(TEXT("a name is cut to 200"), FrmMapLiveOf<FTrainsParse>(FrmMapLiveTrainWith(*(TEXT(",\"Name\":\"") + Long + TEXT("\"")))).Json.Contains(TEXT("\"name\":\"") + FString::ChrN(200, TEXT('a')) + TEXT("\",")));
	TestTrue(TEXT("quote and backslash are escaped"), FrmMapLiveOf<FTrainsParse>(FrmMapLiveTrainWith(TEXT(",\"Name\":\"a\\\"b\\\\c\""))).Json.Contains(TEXT("\"name\":\"a\\\"b\\\\c\"")));
	const FString Accented = FString::Printf(TEXT("Z%cge"), TCHAR(0x00FC));
	TestTrue(TEXT("non-ASCII is kept"), FrmMapLiveOf<FTrainsParse>(FrmMapLiveTrainWith(*(TEXT(",\"Name\":\"") + Accented + TEXT("\"")))).Json.Contains(TEXT("\"name\":\"") + Accented + TEXT("\"")));

	// Bounds: 1e8 cm is exactly 1,000,000 m (kept); 100000100 cm is 1,000,001 m (dropped, not refused).
	const FString Edge = TEXT("[{\"ID\":\"T\",\"Name\":\"n\",\"Status\":\"s\",\"location\":{\"x\":100000000,\"y\":-100000000,\"z\":0}}]");
	TestEqual(TEXT("the bound itself is kept"), FrmMapLiveOf<FTrainsParse>(Edge).Kept, 1);
	const FFrmMapLiveRun Past = FrmMapLiveOf<FTrainsParse>(TEXT("[{\"ID\":\"T\",\"Name\":\"n\",\"Status\":\"s\",\"location\":{\"x\":100000100,\"y\":0,\"z\":0}}]"));
	TestTrue(TEXT("one metre past it is dropped, and the body is still valid"), Past.bOk && Past.Kept == 0 && Past.Dropped == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmMapLiveCapsTest, "SatisDash.Core.FrmMapLive.Caps", SatisDash::Test::Flags)

bool FFrmMapLiveCapsTest::RunTest(const FString& Parameters)
{
	const FFrmMapLiveRun Many = FrmMapLiveOf<FTrainsParse>(FrmMapLiveTrains(600));
	TestTrue(TEXT("600 trains parse"), Many.bOk);
	TestEqual(TEXT("500 are kept"), Many.Kept, MapLiveMaxItems);
	TestEqual(TEXT("100 are valid but over the cap"), Many.Over, 100);
	TestTrue(TEXT("the first ones: T499 is in, T500 is not"), Many.Json.Contains(TEXT("\"id\":\"T499\"")) && !Many.Json.Contains(TEXT("\"id\":\"T500\"")));

	// Dropped ones (bad coordinates) do not count towards the 500, as sampler.ts counts after the mappers.
	FString Bad;
	for (int32 I = 0; I < 10; ++I)
	{
		Bad += FString::Printf(TEXT("{\"ID\":\"X%d\",\"Name\":\"n\",\"Status\":\"s\",\"location\":{\"x\":1e12,\"y\":0,\"z\":0}},"), I);
	}
	const FFrmMapLiveRun Mixed = FrmMapLiveOf<FTrainsParse>(TEXT("[") + Bad + FrmMapLiveTrains(500).RightChop(1));
	TestTrue(TEXT("10 dropped then 500 valid: all 500 kept"), Mixed.bOk && Mixed.Kept == 500 && Mixed.Dropped == 10 && Mixed.Over == 0);

	// zod validates the whole array before conform cuts it: a bad record past the cap still refuses the body.
	TestFalse(TEXT("an invalid 601st record refuses the body"), FrmMapLiveOf<FTrainsParse>(FrmMapLiveTrains(600).LeftChop(1) + TEXT(",{\"ID\":\"\"}]")).bOk);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmMapLiveSlicesTest, "SatisDash.Core.FrmMapLive.Slices", SatisDash::Test::Flags)

bool FFrmMapLiveSlicesTest::RunTest(const FString& Parameters)
{
	const FString Body = FrmMapLiveTrains(12);
	const FFrmMapLiveRun One = FrmMapLiveOf<FTrainsParse>(Body, 1);
	const FFrmMapLiveRun All = FrmMapLiveOf<FTrainsParse>(Body, FrmSliceRecords);
	TestEqual(TEXT("one record a slice: 12 records and the end"), One.Steps, 13);
	TestEqual(TEXT("2,000 a slice: one step"), All.Steps, 1);
	SatisDash::Test::SameText(*this, TEXT("the slice size changes nothing in the output"), One.Json, All.Json);
	TestEqual(TEXT("and the byte count is the text's UTF-8 length"), All.Bytes, int64(FTCHARToUTF8(*All.Json).Length()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmMapLiveJoinTest, "SatisDash.Core.FrmMapLive.Join", SatisDash::Test::Flags)

bool FFrmMapLiveJoinTest::RunTest(const FString& Parameters)
{
	const TOptional<FString> Joined = JoinMapLive(TEXT("[1]"), 3, TEXT("[2]"), 3);
	SatisDash::Test::SameText(*this, TEXT("the two arrays in one object"), Joined.Get(FString()), TEXT("{\"trains\":[1],\"stations\":[2]}"));
	TestEqual(TEXT("the wrapper's bytes are as counted"), int64(Joined.Get(FString()).Len()), MapLiveWrapperBytes + 3 + 3);
	// The cap is 256,000 UTF-8 bytes of the whole object, and exactly the cap is allowed (sampler.ts: > MAX).
	const int64 Room = MapLiveMaxBytes - MapLiveWrapperBytes;
	TestTrue(TEXT("exactly the cap"), JoinMapLive(TEXT("[]"), Room - 2, TEXT("[]"), 2).IsSet());
	TestFalse(TEXT("one byte over: the whole part is left out"), JoinMapLive(TEXT("[]"), Room - 1, TEXT("[]"), 2).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmMapLiveGoldenTest, "SatisDash.Core.FrmMapLive.Golden", SatisDash::Test::Flags)

bool FFrmMapLiveGoldenTest::RunTest(const FString& Parameters)
{
	const FString TrainsBody = SatisDash::Test::Fixture(TEXT("frm-getTrains.json"));
	const FString StationsBody = SatisDash::Test::Fixture(TEXT("frm-getTrainStation.json"));
	const FString Expected = SatisDash::Test::Fixture(TEXT("frm-mapLive.expected.json"));
	if (!TestFalse(TEXT("the fixtures were found"), TrainsBody.IsEmpty() || StationsBody.IsEmpty() || Expected.IsEmpty()))
	{
		return false;
	}
	const FFrmMapLiveRun Trains = FrmMapLiveOf<FTrainsParse>(TrainsBody);
	const FFrmMapLiveRun Stations = FrmMapLiveOf<FStationsParse>(StationsBody);
	TestTrue(TEXT("both parse"), Trains.bOk && Stations.bOk);
	TestEqual(TEXT("the train out of bounds was dropped"), Trains.Dropped, 1);
	const TOptional<FString> Joined = JoinMapLive(Trains.Json, Trains.Bytes, Stations.Json, Stations.Bytes);
	TestTrue(TEXT("under the cap"), Joined.IsSet());
	TestTrue(FString::Printf(TEXT("is the Node agent's mapLive; got %s"), *Joined.Get(FString())), SatisDash::Test::SameJson(Joined.Get(FString()), Expected));
	return true;
}
```

- [ ] **Step 3: Run them to see them fail**

Run the mod tests with `--min 115`.
Expected: the build fails with `'FrmMap.h': No such file` (and the other new headers).

- [ ] **Step 4: Implement**

`JsonUtil.h`, in `SatisDash::Json`, after `Write`:

```cpp
	/** Appends Value as a quoted JSON string: " \ and the named controls escaped, other controls below 0x20 as \u00xx, the rest as is. */
	SATISDASHCORE_API void AppendString(FString& Out, const FString& Value);
```

`JsonUtil.cpp`, at the end:

```cpp
void SatisDash::Json::AppendString(FString& Out, const FString& Value)
{
	Out.AppendChar(TEXT('"'));
	for (const TCHAR C : Value)
	{
		switch (C)
		{
		case TEXT('"'): Out += TEXT("\\\""); break;
		case TEXT('\\'): Out += TEXT("\\\\"); break;
		case TEXT('\n'): Out += TEXT("\\n"); break;
		case TEXT('\r'): Out += TEXT("\\r"); break;
		case TEXT('\t'): Out += TEXT("\\t"); break;
		case TEXT('\b'): Out += TEXT("\\b"); break;
		case TEXT('\f'): Out += TEXT("\\f"); break;
		default:
			if (C < 0x20)
			{
				Out.Appendf(TEXT("\\u%04x"), uint32(C));
			}
			else
			{
				Out.AppendChar(C);
			}
		}
	}
	Out.AppendChar(TEXT('"'));
}
```

`FrmMap.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "SatisDash/JsonTokens.h"

namespace SatisDash
{
	/**
	 * Game units (centimetres) to the whole metres the map contract uses (map/coordinates.ts, map.ts WholeMetreSchema), and the
	 * location every FRM map record carries (rawSchemas.ts RawFrmLocationSchema).
	 */

	/** JavaScript's Math.round: a half goes toward +infinity (-1.5 is -1, 2.5 is 3, -0.5 is 0). Not FMath::RoundToDouble, which is floor(x + 0.5) and gets 0.49999999999999994 wrong. */
	SATISDASHCORE_API double JsRound(double Value);
	/** toWholeMetres: Math.round(gameUnits / 100), the division a true division (see FrmMap.cpp). */
	SATISDASHCORE_API double ToWholeMetres(double GameUnits);
	/** inWholeMetreBounds: finite and within +/-1,000,000 m. */
	SATISDASHCORE_API bool InWholeMetreBounds(double WholeMetres);
	/**
	 * Reads the object whose ObjectStart was just returned (Started): finite numbers x, y and z are required (z is read and
	 * dropped), rotation is optional but must be a finite number if present, every other field is skipped. False when it is
	 * not that, or on a reader failure. The last of a repeated key wins, as JSON.parse's does.
	 */
	SATISDASHCORE_API bool ReadFrmLocation(FTokenReader& Reader, EJsonNotation Started, double& OutX, double& OutY);
}
```

`FrmMap.cpp`:

```cpp
#include "SatisDash/FrmMap.h"

namespace
{
	bool FrmMapIs(const FString& Key, const TCHAR* Name)
	{
		return Key.Equals(Name, ESearchCase::CaseSensitive);
	}
}

double SatisDash::JsRound(double Value)
{
	if (!FMath::IsFinite(Value))
	{
		return Value;
	}
	// value - floor(value) is exact, so the half test is exact too.
	const double Floor = FMath::FloorToDouble(Value);
	return Value - Floor >= 0.5 ? Floor + 1.0 : Floor;
}

double SatisDash::ToWholeMetres(double GameUnits)
{
	// UE builds with /fp:fast, which turns a division by the constant 100.0 into a multiplication by 0.01 and lands one ulp
	// away for some values (see FrmFactoryMetres in FrmFactory.cpp). The volatile divisor keeps the true division.
	volatile double Hundred = 100.0;
	return JsRound(GameUnits / Hundred);
}

bool SatisDash::InWholeMetreBounds(double WholeMetres)
{
	return FMath::IsFinite(WholeMetres) && FMath::Abs(WholeMetres) <= 1000000.0;
}

bool SatisDash::ReadFrmLocation(FTokenReader& Reader, EJsonNotation Started, double& OutX, double& OutY)
{
	if (Started != EJsonNotation::ObjectStart)
	{
		return false;
	}
	uint8 Seen = 0;
	EJsonNotation N;
	while (Reader.Next(N))
	{
		if (N == EJsonNotation::ObjectEnd)
		{
			return Seen == 7; // x, y and z
		}
		const FString Key = Reader.Name();
		const bool bX = FrmMapIs(Key, TEXT("x"));
		const bool bY = FrmMapIs(Key, TEXT("y"));
		const bool bZ = FrmMapIs(Key, TEXT("z"));
		if (bX || bY || bZ || FrmMapIs(Key, TEXT("rotation")))
		{
			if (N != EJsonNotation::Number || !FMath::IsFinite(Reader.Number()))
			{
				return false;
			}
			if (bX)
			{
				OutX = Reader.Number();
				Seen |= 1;
			}
			else if (bY)
			{
				OutY = Reader.Number();
				Seen |= 2;
			}
			else if (bZ)
			{
				Seen |= 4;
			}
		}
		else if (!Reader.SkipValue(N))
		{
			return false;
		}
	}
	return false;
}
```

`FrmRecords.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "SatisDash/JsonTokens.h"

namespace SatisDash
{
	/** FRM bridge spec "Threading": about 2,000 records per worker slice. */
	inline constexpr int32 FrmSliceRecords = 2000;

	/**
	 * An FRM body that is an array of objects, read a slice of records at a time (FRM bridge spec "Memory": no JSON tree) and
	 * written straight to compact JSON text. A subclass reads one record's object (ReadRecord), validates it against the Node
	 * agent's zod schema, maps it, and appends its JSON; this class owns the array, the slices, the caps and the counts.
	 *
	 *  - Every record is read and validated, even past the cap (zod validates the whole array before the Node agent cuts it): an
	 *    invalid record anywhere makes the step Invalid and the whole part is left out.
	 *  - The output is Prefix + "[" + records + "]" + Suffix. A world body uses Prefix {"observedAt":"...","data": and Suffix }.
	 *  - Written by hand with Json::AppendString and FString::AppendInt, not TJsonWriter, which holds every byte twice (Plan 3B).
	 *
	 * Pure, and touched by one thread at a time (FFrmBridge hands it to the worker a slice at a time). Not copyable: the reader
	 * holds a view of Body.
	 */
	class SATISDASHCORE_API FFrmArrayParse
	{
	public:
		enum class EStep : uint8 { More, Done, Invalid };

		virtual ~FFrmArrayParse() = default;
		FFrmArrayParse(const FFrmArrayParse&) = delete;
		FFrmArrayParse& operator=(const FFrmArrayParse&) = delete;

		/** Reads at most MaxPerSlice more records. After Done or Invalid it does nothing more. */
		EStep Step(int32 MaxPerSlice = FrmSliceRecords);
		/** After Done: the text. Empties it. */
		FString TakeJson();
		/** After Done: the text, not emptied (for hashing it). */
		const FString& PeekJson() const { return Out; }
		/** After Done: where the array's "[" is in PeekJson(), and one past its "]". */
		int32 DataFrom() const { return Prefix.Len(); }
		int32 DataEnd() const { return ArrayEnd; }
		/** After Done: the UTF-8 length of the whole text, measured before it is taken. */
		int64 Utf8Bytes() const { return Bytes; }
		/** Written. */
		int32 Kept() const { return KeptCount; }
		/** Valid but not sent: a bad coordinate, or too few or too many points. */
		int32 Dropped() const { return DroppedCount; }
		/** Valid and sendable but past the cap. */
		int32 Over() const { return OverCount; }

	protected:
		enum class ERecord : uint8 { Invalid, Dropped, Over, Kept };

		FFrmArrayParse(TArray<uint8> InBody, int32 InMaxKept, FString InPrefix, FString InSuffix);
		/**
		 * Called with an ObjectStart just read: reads through its ObjectEnd. Invalid: the record is not what zod accepts.
		 * Dropped: valid, but the mapper drops it. Over: valid and sendable, but !bRoom (nothing is written). Kept: written
		 * (call BeginRecord() first, then append the record's JSON to Out).
		 */
		virtual ERecord ReadRecord(bool bRoom) = 0;
		void BeginRecord();
		static bool Is(const FString& Key, const TCHAR* Name);

		TArray<uint8> Body;
		FTokenReader Reader;
		FString Out;

	private:
		EStep Fail();

		const int32 MaxKept;
		const FString Prefix;
		const FString Suffix;
		EStep State = EStep::More;
		bool bStarted = false;
		int32 KeptCount = 0;
		int32 DroppedCount = 0;
		int32 OverCount = 0;
		int32 ArrayEnd = 0;
		int64 Bytes = 0;
	};
}
```

`FrmRecords.cpp`:

```cpp
#include "SatisDash/FrmRecords.h"

SatisDash::FFrmArrayParse::FFrmArrayParse(TArray<uint8> InBody, int32 InMaxKept, FString InPrefix, FString InSuffix)
	: Body{MoveTemp(InBody)}
	, Reader{Body}
	, MaxKept{InMaxKept}
	, Prefix{MoveTemp(InPrefix)}
	, Suffix{MoveTemp(InSuffix)}
{
}

bool SatisDash::FFrmArrayParse::Is(const FString& Key, const TCHAR* Name)
{
	return Key.Equals(Name, ESearchCase::CaseSensitive);
}

void SatisDash::FFrmArrayParse::BeginRecord()
{
	if (KeptCount > 0)
	{
		Out.AppendChar(TEXT(','));
	}
}

SatisDash::FFrmArrayParse::EStep SatisDash::FFrmArrayParse::Fail()
{
	State = EStep::Invalid;
	Out.Empty();
	Body.Empty(); // the reader is not used again
	return State;
}

SatisDash::FFrmArrayParse::EStep SatisDash::FFrmArrayParse::Step(int32 MaxPerSlice)
{
	if (State != EStep::More)
	{
		return State;
	}
	EJsonNotation N;
	if (!bStarted)
	{
		bStarted = true;
		if (!Reader.Next(N) || N != EJsonNotation::ArrayStart)
		{
			return Fail();
		}
		Out = Prefix;
		Out.AppendChar(TEXT('['));
	}
	for (int32 Read = 0; Read < MaxPerSlice; ++Read)
	{
		if (!Reader.Next(N))
		{
			return Fail();
		}
		if (N == EJsonNotation::ArrayEnd)
		{
			if (!Reader.AtCleanEnd())
			{
				return Fail();
			}
			Out.AppendChar(TEXT(']'));
			ArrayEnd = Out.Len();
			Out += Suffix;
			Bytes = FTCHARToUTF8_Convert::ConvertedLength(*Out, Out.Len());
			Body.Empty();
			State = EStep::Done;
			return State;
		}
		if (N != EJsonNotation::ObjectStart)
		{
			return Fail();
		}
		switch (ReadRecord(KeptCount < MaxKept))
		{
		case ERecord::Invalid: return Fail();
		case ERecord::Dropped: ++DroppedCount; break;
		case ERecord::Over: ++OverCount; break;
		case ERecord::Kept: ++KeptCount; break;
		}
	}
	return EStep::More;
}

FString SatisDash::FFrmArrayParse::TakeJson()
{
	return State == EStep::Done ? MoveTemp(Out) : FString();
}
```

`FrmMapLive.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "SatisDash/FrmRecords.h"

namespace SatisDash
{
	/** sampler.ts MAP_LIVE_AGENT_MAX_ITEMS and MAP_LIVE_AGENT_MAX_BYTES (tighter than the schema's MAP_LIVE_MAX_*, 2,000). */
	inline constexpr int32 MapLiveMaxItems = 500;
	inline constexpr int64 MapLiveMaxBytes = 256000;
	/** The bytes of {"trains":  ,"stations":  } around the two arrays: 10 + 12 + 1. */
	inline constexpr int64 MapLiveWrapperBytes = 23;

	/**
	 * getTrains' body to the trains array [{id,name,x,y,status}], as the Node agent makes it (rawSchemas.ts RawFrmTrainSchema,
	 * trainsMapper.ts, coordinates.ts, then conformMapLive's 500 and 200). Dropped() counts trains with a coordinate the
	 * contract can't hold (they do not count towards the 500).
	 */
	class SATISDASHCORE_API FTrainsParse final : public FFrmArrayParse
	{
	public:
		explicit FTrainsParse(TArray<uint8> InBody);

	protected:
		virtual ERecord ReadRecord(bool bRoom) override;
	};

	/** getTrainStation's body to the stations array [{id,name,x,y}] (RawFrmTrainStationSchema, stationsMapper.ts). */
	class SATISDASHCORE_API FStationsParse final : public FFrmArrayParse
	{
	public:
		explicit FStationsParse(TArray<uint8> InBody);

	protected:
		virtual ERecord ReadRecord(bool bRoom) override;
	};

	/**
	 * {"trains":<TrainsArray>,"stations":<StationsArray>}, or unset when the whole object would pass MapLiveMaxBytes UTF-8
	 * bytes (sampler.ts leaves the whole part out then, "never cut short by bytes"). The byte counts are the arrays' own
	 * (FFrmArrayParse::Utf8Bytes), measured on the worker.
	 */
	SATISDASHCORE_API TOptional<FString> JoinMapLive(const FString& TrainsArray, int64 TrainsBytes, const FString& StationsArray, int64 StationsBytes);
}
```

`FrmMapLive.cpp`:

```cpp
#include "SatisDash/FrmMapLive.h"

#include "SatisDash/FrmMap.h"
#include "SatisDash/JsonUtil.h"
#include "SatisDash/Snapshot.h"

SatisDash::FTrainsParse::FTrainsParse(TArray<uint8> InBody)
	: FFrmArrayParse(MoveTemp(InBody), MapLiveMaxItems, FString(), FString())
{
}

auto SatisDash::FTrainsParse::ReadRecord(bool bRoom) -> ERecord
{
	TOptional<FString> Id;
	TOptional<FString> Name;
	TOptional<FString> Status;
	bool bLocation = false;
	double X = 0;
	double Y = 0;
	EJsonNotation N;
	while (Reader.Next(N))
	{
		if (N == EJsonNotation::ObjectEnd)
		{
			if (!Id.IsSet() || Id->IsEmpty() || !Name.IsSet() || !Status.IsSet() || !bLocation)
			{
				return ERecord::Invalid;
			}
			const double Mx = ToWholeMetres(X);
			const double My = ToWholeMetres(Y);
			if (!InWholeMetreBounds(Mx) || !InWholeMetreBounds(My))
			{
				return ERecord::Dropped;
			}
			if (!bRoom)
			{
				return ERecord::Over;
			}
			BeginRecord();
			Out += TEXT("{\"id\":");
			Json::AppendString(Out, ClampString(Id.GetValue()));
			Out += TEXT(",\"name\":");
			Json::AppendString(Out, ClampString(Name.GetValue()));
			Out += TEXT(",\"x\":");
			Out.AppendInt(int32(Mx));
			Out += TEXT(",\"y\":");
			Out.AppendInt(int32(My));
			Out += TEXT(",\"status\":");
			Json::AppendString(Out, ClampString(Status.GetValue()));
			Out.AppendChar(TEXT('}'));
			return ERecord::Kept;
		}
		const FString Key = Reader.Name();
		const bool bId = Is(Key, TEXT("ID"));
		const bool bName = Is(Key, TEXT("Name"));
		const bool bStatus = Is(Key, TEXT("Status"));
		if (bId || bName || bStatus)
		{
			if (N != EJsonNotation::String)
			{
				return ERecord::Invalid;
			}
			(bId ? Id : bName ? Name : Status) = Reader.String();
		}
		else if (Is(Key, TEXT("location")))
		{
			if (!ReadFrmLocation(Reader, N, X, Y))
			{
				return ERecord::Invalid;
			}
			bLocation = true;
		}
		else if (!Reader.SkipValue(N))
		{
			return ERecord::Invalid;
		}
	}
	return ERecord::Invalid;
}

SatisDash::FStationsParse::FStationsParse(TArray<uint8> InBody)
	: FFrmArrayParse(MoveTemp(InBody), MapLiveMaxItems, FString(), FString())
{
}

auto SatisDash::FStationsParse::ReadRecord(bool bRoom) -> ERecord
{
	TOptional<FString> Id;
	TOptional<FString> Name;
	bool bLocation = false;
	double X = 0;
	double Y = 0;
	EJsonNotation N;
	while (Reader.Next(N))
	{
		if (N == EJsonNotation::ObjectEnd)
		{
			if (!Id.IsSet() || Id->IsEmpty() || !Name.IsSet() || !bLocation)
			{
				return ERecord::Invalid;
			}
			const double Mx = ToWholeMetres(X);
			const double My = ToWholeMetres(Y);
			if (!InWholeMetreBounds(Mx) || !InWholeMetreBounds(My))
			{
				return ERecord::Dropped;
			}
			if (!bRoom)
			{
				return ERecord::Over;
			}
			BeginRecord();
			Out += TEXT("{\"id\":");
			Json::AppendString(Out, ClampString(Id.GetValue()));
			Out += TEXT(",\"name\":");
			Json::AppendString(Out, ClampString(Name.GetValue()));
			Out += TEXT(",\"x\":");
			Out.AppendInt(int32(Mx));
			Out += TEXT(",\"y\":");
			Out.AppendInt(int32(My));
			Out.AppendChar(TEXT('}'));
			return ERecord::Kept;
		}
		const FString Key = Reader.Name();
		const bool bId = Is(Key, TEXT("ID"));
		if (bId || Is(Key, TEXT("Name")))
		{
			if (N != EJsonNotation::String)
			{
				return ERecord::Invalid;
			}
			(bId ? Id : Name) = Reader.String();
		}
		else if (Is(Key, TEXT("location")))
		{
			if (!ReadFrmLocation(Reader, N, X, Y))
			{
				return ERecord::Invalid;
			}
			bLocation = true;
		}
		else if (!Reader.SkipValue(N))
		{
			return ERecord::Invalid;
		}
	}
	return ERecord::Invalid;
}

TOptional<FString> SatisDash::JoinMapLive(const FString& TrainsArray, int64 TrainsBytes, const FString& StationsArray, int64 StationsBytes)
{
	if (MapLiveWrapperBytes + TrainsBytes + StationsBytes > MapLiveMaxBytes)
	{
		return {};
	}
	FString Out;
	Out.Reserve(TrainsArray.Len() + StationsArray.Len() + 24);
	Out += TEXT("{\"trains\":");
	Out += TrainsArray;
	Out += TEXT(",\"stations\":");
	Out += StationsArray;
	Out.AppendChar(TEXT('}'));
	return Out;
}
```

Notes for the implementer:
- Name collision: the classes' member `Out` is the protected `FString Out` of `FFrmArrayParse`; `JoinMapLive` has its own local `Out`.
- `ERecord` is a protected nested enum of the base. The trailing return type (`-> ERecord`) lets the out-of-class definitions name it.

- [ ] **Step 5: Run the tests**

Expected: `judge-mod-test --min 115` passes. `check-api` will report new symbols. Rows looked up for this plan (add them, each `header only`): `FMath::FloorToDouble` `Engine/Source/Runtime/Core/Public/GenericPlatform/GenericPlatformMath.h:295` (declared in FGenericPlatformMath, which FMath inherits); `FString::AppendInt` `Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:2221`; `FString::AppendChar` `.../UnrealString.h.inl:447`; `FString::Appendf` `.../UnrealString.h.inl:1614`; `FString::Reserve` `.../UnrealString.h.inl:2200` (a method called with `.` is covered by any row ending `::Reserve`, so the existing `TArray::Reserve` row may already do); `FMath::IsNaN` `.../GenericPlatform/GenericPlatformMath.h:576`. Look up anything else it reports (`FString::Equals`, `FMath::Abs`, ...) in the pinned headers with `path:line`. The tests use `std::numeric_limits` for NaN and infinity: UE's `TNumericLimits` has no NaN or infinity (`Math/NumericLimits.h`).

- [ ] **Step 6: Commit**

```bash
git add mod/SatisDashAgent/Source api-sources.md
git -c core.safecrlf=false commit -m "Whole-metre coordinates, the sliced array reader, trains and stations (FRM bridge 3B Task 2)" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

### Task 3: The bridge reads mapLive, and the driver sends it

**Files:**
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Frm.h` (two endpoints)
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/FrmBridge.h`, `Private/FrmBridge.cpp`
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Snapshot.h`, `Private/Snapshot.cpp`
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Driver.h`, `Private/Driver.cpp`
- Test: `FrmBridgeTest.cpp`, `SnapshotTest.cpp`, `DriverFrmTest.cpp`

**Interfaces:**
- Consumes: `FTrainsParse`, `FStationsParse`, `JoinMapLive` (Task 2); the splice table (Task 1).
- Produces:
  - `FrmGetTrains`, `FrmGetTrainStation`: live lane, not large, not passable, `FrmSmallCapBytes`, 5 s.
  - `FFrmBridge::RequestPass(bFactoryDue)` also starts a mapLive cycle when `bFactoryDue` (two reads, trains then stations); `TOptional<FString> TakeMapLive(double Now, double MaxAgeSeconds)`.
  - `FSnapshot::MapLiveJson`, spliced as `,"mapLive":` after `factory`.
  - `FDriver::EncodePending() const`. Any snapshot that carries `FactoryJson` or `MapLiveJson` is encoded on the worker.

Design (spec "Components", "Load spreading", "Failures"):
- mapLive is read at the factory cadence: when the driver's pass finds the factory due, the bridge queues `getTrains` and `getTrainStation` after `getFactory`.
- They are two ordinary reads in the live lane. One FRM request is in flight at a time, so they run one after the other. Neither passes a large parse: they wait for the factory parse to finish, as the spec says only `getPlayer` passes.
- Each body is parsed on the worker (`FTrainsParse`, `FStationsParse`, one slice at a time). When both are settled the part is `JoinMapLive` of the two arrays. If either failed (FRM unreachable, HTTP error, over its cap, invalid body) or the joined JSON passes 256,000 bytes, the part is left out with one rate-limited warning. The next cycle starts only when the last has settled.
- The part ages from the first of its two reads (Task 1's age rule), and is allowed `FactorySeconds`.

- [ ] **Step 1: Write the failing tests**

`FrmBridgeTest.cpp`: replace `FFrmBridgeFixture` with this (adds `serve` and `cycle`), and add the constants beside the existing two:

```cpp
	const TCHAR* FrmBridgeOneTrain = TEXT("[{\"ID\":\"T1\",\"Name\":\"Train\",\"Status\":\"Self-Driving\",\"location\":{\"x\":150,\"y\":-150,\"z\":0}}]");
	const TCHAR* FrmBridgeOneStation = TEXT("[{\"ID\":\"S1\",\"Name\":\"Station\",\"location\":{\"x\":5049,\"y\":-5051,\"z\":0}}]");
	const TCHAR* FrmBridgeMapLive = TEXT("{\"trains\":[{\"id\":\"T1\",\"name\":\"Train\",\"x\":2,\"y\":-1,\"status\":\"Self-Driving\"}],\"stations\":[{\"id\":\"S1\",\"name\":\"Station\",\"x\":50,\"y\":-51}]}");

	struct FFrmBridgeFixture
	{
		SatisDash::Test::FFakeTransport Net;
		SatisDash::Test::FFakeWorker Worker;
		FFrmBridge Bridge{ Net, Worker };
		double T = 100;
		void tick(int32 Times = 1)
		{
			for (int32 I = 0; I < Times; ++I)
			{
				T += 0.5;
				Bridge.Tick(T, 8080, FString());
			}
		}
		/** Ticks until FRM has been asked for Path (at most MaxTicks), then answers it. False if it was never asked. */
		bool serve(const TCHAR* Path, const FHttpReply& Reply, int32 MaxTicks = 8)
		{
			for (int32 I = 0; I < MaxTicks && Net.pending(Path) == 0; ++I)
			{
				tick();
			}
			return Net.answer(Path, Reply);
		}
		/** One factory-cadence pass with FRM answering every read in turn (players and factory fine, the others as given). */
		void cycle(const FHttpReply& Trains, const FHttpReply& Stations)
		{
			Bridge.RequestPass(true);
			serve(TEXT("/getPlayer"), SatisDash::Test::Reply(200, FrmBridgeOnePlayer));
			serve(TEXT("/getFactory"), SatisDash::Test::Reply(200, FrmBridgeOneBuilding));
			serve(TEXT("/getTrains"), Trains);
			serve(TEXT("/getTrainStation"), Stations);
			tick(4);
		}
	};
```

(`FHttpReply` is `SatisDash::FHttpReply`; the file has `using namespace SatisDash;`.) Then the tests, after `FFrmBridgeFactoryTest`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmBridgeMapLiveTest, "SatisDash.Core.FrmBridge.MapLive", SatisDash::Test::Flags)

bool FFrmBridgeMapLiveTest::RunTest(const FString& Parameters)
{
	FFrmBridgeFixture F;
	// getTrains isn't marked passable: it waits while the factory body parses. Only getPlayer passes a large parse.
	F.Bridge.RequestPass(true);
	TestTrue(TEXT("players asked"), F.serve(TEXT("/getPlayer"), SatisDash::Test::Reply(200, FrmBridgeOnePlayer)));
	F.tick(2); // the players parse runs and is harvested
	F.Worker.bHold = true;
	TestTrue(TEXT("factory asked"), F.serve(TEXT("/getFactory"), SatisDash::Test::Reply(200, FrmBridgeOneBuilding)));
	F.tick(2);
	TestEqual(TEXT("getTrains waits while the factory body is held"), F.Net.pending(TEXT("/getTrains")), 0);
	F.Worker.bHold = false;
	F.Worker.release();
	TestTrue(TEXT("then getTrains is asked"), F.serve(TEXT("/getTrains"), SatisDash::Test::Reply(200, FrmBridgeOneTrain)));
	TestTrue(TEXT("and getTrainStation, after it: one request at a time"), F.serve(TEXT("/getTrainStation"), SatisDash::Test::Reply(200, FrmBridgeOneStation)));
	F.tick(4);
	SatisDash::Test::SameText(*this, TEXT("the part is the Node agent's mapLive"), F.Bridge.TakeMapLive(F.T, 30).Get(FString()), FrmBridgeMapLive);
	TestFalse(TEXT("taken once"), F.Bridge.TakeMapLive(F.T, 30).IsSet());

	// Either read failing, or invalid, leaves the whole part out (the Node agent reads both and one failure fails both).
	F.cycle(SatisDash::Test::Reply(200, FrmBridgeOneTrain), SatisDash::Test::Reply(500, FString()));
	TestFalse(TEXT("stations failed: no part"), F.Bridge.TakeMapLive(F.T, 30).IsSet());
	F.cycle(SatisDash::Test::Reply(200, TEXT("[{\"ID\":\"\"}]")), SatisDash::Test::Reply(200, FrmBridgeOneStation));
	TestFalse(TEXT("trains invalid: no part"), F.Bridge.TakeMapLive(F.T, 30).IsSet());
	F.cycle(SatisDash::Test::Reply(0, FString()), SatisDash::Test::Reply(0, FString()));
	TestFalse(TEXT("FRM down: no part"), F.Bridge.TakeMapLive(F.T, 30).IsSet());
	F.cycle(SatisDash::Test::Reply(200, FrmBridgeOneTrain), SatisDash::Test::Reply(200, FrmBridgeOneStation));
	SatisDash::Test::SameText(*this, TEXT("and it recovers when FRM returns"), F.Bridge.TakeMapLive(F.T, 30).Get(FString()), FrmBridgeMapLive);

	// An old part is dropped: a part ages from its FRM read.
	F.cycle(SatisDash::Test::Reply(200, FrmBridgeOneTrain), SatisDash::Test::Reply(200, FrmBridgeOneStation));
	TestFalse(TEXT("older than the factory cadence: dropped"), F.Bridge.TakeMapLive(F.T + 31, 30).IsSet());
	return true;
}

namespace
{
	/** Count records with long strings, so a train and a station each come to about 400 bytes of output. */
	FString FrmBridgeBig(bool bTrains, int32 Count)
	{
		const FString Id = FString::ChrN(100, TEXT('i'));
		const FString Name = FString::ChrN(200, TEXT('n'));
		const FString Status = bTrains ? TEXT("\"Status\":\"") + FString::ChrN(50, TEXT('s')) + TEXT("\",") : FString();
		FString Json = TEXT("[");
		for (int32 I = 0; I < Count; ++I)
		{
			Json += FString::Printf(TEXT("%s{\"ID\":\"%s%d\",\"Name\":\"%s\",%s\"location\":{\"x\":%d,\"y\":0,\"z\":0}}"), I > 0 ? TEXT(",") : TEXT(""), *Id, I, *Name, *Status, I * 100);
		}
		return Json + TEXT("]");
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmBridgeMapLiveCapTest, "SatisDash.Core.FrmBridge.MapLiveCap", SatisDash::Test::Flags)

bool FFrmBridgeMapLiveCapTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	FFrmBridgeFixture F;
	// 500 trains and 500 stations, each array under 256,000 bytes, together over it: the whole part is left out.
	for (int32 Round = 0; Round < 2; ++Round)
	{
		F.cycle(SatisDash::Test::Reply(200, FrmBridgeBig(true, 500)), SatisDash::Test::Reply(200, FrmBridgeBig(false, 500)));
		TestFalse(TEXT("over 256,000 bytes: no part"), F.Bridge.TakeMapLive(F.T, 30).IsSet());
	}
	int32 CapLines = 0;
	for (const FString& Line : Log.Take())
	{
		CapLines += Line.Contains(TEXT("mapLive is over its")) ? 1 : 0;
	}
	TestEqual(TEXT("one warning, not one a cycle"), CapLines, 1);
	// Just under: a smaller pair goes through.
	F.cycle(SatisDash::Test::Reply(200, FrmBridgeBig(true, 100)), SatisDash::Test::Reply(200, FrmBridgeBig(false, 100)));
	TestTrue(TEXT("100 of each is under the cap"), F.Bridge.TakeMapLive(F.T, 30).IsSet());
	return SatisDash::Test::NoErrorLines(*this, Log);
}
```

`SnapshotTest.cpp`, at the end:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnapshotMapLiveTest, "SatisDash.Core.Snapshot.MapLive", SatisDash::Test::Flags)

bool FSnapshotMapLiveTest::RunTest(const FString& Parameters)
{
	const FString Live = SatisDash::Test::Fixture(TEXT("frm-mapLive.expected.json"));
	if (!TestFalse(TEXT("the mapLive fixture was found"), Live.IsEmpty()))
	{
		return false;
	}
	FSnapshot Snapshot = Base();
	Snapshot.PlayersJson = TEXT("{\"available\":false,\"players\":[]}");
	Snapshot.FactoryJson = TEXT("{\"buildings\":[]}");
	Snapshot.MapLiveJson = Live;
	const FString Json = SnapshotToJson(Snapshot);
	const TSharedPtr<FJsonObject> Parsed = SatisDash::Json::ParseObject(Json);
	if (!TestTrue(TEXT("valid JSON"), Parsed.IsValid()) || !TestTrue(TEXT("with mapLive"), Parsed->HasField(TEXT("mapLive"))))
	{
		return false;
	}
	TestEqual(TEXT("two trains"), Parsed->GetObjectField(TEXT("mapLive"))->GetArrayField(TEXT("trains")).Num(), 2);
	TestEqual(TEXT("two stations"), Parsed->GetObjectField(TEXT("mapLive"))->GetArrayField(TEXT("stations")).Num(), 2);
	TestTrue(TEXT("players, then factory, then mapLive"),
		Json.Find(TEXT("\"players\":")) < Json.Find(TEXT("\"factory\":")) && Json.Find(TEXT("\"factory\":")) < Json.Find(TEXT("\"mapLive\":")));
	TestFalse(TEXT("no mapLive without a part"), SatisDash::Json::ParseObject(SnapshotToJson(Base()))->HasField(TEXT("mapLive")));
	FSnapshot OnlyLive = Base();
	OnlyLive.MapLiveJson = Live;
	TestTrue(TEXT("mapLive alone is spliced too"), SatisDash::Json::ParseObject(SnapshotToJson(OnlyLive)).IsValid());
	return true;
}
```

`DriverFrmTest.cpp`: add to the anonymous namespace at the top:

```cpp
	const TCHAR* DriverFrmTrain = TEXT("[{\"ID\":\"T1\",\"Name\":\"A\",\"Status\":\"s\",\"location\":{\"x\":150,\"y\":-150,\"z\":0}}]");
	const TCHAR* DriverFrmStation = TEXT("[{\"ID\":\"S1\",\"Name\":\"B\",\"location\":{\"x\":5049,\"y\":-5051,\"z\":0}}]");
	const TCHAR* DriverFrmMapLive = TEXT("\"mapLive\":{\"trains\":[{\"id\":\"T1\",\"name\":\"A\",\"x\":2,\"y\":-1,\"status\":\"s\"}],\"stations\":[{\"id\":\"S1\",\"name\":\"B\",\"x\":50,\"y\":-51}]}");

	/**
	 * Runs the driver until FRM has been asked for Path, then answers it. False if it never was asked within 40 s. While it
	 * waits for any other path it answers a routine getPlayer with Code 0, because one FRM request is in flight at a time
	 * and an unanswered one would hold up the read being waited for.
	 */
	bool DriverFrmServe(FDriverFixture& F, FDriver& Driver, const TCHAR* Path, const FHttpReply& Reply)
	{
		const bool bWantPlayers = FString(Path).Equals(TEXT("/getPlayer"), ESearchCase::CaseSensitive);
		const bool bAsked = F.runUntil(Driver, [&F, Path, bWantPlayers]
		{
			if (!bWantPlayers)
			{
				F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(0, FString()));
			}
			return F.Net.pending(Path) > 0;
		}, 40.0);
		return bAsked && F.Net.answer(Path, Reply);
	}

	/** Answers snapshots until none is open and the driver has nothing more to send within a few ticks; returns every body seen. */
	TArray<FString> DriverFrmRun(FDriverFixture& F, FDriver& Driver, double Seconds)
	{
		TArray<FString> Bodies;
		for (double Done = 0; Done < Seconds; Done += 0.5)
		{
			F.run(Driver, 0.5);
			Bodies.Append(DriverFrmDrain(F));
		}
		return Bodies;
	}
```

(`DriverFrmDrain` is defined above in the same anonymous namespace. A snapshot over 1,024 bytes is gzipped and `lastBody` cannot read it: the fakes keep names short for that reason.) Then, after `FDriverFrmDownTest`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverFrmMapLiveTest, "SatisDash.Core.Driver.FrmMapLive", SatisDash::Test::Flags)

bool FDriverFrmMapLiveTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	TestTrue(TEXT("players asked"), DriverFrmServe(F, Driver, TEXT("/getPlayer"), SatisDash::Test::Reply(200, TEXT("[{\"Name\":\"Player One\",\"Online\":true}]"))));
	TestTrue(TEXT("factory asked"), DriverFrmServe(F, Driver, TEXT("/getFactory"), SatisDash::Test::Reply(0, FString())));
	TestTrue(TEXT("then trains"), DriverFrmServe(F, Driver, TEXT("/getTrains"), SatisDash::Test::Reply(200, DriverFrmTrain)));
	TestTrue(TEXT("then stations"), DriverFrmServe(F, Driver, TEXT("/getTrainStation"), SatisDash::Test::Reply(200, DriverFrmStation)));

	// Spec "Threading": a snapshot that carries mapLive is serialised and gzipped on the worker, never on the game thread.
	F.Worker.bHold = true;
	bool bEncode = false;
	for (int32 I = 0; I < 40 && !bEncode; ++I)
	{
		F.run(Driver, 0.5);
		bEncode = Driver.EncodePending();
		if (!bEncode)
		{
			F.Worker.release(); // a parse job: let it finish
		}
	}
	TestTrue(TEXT("the snapshot that takes mapLive is handed to the worker to encode"), bEncode);
	F.Worker.bHold = false;
	F.Worker.release();
	const TArray<FString> Bodies = DriverFrmRun(F, Driver, 6.0);
	bool bMapLive = false;
	for (const FString& Body : Bodies)
	{
		bMapLive = bMapLive || Body.Contains(DriverFrmMapLive);
		TestTrue(TEXT("every snapshot still carries status"), Body.Contains(TEXT("\"status\"")));
	}
	TestTrue(TEXT("a snapshot carries mapLive, as the Node agent builds it"), bMapLive);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverFrmMapLiveDownTest, "SatisDash.Core.Driver.FrmMapLiveDown", SatisDash::Test::Flags)

bool FDriverFrmMapLiveDownTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	for (const TCHAR* Path : { TEXT("/getPlayer"), TEXT("/getFactory"), TEXT("/getTrains"), TEXT("/getTrainStation") })
	{
		TestTrue(FString::Printf(TEXT("%s asked"), Path), DriverFrmServe(F, Driver, Path, SatisDash::Test::Reply(0, FString())));
	}
	TArray<FString> Bodies = DriverFrmRun(F, Driver, 8.0);
	TestTrue(TEXT("snapshots keep flowing"), Bodies.Num() > 0);
	for (const FString& Body : Bodies)
	{
		TestFalse(TEXT("FRM down: no mapLive"), Body.Contains(TEXT("\"mapLive\"")));
		TestTrue(TEXT("status keeps flowing"), Body.Contains(TEXT("\"status\"")));
		TestTrue(TEXT("players are unavailable, not stale"), !Body.Contains(TEXT("\"players\"")) || Body.Contains(TEXT("\"players\":{\"available\":false,\"players\":[]}")));
	}
	// FRM returns: the next factory pass (30 s) asks again and the part comes back.
	TestTrue(TEXT("players asked again"), DriverFrmServe(F, Driver, TEXT("/getPlayer"), SatisDash::Test::Reply(0, FString())));
	TestTrue(TEXT("factory asked again"), DriverFrmServe(F, Driver, TEXT("/getFactory"), SatisDash::Test::Reply(0, FString())));
	TestTrue(TEXT("trains asked again"), DriverFrmServe(F, Driver, TEXT("/getTrains"), SatisDash::Test::Reply(200, DriverFrmTrain)));
	TestTrue(TEXT("stations asked again"), DriverFrmServe(F, Driver, TEXT("/getTrainStation"), SatisDash::Test::Reply(200, DriverFrmStation)));
	Bodies = DriverFrmRun(F, Driver, 12.0);
	bool bMapLive = false;
	for (const FString& Body : Bodies)
	{
		bMapLive = bMapLive || Body.Contains(DriverFrmMapLive);
	}
	TestTrue(TEXT("and the mod recovers"), bMapLive);
	return true;
}
```

- [ ] **Step 2: Run them to see them fail**

Run the mod tests with `--min 120`.
Expected: the build fails with `'TakeMapLive': is not a member of 'SatisDash::FFrmBridge'` and `'MapLiveJson': is not a member of 'SatisDash::FSnapshot'`.

- [ ] **Step 3: Implement**

`Frm.h`, after the two existing endpoints (FRM serves both on its own server thread, spec "Load spreading": `FicsitRemoteMonitoring.cpp:796-896`; the exact lines were not re-read for this plan):

```cpp
	inline constexpr FFrmEndpoint FrmGetTrains{ TEXT("getTrains"), EFrmLane::Live, false, false, FrmSmallCapBytes, 5.f };
	inline constexpr FFrmEndpoint FrmGetTrainStation{ TEXT("getTrainStation"), EFrmLane::Live, false, false, FrmSmallCapBytes, 5.f };
```

`Snapshot.h`, in `FSnapshot` after `FactoryJson`:

```cpp
		/** The mapLive part ({"trains":[...],"stations":[...]}, FRM bridge 3B) as JSON, joined by FFrmBridge; spliced in as is. */
		TOptional<FString> MapLiveJson;
```

`Snapshot.cpp`: add the third row to the `Parts` table (Task 1) after `factory`:

```cpp
		{ TEXT(",\"mapLive\":"), &Snapshot.MapLiveJson },
```

`FrmBridge.h`: `#include "SatisDash/FrmMapLive.h"`; update the class comment's first paragraph to say it reads "players, the factory and mapLive"; in the public section after `TakeFactory`:

```cpp
		/** The newest mapLive part ({"trains":[...],"stations":[...]}) finished within MaxAgeSeconds, once. */
		TOptional<FString> TakeMapLive(double Now, double MaxAgeSeconds);
```

and in the private section (before `Harvest`):

```cpp
		/** One of mapLive's two reads: not asked, waiting for FRM, parsing on the worker, or settled (Ok or Failed). */
		struct FMapJob
		{
			TUniquePtr<FFrmArrayParse> Parse;
			FFrmArrayParse::EStep Last = FFrmArrayParse::EStep::More;
			bool bRan = false;
		};
		struct FMapSide
		{
			enum class EState : uint8 { Idle, Waiting, Parsing, Ok, Failed };
			EState State = EState::Idle;
			TSharedPtr<FMapJob> Job;
			FString Json;
			int64 Bytes = 0;
			double ReadAt = 0;
		};
		void StartMapCycle();
		void HarvestMapSide(FMapSide& Side);
		void FinishMapLive(double Now);
```

with members `FMapSide MapTrains; FMapSide MapStations; TOptional<FReady> MapLiveReady;` (declare them after `FactoryReady`). Memory comment: add "one mapLive pair: two arrays of at most 500 records each (under 256 KB joined, or dropped), one body of at most 4 MB being parsed".

`FrmBridge.cpp`:

In `RequestPass`, inside `if (bFactoryDue)` after the factory enqueue: `StartMapCycle();`. Then the new functions:

```cpp
void SatisDash::FFrmBridge::StartMapCycle()
{
	using EState = FMapSide::EState;
	if (MapTrains.State != EState::Idle || MapStations.State != EState::Idle)
	{
		return; // the last pair has not settled: this cadence tick skips mapLive
	}
	MapTrains.State = EState::Waiting;
	MapStations.State = EState::Waiting;
	FrmLanes.Enqueue(FFrmRead{ &FrmGetTrains, [this](FHttpReply& Reply)
	{
		if (Reply.Code >= 200 && Reply.Code < 300)
		{
			MapTrains.Job = MakeShared<FMapJob>();
			MapTrains.Job->Parse = MakeUnique<FTrainsParse>(MoveTemp(Reply.Body));
			MapTrains.ReadAt = LastNow;
			MapTrains.State = FMapSide::EState::Parsing;
		}
		else
		{
			MapTrains.State = FMapSide::EState::Failed;
		}
	} });
	FrmLanes.Enqueue(FFrmRead{ &FrmGetTrainStation, [this](FHttpReply& Reply)
	{
		if (Reply.Code >= 200 && Reply.Code < 300)
		{
			MapStations.Job = MakeShared<FMapJob>();
			MapStations.Job->Parse = MakeUnique<FStationsParse>(MoveTemp(Reply.Body));
			MapStations.ReadAt = LastNow;
			MapStations.State = FMapSide::EState::Parsing;
		}
		else
		{
			MapStations.State = FMapSide::EState::Failed;
		}
	} });
}

void SatisDash::FFrmBridge::HarvestMapSide(FMapSide& Side)
{
	if (!Side.Job.IsValid() || !Side.Job->bRan)
	{
		return;
	}
	Side.Job->bRan = false;
	if (Side.Job->Last == FFrmArrayParse::EStep::Done)
	{
		Side.Bytes = Side.Job->Parse->Utf8Bytes();
		Side.Json = Side.Job->Parse->TakeJson();
		Side.State = FMapSide::EState::Ok;
		Side.Job.Reset();
	}
	else if (Side.Job->Last == FFrmArrayParse::EStep::Invalid)
	{
		Side.State = FMapSide::EState::Failed;
		Side.Job.Reset();
	}
}

void SatisDash::FFrmBridge::FinishMapLive(double Now)
{
	using EState = FMapSide::EState;
	const auto Settled = [](const FMapSide& Side) { return Side.State == EState::Ok || Side.State == EState::Failed; };
	if (!Settled(MapTrains) || !Settled(MapStations))
	{
		return;
	}
	if (MapTrains.State == EState::Ok && MapStations.State == EState::Ok)
	{
		TOptional<FString> Joined = JoinMapLive(MapTrains.Json, MapTrains.Bytes, MapStations.Json, MapStations.Bytes);
		if (Joined.IsSet())
		{
			MapLiveReady = FReady{ MoveTemp(Joined.GetValue()), FMath::Min(MapTrains.ReadAt, MapStations.ReadAt) };
		}
		else if (FailLog.ShouldLog(TEXT("mapLiveOverCap"), Now))
		{
			UE_LOG(LogSatisDash, Warning, TEXT("mapLive is over its %lld-byte cap; leaving it out of the snapshot"), MapLiveMaxBytes);
		}
	}
	else
	{
		NoteFailed(TEXT("mapLive"), Now);
	}
	MapTrains = FMapSide();
	MapStations = FMapSide();
}

TOptional<FString> SatisDash::FFrmBridge::TakeMapLive(double Now, double MaxAgeSeconds)
{
	return TakeFresh(MapLiveReady, Now, MaxAgeSeconds);
}
```

In `Harvest` (inside the function, after the players block): `HarvestMapSide(MapTrains); HarvestMapSide(MapStations);`. In `LaunchNext`, after the players branch and before the factory branch:

```cpp
	else if (MapTrains.Job.IsValid() && !MapTrains.Job->bRan)
	{
		Worker.Launch([Job = MapTrains.Job] { Job->Last = Job->Parse->Step(); Job->bRan = true; });
	}
	else if (MapStations.Job.IsValid() && !MapStations.Job->bRan)
	{
		Worker.Launch([Job = MapStations.Job] { Job->Last = Job->Parse->Step(); Job->bRan = true; });
	}
```

In `Tick`, after the `if (Worker.IsIdle()) { Harvest(Now); LaunchNext(); }` block and before `FrmLanes.Pump(...)`: `FinishMapLive(Now);` (it reads only state flags, so it needs no idle worker: a read that failed has no job). In the encode job lambda in `LaunchNext`, next to the other `Reset()`s: `Job->Snapshot.MapLiveJson.Reset();`. In `Stop()`: `MapTrains = FMapSide(); MapStations = FMapSide(); MapLiveReady.Reset();`.

`Driver.h`: in the public section `bool EncodePending() const { return Bridge.EncodePending(); }` (a seam for the driver tests, like `Queued()`); in the class comment's Memory paragraph add "mapLive's pair as FrmBridge.h bounds it".

`Driver.cpp`, in `TickSample` replace the factory/encode block:

```cpp
		Bridge.RequestPass(bFactoryDue);
		Snapshot.PlayersJson = Bridge.TakePlayers(Now, 2.0 * Cadence.StatusSeconds);
		Snapshot.FactoryJson = Bridge.TakeFactory(Now, Cadence.FactorySeconds);
		Snapshot.MapLiveJson = Bridge.TakeMapLive(Now, Cadence.FactorySeconds);
	}
	if (Snapshot.FactoryJson.IsSet() || Snapshot.MapLiveJson.IsSet())
	{
		Bridge.EncodeSnapshot(MoveTemp(Snapshot)); // up to megabytes of JSON, or up to 256 KB of mapLive: serialised and gzipped on the worker
	}
```

(The `else { Pusher.Enqueue(SnapshotToJson(Snapshot)); }` stays.)

Existing tests that need a one-line fix (the mapLive reads now sit behind `getFactory` in the live lane, and one request is in flight at a time, so an unanswered `getTrains` holds up whatever comes after it). Run the whole suite and fix each that fails the same way:
- `FFrmBridgeFactoryTest` (`FrmBridgeTest.cpp`): its last check expects the second `getFactory` read to start after the first part is taken. Before that final `F.tick(2)`, answer the two reads that sit ahead of it: `F.serve(TEXT("/getTrains"), SatisDash::Test::Reply(0, FString())); F.serve(TEXT("/getTrainStation"), SatisDash::Test::Reply(0, FString()));`.
- `DriverFrmTest.cpp`'s `FrmParts`, `FrmDown`, `FrmStops`, `FrmEncodeWait`, `FrmEncodeAutoPause`, `FrmStopsClean` leave `getTrains` unanswered. They are expected to pass as they are, because none of them needs a later FRM read; if one does not, answer both reads with `Reply(0, FString())` where the test answers `/getFactory`.

- [ ] **Step 4: Run the tests**

Expected: `judge-mod-test --min 120` passes. `check-api` reports nothing new unless `TUniquePtr`/`MakeUnique` rows are missing (both exist).

- [ ] **Step 5: Commit**

```bash
git add mod/SatisDashAgent/Source api-sources.md
git -c core.safecrlf=false commit -m "The bridge reads mapLive at the factory cadence and the driver sends it (FRM bridge 3B Task 3)" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

### Task 4: The golden for mapLive, the evidence, and the enrol judge

**Files:**
- Modify: `harness/scripts/frm-golden.ts`
- Modify: `harness/src/recorder.ts`, `harness/test/recorder.test.ts`
- Modify: `harness/test/snapshot-fixtures.test.ts`
- Modify: `mod/SatisDashAgent/Source/SatisDashCoreTests/Fixtures/frm-mapLive.expected.json` (written by the script)
- Modify (**owner ruling required**): `runs/enrol/pass.ts`, `harness/test/enrol-pass.test.ts`
- Modify: `README.md`, `.github/workflows/mod-test.yml` (the floor, if the owner agrees)

**Interfaces:**
- Consumes: the satisfactory-dash checkout named as `s5.checkout` in the runner's `harness.json` (read-only, with `node_modules` installed): `agent/src/sampler.ts` (`Sampler`, `conformMapLive` through it) and `packages/game-adapter` (`SatisfactoryServerAdapter.getTrains/getTrainStations`).
- Produces: `frm-golden.ts --kind mapLive --in <getTrains body> --in2 <getTrainStation body>` writes the Node agent's `mapLive` (or `null` when its 256,000-byte rule leaves it out). The recorder keeps a hash, not the name, of each train and station in evidence.

- [ ] **Step 1: Extend the golden script**

Replace the file with:

```ts
// Writes what satisfactory-dash's Node agent sends for FRM bodies (FRM bridge Plan 3A Tasks 5-6, Plan 3B Tasks 4 and 6). It
// imports the agent's own code from a satisfactory-dash checkout with node_modules installed, so the expected output is the
// Node agent's, byte for byte in meaning. Usage:
//   npx tsx scripts/frm-golden.ts --dash <satisfactory-dash checkout> --kind players|factory|rails|resourceNodes --in <body.json> --out <expected.json>
//   npx tsx scripts/frm-golden.ts --dash <checkout> --kind mapLive --in <getTrains body.json> --in2 <getTrainStation body.json> --out <expected.json>
// rails and resourceNodes write the layer's `data` array (what worldReader.ts hashes and posts under `data`); mapLive writes the
// snapshot's mapLive part exactly as Sampler builds it (500 per kind, strings cut, left out as `null` past 256,000 bytes).
import { readFileSync, writeFileSync } from "node:fs";
import { pathToFileURL } from "node:url";

const arg = (name: string) => {
  const at = process.argv.indexOf(`--${name}`);
  const value = at === -1 ? undefined : process.argv[at + 1];
  if (!value) throw new Error(`missing --${name}`);
  return value;
};
const dash = arg("dash");
const kind = arg("kind");
const readBody = (name: string) => JSON.parse(readFileSync(arg(name), "utf8"));
const body = readBody("in");
const stations = kind === "mapLive" ? readBody("in2") : undefined;
const load = (path: string) => import(pathToFileURL(`${dash}/${path}`).href);
const adapterModule = await load("packages/game-adapter/src/index.ts");
const conform = await load("agent/src/conform.ts");
const adapter = new adapterModule.SatisfactoryServerAdapter(
  { call: async () => { throw new Error("no vanilla API in a golden"); } },
  { get: async (endpoint: string) => (endpoint === "getTrainStation" ? stations : body) },
);

/** The snapshot's mapLive as the Node agent's Sampler builds it from these two bodies. */
async function mapLive() {
  const { Sampler } = await load("agent/src/sampler.ts");
  const reader = {
    readStatus: async () => ({ tickHealth: "healthy", isGameRunning: true, gamePaused: false, sessionName: "golden", connectedPlayers: 0, playerLimit: 4, tickRate: 30, totalGameDurationSeconds: 1 }),
    readPower: async () => ({ circuits: [] }),
    readFactory: async () => ({ buildings: [] }),
    readPlayers: async () => ({ available: false, players: [] }),
    readAutoPause: async () => false,
    applyAutoPause: async () => undefined,
    readMapLive: async () => {
      const [trains, stationList] = await Promise.all([adapter.getTrains(), adapter.getTrainStations()]);
      return { trains: trains.data, stations: stationList.data, dropped: trains.dropped + stationList.dropped };
    },
  };
  const logger = { info() {}, warn() {}, error() {}, addSecret() {} };
  const sampler = new Sampler({ reader, cadence: () => ({ statusSeconds: 5, powerSeconds: 5, factorySeconds: 30 }), logger });
  const snapshot = await sampler.sample(Date.now());
  return snapshot.mapLive ?? null;
}

const kinds: Record<string, () => Promise<unknown>> = {
  factory: async () => conform.conformFactory({ buildings: (await adapter.getFactoryBuildings()).map(adapterModule.mapFactoryBuilding) }).value,
  players: async () => conform.conformPlayers(await adapterModule.readPlayers(() => adapter.getPlayers())).value,
  rails: async () => (await adapter.getRails()).data,
  resourceNodes: async () => (await adapter.getResourceNodes()).data,
  mapLive,
};
const produce = kinds[kind];
if (!produce) throw new Error(`unknown --kind ${kind}`);
writeFileSync(arg("out"), `${JSON.stringify(await produce(), null, 2)}\n`);
console.log(`wrote ${kind} for ${arg("in")}`);
```

Before running it, open `agent/src/sampler.ts`, `agent/src/logger.ts` and `packages/shared/src/status.ts` in the checkout and confirm: `Sampler`'s constructor takes `{ reader, cadence, logger }`; `AgentLogger` has `info`, `warn`, `error`, `addSecret`; `readStatus` returns every `Status` field the stub names (the stub's field list was taken from `StatusSchema`'s use in `conform.ts`; add any field the schema requires).

- [ ] **Step 2: Write the expected file with the Node agent's code**

Run (the checkout path is the runner config's `s5.checkout`):

```powershell
cd harness
npx tsx scripts/frm-golden.ts --dash <s5.checkout> --kind mapLive --in ..\mod\SatisDashAgent\Source\SatisDashCoreTests\Fixtures\frm-getTrains.json --in2 ..\mod\SatisDashAgent\Source\SatisDashCoreTests\Fixtures\frm-getTrainStation.json --out ..\mod\SatisDashAgent\Source\SatisDashCoreTests\Fixtures\frm-mapLive.expected.json
```

Expected: `wrote mapLive for ...`. The file must be data-equal to the hand-worked one in Task 2 (two trains `-1026,-1473` and `2,-1`; two stations `-1000,-1473` and `50,-51`; the far train dropped). If it differs, the Node agent is right: stop and report which value differs (CLAUDE.md rule 3 applies to evidence, and a golden that disagrees with the hand-worked values means the plan or the port is wrong). Commit the script's output as written; do not edit it by hand.

- [ ] **Step 3: Write the harness tests**

`harness/test/snapshot-fixtures.test.ts`: add `import { MapLiveSchema } from "../contract/src/map";` and, inside the describe:

```ts
  it("frm-mapLive.expected.json is a valid mapLive part (the contract's caps and shapes)", () => {
    const r = MapLiveSchema.safeParse(load("frm-mapLive.expected.json"));
    expect(r.success ? [] : r.error.issues).toEqual([]);
  });
```

`harness/test/recorder.test.ts`: add after the existing player-name test (use its helpers `upstream`, `recorderFor`, `send`, `MOD_UA`, `SECRET`; mirror that test's request shape):

```ts
  it("records train and station names as hashes, never the names (they are labels a player typed)", async () => {
    const up = await upstream(() => ({ status: 200, body: '{"commandsPending":false}' }));
    const rec = await recorderFor(up.url);
    const live = {
      trains: [{ id: "T1", name: "Test Train A", x: 1, y: 2, status: "Self-Driving" }],
      stations: [{ id: "S1", name: "Test Station A", x: 3, y: 4 }],
    };
    await send(rec.url, "POST", "/agent/v1/snapshots", {
      headers: { "user-agent": MOD_UA, "content-type": "application/json" },
      body: JSON.stringify({ ...snapshot, mapLive: live }),
    });
    const recorded = rec.entries[0]!.snapshot as { mapLive: typeof live };
    expect(recorded.mapLive.trains[0]).toEqual({ id: "T1", name: sessionHash("Test Train A"), x: 1, y: 2, status: "Self-Driving" });
    expect(recorded.mapLive.stations[0]).toEqual({ id: "S1", name: sessionHash("Test Station A"), x: 3, y: 4 });
    expect(JSON.stringify(rec.entries)).not.toContain("Test Train A");
    expect(JSON.stringify(rec.entries)).not.toContain("Test Station A");
  });
```

- [ ] **Step 4: Run them to see them fail**

`cd harness; npx vitest run test/recorder.test.ts test/snapshot-fixtures.test.ts --reporter=dot`
Expected: the recorder test fails (the names are recorded as sent); the fixtures test passes.

- [ ] **Step 5: The recorder hashes mapLive names**

`harness/src/recorder.ts`: after `hashPlayers` add

```ts
/** Train and station names are labels a player typed, so evidence keeps a hash of each, as it does for player names and the session. */
function hashMapLive(snapshot: unknown): unknown {
  const live = (snapshot as { mapLive?: { trains?: unknown; stations?: unknown } } | null)?.mapLive;
  if (!live || typeof live !== "object") return snapshot;
  const hashNames = (list: unknown): unknown =>
    Array.isArray(list)
      ? list.map((item) => (item && typeof item === "object" && typeof (item as { name?: unknown }).name === "string" ? { ...item, name: sessionHash((item as { name: string }).name) } : item))
      : list;
  return { ...(snapshot as object), mapLive: { ...live, trains: hashNames(live.trains), stations: hashNames(live.stations) } };
}
```

and apply it where `hashPlayers` is applied (both places): `hashSession(hashPlayers(hashMapLive(...)))`. The oversized path keeps only the small parts, so it needs no change. Update the header comment (line 3) to "and the session name, player names and mapLive names only as their hash".

Run the two harness test files again: both pass.

- [ ] **Step 6: OWNER RULING REQUIRED before this step: the enrol judge allows `mapLive`**

`runs/enrol/pass.ts` is a pass condition (CLAUDE.md rule 3). **Do not make this edit, and do not merge PR 3B-1, until the owner has ruled on it first-hand.** The owner said on 2026-10-24 that `mapLive` stays rejected until 3B; this asks to lift that, because every 30 s snapshot of the new code carries `mapLive` once FRM answers, and S13 would otherwise fail every world round. Proposed diff (the ruling's date goes where marked):

```diff
-// Owner ruling (2026-10-24, FRM bridge Plan 3A Task 10; S13 snapshot fields): the FRM bridge adds players and factory to
-// the snapshot (spec "What the Node agent sends"), so both join SNAPSHOT_KEYS. Any other field still fails; mapLive is
-// added with Plan 3B.
+// Owner ruling (2026-10-24, FRM bridge Plan 3A Task 10; S13 snapshot fields): the FRM bridge adds players and factory to
+// the snapshot (spec "What the Node agent sends"), so both join SNAPSHOT_KEYS. Any other field still fails.
+// Owner ruling (<the date the owner rules>, FRM bridge Plan 3B Task 4; S13 snapshot fields): mapLive (trains and stations,
+// spec "What the Node agent sends") joins SNAPSHOT_KEYS with PR 3B-1. Any other field still fails.
-const SNAPSHOT_KEYS = new Set(["agentVersion", "observedAt", "reachable", "paused", "status", "power", "settings", "players", "factory"]);
+const SNAPSHOT_KEYS = new Set(["agentVersion", "observedAt", "reachable", "paused", "status", "power", "settings", "players", "factory", "mapLive"]);
```

and in `harness/test/enrol-pass.test.ts` replace the test at `accepts players and factory in a snapshot (S13, owner ruling 2026-10-24) but not mapLive` with:

```ts
  it("accepts players, factory and mapLive in a snapshot (S13, owner rulings 2026-10-24 and <date>) but no other field", () => {
    const ok = round(30, true, { snapshot: { players: { available: true, players: [] }, factory: { available: true, buildings: [] }, mapLive: { trains: [], stations: [] } } });
    expect(evaluate(evidence(withRound(ok))).reasons.filter((x) => x.includes("S13"))).toEqual([]);
    const bad = round(30, true, { snapshot: { players: { available: true, players: [] }, conveyors: {} } });
    expect(evaluate(evidence(withRound(bad))).reasons).toContain(`${bad.at}: S13: unexpected snapshot field conveyors`);
  });
```

(Keep the unmodified test above it, `fails an extra top-level snapshot field (S13)`.) Then `npx vitest run test/enrol-pass.test.ts --reporter=dot` passes.

- [ ] **Step 7: Docs and the floor**

`README.md`, in the paragraph that says what the mod sends beyond status and power, add `mapLive` (trains and stations, from FRM's `getTrains` and `getTrainStation`) beside players and the factory. **OWNER DECISION (stricter, not a weakening):** `.github/workflows/mod-test.yml:36` runs `--min 80` while the suite has 104 tests (and 120 after this PR). Raising the floor to 120 cannot make a failing run pass; it is a limit in a workflow file, so make this edit only if the owner agrees:

```diff
-      - run: npm run harness -- test-mod --out "$env:RUNNER_TEMP/mod-test" --min 80
+      - run: npm run harness -- test-mod --out "$env:RUNNER_TEMP/mod-test" --min 120
```

- [ ] **Step 8: Run everything for PR 3B-1**

```powershell
cd harness
npx vitest run --reporter=dot
npm run typecheck
npm run harness -- check-api
npm run harness -- verify-contract
```

Expected: all pass; `check-api` ends `0 missing, 0 bad rows`. Then the mod tests with `--min 120`.

- [ ] **Step 9: Commit, then the owner's gates for PR 3B-1**

```bash
git add harness mod/SatisDashAgent/Source/SatisDashCoreTests/Fixtures README.md
git -c core.safecrlf=false commit -m "mapLive golden from the Node agent, names hashed in evidence (FRM bridge 3B Task 4)" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
# only after the owner's ruling:
git add runs/enrol/pass.ts harness/test/enrol-pass.test.ts
git -c core.safecrlf=false commit -m "Enrol judge: S13 allows mapLive (owner ruling <date>) (FRM bridge 3B Task 4)" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

Before PR 3B-1 merges the owner dispatches `mod-test` on the branch and approves its `ue-runner` deployment, then the core runs `enrol,revoke,command` through `run-backend.ps1`.

---

## PR 3B-2: the world sender (spec step 4)

### Task 5: The world request, its answer, and the two world endpoints

**Files:**
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Requests.h`, `Private/Requests.cpp`
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Responses.h`, `Private/Responses.cpp`
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Frm.h`
- Test: `RequestsTest.cpp`, `ResponsesTest.cpp`, `FrmQueueTest.cpp`

**Interfaces:**
- Produces:
  - `FRequestSpec BuildWorldRequest(const FAgentConfig&, const FCredential&, const TCHAR* Layer, FEncodedBody Body)`: `POST <backend>/agent/v1/world/<Layer>`, the secret only in `Authorization`, `Content-Type: application/json`, `Content-Encoding: gzip` when `Body.bGzip`, the body bytes as given (backendClient.ts `postWorldLayer`, `endpoints.agentApi.world`).
  - `struct FWorldResponse { bool bAccepted; bool bUnchanged; }` and `TOptional<FWorldResponse> ParseWorldResponse(const FHttpReply&)`: a 2xx JSON object with boolean `accepted` and `unchanged` (`WorldIngestResponseSchema`); unset otherwise. The Node agent treats a 2xx whose body is not that as transient (`backendClient.ts` `request`: "not in the expected form"), so the sender does too.
  - `FrmGetTrainRails`, `FrmGetResourceNode`: world lane, large, not passable, `FrmSmallCapBytes` (4 MB), 15 s.

- [ ] **Step 1: Write the failing tests**

`RequestsTest.cpp`, at the end (uses the file's `Config()`, `Credential()`, `Header()`, `BodyText()`):

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRequestsWorldTest, "SatisDash.Core.Requests.World", SatisDash::Test::Flags)

bool FRequestsWorldTest::RunTest(const FString& Parameters)
{
	FEncodedBody Plain;
	Plain.Bytes = SatisDash::Test::Utf8(TEXT("{\"observedAt\":\"2026-10-24T12:00:00.000Z\",\"data\":[]}"));
	const FRequestSpec Spec = BuildWorldRequest(Config(), Credential(), TEXT("rails"), Plain);
	SatisDash::Test::SameText(*this, TEXT("verb"), Spec.Verb, TEXT("POST"));
	SatisDash::Test::SameText(*this, TEXT("url"), Spec.Url, TEXT("https://example.com/base/agent/v1/world/rails"));
	SatisDash::Test::SameText(*this, TEXT("the secret only in Authorization"), Header(Spec, TEXT("Authorization")), TEXT("Bearer ") + SatisDash::Test::FakeSecret());
	SatisDash::Test::SameText(*this, TEXT("content type"), Header(Spec, TEXT("Content-Type")), TEXT("application/json"));
	TestTrue(TEXT("not gzipped"), Header(Spec, TEXT("Content-Encoding")).IsEmpty());
	SatisDash::Test::SameText(*this, TEXT("the body as given"), BodyText(Spec), TEXT("{\"observedAt\":\"2026-10-24T12:00:00.000Z\",\"data\":[]}"));
	SatisDash::Test::SameText(*this, TEXT("user agent: the mod's version only"), Header(Spec, TEXT("User-Agent")), TEXT("satisfactory-dash-") + AgentVersion());
	TestEqual(TEXT("the backend's timeout"), Spec.TimeoutSeconds, RequestTimeoutSeconds);
	TestEqual(TEXT("and the backend's 64 KB answer cap"), Spec.ResponseCapBytes, int64(MaxResponseBytes));

	FEncodedBody Packed;
	Packed.Bytes = TArray<uint8>{ 0x1F, 0x8B, 0x08 };
	Packed.bGzip = true;
	const FRequestSpec Gz = BuildWorldRequest(Config(), Credential(), TEXT("resourceNodes"), Packed);
	SatisDash::Test::SameText(*this, TEXT("another layer's url"), Gz.Url, TEXT("https://example.com/base/agent/v1/world/resourceNodes"));
	SatisDash::Test::SameText(*this, TEXT("gzip says so"), Header(Gz, TEXT("Content-Encoding")), TEXT("gzip"));
	TestTrue(TEXT("and the bytes are untouched"), Gz.Body == Packed.Bytes);
	return true;
}
```

`ResponsesTest.cpp`, at the end:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResponsesWorldTest, "SatisDash.Core.Responses.World", SatisDash::Test::Flags)

bool FResponsesWorldTest::RunTest(const FString& Parameters)
{
	const TOptional<FWorldResponse> Accepted = ParseWorldResponse(SatisDash::Test::Reply(200, TEXT("{\"accepted\":true,\"unchanged\":false}")));
	TestTrue(TEXT("200 with both booleans"), Accepted.IsSet() && Accepted->bAccepted && !Accepted->bUnchanged);
	const TOptional<FWorldResponse> Same = ParseWorldResponse(SatisDash::Test::Reply(202, TEXT("{\"accepted\":true,\"unchanged\":true}")));
	TestTrue(TEXT("202, unchanged"), Same.IsSet() && Same->bUnchanged);
	TestTrue(TEXT("accepted false is still a valid answer (the Node agent's schema takes it)"), ParseWorldResponse(SatisDash::Test::Reply(200, TEXT("{\"accepted\":false,\"unchanged\":false}"))).IsSet());
	TestFalse(TEXT("a missing field"), ParseWorldResponse(SatisDash::Test::Reply(200, TEXT("{\"accepted\":true}"))).IsSet());
	TestFalse(TEXT("a string for a boolean"), ParseWorldResponse(SatisDash::Test::Reply(200, TEXT("{\"accepted\":\"yes\",\"unchanged\":false}"))).IsSet());
	TestFalse(TEXT("an empty body"), ParseWorldResponse(SatisDash::Test::Reply(200, FString())).IsSet());
	TestFalse(TEXT("an array"), ParseWorldResponse(SatisDash::Test::Reply(200, TEXT("[]"))).IsSet());
	TestFalse(TEXT("not json"), ParseWorldResponse(SatisDash::Test::Reply(200, TEXT("{\"accepted\":true,\"unchanged\":false}"), TEXT("text/html"))).IsSet());
	TestFalse(TEXT("an error status"), ParseWorldResponse(SatisDash::Test::Reply(503, TEXT("{\"accepted\":true,\"unchanged\":false}"))).IsSet());
	return true;
}
```

`FrmQueueTest.cpp`, at the end:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmQueueWorldEndpointsTest, "SatisDash.Core.FrmQueue.WorldEndpoints", SatisDash::Test::Flags)

bool FFrmQueueWorldEndpointsTest::RunTest(const FString& Parameters)
{
	for (const FFrmEndpoint* Large : { &FrmGetTrainRails, &FrmGetResourceNode })
	{
		TestTrue(FString::Printf(TEXT("%s: world lane"), Large->Name), Large->Lane == EFrmLane::World);
		TestTrue(FString::Printf(TEXT("%s: large, and never passes a large parse"), Large->Name), Large->bLarge && !Large->bMayPassLarge);
		TestEqual(FString::Printf(TEXT("%s: 4 MB cap"), Large->Name), Large->CapBytes, FrmSmallCapBytes);
		TestEqual(FString::Printf(TEXT("%s: 15 s"), Large->Name), Large->TimeoutSeconds, 15.f);
	}
	for (const FFrmEndpoint* Small : { &FrmGetTrains, &FrmGetTrainStation })
	{
		TestTrue(FString::Printf(TEXT("%s: live lane, small, not passable"), Small->Name), Small->Lane == EFrmLane::Live && !Small->bLarge && !Small->bMayPassLarge);
		TestEqual(FString::Printf(TEXT("%s: 4 MB cap"), Small->Name), Small->CapBytes, FrmSmallCapBytes);
		TestEqual(FString::Printf(TEXT("%s: 5 s"), Small->Name), Small->TimeoutSeconds, 5.f);
	}
	SatisDash::Test::SameText(*this, TEXT("rails url"), BuildFrmRequest(8080, FString(), FrmGetTrainRails).Url, TEXT("http://127.0.0.1:8080/getTrainRails"));
	SatisDash::Test::SameText(*this, TEXT("nodes url"), BuildFrmRequest(18081, FString(), FrmGetResourceNode).Url, TEXT("http://127.0.0.1:18081/getResourceNode"));

	// The real endpoints in the queue: queued world first, live second, and the live lane still goes first.
	SatisDash::Test::FFakeTransport Net;
	FFrmQueue FrmLanes{ Net };
	FrmLanes.Enqueue(FrmQueueRead(FrmGetTrainRails, nullptr));
	FrmLanes.Enqueue(FrmQueueRead(FrmGetResourceNode, nullptr));
	FrmLanes.Enqueue(FrmQueueRead(FrmGetTrains, nullptr));
	FrmLanes.Enqueue(FrmQueueRead(FrmGetPlayer, nullptr));
	FrmLanes.Pump(8080, FString(), false);
	TestEqual(TEXT("live first, in order: trains"), Net.pending(TEXT("/getTrains")), 1);
	Net.answer(TEXT("/getTrains"), SatisDash::Test::Reply(200, TEXT("[]")));
	FrmLanes.Pump(8080, FString(), true);
	TestEqual(TEXT("a large parse is held: getPlayer passes it"), Net.pending(TEXT("/getPlayer")), 1);
	Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, TEXT("[]")));
	FrmLanes.Pump(8080, FString(), true);
	TestEqual(TEXT("and the world lane waits for the parse"), Net.Open.Num(), 0);
	FrmLanes.Pump(8080, FString(), false);
	TestEqual(TEXT("then rails"), Net.pending(TEXT("/getTrainRails")), 1);
	Net.answer(TEXT("/getTrainRails"), SatisDash::Test::Reply(200, TEXT("[]")));
	FrmLanes.Pump(8080, FString(), true);
	TestEqual(TEXT("resource nodes wait for the rails parse: one large body at a time"), Net.Open.Num(), 0);
	FrmLanes.Pump(8080, FString(), false);
	TestEqual(TEXT("then resource nodes"), Net.pending(TEXT("/getResourceNode")), 1);
	return true;
}
```

- [ ] **Step 2: Run them to see them fail**

Run the mod tests with `--min 123`.
Expected: the build fails with `'BuildWorldRequest': identifier not found` and `'FrmGetTrainRails': undeclared identifier`.

- [ ] **Step 3: Implement**

`Requests.h`, after `BuildSnapshotRequest(... FEncodedBody ...)`:

```cpp
	/** POST /agent/v1/world/<Layer> (backendClient.ts postWorldLayer): a world layer's body, already encoded (gzipped when over 1 KB). The secret goes only in the Authorization header (S7). */
	SATISDASHCORE_API FRequestSpec BuildWorldRequest(const FAgentConfig& Config, const FCredential& Credential, const TCHAR* Layer, FEncodedBody Body);
```

`Requests.cpp`, after the second `BuildSnapshotRequest`:

```cpp
SatisDash::FRequestSpec SatisDash::BuildWorldRequest(const FAgentConfig& Config, const FCredential& Credential, const TCHAR* Layer, FEncodedBody Body)
{
	FRequestSpec Spec = Start(TEXT("POST"), Config.BackendUrl + TEXT("/agent/v1/world/") + PercentEncode(FString(Layer)));
	Authorize(Spec, Credential);
	SetBody(Spec, MoveTemp(Body));
	return Spec;
}
```

`Responses.h`, after `FSnapshotResponse`:

```cpp
	/** WorldIngestResponseSchema (map.ts): what POST /agent/v1/world/:layer answers on 200 or 202. */
	struct FWorldResponse
	{
		bool bAccepted = false;
		bool bUnchanged = false;
	};
```

and after `ParseSnapshotResponse`:

```cpp
	/** A 2xx JSON object (S4's limits) with boolean accepted and unchanged. Unset otherwise: the sender treats that as transient, as backendClient.ts does. */
	SATISDASHCORE_API TOptional<FWorldResponse> ParseWorldResponse(const FHttpReply& Reply);
```

`Responses.cpp`, at the end:

```cpp
TOptional<SatisDash::FWorldResponse> SatisDash::ParseWorldResponse(const FHttpReply& Reply)
{
	if (Reply.Code < 200 || Reply.Code >= 300)
	{
		return {};
	}
	const TSharedPtr<FJsonObject> Object = ReadJsonBody(Reply);
	const TSharedPtr<FJsonValue> Accepted = SatisDash::Json::Field(Object, TEXT("accepted"), EJson::Boolean);
	const TSharedPtr<FJsonValue> Unchanged = SatisDash::Json::Field(Object, TEXT("unchanged"), EJson::Boolean);
	if (!Accepted.IsValid() || !Unchanged.IsValid())
	{
		return {};
	}
	FWorldResponse Out;
	Out.bAccepted = Accepted->AsBool();
	Out.bUnchanged = Unchanged->AsBool();
	return Out;
}
```

`Frm.h`, after the two train endpoints:

```cpp
	// Both are large (the captured world's 608 nodes are about 288 KB; a big world's rails more), so they are world-lane reads
	// that never start while another large body is held. FRM runs getTrainRails on its own server thread and getResourceNode
	// on the game thread (spec "Load spreading"), which is why the node read has the longest cadence. The 15 s timeout is the
	// mod's; the Node agent uses 5 s for every FRM read (connection.ts:47). The cap is the spec's "4 MB for the rest".
	inline constexpr FFrmEndpoint FrmGetTrainRails{ TEXT("getTrainRails"), EFrmLane::World, true, false, FrmSmallCapBytes, 15.f };
	inline constexpr FFrmEndpoint FrmGetResourceNode{ TEXT("getResourceNode"), EFrmLane::World, true, false, FrmSmallCapBytes, 15.f };
```

(`FrmSmallCapBytes` is named for the "small" tier of the spec's cap table; do not rename it here.)

- [ ] **Step 4: Run the tests**

Expected: `judge-mod-test --min 123` passes; `check-api` clean (no new symbol).

- [ ] **Step 5: Commit**

```bash
git add mod/SatisDashAgent/Source api-sources.md
git -c core.safecrlf=false commit -m "The world request, its answer and the rails and resource-node endpoints (FRM bridge 3B Task 5)" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

### Task 6: Rails and resource nodes

**Files:**
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/FrmRecords.h`, `Private/FrmRecords.cpp` (`WorldBodyPrefix`)
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Snapshot.h`, `Private/Snapshot.cpp` (`ClampStringTo`)
- Create: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/FrmRails.h`, `Private/FrmRails.cpp`
- Create: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/FrmResourceNodes.h`, `Private/FrmResourceNodes.cpp`
- Create: `.../SatisDashCoreTests/Fixtures/frm-getTrainRails.json`, `frm-getTrainRails.expected.json`, `frm-getResourceNode.json`, `frm-getResourceNode.expected.json`
- Test: new `FrmRailsTest.cpp`, new `FrmResourceNodesTest.cpp`; `harness/test/snapshot-fixtures.test.ts`

**Interfaces:**
- Consumes: `FFrmArrayParse`, `ReadFrmLocation`, `ToWholeMetres`, `InWholeMetreBounds`, `Json::AppendString` (Task 2).
- Produces:
  - `FString WorldBodyPrefix(const FString& ObservedAt)`: `{"observedAt":"...","data":`.
  - `FString ClampStringTo(const FString&, int32 MaxChars)` (`ClampString` is `ClampStringTo(.., 200)`).
  - `FRailsParse(TArray<uint8> Body, const FString& ObservedAt)` and `FResourceNodesParse(...)`: each yields the whole world body `{"observedAt":...,"data":[...]}`; `DataFrom()`/`DataEnd()` bound the `data` array text.
  - `FString MapNodeType(const FString& Raw)`; `MaxRailSegments = 5000`, `MaxRailPoints = 1000`, `MaxResourceNodes = 5000`, `MaxNodeFieldChars = 40`.

The Node agent's rules being ported:
- **Rails** (`RawFrmTrainRailSchema`, `railsMapper.ts`): the body is an array of objects with a string `ID` of at least 1 character and a `SplineData` array of objects with finite numbers `x` and `y` (anything else in a point, including `z`, is skipped). A segment's points are each rounded to whole metres; a point out of +/-1,000,000 m is skipped; a point equal to the last kept one (after rounding) is skipped; a segment left with fewer than 2 points is dropped. Output `{"id","points":[[x,y],...]}`.
- **Resource nodes** (`RawFrmResourceNodeSchema`, `resourceNodesMapper.ts`, `nodeType.ts`): strings `Name`, `Purity`, `NodeType`, boolean `Exploited`, and a `location` as above. Output `{"type":Name,"purity":Purity lower-cased,"nodeType":mapNodeType(NodeType),"x","y","exploited"}`; a node with a bad coordinate is dropped. `Purity` is used, not `EnumPurity` (whose "Inpure" typo the Node agent does not correct).
- Where the contract (`map.ts`) is stricter than the Node agent's code, the mod follows the contract (owner-facing finding 3): strings cut to 200 (`purity` and `nodeType` to 40), a segment with more than 1,000 points dropped, the first 5,000 items kept; all three are counted.

- [ ] **Step 1: The fixtures**

`Fixtures/frm-getTrainRails.json`:

```json
[
{"ID":"Build_RailroadTrack_C_1","ClassName":"Build_RailroadTrack_C","location":{"x":0,"y":0,"z":0},"Length":1000,"Connected0":true,"Connected1":true,"SplineData":[{"x":0,"y":0,"z":0},{"x":10,"y":10,"z":0},{"x":250,"y":0,"z":0},{"x":250.4,"y":0,"z":0},{"x":500,"y":1.0e12,"z":0},{"x":1050,"y":-50,"z":0}]},
{"ID":"Build_RailroadTrack_C_2","SplineData":[{"x":100,"y":100},{"x":120,"y":120}]},
{"ID":"Build_RailroadTrack_C_3","SplineData":[]}
]
```

`Fixtures/frm-getTrainRails.expected.json` (hand-worked; replaced by the Node agent's output in Step 6): the first rail keeps (0,0), skips (10,10) (rounds to the same point), keeps (250,0) as (3,0), skips (250.4,0), skips the out-of-bounds point, keeps (1050,-50) as (11,0) (10.5 rounds up, -0.5 rounds to 0); the second has one point left and the third none, so both are dropped:

```json
[
  { "id": "Build_RailroadTrack_C_1", "points": [[0, 0], [3, 0], [11, 0]] }
]
```

`Fixtures/frm-getResourceNode.json`:

```json
[
{"ID":"BP_ResourceNode1","Name":"Iron Ore","ClassName":"BP_ResourceNode_C","Purity":"Normal","EnumPurity":"RP_Normal","NodeType":"Node","Exploited":false,"ResourceForm":"RF_SOLID","location":{"x":-12345,"y":67890,"z":100}},
{"Name":"Crude Oil","Purity":"Impure","EnumPurity":"RP_Inpure","NodeType":"Fracking Satellite","Exploited":true,"location":{"x":50,"y":-50,"z":0}},
{"Name":"Geothermal","Purity":"Pure","NodeType":"Geyser","Exploited":false,"location":{"x":0,"y":0,"z":0}},
{"Name":"SAM","Purity":"Pure","NodeType":"Some New TYPE","Exploited":false,"location":{"x":1.0e12,"y":0,"z":0}},
{"Name":"Copper Ore","Purity":"PURE","NodeType":"some  new   TYPE","Exploited":false,"location":{"x":100,"y":200,"z":0}}
]
```

`Fixtures/frm-getResourceNode.expected.json` (hand-worked: -123.45 rounds to -123, 678.9 to 679, 0.5 to 1, -0.5 to 0; the SAM node is out of bounds and dropped; "Some New TYPE" and "some  new   TYPE" both become `someNewType`):

```json
[
  { "type": "Iron Ore", "purity": "normal", "nodeType": "node", "x": -123, "y": 679, "exploited": false },
  { "type": "Crude Oil", "purity": "impure", "nodeType": "frackingSatellite", "x": 1, "y": 0, "exploited": true },
  { "type": "Geothermal", "purity": "pure", "nodeType": "geyser", "x": 0, "y": 0, "exploited": false },
  { "type": "Copper Ore", "purity": "pure", "nodeType": "someNewType", "x": 1, "y": 2, "exploited": false }
]
```

- [ ] **Step 2: Write the failing tests**

`FrmRailsTest.cpp`:

```cpp
#include "SatisDash/FrmRails.h"
#include "Dom/JsonObject.h"
#include "SatisDash/JsonUtil.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	const TCHAR* FrmRailsAt = TEXT("2026-10-24T12:00:00.000Z");

	struct FFrmRailsRun
	{
		bool bOk = false;
		FString Json;
		FString Data;
		int32 Kept = 0;
		int32 Dropped = 0;
		int32 Over = 0;
		int32 Steps = 0;
		int64 Bytes = 0;
	};

	FFrmRailsRun FrmRailsOf(const FString& Json, int32 Slice = FrmSliceRecords)
	{
		FRailsParse Parse{ SatisDash::Test::Utf8(Json), FrmRailsAt };
		FFrmRailsRun Run;
		FFrmArrayParse::EStep Step = FFrmArrayParse::EStep::More;
		while (Step == FFrmArrayParse::EStep::More)
		{
			Step = Parse.Step(Slice);
			++Run.Steps;
		}
		Run.bOk = Step == FFrmArrayParse::EStep::Done;
		Run.Kept = Parse.Kept();
		Run.Dropped = Parse.Dropped();
		Run.Over = Parse.Over();
		Run.Bytes = Parse.Utf8Bytes();
		if (Run.bOk)
		{
			Run.Data = Parse.PeekJson().Mid(Parse.DataFrom(), Parse.DataEnd() - Parse.DataFrom());
			Run.Json = Parse.TakeJson();
		}
		return Run;
	}

	/** The body a world sender posts for this data array. */
	FString FrmRailsBody(const FString& Data)
	{
		return FString::Printf(TEXT("{\"observedAt\":\"%s\",\"data\":%s}"), FrmRailsAt, *Data);
	}

	/** One segment; Spline is the inside of the SplineData array. */
	FString FrmRailsSegment(const FString& Id, const FString& Spline)
	{
		return FString::Printf(TEXT("{\"ID\":\"%s\",\"SplineData\":[%s]}"), *Id, *Spline);
	}

	/** Count distinct points, one metre apart on x (x = i * 100 cm). */
	FString FrmRailsPoints(int32 Count)
	{
		FString Points;
		for (int32 I = 0; I < Count; ++I)
		{
			Points += FString::Printf(TEXT("%s{\"x\":%d,\"y\":0}"), I > 0 ? TEXT(",") : TEXT(""), I * 100);
		}
		return Points;
	}

	int32 FrmRailsPointCount(const FString& Body, int32 Segment)
	{
		const TSharedPtr<FJsonObject> Parsed = SatisDash::Json::ParseObject(Body);
		if (!Parsed.IsValid())
		{
			return -1;
		}
		const TArray<TSharedPtr<FJsonValue>>& Data = Parsed->GetArrayField(TEXT("data"));
		return Data.IsValidIndex(Segment) ? Data[Segment]->AsObject()->GetArrayField(TEXT("points")).Num() : -1;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmRailsRulesTest, "SatisDash.Core.FrmRails.Rules", SatisDash::Test::Flags)

bool FFrmRailsRulesTest::RunTest(const FString& Parameters)
{
	const FFrmRailsRun Simple = FrmRailsOf(TEXT("[") + FrmRailsSegment(TEXT("R1"), TEXT("{\"x\":0,\"y\":0},{\"x\":300,\"y\":0}")) + TEXT("]"));
	SatisDash::Test::SameText(*this, TEXT("the whole world body, with observedAt, in the Node agent's field order"), Simple.Json, FrmRailsBody(TEXT("[{\"id\":\"R1\",\"points\":[[0,0],[3,0]]}]")));
	SatisDash::Test::SameText(*this, TEXT("DataFrom and DataEnd bound the data array"), Simple.Data, TEXT("[{\"id\":\"R1\",\"points\":[[0,0],[3,0]]}]"));
	TestEqual(TEXT("the byte count is the body's UTF-8 length"), Simple.Bytes, int64(FTCHARToUTF8(*Simple.Json).Length()));
	SatisDash::Test::SameText(*this, TEXT("no rails: an empty layer, which still posts the first time"), FrmRailsOf(TEXT("[]")).Json, FrmRailsBody(TEXT("[]")));

	// railsMapper.ts: rounding (2.5 is 3, 10.5 is 11, -0.5 is 0), consecutive duplicates collapsed, a bad point skipped.
	const FFrmRailsRun Mapped = FrmRailsOf(TEXT("[") + FrmRailsSegment(TEXT("R1"),
		TEXT("{\"x\":0,\"y\":0,\"z\":0},{\"x\":10,\"y\":10},{\"x\":250,\"y\":0},{\"x\":250.4,\"y\":0},{\"x\":500,\"y\":1.0e12},{\"x\":1050,\"y\":-50}")) + TEXT("]"));
	SatisDash::Test::SameText(*this, TEXT("the fixture's first rail"), Mapped.Data, TEXT("[{\"id\":\"R1\",\"points\":[[0,0],[3,0],[11,0]]}]"));
	// A segment with fewer than two points left is dropped, not refused.
	const FFrmRailsRun Short = FrmRailsOf(TEXT("[") + FrmRailsSegment(TEXT("R1"), TEXT("{\"x\":0,\"y\":0},{\"x\":1e12,\"y\":0}")) + TEXT(",") + FrmRailsSegment(TEXT("R2"), TEXT("")) + TEXT("]"));
	TestTrue(TEXT("one point left and none: both dropped, the body still valid"), Short.bOk && Short.Kept == 0 && Short.Dropped == 2);
	SatisDash::Test::SameText(*this, TEXT("and the layer is empty"), Short.Data, TEXT("[]"));

	// Tolerated: fields the map doesn't use, however shaped.
	TestTrue(TEXT("extra fields, z and pitch are skipped"), FrmRailsOf(TEXT("[{\"ID\":\"R1\",\"Connected0\":true,\"location\":{\"x\":1},\"Cars\":[[1,[2]]],\"SplineData\":[{\"x\":0,\"y\":0,\"z\":9,\"pitch\":1},{\"x\":300,\"y\":0}]}]")).Kept == 1);
	// Refused as zod's RawFrmTrainRailSchema refuses them: the whole layer is left out.
	for (const TCHAR* Bad : {
		TEXT("[{\"ID\":\"\",\"SplineData\":[]}]"), TEXT("[{\"SplineData\":[]}]"), TEXT("[{\"ID\":1,\"SplineData\":[]}]"),
		TEXT("[{\"ID\":\"R\"}]"), TEXT("[{\"ID\":\"R\",\"SplineData\":null}]"), TEXT("[{\"ID\":\"R\",\"SplineData\":{}}]"),
		TEXT("[{\"ID\":\"R\",\"SplineData\":[1]}]"), TEXT("[{\"ID\":\"R\",\"SplineData\":[{\"x\":0}]}]"),
		TEXT("[{\"ID\":\"R\",\"SplineData\":[{\"x\":\"0\",\"y\":0}]}]"), TEXT("[{\"ID\":\"R\",\"SplineData\":[{\"x\":1e999,\"y\":0}]}]"),
		TEXT("[1]"), TEXT("{}"), TEXT("[] []"), TEXT("") })
	{
		TestFalse(FString::Printf(TEXT("refused: %s"), Bad), FrmRailsOf(Bad).bOk);
	}
	TestFalse(TEXT("10,000 deep"), FrmRailsOf(FString::ChrN(10000, TEXT('[')) + FString::ChrN(10000, TEXT(']'))).bOk);
	TestFalse(TEXT("deep inside a skipped field"), FrmRailsOf(TEXT("[{\"ID\":\"R\",\"SplineData\":[],\"x\":") + FString::ChrN(10000, TEXT('[')) + FString::ChrN(10000, TEXT(']')) + TEXT("}]")).bOk);

	// Strings: the id is cut to 200 (the contract's bound), escaped.
	const FFrmRailsRun Long = FrmRailsOf(TEXT("[") + FrmRailsSegment(FString::ChrN(201, TEXT('a')), TEXT("{\"x\":0,\"y\":0},{\"x\":300,\"y\":0}")) + TEXT("]"));
	TestTrue(TEXT("a 201-character id is cut to 200"), Long.Data.Contains(TEXT("\"id\":\"") + FString::ChrN(200, TEXT('a')) + TEXT("\",")));

	// Slices: the output doesn't depend on the slice size.
	FString Many = TEXT("[");
	for (int32 I = 0; I < 12; ++I)
	{
		Many += (I > 0 ? TEXT(",") : TEXT("")) + FrmRailsSegment(FString::Printf(TEXT("R%d"), I), TEXT("{\"x\":0,\"y\":0},{\"x\":300,\"y\":0}"));
	}
	Many += TEXT("]");
	const FFrmRailsRun OneAtATime = FrmRailsOf(Many, 1);
	const FFrmRailsRun AllAtOnce = FrmRailsOf(Many);
	TestEqual(TEXT("one record a slice: 12 records and the end"), OneAtATime.Steps, 13);
	TestEqual(TEXT("2,000 a slice: one step"), AllAtOnce.Steps, 1);
	SatisDash::Test::SameText(*this, TEXT("the same output"), OneAtATime.Json, AllAtOnce.Json);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmRailsCapsTest, "SatisDash.Core.FrmRails.Caps", SatisDash::Test::Flags)

bool FFrmRailsCapsTest::RunTest(const FString& Parameters)
{
	// 5,000 segments: the first 5,000 kept, the rest counted (the contract's cap; the Node agent would post them all and be refused).
	FString Many = TEXT("[");
	for (int32 I = 0; I < MaxRailSegments + 1; ++I)
	{
		Many += (I > 0 ? TEXT(",") : TEXT("")) + FrmRailsSegment(FString::Printf(TEXT("R%d"), I), TEXT("{\"x\":0,\"y\":0},{\"x\":300,\"y\":0}"));
	}
	Many += TEXT("]");
	const FFrmRailsRun Capped = FrmRailsOf(Many);
	TestTrue(TEXT("5,001 segments parse"), Capped.bOk);
	TestEqual(TEXT("5,000 kept"), Capped.Kept, MaxRailSegments);
	TestEqual(TEXT("one over the cap"), Capped.Over, 1);
	TestFalse(TEXT("the 5,001st is not in the layer"), Capped.Json.Contains(FString::Printf(TEXT("\"id\":\"R%d\""), MaxRailSegments)));

	// 1,000 points are kept; 1,001 drop the segment (a partial line would mislead).
	const FFrmRailsRun Exactly = FrmRailsOf(TEXT("[") + FrmRailsSegment(TEXT("R1"), FrmRailsPoints(MaxRailPoints)) + TEXT("]"));
	TestEqual(TEXT("1,000 points: kept whole"), FrmRailsPointCount(Exactly.Json, 0), MaxRailPoints);
	const FFrmRailsRun TooLong = FrmRailsOf(TEXT("[") + FrmRailsSegment(TEXT("R1"), FrmRailsPoints(MaxRailPoints + 1)) + TEXT(",") + FrmRailsSegment(TEXT("R2"), FrmRailsPoints(3)) + TEXT("]"));
	TestTrue(TEXT("1,001 points: the segment is dropped, the next one kept"), TooLong.bOk && TooLong.Dropped == 1 && TooLong.Kept == 1 && FrmRailsPointCount(TooLong.Json, 0) == 3);

	// zod validates every segment before the cap applies: a bad one past the cap still refuses the layer.
	TestFalse(TEXT("an invalid 5,002nd segment refuses the layer"), FrmRailsOf(Many.LeftChop(1) + TEXT(",{\"ID\":\"\"}]")).bOk);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmRailsGoldenTest, "SatisDash.Core.FrmRails.Golden", SatisDash::Test::Flags)

bool FFrmRailsGoldenTest::RunTest(const FString& Parameters)
{
	const FString Body = SatisDash::Test::Fixture(TEXT("frm-getTrainRails.json"));
	const FString Expected = SatisDash::Test::Fixture(TEXT("frm-getTrainRails.expected.json"));
	if (!TestFalse(TEXT("the fixtures were found"), Body.IsEmpty() || Expected.IsEmpty()))
	{
		return false;
	}
	const FFrmRailsRun Run = FrmRailsOf(Body);
	TestTrue(TEXT("parses"), Run.bOk);
	TestEqual(TEXT("two segments dropped"), Run.Dropped, 2);
	TestTrue(FString::Printf(TEXT("is the Node agent's rails layer; got %s"), *Run.Json), SatisDash::Test::SameJson(Run.Json, FrmRailsBody(Expected)));
	return true;
}
```

`FrmResourceNodesTest.cpp`:

```cpp
#include "SatisDash/FrmResourceNodes.h"
#include "SatisDash/Snapshot.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	const TCHAR* FrmNodesAt = TEXT("2026-10-24T12:00:00.000Z");

	struct FFrmNodesRun
	{
		bool bOk = false;
		FString Json;
		FString Data;
		int32 Kept = 0;
		int32 Dropped = 0;
		int32 Over = 0;
	};

	FFrmNodesRun FrmNodesOf(const FString& Json)
	{
		FResourceNodesParse Parse{ SatisDash::Test::Utf8(Json), FrmNodesAt };
		FFrmNodesRun Run;
		FFrmArrayParse::EStep Step = FFrmArrayParse::EStep::More;
		while (Step == FFrmArrayParse::EStep::More)
		{
			Step = Parse.Step();
		}
		Run.bOk = Step == FFrmArrayParse::EStep::Done;
		Run.Kept = Parse.Kept();
		Run.Dropped = Parse.Dropped();
		Run.Over = Parse.Over();
		if (Run.bOk)
		{
			Run.Data = Parse.PeekJson().Mid(Parse.DataFrom(), Parse.DataEnd() - Parse.DataFrom());
			Run.Json = Parse.TakeJson();
		}
		return Run;
	}

	/** One node; Extra is appended inside the object, and a repeated key wins, as in JSON.parse. */
	FString FrmNodesOne(const TCHAR* Extra)
	{
		return FString::Printf(TEXT("[{\"Name\":\"Iron Ore\",\"Purity\":\"Normal\",\"NodeType\":\"Node\",\"Exploited\":false,\"location\":{\"x\":100,\"y\":200,\"z\":0}%s}]"), Extra);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmResourceNodesRulesTest, "SatisDash.Core.FrmResourceNodes.Rules", SatisDash::Test::Flags)

bool FFrmResourceNodesRulesTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::SameText(*this, TEXT("a node, fields in the Node agent's order"), FrmNodesOf(FrmNodesOne(TEXT(""))).Data,
		TEXT("[{\"type\":\"Iron Ore\",\"purity\":\"normal\",\"nodeType\":\"node\",\"x\":1,\"y\":2,\"exploited\":false}]"));
	SatisDash::Test::SameText(*this, TEXT("the world body wraps it"), FrmNodesOf(FrmNodesOne(TEXT(""))).Json,
		FString::Printf(TEXT("{\"observedAt\":\"%s\",\"data\":[{\"type\":\"Iron Ore\",\"purity\":\"normal\",\"nodeType\":\"node\",\"x\":1,\"y\":2,\"exploited\":false}]}"), FrmNodesAt));
	TestTrue(TEXT("exploited true"), FrmNodesOf(FrmNodesOne(TEXT(",\"Exploited\":true"))).Data.Contains(TEXT("\"exploited\":true")));
	TestTrue(TEXT("FRM's \"Inpure\" typo is not corrected: Purity is used, not EnumPurity"), FrmNodesOf(FrmNodesOne(TEXT(",\"Purity\":\"Inpure\",\"EnumPurity\":\"RP_Inpure\""))).Data.Contains(TEXT("\"purity\":\"inpure\"")));
	TestTrue(TEXT("a fracking satellite"), FrmNodesOf(FrmNodesOne(TEXT(",\"NodeType\":\"Fracking Satellite\""))).Data.Contains(TEXT("\"nodeType\":\"frackingSatellite\"")));

	// Tolerated: the extras FRM sends.
	TestTrue(TEXT("extras are skipped"), FrmNodesOf(FrmNodesOne(TEXT(",\"ID\":\"BP_1\",\"ClassName\":\"c\",\"ResourceForm\":\"RF_SOLID\",\"Extra\":{\"a\":[1,null]}"))).Kept == 1);
	// Refused as zod's RawFrmResourceNodeSchema refuses them.
	for (const TCHAR* Bad : { TEXT(",\"Name\":null"), TEXT(",\"Purity\":1"), TEXT(",\"NodeType\":null"), TEXT(",\"Exploited\":\"no\""), TEXT(",\"Exploited\":null"),
		TEXT(",\"location\":{\"x\":1,\"y\":2}"), TEXT(",\"location\":{\"x\":1e999,\"y\":2,\"z\":0}"), TEXT(",\"location\":null") })
	{
		TestFalse(FString::Printf(TEXT("refused: %s"), Bad), FrmNodesOf(FrmNodesOne(Bad)).bOk);
	}
	TestFalse(TEXT("a node without Exploited"), FrmNodesOf(TEXT("[{\"Name\":\"a\",\"Purity\":\"p\",\"NodeType\":\"n\",\"location\":{\"x\":1,\"y\":2,\"z\":3}}]")).bOk);
	TestFalse(TEXT("not an array"), FrmNodesOf(TEXT("{}")).bOk);
	TestFalse(TEXT("trailing input"), FrmNodesOf(TEXT("[] []")).bOk);

	// A bad coordinate drops the node, not the layer.
	const FFrmNodesRun Far = FrmNodesOf(FrmNodesOne(TEXT(",\"location\":{\"x\":1e12,\"y\":0,\"z\":0}")));
	TestTrue(TEXT("out of bounds: dropped"), Far.bOk && Far.Kept == 0 && Far.Dropped == 1);

	// Cuts: type to 200, purity and nodeType to 40 (the contract's bounds); a surrogate pair is never split.
	TestTrue(TEXT("type cut to 200"), FrmNodesOf(FrmNodesOne(*(TEXT(",\"Name\":\"") + FString::ChrN(201, TEXT('a')) + TEXT("\"")))).Data.Contains(TEXT("\"type\":\"") + FString::ChrN(200, TEXT('a')) + TEXT("\",")));
	TestTrue(TEXT("purity cut to 40"), FrmNodesOf(FrmNodesOne(*(TEXT(",\"Purity\":\"") + FString::ChrN(41, TEXT('P')) + TEXT("\"")))).Data.Contains(TEXT("\"purity\":\"") + FString::ChrN(40, TEXT('p')) + TEXT("\",")));
	TestTrue(TEXT("nodeType cut to 40"), FrmNodesOf(FrmNodesOne(*(TEXT(",\"NodeType\":\"") + FString::ChrN(41, TEXT('t')) + TEXT("\"")))).Data.Contains(TEXT("\"nodeType\":\"") + FString::ChrN(40, TEXT('t')) + TEXT("\",")));
	const FString Pair = FString::ChrN(39, TEXT('a')) + FString::Printf(TEXT("%c%c"), TCHAR(0xD83D), TCHAR(0xDE00));
	SatisDash::Test::SameText(*this, TEXT("a pair at the cut is dropped whole"), ClampStringTo(Pair, 40), FString::ChrN(39, TEXT('a')));
	SatisDash::Test::SameText(*this, TEXT("under the cut: unchanged"), ClampStringTo(TEXT("abc"), 40), TEXT("abc"));
	SatisDash::Test::SameText(*this, TEXT("ClampString is the 200 cut"), ClampString(FString::ChrN(201, TEXT('b'))), FString::ChrN(200, TEXT('b')));

	// 5,000 nodes kept; the 5,001st counted.
	FString Many = TEXT("[");
	for (int32 I = 0; I < MaxResourceNodes + 1; ++I)
	{
		Many += FString::Printf(TEXT("%s{\"Name\":\"n%d\",\"Purity\":\"Pure\",\"NodeType\":\"Node\",\"Exploited\":false,\"location\":{\"x\":%d,\"y\":0,\"z\":0}}"), I > 0 ? TEXT(",") : TEXT(""), I, I * 100);
	}
	Many += TEXT("]");
	const FFrmNodesRun Capped = FrmNodesOf(Many);
	TestTrue(TEXT("5,001 nodes: 5,000 kept, one over"), Capped.bOk && Capped.Kept == MaxResourceNodes && Capped.Over == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmResourceNodesNodeTypeTest, "SatisDash.Core.FrmResourceNodes.NodeType", SatisDash::Test::Flags)

bool FFrmResourceNodesNodeTypeTest::RunTest(const FString& Parameters)
{
	// nodeType.ts: three known values map explicitly; anything else is camel-cased (first word lower case, later words
	// capitalised, split on JavaScript's \s).
	const TCHAR* Cases[][2] = {
		{ TEXT("Node"), TEXT("node") }, { TEXT("Fracking Satellite"), TEXT("frackingSatellite") }, { TEXT("Geyser"), TEXT("geyser") },
		{ TEXT(""), TEXT("") }, { TEXT("   "), TEXT("") }, { TEXT("  padded  "), TEXT("padded") },
		{ TEXT("Some New TYPE"), TEXT("someNewType") }, { TEXT("some  new   TYPE"), TEXT("someNewType") }, { TEXT("ALLCAPS"), TEXT("allcaps") },
		{ TEXT("node"), TEXT("node") }, { TEXT("A B"), TEXT("aB") }, { TEXT("x"), TEXT("x") },
		{ TEXT("constructor"), TEXT("constructor") }, { TEXT("toString"), TEXT("tostring") }, { TEXT("a\tb\nc"), TEXT("aBC") },
	};
	for (const TCHAR* (&Case)[2] : Cases)
	{
		SatisDash::Test::SameText(*this, FString::Printf(TEXT("'%s'"), Case[0]), MapNodeType(Case[0]), Case[1]);
	}
	SatisDash::Test::SameText(*this, TEXT("a no-break space splits words, as \\s does"), MapNodeType(FString::Printf(TEXT("a%cb"), TCHAR(0x00A0))), TEXT("aB"));
	SatisDash::Test::SameText(*this, TEXT("so does U+2003"), MapNodeType(FString::Printf(TEXT("a%cb"), TCHAR(0x2003))), TEXT("aB"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmResourceNodesGoldenTest, "SatisDash.Core.FrmResourceNodes.Golden", SatisDash::Test::Flags)

bool FFrmResourceNodesGoldenTest::RunTest(const FString& Parameters)
{
	const FString Body = SatisDash::Test::Fixture(TEXT("frm-getResourceNode.json"));
	const FString Expected = SatisDash::Test::Fixture(TEXT("frm-getResourceNode.expected.json"));
	if (!TestFalse(TEXT("the fixtures were found"), Body.IsEmpty() || Expected.IsEmpty()))
	{
		return false;
	}
	const FFrmNodesRun Run = FrmNodesOf(Body);
	TestTrue(TEXT("parses"), Run.bOk);
	TestEqual(TEXT("the node out of bounds was dropped"), Run.Dropped, 1);
	TestTrue(FString::Printf(TEXT("is the Node agent's resourceNodes layer; got %s"), *Run.Json),
		SatisDash::Test::SameJson(Run.Json, FString::Printf(TEXT("{\"observedAt\":\"%s\",\"data\":%s}"), FrmNodesAt, *Expected)));
	return true;
}
```

The `toString` row pins the JavaScript hole the Node agent closed with `Object.hasOwn`: a plain object lookup would return a function for it; here it is just a word, lower-cased to `tostring`.

- [ ] **Step 3: Run them to see them fail**

Run the mod tests with `--min 129`.
Expected: the build fails with `'FrmRails.h': No such file`.

- [ ] **Step 4: Implement**

`Snapshot.h`, after `ClampString`:

```cpp
	/** At most MaxChars UTF-16 code units, never splitting a pair; ClampString is the 200-character cut. */
	SATISDASHCORE_API FString ClampStringTo(const FString& Value, int32 MaxChars);
```

`Snapshot.cpp`, replace `ClampString`'s body with a call and add the general one:

```cpp
FString SatisDash::ClampStringTo(const FString& Value, int32 MaxChars)
{
	if (MaxChars <= 0)
	{
		return FString();
	}
	if (Value.Len() <= MaxChars)
	{
		return Value;
	}
	int32 Keep = MaxChars;
	const TCHAR Last = Value[Keep - 1];
	if (Last >= 0xD800 && Last <= 0xDBFF)
	{
		--Keep; // a high surrogate whose pair would be cut off
	}
	return Value.Left(Keep);
}

FString SatisDash::ClampString(const FString& Value)
{
	return ClampStringTo(Value, MaxStringLength);
}
```

`FrmRecords.h`, after `FrmSliceRecords`:

```cpp
	/** {"observedAt":"<ObservedAt>","data": : what a world body (worldReader.ts: { observedAt, data }) is written after. */
	SATISDASHCORE_API FString WorldBodyPrefix(const FString& ObservedAt);
```

`FrmRecords.cpp`: add `#include "SatisDash/JsonUtil.h"` and

```cpp
FString SatisDash::WorldBodyPrefix(const FString& ObservedAt)
{
	FString Prefix = TEXT("{\"observedAt\":");
	Json::AppendString(Prefix, ObservedAt);
	Prefix += TEXT(",\"data\":");
	return Prefix;
}
```

`FrmRails.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "SatisDash/FrmRecords.h"

namespace SatisDash
{
	/** map.ts MAP_WORLD_MAX_ITEMS and MAP_WORLD_MAX_POLYLINE_POINTS. */
	inline constexpr int32 MaxRailSegments = 5000;
	inline constexpr int32 MaxRailPoints = 1000;

	/**
	 * getTrainRails' body to the rails world body {"observedAt":...,"data":[{"id","points":[[x,y],...]}]}, as the Node agent
	 * makes it (rawSchemas.ts RawFrmTrainRailSchema, railsMapper.ts, coordinates.ts). Dropped() counts segments left with
	 * fewer than 2 points or with more than MaxRailPoints; Over() those past MaxRailSegments.
	 */
	class SATISDASHCORE_API FRailsParse final : public FFrmArrayParse
	{
	public:
		FRailsParse(TArray<uint8> InBody, const FString& ObservedAt);

	protected:
		virtual ERecord ReadRecord(bool bRoom) override;

	private:
		bool ReadSpline(EJsonNotation Started, TArray<int32>& Points, bool& bTooLong);
	};
}
```

`FrmRails.cpp`:

```cpp
#include "SatisDash/FrmRails.h"

#include "SatisDash/FrmMap.h"
#include "SatisDash/JsonUtil.h"
#include "SatisDash/Snapshot.h"

SatisDash::FRailsParse::FRailsParse(TArray<uint8> InBody, const FString& ObservedAt)
	: FFrmArrayParse(MoveTemp(InBody), MaxRailSegments, WorldBodyPrefix(ObservedAt), FString(TEXT("}")))
{
}

bool SatisDash::FRailsParse::ReadSpline(EJsonNotation Started, TArray<int32>& Points, bool& bTooLong)
{
	if (Started != EJsonNotation::ArrayStart)
	{
		return false;
	}
	Points.Reset(); // a repeated SplineData key: the last one wins
	bTooLong = false;
	bool bHasLast = false;
	int32 LastX = 0;
	int32 LastY = 0;
	EJsonNotation N;
	while (Reader.Next(N))
	{
		if (N == EJsonNotation::ArrayEnd)
		{
			return true;
		}
		if (N != EJsonNotation::ObjectStart)
		{
			return false;
		}
		double X = 0;
		double Y = 0;
		uint8 Seen = 0;
		bool bClosed = false;
		while (Reader.Next(N))
		{
			if (N == EJsonNotation::ObjectEnd)
			{
				bClosed = true;
				break;
			}
			const FString Key = Reader.Name();
			const bool bX = Is(Key, TEXT("x"));
			const bool bY = Is(Key, TEXT("y"));
			if (bX || bY)
			{
				if (N != EJsonNotation::Number || !FMath::IsFinite(Reader.Number()))
				{
					return false;
				}
				(bX ? X : Y) = Reader.Number();
				Seen |= uint8(bX ? 1 : 2);
			}
			else if (!Reader.SkipValue(N))
			{
				return false;
			}
		}
		if (!bClosed || Seen != 3)
		{
			return false;
		}
		const double Mx = ToWholeMetres(X);
		const double My = ToWholeMetres(Y);
		if (!InWholeMetreBounds(Mx) || !InWholeMetreBounds(My))
		{
			continue; // railsMapper.ts: a bad point is skipped, not the segment
		}
		const int32 Ix = int32(Mx);
		const int32 Iy = int32(My);
		if (bHasLast && LastX == Ix && LastY == Iy)
		{
			continue; // consecutive duplicates (the spline is far finer than a metre) are collapsed
		}
		bHasLast = true;
		LastX = Ix;
		LastY = Iy;
		if (Points.Num() / 2 >= MaxRailPoints)
		{
			bTooLong = true; // still read and validated, no longer stored
			continue;
		}
		Points.Add(Ix);
		Points.Add(Iy);
	}
	return false;
}

auto SatisDash::FRailsParse::ReadRecord(bool bRoom) -> ERecord
{
	TOptional<FString> Id;
	bool bSpline = false;
	bool bTooLong = false;
	TArray<int32> Points; // x, y pairs in whole metres
	EJsonNotation N;
	while (Reader.Next(N))
	{
		if (N == EJsonNotation::ObjectEnd)
		{
			if (!Id.IsSet() || Id->IsEmpty() || !bSpline)
			{
				return ERecord::Invalid;
			}
			if (bTooLong || Points.Num() < 4)
			{
				return ERecord::Dropped;
			}
			if (!bRoom)
			{
				return ERecord::Over;
			}
			BeginRecord();
			Out += TEXT("{\"id\":");
			Json::AppendString(Out, ClampString(Id.GetValue()));
			Out += TEXT(",\"points\":[");
			for (int32 I = 0; I < Points.Num(); I += 2)
			{
				if (I > 0)
				{
					Out.AppendChar(TEXT(','));
				}
				Out.AppendChar(TEXT('['));
				Out.AppendInt(Points[I]);
				Out.AppendChar(TEXT(','));
				Out.AppendInt(Points[I + 1]);
				Out.AppendChar(TEXT(']'));
			}
			Out += TEXT("]}");
			return ERecord::Kept;
		}
		const FString Key = Reader.Name();
		if (Is(Key, TEXT("ID")))
		{
			if (N != EJsonNotation::String)
			{
				return ERecord::Invalid;
			}
			Id = Reader.String();
		}
		else if (Is(Key, TEXT("SplineData")))
		{
			if (!ReadSpline(N, Points, bTooLong))
			{
				return ERecord::Invalid;
			}
			bSpline = true;
		}
		else if (!Reader.SkipValue(N))
		{
			return ERecord::Invalid;
		}
	}
	return ERecord::Invalid;
}
```

`FrmResourceNodes.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "SatisDash/FrmRecords.h"

namespace SatisDash
{
	/** map.ts MAP_WORLD_MAX_ITEMS, and the bound on purity and nodeType. */
	inline constexpr int32 MaxResourceNodes = 5000;
	inline constexpr int32 MaxNodeFieldChars = 40;

	/** nodeType.ts mapNodeType: "Node", "Fracking Satellite" and "Geyser" map explicitly; anything else is camel-cased on JavaScript's \s. */
	SATISDASHCORE_API FString MapNodeType(const FString& Raw);

	/**
	 * getResourceNode's body to the resourceNodes world body {"observedAt":...,"data":[{"type","purity","nodeType","x","y",
	 * "exploited"}]} (rawSchemas.ts RawFrmResourceNodeSchema, resourceNodesMapper.ts). Dropped() counts nodes with a bad coordinate.
	 */
	class SATISDASHCORE_API FResourceNodesParse final : public FFrmArrayParse
	{
	public:
		FResourceNodesParse(TArray<uint8> InBody, const FString& ObservedAt);

	protected:
		virtual ERecord ReadRecord(bool bRoom) override;
	};
}
```

`FrmResourceNodes.cpp`:

```cpp
#include "SatisDash/FrmResourceNodes.h"

#include "SatisDash/FrmMap.h"
#include "SatisDash/JsonUtil.h"
#include "SatisDash/Snapshot.h"

namespace
{
	/** JavaScript's \s: the ECMAScript WhiteSpace and LineTerminator characters. */
	bool FrmNodesIsJsSpace(TCHAR C)
	{
		switch (C)
		{
		case 0x0009: case 0x000A: case 0x000B: case 0x000C: case 0x000D: case 0x0020: case 0x00A0: case 0x1680:
		case 0x2028: case 0x2029: case 0x202F: case 0x205F: case 0x3000: case 0xFEFF:
			return true;
		default:
			return C >= 0x2000 && C <= 0x200A;
		}
	}
}

FString SatisDash::MapNodeType(const FString& Raw)
{
	if (Raw.Equals(TEXT("Node"), ESearchCase::CaseSensitive))
	{
		return TEXT("node");
	}
	if (Raw.Equals(TEXT("Fracking Satellite"), ESearchCase::CaseSensitive))
	{
		return TEXT("frackingSatellite");
	}
	if (Raw.Equals(TEXT("Geyser"), ESearchCase::CaseSensitive))
	{
		return TEXT("geyser");
	}
	TArray<FString> Words;
	FString Word;
	for (const TCHAR C : Raw)
	{
		if (FrmNodesIsJsSpace(C))
		{
			if (!Word.IsEmpty())
			{
				Words.Add(Word);
				Word.Reset();
			}
		}
		else
		{
			Word.AppendChar(C);
		}
	}
	if (!Word.IsEmpty())
	{
		Words.Add(Word);
	}
	FString Out;
	for (int32 I = 0; I < Words.Num(); ++I)
	{
		Out += I == 0 ? Words[I].ToLower() : Words[I].Left(1).ToUpper() + Words[I].Mid(1).ToLower();
	}
	return Out;
}

SatisDash::FResourceNodesParse::FResourceNodesParse(TArray<uint8> InBody, const FString& ObservedAt)
	: FFrmArrayParse(MoveTemp(InBody), MaxResourceNodes, WorldBodyPrefix(ObservedAt), FString(TEXT("}")))
{
}

auto SatisDash::FResourceNodesParse::ReadRecord(bool bRoom) -> ERecord
{
	TOptional<FString> Name;
	TOptional<FString> Purity;
	TOptional<FString> NodeType;
	TOptional<bool> Exploited;
	bool bLocation = false;
	double X = 0;
	double Y = 0;
	EJsonNotation N;
	while (Reader.Next(N))
	{
		if (N == EJsonNotation::ObjectEnd)
		{
			if (!Name.IsSet() || !Purity.IsSet() || !NodeType.IsSet() || !Exploited.IsSet() || !bLocation)
			{
				return ERecord::Invalid;
			}
			const double Mx = ToWholeMetres(X);
			const double My = ToWholeMetres(Y);
			if (!InWholeMetreBounds(Mx) || !InWholeMetreBounds(My))
			{
				return ERecord::Dropped;
			}
			if (!bRoom)
			{
				return ERecord::Over;
			}
			BeginRecord();
			Out += TEXT("{\"type\":");
			Json::AppendString(Out, ClampString(Name.GetValue()));
			Out += TEXT(",\"purity\":");
			Json::AppendString(Out, ClampStringTo(Purity->ToLower(), MaxNodeFieldChars));
			Out += TEXT(",\"nodeType\":");
			Json::AppendString(Out, ClampStringTo(MapNodeType(NodeType.GetValue()), MaxNodeFieldChars));
			Out += TEXT(",\"x\":");
			Out.AppendInt(int32(Mx));
			Out += TEXT(",\"y\":");
			Out.AppendInt(int32(My));
			Out += Exploited.GetValue() ? TEXT(",\"exploited\":true}") : TEXT(",\"exploited\":false}");
			return ERecord::Kept;
		}
		const FString Key = Reader.Name();
		const bool bName = Is(Key, TEXT("Name"));
		const bool bPurity = Is(Key, TEXT("Purity"));
		const bool bNodeType = Is(Key, TEXT("NodeType"));
		if (bName || bPurity || bNodeType)
		{
			if (N != EJsonNotation::String)
			{
				return ERecord::Invalid;
			}
			(bName ? Name : bPurity ? Purity : NodeType) = Reader.String();
		}
		else if (Is(Key, TEXT("Exploited")))
		{
			if (N != EJsonNotation::Boolean)
			{
				return ERecord::Invalid;
			}
			Exploited = Reader.Boolean();
		}
		else if (Is(Key, TEXT("location")))
		{
			if (!ReadFrmLocation(Reader, N, X, Y))
			{
				return ERecord::Invalid;
			}
			bLocation = true;
		}
		else if (!Reader.SkipValue(N))
		{
			return ERecord::Invalid;
		}
	}
	return ERecord::Invalid;
}
```

(`FString::ToLower()` has a const-ref overload returning a copy; if the compiler picks the rvalue one for a `const FString&` receiver, `ToLower()` on a `TOptional` member's const value is still fine.)

- [ ] **Step 5: Run the tests**

Expected: `judge-mod-test --min 129` passes. Add `check-api` rows as reported (likely `FString::ToLower`, `FString::ToUpper`, `FString::Left`, `FString::Mid`, `FString::Reset`).

- [ ] **Step 6: The goldens from the Node agent's code**

Write the two expected files with the Node agent's own mappers (see Task 4 for `<s5.checkout>`):

```powershell
cd harness
npx tsx scripts/frm-golden.ts --dash <s5.checkout> --kind rails --in ..\mod\SatisDashAgent\Source\SatisDashCoreTests\Fixtures\frm-getTrainRails.json --out ..\mod\SatisDashAgent\Source\SatisDashCoreTests\Fixtures\frm-getTrainRails.expected.json
npx tsx scripts/frm-golden.ts --dash <s5.checkout> --kind resourceNodes --in ..\mod\SatisDashAgent\Source\SatisDashCoreTests\Fixtures\frm-getResourceNode.json --out ..\mod\SatisDashAgent\Source\SatisDashCoreTests\Fixtures\frm-getResourceNode.expected.json
```

Each must be data-equal to the hand-worked file in Step 1. If one differs, the Node agent is right: stop and report which value differs. Commit what the script wrote.

`harness/test/snapshot-fixtures.test.ts`: add the imports `RailsLayerDataSchema, ResourceNodesLayerDataSchema, RailsWorldIngestRequestSchema` from `"../contract/src/map"` and

```ts
  it("the world fixtures' expected layers are valid against the contract", () => {
    expect(RailsLayerDataSchema.safeParse(load("frm-getTrainRails.expected.json")).success).toBe(true);
    expect(ResourceNodesLayerDataSchema.safeParse(load("frm-getResourceNode.expected.json")).success).toBe(true);
    const body = { observedAt: "2026-10-24T12:00:00.000Z", data: load("frm-getTrainRails.expected.json") };
    expect(RailsWorldIngestRequestSchema.safeParse(body).success).toBe(true); // strict: exactly observedAt and data
  });
```

Run `npx vitest run test/snapshot-fixtures.test.ts --reporter=dot`.

- [ ] **Step 7: Commit**

```bash
git add mod/SatisDashAgent/Source harness/test api-sources.md
git -c core.safecrlf=false commit -m "Rails and resource nodes from FRM, field by field from the Node agent (FRM bridge 3B Task 6)" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

### Task 7: The world job and the bridge's world slots

**Files:**
- Create: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/FrmWorld.h`, `Private/FrmWorld.cpp`
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/FrmBridge.h`, `Private/FrmBridge.cpp`
- Test: new `FrmWorldTest.cpp`; `FrmBridgeTest.cpp`
- Modify: `api-sources.md`

**Interfaces:**
- Consumes: `FRailsParse`, `FResourceNodesParse`, `WorldBodyPrefix` (Task 6), `EncodeBody`/`FEncodedBody` (`Requests.h`), `FrmGetTrainRails`/`FrmGetResourceNode` (Task 5), `FBlake3`/`FBlake3Hash` (`Hash/Blake3.h`).
- Produces:
  - `enum class EWorldLayer { Rails, ResourceNodes }`, `WorldLayerCount`, `FWorldLayerSpec { Name, Endpoint, FirstSeconds, IntervalSeconds }`, `const FWorldLayerSpec& WorldLayer(EWorldLayer)`: rails `120 s` then every `600 s`, resource nodes `480 s` then every `1800 s`.
  - `FWorldOutcome { Kind, RawHash, DataHash, Items, Dropped, Over, Bytes, Body }` with kinds `ReadFailed`, `Invalid`, `SameRaw`, `SameData`, `OverCap`, `Ready`.
  - `FWorldRead(Layer, Body, ObservedAt, LastRaw, LastData, MaxBodyBytes = WorldLayerMaxBytes)`: `bool Step()` (true when finished), `FWorldOutcome TakeOutcome()`.
  - `FFrmBridge::RequestWorld(Layer, ObservedAt, LastRaw, LastData)` (false while that layer is already requested, parsing, or has an outcome waiting) and `TOptional<FWorldOutcome> TakeWorld(Layer)`.

How a world read goes (spec "Load spreading": "Unchanged world layers skip their parse"; worldReader.ts `cycle`):
1. The bridge queues the layer's FRM endpoint in the world lane. A reply that is not 2xx is a `ReadFailed` outcome at once.
2. A 2xx body becomes a job on the worker, one slice per task:
   - **Hashing.** The raw bytes go through BLAKE3, 1 MiB a slice. If the hash equals `LastRaw` the job ends as `SameRaw`: no parse, nothing to send. The raw body is freed.
   - **Parsing.** `FRailsParse`/`FResourceNodesParse`, 2,000 records a slice. An invalid body ends as `Invalid`.
   - **Finishing** (one task): hash the `data` array text; equal to `LastData` ends as `SameData` (the mapped layer did not change). Otherwise measure the whole body's UTF-8 bytes: over `MaxBodyBytes` ends as `OverCap` (the backend keeps its last copy); else `EncodeBody` (gzip over 1 KB) and `Ready`.
3. `LastRaw` and `LastData` are the layer's last settled raw hash and last accepted data hash (the sender owns them, Task 8). They are copied into the job, so nothing the game thread does while the job runs can race with it.
4. A world job counts as a held large body: `Pump` gets `bLargeBusy` while one exists, so getPlayer is the only read that passes it and no second large read starts.

- [ ] **Step 1: Write the failing tests**

`FrmWorldTest.cpp`:

```cpp
#include "SatisDash/FrmWorld.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	const TCHAR* FrmWorldAt = TEXT("2026-10-24T12:00:00.000Z");
	const TCHAR* FrmWorldRails = TEXT("[{\"ID\":\"R1\",\"SplineData\":[{\"x\":0,\"y\":0},{\"x\":300,\"y\":0}]}]");
	const TCHAR* FrmWorldData = TEXT("[{\"id\":\"R1\",\"points\":[[0,0],[3,0]]}]");

	struct FFrmWorldRun
	{
		FWorldOutcome Outcome;
		int32 Steps = 0;
	};

	FFrmWorldRun FrmWorldRun(const FString& Body, TOptional<FBlake3Hash> LastRaw = TOptional<FBlake3Hash>(), TOptional<FBlake3Hash> LastData = TOptional<FBlake3Hash>(), int64 MaxBytes = WorldLayerMaxBytes)
	{
		FWorldRead Read{ EWorldLayer::Rails, SatisDash::Test::Utf8(Body), FString(FrmWorldAt), LastRaw, LastData, MaxBytes };
		FFrmWorldRun Run;
		bool bDone = false;
		while (!bDone)
		{
			bDone = Read.Step();
			++Run.Steps;
		}
		Run.Outcome = Read.TakeOutcome();
		return Run;
	}

	FBlake3Hash FrmWorldRawHash(const FString& Body)
	{
		const TArray<uint8> Bytes = SatisDash::Test::Utf8(Body);
		return FBlake3::HashBuffer(Bytes.GetData(), uint64(Bytes.Num()));
	}

	/** What the data hash covers: the data array text, as UTF-16, never observedAt. */
	FBlake3Hash FrmWorldDataHash(const FString& DataText)
	{
		return FBlake3::HashBuffer(*DataText, uint64(DataText.Len()) * sizeof(TCHAR));
	}

	FString FrmWorldText(const FEncodedBody& Body)
	{
		const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Body.Bytes.GetData()), Body.Bytes.Num());
		return FString::ConstructFromPtrSize(Text.Get(), Text.Length());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmWorldReadTest, "SatisDash.Core.FrmWorld.Read", SatisDash::Test::Flags)

bool FFrmWorldReadTest::RunTest(const FString& Parameters)
{
	using EKind = FWorldOutcome::EKind;
	const FFrmWorldRun First = FrmWorldRun(FrmWorldRails);
	TestTrue(TEXT("the first read of a layer is Ready"), First.Outcome.Kind == EKind::Ready);
	TestEqual(TEXT("one item"), First.Outcome.Items, 1);
	TestEqual(TEXT("a hash step, then the parse and finish"), First.Steps, 2);
	SatisDash::Test::SameText(*this, TEXT("the body is the Node agent's { observedAt, data }, not gzipped under 1 KB"), FrmWorldText(First.Outcome.Body),
		FString::Printf(TEXT("{\"observedAt\":\"%s\",\"data\":%s}"), FrmWorldAt, FrmWorldData));
	TestTrue(TEXT("the raw hash is of the raw bytes"), First.Outcome.RawHash == FrmWorldRawHash(FrmWorldRails));
	TestTrue(TEXT("the data hash is of the data array text only"), First.Outcome.DataHash == FrmWorldDataHash(FrmWorldData));
	TestEqual(TEXT("the byte count is the whole body's"), First.Outcome.Bytes, int64(FTCHARToUTF8(*FrmWorldText(First.Outcome.Body)).Length()));

	// The same raw body: one hash step, no parse, nothing to send.
	const FFrmWorldRun Same = FrmWorldRun(FrmWorldRails, First.Outcome.RawHash, First.Outcome.DataHash);
	TestTrue(TEXT("same raw: SameRaw"), Same.Outcome.Kind == EKind::SameRaw);
	TestEqual(TEXT("and only the hash step ran: no parse"), Same.Steps, 1);
	TestTrue(TEXT("no body"), Same.Outcome.Body.Bytes.Num() == 0 && Same.Outcome.Items == 0);

	// A different raw body that maps to the same data (an extra z): parsed, but not sent.
	const FString Noisy = TEXT("[{\"ID\":\"R1\",\"SplineData\":[{\"x\":0,\"y\":0,\"z\":5},{\"x\":300,\"y\":0,\"z\":7}]}]");
	const FFrmWorldRun SameData = FrmWorldRun(Noisy, First.Outcome.RawHash, First.Outcome.DataHash);
	TestTrue(TEXT("same mapped data: SameData"), SameData.Outcome.Kind == EKind::SameData);
	TestTrue(TEXT("with its own raw hash, to be remembered"), SameData.Outcome.RawHash == FrmWorldRawHash(Noisy) && !(SameData.Outcome.RawHash == First.Outcome.RawHash));
	TestEqual(TEXT("it was parsed"), SameData.Steps, 2);
	TestTrue(TEXT("and has no body"), SameData.Outcome.Body.Bytes.Num() == 0);

	// A layer that changed is Ready.
	const FFrmWorldRun Changed = FrmWorldRun(TEXT("[{\"ID\":\"R1\",\"SplineData\":[{\"x\":0,\"y\":0},{\"x\":600,\"y\":0}]}]"), First.Outcome.RawHash, First.Outcome.DataHash);
	TestTrue(TEXT("changed data is Ready"), Changed.Outcome.Kind == EKind::Ready && !(Changed.Outcome.DataHash == First.Outcome.DataHash));
	// The first read after start always sends, even an empty layer.
	const FFrmWorldRun Empty = FrmWorldRun(TEXT("[]"));
	SatisDash::Test::SameText(*this, TEXT("an empty layer is Ready"), FrmWorldText(Empty.Outcome.Body), FString::Printf(TEXT("{\"observedAt\":\"%s\",\"data\":[]}"), FrmWorldAt));
	// A bad body.
	TestTrue(TEXT("not an array: Invalid"), FrmWorldRun(TEXT("{}")).Outcome.Kind == EKind::Invalid);
	TestTrue(TEXT("an invalid record: Invalid"), FrmWorldRun(TEXT("[{\"ID\":\"\"}]")).Outcome.Kind == EKind::Invalid);
	TestTrue(TEXT("empty: Invalid"), FrmWorldRun(FString()).Outcome.Kind == EKind::Invalid);
	// Dropped and over-cap items are reported.
	const FFrmWorldRun Dropped = FrmWorldRun(TEXT("[{\"ID\":\"R1\",\"SplineData\":[]}]"));
	TestTrue(TEXT("a dropped segment is counted and the empty layer still Ready"), Dropped.Outcome.Kind == EKind::Ready && Dropped.Outcome.Dropped == 1);

	// 2,000,000 bytes of the whole body, before gzip: exactly the cap is allowed, one more is not.
	const int64 Exact = First.Outcome.Bytes;
	TestTrue(TEXT("exactly the cap: Ready"), FrmWorldRun(FrmWorldRails, TOptional<FBlake3Hash>(), TOptional<FBlake3Hash>(), Exact).Outcome.Kind == EKind::Ready);
	const FFrmWorldRun Over = FrmWorldRun(FrmWorldRails, TOptional<FBlake3Hash>(), TOptional<FBlake3Hash>(), Exact - 1);
	TestTrue(TEXT("one byte over: OverCap, with no body"), Over.Outcome.Kind == EKind::OverCap && Over.Outcome.Body.Bytes.Num() == 0);
	TestTrue(TEXT("and its hashes, so it can be remembered"), Over.Outcome.RawHash == First.Outcome.RawHash && Over.Outcome.DataHash == First.Outcome.DataHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmWorldSlicesTest, "SatisDash.Core.FrmWorld.Slices", SatisDash::Test::Flags)

bool FFrmWorldSlicesTest::RunTest(const FString& Parameters)
{
	// 1.1 MB of raw body is two hash slices (1 MiB each), then the parse: three steps. The sliced hash equals the one-shot one.
	const FString Big = TEXT("[{\"ID\":\"R1\",\"Pad\":\"") + FString::ChrN(1100000, TEXT('a')) + TEXT("\",\"SplineData\":[{\"x\":0,\"y\":0},{\"x\":300,\"y\":0}]}]");
	const FFrmWorldRun Run = FrmWorldRun(Big);
	TestEqual(TEXT("two hash slices and a parse"), Run.Steps, 3);
	TestTrue(TEXT("Ready"), Run.Outcome.Kind == FWorldOutcome::EKind::Ready);
	TestTrue(TEXT("the sliced raw hash equals the one-shot hash"), Run.Outcome.RawHash == FrmWorldRawHash(Big));
	TestEqual(TEXT("the pad was skipped, not stored: the data is one rail"), Run.Outcome.Items, 1);
	// A body under 1 MiB is a single hash step.
	TestEqual(TEXT("a small body: one hash step and a parse"), FrmWorldRun(FrmWorldRails).Steps, 2);
	return true;
}
```

`FrmBridgeTest.cpp`: add `#include "SatisDash/FrmWorld.h"` and after `FFrmBridgeMapLiveCapTest`:

```cpp
namespace
{
	const TCHAR* FrmBridgeRailsBody = TEXT("[{\"ID\":\"R1\",\"SplineData\":[{\"x\":0,\"y\":0},{\"x\":300,\"y\":0}]}]");
	const TCHAR* FrmBridgeNodesBody = TEXT("[{\"Name\":\"Iron Ore\",\"Purity\":\"Normal\",\"NodeType\":\"Node\",\"Exploited\":false,\"location\":{\"x\":100,\"y\":200,\"z\":0}}]");
	const TCHAR* FrmBridgeWorldAt = TEXT("2026-10-24T12:00:00.000Z");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmBridgeWorldTest, "SatisDash.Core.FrmBridge.World", SatisDash::Test::Flags)

bool FFrmBridgeWorldTest::RunTest(const FString& Parameters)
{
	using EKind = FWorldOutcome::EKind;
	FFrmBridgeFixture F;
	TestTrue(TEXT("rails asked for"), F.Bridge.RequestWorld(EWorldLayer::Rails, FrmBridgeWorldAt, TOptional<FBlake3Hash>(), TOptional<FBlake3Hash>()));
	TestFalse(TEXT("one request per layer at a time"), F.Bridge.RequestWorld(EWorldLayer::Rails, FrmBridgeWorldAt, TOptional<FBlake3Hash>(), TOptional<FBlake3Hash>()));
	TestTrue(TEXT("resource nodes asked for"), F.Bridge.RequestWorld(EWorldLayer::ResourceNodes, FrmBridgeWorldAt, TOptional<FBlake3Hash>(), TOptional<FBlake3Hash>()));
	TestTrue(TEXT("getTrainRails is read first (queued first)"), F.serve(TEXT("/getTrainRails"), SatisDash::Test::Reply(200, FrmBridgeRailsBody)));

	// The rails job is held on the worker: it holds a large body, so getPlayer passes it but the nodes read waits.
	F.Worker.bHold = true;
	F.Bridge.RequestPass(false);
	F.tick(2);
	TestEqual(TEXT("getPlayer passes a large world parse"), F.Net.pending(TEXT("/getPlayer")), 1);
	F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, FrmBridgeOnePlayer));
	F.tick(2);
	TestEqual(TEXT("the second large read waits for the first parse"), F.Net.pending(TEXT("/getResourceNode")), 0);
	TestFalse(TEXT("and nothing is ready yet"), F.Bridge.TakeWorld(EWorldLayer::Rails).IsSet());

	F.Worker.bHold = false;
	F.Worker.release();
	F.tick(8);
	TOptional<FWorldOutcome> Rails = F.Bridge.TakeWorld(EWorldLayer::Rails);
	TestTrue(TEXT("the rails outcome is Ready"), Rails.IsSet() && Rails->Kind == EKind::Ready && Rails->Items == 1);
	TestFalse(TEXT("taken once"), F.Bridge.TakeWorld(EWorldLayer::Rails).IsSet());
	TestTrue(TEXT("then the nodes read starts"), F.serve(TEXT("/getResourceNode"), SatisDash::Test::Reply(200, FrmBridgeNodesBody)));
	F.tick(8);
	const TOptional<FWorldOutcome> Nodes = F.Bridge.TakeWorld(EWorldLayer::ResourceNodes);
	TestTrue(TEXT("the nodes outcome is Ready"), Nodes.IsSet() && Nodes->Kind == EKind::Ready && Nodes->Items == 1);

	// The same raw body again, with the hash the first outcome gave: one worker task, no parse.
	const int32 Launched = F.Worker.Launched;
	F.Bridge.RequestWorld(EWorldLayer::Rails, FrmBridgeWorldAt, Rails->RawHash, Rails->DataHash);
	F.serve(TEXT("/getTrainRails"), SatisDash::Test::Reply(200, FrmBridgeRailsBody));
	F.tick(6);
	const TOptional<FWorldOutcome> Again = F.Bridge.TakeWorld(EWorldLayer::Rails);
	TestTrue(TEXT("SameRaw"), Again.IsSet() && Again->Kind == EKind::SameRaw);
	TestEqual(TEXT("with a single task on the worker"), F.Worker.Launched, Launched + 1);

	// A failed read is an outcome too, so the sender can schedule the next cycle.
	F.Bridge.RequestWorld(EWorldLayer::Rails, FrmBridgeWorldAt, TOptional<FBlake3Hash>(), TOptional<FBlake3Hash>());
	F.serve(TEXT("/getTrainRails"), SatisDash::Test::Reply(503, FString()));
	F.tick(2);
	const TOptional<FWorldOutcome> Failed = F.Bridge.TakeWorld(EWorldLayer::Rails);
	TestTrue(TEXT("ReadFailed"), Failed.IsSet() && Failed->Kind == EKind::ReadFailed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmBridgeWorldStopTest, "SatisDash.Core.FrmBridge.WorldStop", SatisDash::Test::Flags)

bool FFrmBridgeWorldStopTest::RunTest(const FString& Parameters)
{
	FFrmBridgeFixture F;
	F.Bridge.RequestWorld(EWorldLayer::Rails, FrmBridgeWorldAt, TOptional<FBlake3Hash>(), TOptional<FBlake3Hash>());
	F.tick();
	TestEqual(TEXT("the read is in flight"), F.Net.pending(TEXT("/getTrainRails")), 1);
	F.Bridge.RequestWorld(EWorldLayer::ResourceNodes, FrmBridgeWorldAt, TOptional<FBlake3Hash>(), TOptional<FBlake3Hash>());
	F.Bridge.Stop();
	F.Net.answer(TEXT("/getTrainRails"), SatisDash::Test::Reply(200, FrmBridgeRailsBody));
	F.tick(6);
	TestFalse(TEXT("a reply after Stop is never used"), F.Bridge.TakeWorld(EWorldLayer::Rails).IsSet());
	TestEqual(TEXT("and the queued read was dropped"), F.Net.sent(TEXT("/getResourceNode")), 0);
	TestEqual(TEXT("nothing was read again"), F.Net.sent(TEXT("/getTrainRails")), 1);
	TestTrue(TEXT("after Stop a layer can be asked for again"), F.Bridge.RequestWorld(EWorldLayer::Rails, FrmBridgeWorldAt, TOptional<FBlake3Hash>(), TOptional<FBlake3Hash>()));
	return true;
}
```

- [ ] **Step 2: Run them to see them fail**

Run the mod tests with `--min 133`.
Expected: the build fails with `'FrmWorld.h': No such file`.

- [ ] **Step 3: Implement**

`FrmWorld.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Hash/Blake3.h"
#include "SatisDash/Frm.h"
#include "SatisDash/FrmRecords.h"
#include "SatisDash/Requests.h"

namespace SatisDash
{
	/** The world layers sent so far (spec "What the Node agent sends"). Plan 3C adds Conveyors as a third row. */
	enum class EWorldLayer : uint8 { Rails, ResourceNodes };
	inline constexpr int32 WorldLayerCount = 2;

	struct FWorldLayerSpec
	{
		/** The layer in the URL (POST /agent/v1/world/<Name>) and in log lines. */
		const TCHAR* Name;
		const FFrmEndpoint* Endpoint;
		/** After the agent starts Running: the first read (spec "Timing": rails 2 min, resource nodes 8 min), times a jitter of +/-10%. */
		double FirstSeconds;
		/** After a cycle ends: the next read (worldReader.ts RAILS_INTERVAL_MS and RESOURCE_NODES_INTERVAL_MS), times the same jitter. */
		double IntervalSeconds;
	};
	SATISDASHCORE_API const FWorldLayerSpec& WorldLayer(EWorldLayer Layer);

	/** map.ts MAP_WORLD_LAYER_MAX_BYTES: the serialised { observedAt, data }, UTF-8, before gzip (worldReader.ts measures it so). */
	inline constexpr int64 WorldLayerMaxBytes = 2000000;
	/** The raw body is hashed this much a worker task (spec "Threading": slices). */
	inline constexpr int64 WorldHashSliceBytes = 1 << 20;

	struct FWorldOutcome
	{
		/**
		 * ReadFailed: FRM unreachable, timed out, an HTTP error, or the body over its cap. Invalid: the body is not what the
		 * Node agent's schema accepts. SameRaw: the raw body equals the last settled one (not parsed). SameData: parsed, the
		 * mapped data equals the last accepted. OverCap: the body is past WorldLayerMaxBytes. Ready: Body is to be posted.
		 */
		enum class EKind : uint8 { ReadFailed, Invalid, SameRaw, SameData, OverCap, Ready };
		EKind Kind = EKind::ReadFailed;
		FBlake3Hash RawHash;
		FBlake3Hash DataHash;
		int32 Items = 0;
		/** Valid items the mapper dropped (a bad coordinate, too few or too many points). */
		int32 Dropped = 0;
		/** Valid items past the item cap. */
		int32 Over = 0;
		/** The whole body's UTF-8 bytes, before gzip (set for Ready and OverCap). */
		int64 Bytes = 0;
		FEncodedBody Body;
	};

	/**
	 * One world body being hashed, parsed and encoded on the worker, a slice per Step (see Plan 3B Task 7). Touched by one
	 * thread at a time. Not copyable: the hasher is not.
	 */
	class SATISDASHCORE_API FWorldRead
	{
	public:
		FWorldRead(EWorldLayer InLayer, TArray<uint8> InBody, FString InObservedAt, TOptional<FBlake3Hash> InLastRaw, TOptional<FBlake3Hash> InLastData, int64 InMaxBodyBytes = WorldLayerMaxBytes);
		FWorldRead(const FWorldRead&) = delete;
		FWorldRead& operator=(const FWorldRead&) = delete;

		/** One slice of work; true when the outcome is ready. */
		bool Step();
		FWorldOutcome TakeOutcome() { return MoveTemp(Outcome); }

	private:
		enum class EStage : uint8 { Hashing, Parsing, Done };
		void Finish();

		EWorldLayer Layer;
		TArray<uint8> Body;
		FString ObservedAt;
		TOptional<FBlake3Hash> LastRaw;
		TOptional<FBlake3Hash> LastData;
		int64 MaxBodyBytes;
		EStage Stage = EStage::Hashing;
		int64 Hashed = 0;
		FBlake3 Hasher;
		TUniquePtr<FFrmArrayParse> Parse;
		FWorldOutcome Outcome;
	};
}
```

`FrmWorld.cpp`:

```cpp
#include "SatisDash/FrmWorld.h"

#include "SatisDash/FrmRails.h"
#include "SatisDash/FrmResourceNodes.h"

namespace
{
	constexpr SatisDash::FWorldLayerSpec FrmWorldLayers[] = {
		{ TEXT("rails"), &SatisDash::FrmGetTrainRails, 120.0, 600.0 },
		{ TEXT("resourceNodes"), &SatisDash::FrmGetResourceNode, 480.0, 1800.0 },
	};

	TUniquePtr<SatisDash::FFrmArrayParse> FrmWorldMakeParse(SatisDash::EWorldLayer Layer, TArray<uint8> Body, const FString& ObservedAt)
	{
		if (Layer == SatisDash::EWorldLayer::Rails)
		{
			return MakeUnique<SatisDash::FRailsParse>(MoveTemp(Body), ObservedAt);
		}
		return MakeUnique<SatisDash::FResourceNodesParse>(MoveTemp(Body), ObservedAt);
	}
}

const SatisDash::FWorldLayerSpec& SatisDash::WorldLayer(EWorldLayer Layer)
{
	return FrmWorldLayers[int32(Layer)];
}

SatisDash::FWorldRead::FWorldRead(EWorldLayer InLayer, TArray<uint8> InBody, FString InObservedAt, TOptional<FBlake3Hash> InLastRaw, TOptional<FBlake3Hash> InLastData, int64 InMaxBodyBytes)
	: Layer{InLayer}
	, Body{MoveTemp(InBody)}
	, ObservedAt{MoveTemp(InObservedAt)}
	, LastRaw{InLastRaw}
	, LastData{InLastData}
	, MaxBodyBytes{InMaxBodyBytes}
{
}

bool SatisDash::FWorldRead::Step()
{
	switch (Stage)
	{
	case EStage::Hashing:
	{
		const int64 Take = FMath::Min<int64>(WorldHashSliceBytes, int64(Body.Num()) - Hashed);
		if (Take > 0)
		{
			Hasher.Update(Body.GetData() + Hashed, uint64(Take));
			Hashed += Take;
		}
		if (Hashed < Body.Num())
		{
			return false;
		}
		Outcome.RawHash = Hasher.Finalize();
		if (LastRaw.IsSet() && LastRaw.GetValue() == Outcome.RawHash)
		{
			// The same bytes as the last settled read: nothing to parse and nothing to send.
			Outcome.Kind = FWorldOutcome::EKind::SameRaw;
			Body.Empty();
			Stage = EStage::Done;
			return true;
		}
		Parse = FrmWorldMakeParse(Layer, MoveTemp(Body), ObservedAt);
		Stage = EStage::Parsing;
		return false;
	}
	case EStage::Parsing:
		switch (Parse->Step())
		{
		case FFrmArrayParse::EStep::More:
			return false;
		case FFrmArrayParse::EStep::Invalid:
			Outcome.Kind = FWorldOutcome::EKind::Invalid;
			Parse.Reset();
			Stage = EStage::Done;
			return true;
		case FFrmArrayParse::EStep::Done:
			Finish();
			Stage = EStage::Done;
			return true;
		}
		return false;
	default:
		return true;
	}
}

void SatisDash::FWorldRead::Finish()
{
	Outcome.Items = Parse->Kept();
	Outcome.Dropped = Parse->Dropped();
	Outcome.Over = Parse->Over();
	Outcome.Bytes = Parse->Utf8Bytes();

	// worldReader.ts: the hash of the projected data comes first; equal to the last accepted one means nothing to send.
	const FString& Json = Parse->PeekJson();
	FBlake3 DataHasher;
	DataHasher.Update(*Json + Parse->DataFrom(), uint64(Parse->DataEnd() - Parse->DataFrom()) * sizeof(TCHAR));
	Outcome.DataHash = DataHasher.Finalize();
	if (LastData.IsSet() && LastData.GetValue() == Outcome.DataHash)
	{
		Outcome.Kind = FWorldOutcome::EKind::SameData;
		Parse.Reset();
		return;
	}
	if (Outcome.Bytes > MaxBodyBytes)
	{
		Outcome.Kind = FWorldOutcome::EKind::OverCap;
		Parse.Reset();
		return;
	}
	Outcome.Body = EncodeBody(Json); // gzipped over 1 KB, converted straight to UTF-8
	Outcome.Kind = FWorldOutcome::EKind::Ready;
	Parse.Reset();
}
```

`FrmBridge.h`: `#include "SatisDash/FrmWorld.h"`; public (after `TakeMapLive`):

```cpp
		/**
		 * Asks FRM for one world layer (world lane) and hashes, parses and encodes its body on the worker. LastRaw and
		 * LastData are the layer's last settled raw hash and last accepted data hash (FWorldSender); they are copied into the
		 * job. False while that layer is already requested, being read, or has an outcome waiting.
		 */
		bool RequestWorld(EWorldLayer Layer, const FString& ObservedAt, TOptional<FBlake3Hash> LastRaw, TOptional<FBlake3Hash> LastData);
		/** The layer's finished outcome, once. */
		TOptional<FWorldOutcome> TakeWorld(EWorldLayer Layer);
```

private (before `Harvest`):

```cpp
		struct FWorldJob
		{
			FWorldJob(EWorldLayer InLayer, TArray<uint8> InBody, const FString& InObservedAt, TOptional<FBlake3Hash> InLastRaw, TOptional<FBlake3Hash> InLastData)
				: Read{InLayer, MoveTemp(InBody), InObservedAt, InLastRaw, InLastData}
			{
			}
			FWorldRead Read;
			bool bFinished = false;
			bool bRan = false;
		};
		struct FWorldSlot
		{
			bool bRequested = false;
			TSharedPtr<FWorldJob> Job;
			TOptional<FWorldOutcome> Outcome;
		};
		bool LargeBusy() const;
		FWorldSlot* NextWorldJob();
```

and the member `FWorldSlot World[WorldLayerCount];` (after `MapLiveReady`). Memory comment: add "one world body (4 MB cap) being hashed and parsed, and its output (at most 2,000,000 bytes, then gzipped), and for each layer the last two hashes (32 bytes each)".

`FrmBridge.cpp`:

```cpp
bool SatisDash::FFrmBridge::LargeBusy() const
{
	return Factory.IsValid() || World[0].Job.IsValid() || World[1].Job.IsValid();
}

SatisDash::FFrmBridge::FWorldSlot* SatisDash::FFrmBridge::NextWorldJob()
{
	for (FWorldSlot& Slot : World)
	{
		if (Slot.Job.IsValid() && !Slot.Job->bRan)
		{
			return &Slot;
		}
	}
	return nullptr;
}

bool SatisDash::FFrmBridge::RequestWorld(EWorldLayer Layer, const FString& ObservedAt, TOptional<FBlake3Hash> LastRaw, TOptional<FBlake3Hash> LastData)
{
	FWorldSlot& Slot = World[int32(Layer)];
	if (Slot.bRequested || Slot.Job.IsValid() || Slot.Outcome.IsSet())
	{
		return false;
	}
	Slot.bRequested = true;
	FrmLanes.Enqueue(FFrmRead{ WorldLayer(Layer).Endpoint, [this, Layer, ObservedAt, LastRaw, LastData](FHttpReply& Reply)
	{
		FWorldSlot& Each = World[int32(Layer)];
		if (Reply.Code >= 200 && Reply.Code < 300)
		{
			Each.Job = MakeShared<FWorldJob>(Layer, MoveTemp(Reply.Body), ObservedAt, LastRaw, LastData);
		}
		else
		{
			FWorldOutcome Failed;
			Failed.Kind = FWorldOutcome::EKind::ReadFailed;
			Each.Outcome = MoveTemp(Failed);
			Each.bRequested = false;
		}
	} });
	return true;
}

TOptional<SatisDash::FWorldOutcome> SatisDash::FFrmBridge::TakeWorld(EWorldLayer Layer)
{
	FWorldSlot& Slot = World[int32(Layer)];
	TOptional<FWorldOutcome> Out = MoveTemp(Slot.Outcome);
	Slot.Outcome.Reset();
	return Out;
}
```

In `Harvest`, after the mapLive sides:

```cpp
	for (FWorldSlot& Slot : World)
	{
		if (!Slot.Job.IsValid() || !Slot.Job->bRan)
		{
			continue;
		}
		Slot.Job->bRan = false;
		if (Slot.Job->bFinished)
		{
			Slot.Outcome = Slot.Job->Read.TakeOutcome();
			Slot.Job.Reset();
			Slot.bRequested = false;
		}
	}
```

In `LaunchNext`, as the last branch of the chain (after the factory branch):

```cpp
	else if (FWorldSlot* Slot = NextWorldJob())
	{
		Worker.Launch([Job = Slot->Job]
		{
			Job->bFinished = Job->Read.Step();
			Job->bRan = true;
		});
	}
```

In `Tick`, change `FrmLanes.Pump(Port, Token, Factory.IsValid());` to `FrmLanes.Pump(Port, Token, LargeBusy());`. In `Stop()`: `for (FWorldSlot& Slot : World) { Slot = FWorldSlot(); }`.

- [ ] **Step 4: Run the tests**

Expected: `judge-mod-test --min 133` passes. `check-api` rows to add (looked up for this plan in the installed 5.6.1 engine): `Hash/Blake3.h` `Engine/Source/Runtime/Core/Public/Hash/Blake3.h:1`; `FBlake3Hash` `.../Hash/Blake3.h:26`; `FBlake3` `.../Hash/Blake3.h:93`; `FBlake3::Update` `.../Hash/Blake3.h:106` (the `const void*, uint64` overload); `FBlake3::Finalize` `.../Hash/Blake3.h:114`; `FBlake3::HashBuffer` `.../Hash/Blake3.h:117` (tests; the `const void*, uint64` overload). All `header only`. Core exports them (`CORE_API`); `SatisDashCore` already depends on `Core`.

- [ ] **Step 5: Commit**

```bash
git add mod/SatisDashAgent/Source api-sources.md
git -c core.safecrlf=false commit -m "The world job: hash the raw body, parse, hash the data, encode, a slice at a time (FRM bridge 3B Task 7)" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

### Task 8: The world sender

**Files:**
- Create: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/WorldSender.h`, `Private/WorldSender.cpp`
- Test: new `WorldSenderTest.cpp`

**Interfaces:**
- Consumes: `FFrmBridge::RequestWorld/TakeWorld` (Task 7), `BuildWorldRequest`, `ParseWorldResponse` (Task 5), `ClassifyReply`, `BackoffSeconds`, `ParseRetryAfterSeconds` (`Pusher.h`), `FRateLimitedLog`.
- Produces: `double JitteredSeconds(double Base, double Random01)`; `struct FWorldContext { double Now; FDateTime Utc; const FAgentConfig* Config; const FCredential* Credential; bool bSnapshotsIdle; }`; `FWorldSender(ITransport&, FFrmBridge&, TFunction<double()> Random01)` with `Start(Now)`, `Stop()`, `Tick(const FWorldContext&)`, `bool TakeAuthRejected()`, `bool Active() const`, `int32 Held() const`.

The sender is the Node agent's `WorldReader` (worldReader.ts), one layer after another, without ever waiting on the game thread:

| Per layer | What it does |
| --- | --- |
| `Waiting` | When `Now >= NextAt`: asks the bridge to read the layer (stamping `observedAt` with the time of the request), then `Reading` |
| `Reading` | When the bridge has an outcome: `ReadFailed`/`Invalid` log once per streak and wait a full interval; `SameRaw` waits; `SameData` remembers the raw hash and waits; `OverCap` logs once, remembers the raw hash and waits; `Ready` holds the encoded body, `Pending` |
| `Pending` | When `Now >= NextAttemptAt`, no post is in flight, the snapshot queue is idle, and a credential is held: builds the request (a copy of the held body, kept for a retry) and sends it, `Posting` |
| `Posting` | On the reply: accepted (2xx whose body is `{accepted, unchanged}`) remembers both hashes, frees the body, waits an interval; transient (no answer, 429, 5xx, or a 2xx not in that form) retries the same body after `max(Retry-After, backoff)`; any other 4xx logs once, drops the body without remembering, and waits an interval (the next cycle reads again); 401 stops the sender and raises `TakeAuthRejected` |

- The wait after a cycle is `JitteredSeconds(Interval)` from the moment the cycle ended, as the Node agent's `loop` sleeps after `cycle`.
- A new layer cycle never starts while its body is held or in flight (the Node agent's `send` blocks its loop the same way).
- One post at a time across layers, and none while a snapshot is queued or in flight (spec "World-layer sends ... go one at a time behind snapshots").
- Stop and Start: `Stop` bumps a generation, drops every held body and hash; replies from an older generation change nothing. `Start` re-arms the boot offsets, so the hashes are forgotten and the first read after a start always posts (spec).

- [ ] **Step 1: Write the failing tests**

`WorldSenderTest.cpp`:

```cpp
#include "DriverFakes.h"
#include "SatisDash/WorldSender.h"

using namespace SatisDash;

namespace
{
	const TCHAR* WorldRailsBody = TEXT("[{\"ID\":\"R1\",\"SplineData\":[{\"x\":0,\"y\":0},{\"x\":300,\"y\":0}]}]");
	const TCHAR* WorldRailsNoisy = TEXT("[{\"ID\":\"R1\",\"SplineData\":[{\"x\":0,\"y\":0,\"z\":5},{\"x\":300,\"y\":0,\"z\":7}]}]");
	const TCHAR* WorldRailsMoved = TEXT("[{\"ID\":\"R1\",\"SplineData\":[{\"x\":0,\"y\":0},{\"x\":600,\"y\":0}]}]");
	const TCHAR* WorldNodesBody = TEXT("[{\"Name\":\"Iron Ore\",\"Purity\":\"Normal\",\"NodeType\":\"Node\",\"Exploited\":false,\"location\":{\"x\":100,\"y\":200,\"z\":0}}]");
	const TCHAR* WorldRailsRead = TEXT("/getTrainRails");
	const TCHAR* WorldNodesRead = TEXT("/getResourceNode");
	const TCHAR* WorldRailsPost = TEXT("/agent/v1/world/rails");
	const TCHAR* WorldNodesPost = TEXT("/agent/v1/world/resourceNodes");
	const TCHAR* WorldAccepted = TEXT("{\"accepted\":true,\"unchanged\":false}");

	/** A bridge, a sender whose jitter is the top of its range (x1.1: rails at 132 s then every 660 s, nodes at 528 s then every 1980 s), and a fake clock. */
	struct FWorldFixture
	{
		SatisDash::Test::FFakeTransport Net;
		SatisDash::Test::FFakeWorker Worker;
		FFrmBridge Bridge{ Net, Worker };
		FWorldSender Sender{ Net, Bridge, [] { return 1.0; } };
		FAgentConfig Config;
		FCredential Credential;
		double T = 1000;
		FDateTime Utc = FDateTime(2026, 10, 24, 12, 0, 0);
		bool bSnapshotsIdle = true;
		/** Answer a resource-node read with Code 0 when it shows up, so a test about rails isn't held up by it (one FRM request in flight). */
		bool bIgnoreNodes = true;

		FWorldFixture()
		{
			Config.BackendUrl = TEXT("http://127.0.0.1:1");
			Credential.AgentSecret = SatisDash::Test::FakeSecret();
			Credential.ServerId = TEXT("srv-1");
		}

		void tick(double Seconds = 0.5)
		{
			for (double Done = 0; Done < Seconds; Done += 0.5)
			{
				T += 0.5;
				Utc += FTimespan::FromMilliseconds(500);
				if (bIgnoreNodes)
				{
					Net.answer(WorldNodesRead, SatisDash::Test::Reply(0, FString()));
				}
				Bridge.Tick(T, 8080, FString());
				Sender.Tick({ T, Utc, &Config, &Credential, bSnapshotsIdle });
			}
		}

		bool until(const TFunction<bool()>& Done, double MaxSeconds)
		{
			for (double Spent = 0; Spent < MaxSeconds && !Done(); Spent += 0.5)
			{
				tick();
			}
			return Done();
		}

		/** Ticks until Path is open (at most MaxSeconds), then answers it. */
		bool serve(const TCHAR* Path, const FHttpReply& Reply, double MaxSeconds = 1000)
		{
			return until([this, Path] { return Net.pending(Path) > 0; }, MaxSeconds) && Net.answer(Path, Reply);
		}

		/** Starts the sender, serves the rails read with Body, and runs until the post is open. */
		bool toRailsPost(const TCHAR* Body)
		{
			return serve(WorldRailsRead, SatisDash::Test::Reply(200, Body)) && until([this] { return Net.pending(WorldRailsPost) > 0; }, 20);
		}

		int32 posts() const { return Net.sent(WorldRailsPost) + Net.sent(WorldNodesPost); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSenderScheduleTest, "SatisDash.Core.WorldSender.Schedule", SatisDash::Test::Flags)

bool FWorldSenderScheduleTest::RunTest(const FString& Parameters)
{
	// worldReader.ts jitteredIntervalMs: [0.9, 1.1] of the base; NaN is the middle; out of range is clamped.
	TestEqual(TEXT("random 0 is 0.9"), JitteredSeconds(100.0, 0.0), 90.0);
	TestEqual(TEXT("random 0.5 is 1.0"), JitteredSeconds(100.0, 0.5), 100.0);
	TestEqual(TEXT("random 1 is 1.1"), JitteredSeconds(100.0, 1.0), 110.0);
	TestEqual(TEXT("NaN is 1.0"), JitteredSeconds(100.0, std::numeric_limits<double>::quiet_NaN()), 100.0);
	TestEqual(TEXT("above 1 is clamped"), JitteredSeconds(100.0, 5.0), 110.0);
	TestEqual(TEXT("below 0 is clamped"), JitteredSeconds(100.0, -1.0), 90.0);

	FWorldFixture F;
	F.bIgnoreNodes = false;
	TestEqual(TEXT("nothing is read before the sender starts"), F.Net.sent(WorldRailsRead), 0);
	F.tick(300);
	TestEqual(TEXT("still nothing: no Start, no reads"), F.Net.sent(WorldRailsRead), 0);

	F.Sender.Start(F.T);
	F.tick(130);
	TestEqual(TEXT("rails are not read before 120 s x 1.1"), F.Net.sent(WorldRailsRead), 0);
	F.tick(4);
	TestEqual(TEXT("rails are read at about 132 s"), F.Net.pending(WorldRailsRead), 1);
	F.Net.answer(WorldRailsRead, SatisDash::Test::Reply(0, FString())); // FRM fails: a full interval is skipped
	F.tick(380);
	TestEqual(TEXT("resource nodes are not read before 480 s x 1.1"), F.Net.sent(WorldNodesRead), 0);
	F.tick(20);
	TestEqual(TEXT("resource nodes are read at about 528 s"), F.Net.pending(WorldNodesRead), 1);
	F.Net.answer(WorldNodesRead, SatisDash::Test::Reply(0, FString()));
	// The failed rails read waited a whole interval (660 s) from the end of its cycle: not read again at 600 s.
	TestEqual(TEXT("rails are not read again early"), F.Net.sent(WorldRailsRead), 1);
	TestTrue(TEXT("and are read again after 660 s"), F.until([&F] { return F.Net.pending(WorldRailsRead) > 0; }, 300));

	// No reads while the agent is not running.
	FWorldFixture Idle;
	Idle.bIgnoreNodes = false;
	Idle.Sender.Start(Idle.T);
	Idle.Sender.Stop();
	Idle.tick(1200);
	TestEqual(TEXT("Stop: nothing is read"), Idle.Net.sent(WorldRailsRead) + Idle.Net.sent(WorldNodesRead), 0);
	TestFalse(TEXT("and it isn't active"), Idle.Sender.Active());
	Idle.Sender.Start(Idle.T);
	Idle.tick(130);
	TestEqual(TEXT("Start re-arms the boot offset"), Idle.Net.sent(WorldRailsRead), 0);
	Idle.tick(6);
	TestEqual(TEXT("rails are read again 132 s after the new Start"), Idle.Net.pending(WorldRailsRead), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSenderSendTest, "SatisDash.Core.WorldSender.Send", SatisDash::Test::Flags)

bool FWorldSenderSendTest::RunTest(const FString& Parameters)
{
	FWorldFixture F;
	F.Sender.Start(F.T);
	TestTrue(TEXT("the first rails read ends in a post"), F.toRailsPost(WorldRailsBody));
	const FString Posted = F.Net.lastBody(WorldRailsPost);
	TestTrue(TEXT("the body starts with observedAt (when FRM was asked, about 132 s after the fixture's 12:00:00)"), Posted.StartsWith(TEXT("{\"observedAt\":\"2026-10-24T12:02:"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("and ends with the data, in the Node agent's shape"), Posted.EndsWith(TEXT("Z\",\"data\":[{\"id\":\"R1\",\"points\":[[0,0],[3,0]]}]}"), ESearchCase::CaseSensitive));
	SatisDash::Test::SameText(*this, TEXT("with the credential"), F.Net.lastAuth(WorldRailsPost), TEXT("Bearer ") + SatisDash::Test::FakeSecret());
	TestEqual(TEXT("one post"), F.Net.sent(WorldRailsPost), 1);
	F.Net.answer(WorldRailsPost, SatisDash::Test::Reply(200, WorldAccepted));

	// The same raw body next cycle: one hash step on the worker, no parse and no post.
	TestTrue(TEXT("the next cycle reads rails again"), F.until([&F] { return F.Net.pending(WorldRailsRead) > 0; }, 700));
	int32 Launched = F.Worker.Launched;
	F.Net.answer(WorldRailsRead, SatisDash::Test::Reply(200, WorldRailsBody));
	F.tick(10);
	TestEqual(TEXT("an unchanged raw body costs one worker task"), F.Worker.Launched, Launched + 1);
	TestEqual(TEXT("and sends nothing"), F.Net.sent(WorldRailsPost), 1);

	// A different raw body with the same mapped data: parsed, not sent.
	TestTrue(TEXT("and again"), F.until([&F] { return F.Net.pending(WorldRailsRead) > 0; }, 700));
	Launched = F.Worker.Launched;
	F.Net.answer(WorldRailsRead, SatisDash::Test::Reply(200, WorldRailsNoisy));
	F.tick(10);
	TestEqual(TEXT("a changed raw body is hashed and parsed"), F.Worker.Launched, Launched + 2);
	TestEqual(TEXT("but the data is the same: nothing is sent"), F.Net.sent(WorldRailsPost), 1);

	// A changed layer is sent.
	TestTrue(TEXT("and again, with a moved rail"), F.until([&F] { return F.Net.pending(WorldRailsRead) > 0; }, 700));
	F.Net.answer(WorldRailsRead, SatisDash::Test::Reply(200, WorldRailsMoved));
	TestTrue(TEXT("a changed layer is posted"), F.until([&F] { return F.Net.pending(WorldRailsPost) > 0; }, 20));
	TestTrue(TEXT("with its new points"), F.Net.lastBody(WorldRailsPost).Contains(TEXT("[[0,0],[6,0]]")));
	F.Net.answer(WorldRailsPost, SatisDash::Test::Reply(200, WorldAccepted));
	TestEqual(TEXT("two posts in all"), F.Net.sent(WorldRailsPost), 2);

	// A restart forgets the hashes: the first read after a start always posts, even the same body, even an empty layer.
	F.Sender.Stop();
	F.Bridge.Stop();
	F.Sender.Start(F.T);
	TestTrue(TEXT("after a restart the same body posts again"), F.toRailsPost(WorldRailsBody));
	F.Net.answer(WorldRailsPost, SatisDash::Test::Reply(200, WorldAccepted));

	FWorldFixture Empty;
	Empty.Sender.Start(Empty.T);
	TestTrue(TEXT("an empty layer is posted the first time"), Empty.toRailsPost(TEXT("[]")));
	TestTrue(TEXT("with an empty data array"), Empty.Net.lastBody(WorldRailsPost).EndsWith(TEXT("\"data\":[]}")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSenderRetryTest, "SatisDash.Core.WorldSender.Retry", SatisDash::Test::Flags)

bool FWorldSenderRetryTest::RunTest(const FString& Parameters)
{
	FWorldFixture F;
	F.Sender.Start(F.T);
	TestTrue(TEXT("a post is open"), F.toRailsPost(WorldRailsBody));
	const FString First = F.Net.lastBody(WorldRailsPost);

	// 503 with Retry-After: 30. The delay is max(Retry-After, backoff) = 30 s, and the same body is sent again.
	FHttpReply Unavailable = SatisDash::Test::Reply(503, TEXT("{}"));
	Unavailable.RetryAfter = TEXT("30");
	F.Net.answer(WorldRailsPost, Unavailable);
	F.tick(25);
	TestEqual(TEXT("no retry before the Retry-After"), F.Net.sent(WorldRailsPost), 1);
	F.tick(6);
	TestEqual(TEXT("retried after it"), F.Net.sent(WorldRailsPost), 2);
	SatisDash::Test::SameText(*this, TEXT("the same body, observedAt included"), F.Net.lastBody(WorldRailsPost), First);
	TestEqual(TEXT("without reading FRM again"), F.Net.sent(WorldRailsRead), 1);

	// 429 with no Retry-After: the second backoff, ceiling 2 s, delay 2 s (random 1).
	F.Net.answer(WorldRailsPost, SatisDash::Test::Reply(429, FString()));
	F.tick(1.5);
	TestEqual(TEXT("429: not yet"), F.Net.sent(WorldRailsPost), 2);
	F.tick(1);
	TestEqual(TEXT("429: retried after the backoff"), F.Net.sent(WorldRailsPost), 3);
	// No answer at all: the third backoff, 4 s.
	F.Net.answer(WorldRailsPost, SatisDash::Test::Reply(0, FString()));
	F.tick(3.5);
	TestEqual(TEXT("no answer: not yet"), F.Net.sent(WorldRailsPost), 3);
	F.tick(1);
	TestEqual(TEXT("no answer: retried"), F.Net.sent(WorldRailsPost), 4);
	// A 2xx that is not { accepted, unchanged } is transient too (backendClient.ts): the fourth backoff, 8 s.
	F.Net.answer(WorldRailsPost, SatisDash::Test::Reply(200, TEXT("{\"nope\":1}")));
	F.tick(7.5);
	TestEqual(TEXT("a 2xx in the wrong form: not yet"), F.Net.sent(WorldRailsPost), 4);
	F.tick(1);
	TestEqual(TEXT("and retried"), F.Net.sent(WorldRailsPost), 5);
	F.Net.answer(WorldRailsPost, SatisDash::Test::Reply(202, WorldAccepted));
	F.tick(100);
	TestEqual(TEXT("accepted: no more posts until the layer changes"), F.Net.sent(WorldRailsPost), 5);
	TestEqual(TEXT("and the held body is freed"), F.Sender.Held(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSenderRefusedTest, "SatisDash.Core.WorldSender.Refused", SatisDash::Test::Flags)

bool FWorldSenderRefusedTest::RunTest(const FString& Parameters)
{
	FWorldFixture F;
	F.Sender.Start(F.T);
	TestTrue(TEXT("a post is open"), F.toRailsPost(WorldRailsBody));
	F.Net.answer(WorldRailsPost, SatisDash::Test::Reply(400, TEXT("{\"error\":{\"code\":\"bad_request\"}}")));
	F.tick(300);
	TestEqual(TEXT("another 4xx: given up, not retried"), F.Net.sent(WorldRailsPost), 1);
	TestEqual(TEXT("and the held body is freed"), F.Sender.Held(), 0);
	// Not remembered: the next cycle reads the same raw body again and posts it again (an older backend may not know the route yet).
	TestTrue(TEXT("the next cycle reads again"), F.until([&F] { return F.Net.pending(WorldRailsRead) > 0; }, 700));
	F.Net.answer(WorldRailsRead, SatisDash::Test::Reply(200, WorldRailsBody));
	TestTrue(TEXT("and posts it again"), F.until([&F] { return F.Net.pending(WorldRailsPost) > 0; }, 20));
	TestEqual(TEXT("a second post"), F.Net.sent(WorldRailsPost), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSenderHaltTest, "SatisDash.Core.WorldSender.Halt", SatisDash::Test::Flags)

bool FWorldSenderHaltTest::RunTest(const FString& Parameters)
{
	FWorldFixture F;
	F.Sender.Start(F.T);
	TestTrue(TEXT("a post is open"), F.toRailsPost(WorldRailsBody));
	F.Net.answer(WorldRailsPost, SatisDash::Test::Reply(401, FString()));
	F.tick();
	TestTrue(TEXT("a 401 halts the agent (the driver reads this)"), F.Sender.TakeAuthRejected());
	TestFalse(TEXT("once"), F.Sender.TakeAuthRejected());
	TestFalse(TEXT("and the sender stops"), F.Sender.Active());
	TestEqual(TEXT("holding nothing"), F.Sender.Held(), 0);
	F.tick(2000);
	TestEqual(TEXT("no more reads"), F.Net.sent(WorldRailsRead), 1);
	TestEqual(TEXT("and no more posts"), F.Net.sent(WorldRailsPost), 1);
	F.Sender.Start(F.T);
	TestTrue(TEXT("a new Start runs it again"), F.Sender.Active() && !F.Sender.TakeAuthRejected());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSenderBehindTest, "SatisDash.Core.WorldSender.Behind", SatisDash::Test::Flags)

bool FWorldSenderBehindTest::RunTest(const FString& Parameters)
{
	FWorldFixture F;
	F.bIgnoreNodes = false;
	F.bSnapshotsIdle = false;
	F.Sender.Start(F.T);
	TestTrue(TEXT("rails are read"), F.serve(WorldRailsRead, SatisDash::Test::Reply(200, WorldRailsBody)));
	TestTrue(TEXT("resource nodes are read"), F.serve(WorldNodesRead, SatisDash::Test::Reply(200, WorldNodesBody)));
	F.tick(20);
	TestEqual(TEXT("both bodies are ready"), F.Sender.Held(), 2);
	TestEqual(TEXT("but a snapshot is queued or in flight: no world post goes"), F.posts(), 0);
	F.bSnapshotsIdle = true;
	F.tick(1);
	TestEqual(TEXT("the snapshots are idle: exactly one post, one at a time"), F.Net.pending(WorldRailsPost) + F.Net.pending(WorldNodesPost), 1);
	TestEqual(TEXT("rails first"), F.Net.pending(WorldRailsPost), 1);
	F.tick(5);
	TestEqual(TEXT("the second waits for the first's answer"), F.posts(), 1);
	F.Net.answer(WorldRailsPost, SatisDash::Test::Reply(200, WorldAccepted));
	F.tick(1);
	TestEqual(TEXT("then resource nodes go"), F.Net.pending(WorldNodesPost), 1);
	F.Net.answer(WorldNodesPost, SatisDash::Test::Reply(200, WorldAccepted));
	F.tick(1);
	TestEqual(TEXT("nothing is held after both are accepted"), F.Sender.Held(), 0);

	// A snapshot becoming busy while a body waits holds the post back again; Stop frees what is held.
	FWorldFixture G;
	G.bSnapshotsIdle = false;
	G.Sender.Start(G.T);
	TestTrue(TEXT("rails read"), G.serve(WorldRailsRead, SatisDash::Test::Reply(200, WorldRailsBody)));
	G.tick(10);
	TestEqual(TEXT("one body held"), G.Sender.Held(), 1);
	G.Sender.Stop();
	TestEqual(TEXT("Stop frees it"), G.Sender.Held(), 0);
	return true;
}
```

- [ ] **Step 2: Run them to see them fail**

Run the mod tests with `--min 139`.
Expected: the build fails with `'WorldSender.h': No such file`.

- [ ] **Step 3: Implement**

`WorldSender.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Config.h"
#include "SatisDash/Credential.h"
#include "SatisDash/FrmBridge.h"
#include "SatisDash/FrmWorld.h"
#include "SatisDash/Ports.h"
#include "SatisDash/RateLimit.h"
#include "SatisDash/Responses.h"

namespace SatisDash
{
	/** worldReader.ts WORLD_JITTER_FRACTION. */
	inline constexpr double WorldJitterFraction = 0.1;

	/** worldReader.ts jitteredIntervalMs: Base x [0.9, 1.1]. Random01 is clamped to [0, 1]; NaN is the middle. */
	SATISDASHCORE_API double JitteredSeconds(double BaseSeconds, double Random01);

	/** What one tick of the sender needs from the driver. The credential is used at once and never kept (Agent.h). */
	struct FWorldContext
	{
		/** Monotonic seconds. */
		double Now;
		/** UTC, for observedAt. */
		FDateTime Utc;
		const FAgentConfig* Config;
		const FCredential* Credential;
		/** No snapshot is queued or in flight: world posts go only then (spec "World-layer sends ... behind snapshots"). */
		bool bSnapshotsIdle;
	};

	/**
	 * The Node agent's WorldReader (agent/src/worldReader.ts) on the game thread's tick: each layer is read on its own slow,
	 * jittered schedule, posted to POST /agent/v1/world/:layer only when its mapped data changed since the last ACCEPTED send,
	 * retried with backoff when the backend is unwell, and given up on until the next cycle when it refuses. The FRM read,
	 * hashing, parsing and gzip are FFrmBridge's (on the worker); this class never touches a body's bytes.
	 *
	 * Memory (it lives for the server's uptime): per layer two hashes (32 bytes each) and at most one encoded body (at most
	 * 2,000,000 bytes before gzip) held from Ready until accepted, refused or stopped.
	 */
	class SATISDASHCORE_API FWorldSender
	{
	public:
		FWorldSender(ITransport& InTransport, FFrmBridge& InBridge, TFunction<double()> InRandom01);
		FWorldSender(const FWorldSender&) = delete;
		FWorldSender& operator=(const FWorldSender&) = delete;

		/** The agent started Running: forget every hash and arm each layer's boot offset (x jitter). */
		void Start(double Now);
		/** The agent left Running: no read, no post, no held body; a reply still on its way changes nothing. */
		void Stop();
		void Tick(const FWorldContext& Context);
		/** True once after a world post got 401: the driver halts the agent as it does for a snapshot 401. */
		bool TakeAuthRejected();
		bool Active() const { return bActive; }
		/** Layers holding an encoded body (waiting for a post or in flight). */
		int32 Held() const;

	private:
		enum class EPhase : uint8 { Waiting, Reading, Pending, Posting };
		struct FLayer
		{
			EPhase Phase = EPhase::Waiting;
			double NextAt = 0;
			double NextAttemptAt = 0;
			int32 Attempts = 0;
			/** The last settled raw body and the last accepted data (see the plan's Global Constraints). */
			TOptional<FBlake3Hash> RawHash;
			TOptional<FBlake3Hash> DataHash;
			FEncodedBody Body;
			FBlake3Hash PendingRaw;
			FBlake3Hash PendingData;
		};

		void TickLayer(EWorldLayer Layer, const FWorldContext& Context);
		void Settle(EWorldLayer Layer, FWorldOutcome Outcome, const FWorldContext& Context);
		void Post(EWorldLayer Layer, const FWorldContext& Context);
		void Finish(EWorldLayer Layer, uint32 SentIn, const FHttpReply& Reply);
		void ScheduleNext(EWorldLayer Layer, double Now);
		bool ShouldLog(EWorldLayer Layer, const TCHAR* Event, double Now);

		ITransport& Transport;
		FFrmBridge& Bridge;
		TFunction<double()> RandomFn;
		FLayer Layers[WorldLayerCount];
		bool bActive = false;
		bool bPostInFlight = false;
		bool bAuthRejected = false;
		double LastNow = 0;
		uint32 Generation = 0;
		/** Bounded: each layer's few events, each at most once per 600 s. */
		FRateLimitedLog Log;
	};
}
```

`WorldSender.cpp`:

```cpp
#include "SatisDash/WorldSender.h"

#include "SatisDash/Log.h"
#include "SatisDash/Pusher.h"
#include "SatisDash/Requests.h"

double SatisDash::JitteredSeconds(double BaseSeconds, double Random01)
{
	const double R = FMath::IsNaN(Random01) ? 0.5 : FMath::Clamp(Random01, 0.0, 1.0);
	return BaseSeconds * (1.0 - WorldJitterFraction + R * 2.0 * WorldJitterFraction);
}

SatisDash::FWorldSender::FWorldSender(ITransport& InTransport, FFrmBridge& InBridge, TFunction<double()> InRandom01)
	: Transport{InTransport}
	, Bridge{InBridge}
	, RandomFn{MoveTemp(InRandom01)}
{
}

void SatisDash::FWorldSender::Start(double Now)
{
	++Generation;
	bActive = true;
	bPostInFlight = false;
	bAuthRejected = false;
	LastNow = Now;
	for (int32 I = 0; I < WorldLayerCount; ++I)
	{
		Layers[I] = FLayer();
		Layers[I].NextAt = Now + JitteredSeconds(WorldLayer(EWorldLayer(I)).FirstSeconds, RandomFn());
	}
}

void SatisDash::FWorldSender::Stop()
{
	++Generation;
	bActive = false;
	bPostInFlight = false;
	for (FLayer& Layer : Layers)
	{
		Layer = FLayer();
	}
}

bool SatisDash::FWorldSender::TakeAuthRejected()
{
	const bool bWas = bAuthRejected;
	bAuthRejected = false;
	return bWas;
}

int32 SatisDash::FWorldSender::Held() const
{
	int32 Count = 0;
	for (const FLayer& Layer : Layers)
	{
		Count += Layer.Body.Bytes.Num() > 0 ? 1 : 0;
	}
	return Count;
}

bool SatisDash::FWorldSender::ShouldLog(EWorldLayer Layer, const TCHAR* Event, double Now)
{
	return Log.ShouldLog(FString::Printf(TEXT("%s:%s"), WorldLayer(Layer).Name, Event), Now);
}

void SatisDash::FWorldSender::ScheduleNext(EWorldLayer Layer, double Now)
{
	FLayer& L = Layers[int32(Layer)];
	L.Phase = EPhase::Waiting;
	L.NextAt = Now + JitteredSeconds(WorldLayer(Layer).IntervalSeconds, RandomFn());
}

void SatisDash::FWorldSender::Tick(const FWorldContext& Context)
{
	if (!bActive)
	{
		return;
	}
	LastNow = Context.Now;
	for (int32 I = 0; I < WorldLayerCount; ++I)
	{
		TickLayer(EWorldLayer(I), Context);
		if (!bActive)
		{
			return; // a 401 stopped it
		}
	}
}

void SatisDash::FWorldSender::TickLayer(EWorldLayer Layer, const FWorldContext& Context)
{
	FLayer& L = Layers[int32(Layer)];
	switch (L.Phase)
	{
	case EPhase::Waiting:
		if (Context.Now >= L.NextAt && Bridge.RequestWorld(Layer, Context.Utc.ToIso8601(), L.RawHash, L.DataHash))
		{
			L.Phase = EPhase::Reading;
		}
		return;
	case EPhase::Reading:
		if (TOptional<FWorldOutcome> Outcome = Bridge.TakeWorld(Layer))
		{
			Settle(Layer, MoveTemp(Outcome.GetValue()), Context);
		}
		return;
	case EPhase::Pending:
		if (Context.Now >= L.NextAttemptAt && !bPostInFlight && Context.bSnapshotsIdle && Context.Config != nullptr && Context.Credential != nullptr)
		{
			Post(Layer, Context);
		}
		return;
	case EPhase::Posting:
		return;
	}
}

void SatisDash::FWorldSender::Settle(EWorldLayer Layer, FWorldOutcome Outcome, const FWorldContext& Context)
{
	using EKind = FWorldOutcome::EKind;
	FLayer& L = Layers[int32(Layer)];
	const TCHAR* Name = WorldLayer(Layer).Name;
	if ((Outcome.Dropped > 0 || Outcome.Over > 0) && ShouldLog(Layer, TEXT("dropped"), Context.Now))
	{
		UE_LOG(LogSatisDash, Warning, TEXT("World layer %s: FRM sent %d items the map can't hold and %d past the cap; left them out"), Name, Outcome.Dropped, Outcome.Over);
	}
	switch (Outcome.Kind)
	{
	case EKind::ReadFailed:
	case EKind::Invalid:
		if (ShouldLog(Layer, TEXT("read"), Context.Now))
		{
			UE_LOG(LogSatisDash, Warning, TEXT("Couldn't read world layer %s from FRM; skipping this cycle"), Name);
		}
		ScheduleNext(Layer, Context.Now);
		return;
	case EKind::SameRaw:
		ScheduleNext(Layer, Context.Now);
		return;
	case EKind::SameData:
		L.RawHash = Outcome.RawHash; // nothing to send, and this body needs no second look
		ScheduleNext(Layer, Context.Now);
		return;
	case EKind::OverCap:
		if (ShouldLog(Layer, TEXT("cap"), Context.Now))
		{
			UE_LOG(LogSatisDash, Warning, TEXT("World layer %s is %lld bytes, over the %lld-byte cap; skipped it (the backend keeps its last copy)"), Name, Outcome.Bytes, WorldLayerMaxBytes);
		}
		L.RawHash = Outcome.RawHash;
		ScheduleNext(Layer, Context.Now);
		return;
	case EKind::Ready:
		L.Body = MoveTemp(Outcome.Body);
		L.PendingRaw = Outcome.RawHash;
		L.PendingData = Outcome.DataHash;
		L.Attempts = 0;
		L.NextAttemptAt = Context.Now;
		L.Phase = EPhase::Pending;
		return;
	}
}

void SatisDash::FWorldSender::Post(EWorldLayer Layer, const FWorldContext& Context)
{
	FLayer& L = Layers[int32(Layer)];
	// The body is copied into the request and kept for a retry (at most the layer's gzipped size, once per change).
	const FRequestSpec Spec = BuildWorldRequest(*Context.Config, *Context.Credential, WorldLayer(Layer).Name, L.Body);
	L.Phase = EPhase::Posting;
	bPostInFlight = true;
	const uint32 SentIn = Generation;
	Transport.Send(Spec, [this, Layer, SentIn](const FHttpReply& Reply) { Finish(Layer, SentIn, Reply); });
}

void SatisDash::FWorldSender::Finish(EWorldLayer Layer, uint32 SentIn, const FHttpReply& Reply)
{
	if (SentIn != Generation || !bActive)
	{
		return; // from an older run: it changes nothing
	}
	bPostInFlight = false;
	FLayer& L = Layers[int32(Layer)];
	const TCHAR* Name = WorldLayer(Layer).Name;
	const double Now = LastNow;
	const EReplyKind Kind = ClassifyReply(Reply.Code, true);
	if (Kind == EReplyKind::AuthRejected)
	{
		bAuthRejected = true;
		bActive = false;
		for (FLayer& Each : Layers)
		{
			Each = FLayer();
		}
		return;
	}
	if (Kind == EReplyKind::Ok && ParseWorldResponse(Reply).IsSet())
	{
		L.RawHash = L.PendingRaw;
		L.DataHash = L.PendingData;
		if (L.Attempts > 0)
		{
			UE_LOG(LogSatisDash, Display, TEXT("Pushing world layer %s recovered after %d attempts"), Name, L.Attempts);
		}
		L.Body = FEncodedBody();
		L.Attempts = 0;
		ScheduleNext(Layer, Now);
		return;
	}
	if (Kind == EReplyKind::Fatal)
	{
		// worldReader.ts: not retried, and its hash is not recorded, so the layer is tried again at its next read.
		if (ShouldLog(Layer, TEXT("refused"), Now))
		{
			UE_LOG(LogSatisDash, Warning, TEXT("The backend refused world layer %s (%d); will try again next cycle"), Name, Reply.Code);
		}
		L.Body = FEncodedBody();
		L.Attempts = 0;
		ScheduleNext(Layer, Now);
		return;
	}
	// Transient, or a 2xx not in the expected form: the same body again after max(Retry-After, backoff).
	if (L.Attempts == 0 && ShouldLog(Layer, TEXT("push"), Now))
	{
		UE_LOG(LogSatisDash, Warning, TEXT("Pushing world layer %s failed (%d); retrying with backoff"), Name, Reply.Code);
	}
	const double Delay = FMath::Max(ParseRetryAfterSeconds(Reply.RetryAfter).Get(0.0), BackoffSeconds(L.Attempts, RandomFn()));
	++L.Attempts;
	L.NextAttemptAt = Now + Delay;
	L.Phase = EPhase::Pending;
}
```

- [ ] **Step 4: Run the tests**

Expected: `judge-mod-test --min 139` passes. Likely `check-api` additions: `FDateTime::ToIso8601` (present), `TFunction` (present); anything else it reports is looked up and added.

Timing note for the implementer: the retry tests count ticks of 0.5 s from the answer, and `Finish` stamps the retry time from the last tick's time (`LastNow`), so a delay of `D` seconds opens on the tick that is `D` seconds later. If a boundary assertion is off by one tick, the fixture (not the delays) is wrong: check that `answer` is called between ticks and that `tick` runs the bridge before the sender, as written.

- [ ] **Step 5: Commit**

```bash
git add mod/SatisDashAgent/Source api-sources.md
git -c core.safecrlf=false commit -m "The world sender: schedule, change-only posts, retries, 401 halt, behind snapshots (FRM bridge 3B Task 8)" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

### Task 9: The driver runs the world sender

**Files:**
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Driver.h`, `Private/Driver.cpp`
- Test: new `DriverWorldTest.cpp`

**Interfaces:**
- Consumes: `FWorldSender` (Task 8).
- Produces: the driver owns an `FWorldSender World` (after `Bridge`), starts it on the edge into Running, stops it on the edge out of Running (next to `Bridge.Stop()`), ticks it every tick while Running, and turns its 401 into the same halt a snapshot 401 makes (`++Generation; Agent.OnAuthRejected()`).

Behaviour (spec "Failures": "The agent isn't running: No FRM reads at all"; "A world-layer post gets 401: Halt the agent, as a snapshot 401 does"):
- World posts go only when no snapshot is queued, in flight, or being encoded (`Pusher.Num() == 0 && !bSendInFlight && !Bridge.EncodePending()`).
- `Start(Now)` runs at the same place the driver resets its other schedules (`NextStatusAt = Now`, ...), so the rails offset counts from when the agent started Running, and a re-enrolment re-arms it.

- [ ] **Step 1: Write the failing tests**

`DriverWorldTest.cpp`:

```cpp
#include "DriverFakes.h"
#include "SatisDash/Driver.h"

using namespace SatisDash;
using SatisDash::Test::FDriverFixture;

namespace
{
	const TCHAR* DriverWorldSnapshots = TEXT("/agent/v1/snapshots");
	const TCHAR* DriverWorldRailsRead = TEXT("/getTrainRails");
	const TCHAR* DriverWorldRailsPost = TEXT("/agent/v1/world/rails");
	const TCHAR* DriverWorldRails = TEXT("[{\"ID\":\"R1\",\"SplineData\":[{\"x\":0,\"y\":0},{\"x\":300,\"y\":0}]}]");
	const TCHAR* DriverWorldAccepted = TEXT("{\"accepted\":true,\"unchanged\":false}");

	/** The routine traffic: snapshots accepted (unless the test wants one open), live and node FRM reads failing fast. */
	void DriverWorldRoutine(FDriverFixture& F, bool bSnapshots)
	{
		while (bSnapshots && F.Net.pending(DriverWorldSnapshots) > 0)
		{
			F.Net.answer(DriverWorldSnapshots, SatisDash::Test::SnapshotOk(false));
		}
		for (const TCHAR* Path : { TEXT("/getPlayer"), TEXT("/getFactory"), TEXT("/getTrains"), TEXT("/getTrainStation"), TEXT("/getResourceNode") })
		{
			F.Net.answer(Path, SatisDash::Test::Reply(0, FString()));
		}
	}

	/** Runs the driver up to Seconds, answering the routine traffic each tick; true as soon as Done is. */
	bool DriverWorldRun(FDriverFixture& F, FDriver& Driver, const TFunction<bool()>& Done, double Seconds, bool bSnapshots = true)
	{
		for (double Spent = 0; Spent < Seconds; Spent += 0.5)
		{
			DriverWorldRoutine(F, bSnapshots);
			if (Done())
			{
				return true;
			}
			F.run(Driver, 0.5);
		}
		DriverWorldRoutine(F, bSnapshots);
		return Done();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverWorldTest, "SatisDash.Core.Driver.World", SatisDash::Test::Flags)

bool FDriverWorldTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	TestEqual(TEXT("no FRM read before enrolling"), F.Net.sent(DriverWorldRailsRead), 0);
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	const double StartedAt = F.T;
	TestTrue(TEXT("rails are read once the agent has run for about 2 minutes"), DriverWorldRun(F, Driver, [&F] { return F.Net.pending(DriverWorldRailsRead) > 0; }, 200));
	const double Waited = F.T - StartedAt;
	TestTrue(TEXT("between 120 s - 10% and 120 s + 10% (plus a tick or two)"), Waited >= 108.0 && Waited <= 140.0);
	F.Net.answer(DriverWorldRailsRead, SatisDash::Test::Reply(200, DriverWorldRails));
	TestTrue(TEXT("the layer is posted"), DriverWorldRun(F, Driver, [&F] { return F.Net.pending(DriverWorldRailsPost) > 0; }, 30));
	TestTrue(TEXT("with the layer's data"), F.Net.lastBody(DriverWorldRailsPost).Contains(TEXT("\"data\":[{\"id\":\"R1\",\"points\":[[0,0],[3,0]]}]")));
	SatisDash::Test::SameText(*this, TEXT("and the credential"), F.Net.lastAuth(DriverWorldRailsPost), TEXT("Bearer ") + SatisDash::Test::FakeSecret());
	F.Net.answer(DriverWorldRailsPost, SatisDash::Test::Reply(200, DriverWorldAccepted));
	DriverWorldRun(F, Driver, []() { return false; }, 20);
	TestEqual(TEXT("one post"), F.Net.sent(DriverWorldRailsPost), 1);
	TestTrue(TEXT("and the agent is still running"), Driver.GetAgent().GetState() == EAgentState::Running);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverWorldHaltTest, "SatisDash.Core.Driver.WorldHalt", SatisDash::Test::Flags)

bool FDriverWorldHaltTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	TestTrue(TEXT("rails are read"), DriverWorldRun(F, Driver, [&F] { return F.Net.pending(DriverWorldRailsRead) > 0; }, 200));
	F.Net.answer(DriverWorldRailsRead, SatisDash::Test::Reply(200, DriverWorldRails));
	TestTrue(TEXT("and posted"), DriverWorldRun(F, Driver, [&F] { return F.Net.pending(DriverWorldRailsPost) > 0; }, 30));
	F.Net.answer(DriverWorldRailsPost, SatisDash::Test::Reply(401, FString()));
	F.run(Driver, 1);
	TestTrue(TEXT("a 401 on a world post halts the agent, as a snapshot 401 does"), Driver.GetAgent().GetState() != EAgentState::Running);
	DriverWorldRun(F, Driver, []() { return false; }, 700);
	TestEqual(TEXT("no more FRM reads"), F.Net.sent(DriverWorldRailsRead), 1);
	TestEqual(TEXT("no more world posts"), F.Net.sent(DriverWorldRailsPost), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverWorldStopsTest, "SatisDash.Core.Driver.WorldStops", SatisDash::Test::Flags)

bool FDriverWorldStopsTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	TestTrue(TEXT("rails are read"), DriverWorldRun(F, Driver, [&F] { return F.Net.pending(DriverWorldRailsRead) > 0; }, 200));

	// The agent is revoked (a snapshot 401) while the rails read is in flight.
	TestTrue(TEXT("a snapshot is open"), DriverWorldRun(F, Driver, [&F] { return F.Net.pending(DriverWorldSnapshots) > 0; }, 20, false));
	F.Net.answer(DriverWorldSnapshots, SatisDash::Test::Reply(401, FString()));
	F.run(Driver, 1);
	TestTrue(TEXT("halted"), Driver.GetAgent().GetState() != EAgentState::Running);
	F.Net.answer(DriverWorldRailsRead, SatisDash::Test::Reply(200, DriverWorldRails)); // the old run's reply, late
	F.run(Driver, 30);
	TestEqual(TEXT("nothing of the old run is posted"), F.Net.sent(DriverWorldRailsPost), 0);

	// Re-enrol: the schedule starts again from the new Running edge.
	F.writeConfig(TEXT("http://127.0.0.1:1"), TEXT("ZZZZ-2222-2222"));
	F.run(Driver, 61);
	F.Net.answer(TEXT("/agent/v1/enroll"), SatisDash::Test::Reply(201, SatisDash::Test::EnrollJson()));
	F.run(Driver, 1);
	TestTrue(TEXT("running again"), Driver.GetAgent().GetState() == EAgentState::Running);
	const double RestartedAt = F.T;
	const int32 Reads = F.Net.sent(DriverWorldRailsRead);
	TestFalse(TEXT("no read in the first 100 s of the new run"), DriverWorldRun(F, Driver, [&F, Reads] { return F.Net.sent(DriverWorldRailsRead) > Reads; }, 100));
	TestTrue(TEXT("then rails are read again, about 2 minutes after the restart"), DriverWorldRun(F, Driver, [&F, Reads] { return F.Net.sent(DriverWorldRailsRead) > Reads; }, 60));
	TestTrue(TEXT("not before 108 s"), F.T - RestartedAt >= 108.0);
	TestEqual(TEXT("and still nothing posted"), F.Net.sent(DriverWorldRailsPost), 0);
	return true;
}
```

- [ ] **Step 2: Run them to see them fail**

Run the mod tests with `--min 142`.
Expected: `Driver.World` fails (no rails read ever happens), `Driver.WorldHalt` and `Driver.WorldStops` fail on their first world assertion.

- [ ] **Step 3: Implement**

`Driver.h`: `#include "SatisDash/WorldSender.h"`; after `FFrmBridge Bridge;` add

```cpp
		/** Bounded: see WorldSender.h. */
		FWorldSender World;
```

and extend the class comment's Memory paragraph with "the world sender as WorldSender.h bounds it".

`Driver.cpp`: in the constructor's initialiser list, after `Bridge{InTransport, InWorker}` add `, World{InTransport, Bridge, Clock.Random01}`. In `Tick`:

- next to `Bridge.Stop();` (leaving Running) add `World.Stop();`;
- in the entering-Running block, after `PollFailures = 0;` add `World.Start(Now);`;
- after the `Bridge.TakeEncoded()` block and before `TickSample(Now);` add:

```cpp
	// World layers (spec "Timing"): posted one at a time, and only while no snapshot is queued, in flight or being encoded.
	if (const FCredential* Credential = Agent.GetCredential())
	{
		World.Tick({ Now, Clock.UtcNow(), &Config, Credential, !bSendInFlight && Pusher.Num() == 0 && !Bridge.EncodePending() });
	}
	if (World.TakeAuthRejected())
	{
		++Generation;
		Agent.OnAuthRejected(); // a 401 on any authenticated call while Running halts the agent (Agent.h)
		return;
	}
```

- [ ] **Step 4: Run the tests**

Expected: `judge-mod-test --min 142` passes. The 700-second run in `WorldHalt` is 1,400 driver ticks: it runs in well under a second.

- [ ] **Step 5: Commit**

```bash
git add mod/SatisDashAgent/Source
git -c core.safecrlf=false commit -m "The driver runs the world sender: started with Running, stopped with it, a 401 halts (FRM bridge 3B Task 9)" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

### Task 10: Evidence for world posts, docs, and the owner's gates for PR 3B-2

**Files:**
- Modify: `harness/src/recorder.ts`, `harness/test/recorder.test.ts`
- Modify: `README.md`
- Modify (**if the owner agrees**): `.github/workflows/mod-test.yml`
- Modify: `api-sources.md` (final check)

**Interfaces:**
- Produces: a recorded call for `POST /agent/v1/world/:layer` carries `world: { layer, items, bytes }` (the unzipped size) and never the layer's data. No judge reads it in 3B; it is evidence for the owner's review of PR 3B-2 and for Plan 3C's `parity` run.

No pass condition changes in this task: no existing judge looks at `world`, so a run's verdict is the same with or without it.

- [ ] **Step 1: Write the failing test**

`harness/test/recorder.test.ts`, after the player-name test (uses the file's `upstream`, `recorderFor`, `send`, `MOD_UA`, `SECRET`, and the `gzipSync` import):

```ts
  it("records a world-layer post as its layer, item count and unzipped size, never its data", async () => {
    const up = await upstream(() => ({ status: 200, body: '{"accepted":true,"unchanged":false}' }));
    const rec = await recorderFor(up.url);
    const body = JSON.stringify({ observedAt: "2026-10-24T12:00:00.000Z", data: [{ id: "R1", points: [[0, 0], [3, 0]] }, { id: "R2", points: [[1, 1], [2, 2]] }] });
    await send(rec.url, "POST", "/agent/v1/world/rails", {
      headers: { "user-agent": MOD_UA, authorization: `Bearer ${SECRET}`, "content-type": "application/json", "content-encoding": "gzip" },
      body: gzipSync(body),
    });
    expect(rec.entries[0]).toMatchObject({ agent: "mod", method: "POST", path: "/agent/v1/world/rails", status: 200, world: { layer: "rails", items: 2, bytes: Buffer.byteLength(body) } });
    expect(JSON.stringify(rec.entries)).not.toContain("R1");
    expect(JSON.stringify(rec.entries)).not.toContain(SECRET);
  });
```

- [ ] **Step 2: Run it to see it fail**

`cd harness; npx vitest run test/recorder.test.ts --reporter=dot`
Expected: fails: the entry has no `world`.

- [ ] **Step 3: Implement**

`harness/src/recorder.ts`: add to `RecordedCall` (after `result?`)

```ts
  /** A world-layer post (POST /agent/v1/world/:layer): which layer, how many items and how many unzipped bytes. The data itself is not kept. */
  world?: { layer: string; items: number; bytes: number };
```

add `const WORLD = /^\/agent\/v1\/world\/([^/]+)$/;` beside `RESULT`, and in `finish`, after the `RESULT` branch of the `else if` chain:

```ts
        } else if (method === "POST" && WORLD.test(path)) {
          // A world body is at most 2,000,000 bytes (map.ts), under the 2 MiB this reads.
          const w = sent.read(req.headers);
          const data = (w?.value as { data?: unknown } | null | undefined)?.data;
          if (w !== undefined && Array.isArray(data)) entry.world = { layer: WORLD.exec(path)![1]!, items: data.length, bytes: w.size };
        }
```

Run the recorder tests again: they pass.

- [ ] **Step 4: Docs and the floor**

`README.md`: where Task 4 added `mapLive`, add that the mod also posts the `rails` and `resourceNodes` map layers to the backend (read from FRM's `getTrainRails` and `getResourceNode`, first about 2 and 8 minutes after start, then every 10 and 30 minutes, sent only when they change). **OWNER DECISION (stricter, not a weakening):** raise the floor the owner chose in Task 4 to the PR's final count:

```diff
-      - run: npm run harness -- test-mod --out "$env:RUNNER_TEMP/mod-test" --min 120
+      - run: npm run harness -- test-mod --out "$env:RUNNER_TEMP/mod-test" --min 142
```

(or from 80 to 142 if Task 4's edit was not made).

- [ ] **Step 5: Run everything for PR 3B-2**

```powershell
cd harness
npx vitest run --reporter=dot
npm run typecheck
npm run harness -- check-api
npm run harness -- verify-contract
```

Expected: all pass; `check-api` ends `0 missing, 0 bad rows`. Then the mod tests with `--min 142`.

- [ ] **Step 6: Commit**

```bash
git add harness README.md api-sources.md
git -c core.safecrlf=false commit -m "World posts recorded as layer, items and size in evidence; docs (FRM bridge 3B Task 10)" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 7: The owner's gates for PR 3B-2**

Before PR 3B-2 merges the owner dispatches `mod-test` on the branch (`gh workflow run mod-test.yml --ref <branch>`) and approves its `ue-runner` deployment, then the core runs `enrol,revoke,command` through `run-backend.ps1` (S13 and the other judges must still pass: the world sender adds no snapshot field). Those runs are not built to last past the 2-minute rails offset, let alone the 8-minute resource-node offset (read the length in a bundle), so they show the sender is harmless, not that it posts. Proof in the game that the layers arrive is Plan 3C's `parity` run ("each world layer once"). If the owner wants it before 3B-2 merges, the way is a long-running variant of an existing run (at least 9 minutes of the mod running against the staged world, reading the recorder's `world` entries); that is the owner's call and a new run, not part of this plan.

---

## Spec coverage

Each requirement of the spec's steps 3 and 4 and where this plan meets it:

| Spec requirement | Task |
| --- | --- |
| `mapLive` from `getTrains` and `getTrainStation`, at the factory cadence, in the snapshot (table "What the Node agent sends") | 3 (`RequestPass`, `TakeMapLive`, driver), 2 (parse) |
| mapLive caps: 500 of each kind, 256,000 serialised bytes, else the part is left out | 2 (`FrmMapLive.Caps`, `.Join`), 3 (`FrmBridge.MapLiveCap`) |
| Mapping ported field by field, conform rules (strings 200, non-finite dropped), Node agent fixtures | 2 (`FrmMap.*`, `FrmMapLive.Rules`, `.Golden`), 4 (golden script) |
| Failures: FRM unreachable, bad body or over its cap: that part left out, snapshot stays `reachable: true`; recovers | 3 (`FrmBridge.MapLive`, `Driver.FrmMapLiveDown`) |
| Threading: parsing on a background task, slices of about 2,000, the game thread never parses; the encode of a big snapshot on the worker | 2 (`FFrmArrayParse`), 3 (`Driver.FrmMapLive`), 7 (`FWorldRead`) |
| No JSON tree: skipped fields never stored, a capped list stops at its cap, a malformed body refused | 2, 6 (the `Rules`/`Caps` tests: skipped extras, 10,000 deep, trailing input) |
| Load spreading: one request in flight, live lane first, large reads wait for the previous large parse, only `getPlayer` passes | 1 (`LargeRule`, `Lanes`), 3, 5 (`WorldEndpoints`), 7 (`FrmBridge.World`) |
| Raw FRM body hashed first; unchanged: no parse, not re-sent | 7 (`FrmWorld.Read`, `.Slices`), 8 (`WorldSender.Send`) |
| A layer is sent only when the hash of its JSON differs from the last accepted send; the first read after start always sends | 7 (`SameData`), 8 (`Send`: restart and empty layer) |
| A layer over 2,000,000 bytes is skipped, the backend keeps its copy | 7 (`OverCap`), 8 (`Settle`) |
| Staggered start: rails 2 min, resource nodes 8 min, with jitter; cadences 10 min and 30 min +/-10%; (conveyors 5 min is 3C) | 7 (`WorldLayer` table), 8 (`Schedule`), 9 (`Driver.World`, `WorldStops`) |
| World sends gzipped, one at a time, behind snapshots, never delaying one | 7 (`EncodeBody`), 8 (`Behind`), 9 |
| Failures: world read fails: skipped cycle; post transient or 429: retry with backoff honouring `Retry-After`; other 4xx: give up until next cycle; 401: halt; agent not running: no FRM reads | 8 (`Retry`, `Refused`, `Halt`, `Schedule`), 9 (`WorldHalt`, `WorldStops`) |
| Caps: rails 5,000 segments of 2 to 1,000 points; resource nodes 5,000 | 6 (`FrmRails.Caps`, `FrmResourceNodes.Rules`) |
| `getResourceNode` runs on the game thread: the answer to a hitch is a longer cadence | 7 (the 30-minute row); measuring is 3C |
| Memory: steady state is each layer's last hash; peak is one large body plus compact output | Global Constraints (hand-written output), 7 (`FWorldRead` frees the raw body at the stage change), 8 (class comment) |
| Security: strings cut to 200, non-finite dropped, no FRM value or token in a log | 2, 6 (`Rules`), 8 (log lines carry names, counts and codes only) |
| Evidence and enrol judge | 4 (S13 ruling, names hashed), 10 (world posts recorded) |
| Unit tests with a fake clock for queue and scheduler | 1, 5, 8, 9 |
| `parity`, `tick-cost` and `frm-down` runs; conveyors | Plan 3C ("Not in 3B") |
