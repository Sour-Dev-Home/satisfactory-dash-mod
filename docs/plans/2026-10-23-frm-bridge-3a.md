# FRM bridge, Plan 3A: the FRM client, players and factory Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The mod reads `players` and `factory` from FRM over loopback HTTP, parses them off the game thread with a token-by-token reader, and sends them in its snapshots exactly as the Node agent does. Nothing it adds may cause a hitch on the host.

**Architecture:** Everything new is pure logic in `SatisDashCore`:
- `FFrmQueue`: every FRM read, two lanes, one request in flight.
- `FTokenReader`: UE's `TJsonReader<UTF8CHAR>` over the reply bytes, so there is no JSON tree.
- `ParsePlayers`, and `FFactoryParse`, which works a slice of buildings at a time.
- `FFrmBridge`: drives the queue and the parsing, and holds the finished parts.
- `IWorker`: a new port that runs pure work off the game thread.

`SatisDashGame` gains `FTaskWorker` (`UE::Tasks`) and a per-request response cap in `FCurlTransport`. The driver asks the bridge for reads on each sample pass, attaches the newest finished parts, and hands a snapshot that carries `factory` to the worker for serialising and gzipping.

**Tech Stack:** Unreal Engine 5.6.1-CSS, SML 3.12.0 (`harness/toolchain.json`), C++20, UE automation tests, and the TypeScript harness (Node, zod 4, Vitest 5).

**Spec:** `docs/specs/2026-10-23-frm-bridge-design.md` (approved 2026-10-23, mod PR #45). This plan covers its "Order of work" steps 1 and 2. The Node agent it matches is satisfactory-dash `agent/` and `packages/game-adapter` at `3bb0638c` (main on 2026-10-23). The contract is re-pinned in Task 1.

## Global Constraints

- Toolchain pinned: engine `5.6.1`, SML `3.12.0` (`harness/toolchain.json`).
- `SatisDashCore` stays pure: `Core`, `Json` and `Projects` only. No FactoryGame, SML or HTTP module, and no Windows-only API. `UE::Tasks` is in `Core`, but only `SatisDashGame` launches tasks (through `IWorker`).
- Test code lives only in `SatisDashCoreTests`. Every test name starts `SatisDash.Core.`, with flags `SatisDash::Test::Flags`.
- **The unity build merges test files:** a file-local name must be unique across the test module. Prefix it with the file's subject (as `DriverTest.cpp` does with `DriverSnapshots`).
- **Protocol strings are compared case-sensitively** (`ESearchCase::CaseSensitive`): FRM field names, `"Unassigned"`, class names.
- **No crash path on outside data:** no `check()`, `ensure()` or `verify()` on anything FRM sends. `TJsonReader::GetValueAsString`, `GetValueAsNumber` and `GetValueAsBoolean` contain a `check()` on the token type (`JsonReader.h:166-208`), so each is called **only** right after `ReadNext` returned that notation.
- Logs go only to `LogSatisDash`, at `Display` or `Warning`, never `Error`, and once per streak (`FRateLimitedLog`). They never carry the FRM token, a player name or any FRM field value, only part names, counts and HTTP codes.
- **The game thread never parses an FRM body** (spec "Threading"). It may only:
  - copy a reply's bytes into a job;
  - write the fixed `{"available":false,"players":[]}`;
  - splice a finished players part into a small snapshot.
- **At most one FRM request in flight, and at most one worker task at a time** (spec "Load spreading").
- **Caps (spec, and the Node agent's conform.ts):**
  - Strings: at most 200 characters (`ClampString`).
  - Counts: 20,000 buildings; 16 rates per list, the first ones kept; 256 players.
  - Response caps: 4 MB for `getPlayer`; 16 MB for `getFactory`.
  - Timeouts: 5 s for `getPlayer` (the Node agent's FRM timeout, `connection.ts:47`); 15 s for `getFactory`.
  - Factory parsing: 2,000 buildings per worker slice.
- `frmPort` defaults to 8080 (FRM `FicsitRemoteMonitoring.cpp:258`, Node `store.ts:29`) and is checked to be 1-65535. `frmToken` is optional. A token with a control character makes the config `Broken` with `BadField` `frmToken`, so it is never sent. The token goes only in `X-FRM-Authorization`.
- Every SML, FactoryGame or engine symbol, include and module used under `mod/` has a row in `api-sources.md`, looked up in the pinned checkout. The rows each task lists were looked up for this plan on 2026-10-23. Anything else `npm run harness -- check-api` reports is looked up and added in the same task. It must end at `0 missing, 0 bad rows`.
- No absolute local paths and no personal information in any committed file. FRM fixtures carry neutral player names only.
- Pass conditions (`runs/*/pass.ts`) are never changed without the owner's ruling (CLAUDE.md rule 3). Task 10 asks for one.

## Review Focus

- **An FRM body that is valid JSON but not what the Node agent accepts:**
  - examples: a building without `IsPaused`; `"Recipe": null`; a rate with `Amount: -0.5`; `1e999`;
  - expected: the whole factory part is left out for that cycle, and so are players for an invalid `getPlayer`, exactly as the Node agent's zod schema refuses the response.
  - Pinned in Task 6 (`SatisDash.Core.FrmFactory.Rules`) and Task 5 (`SatisDash.Core.FrmPlayers.Rules`).
- **A hostile body:**
  - examples: `[[[[…` 10,000 deep, trailing bytes after the array, an empty body, a 16 MB + 1 body;
  - expected: refused without a stack overflow or a crash, and the game unaffected.
  - Pinned in Task 5 (`SatisDash.Core.JsonTokens.Walk`) and Task 2 (`SatisDash.Core.Reply.Limit`).
- **FRM down, or asking for a token:**
  - expected: `players: {available: false, players: []}`, no `factory`, status and power unchanged, and one "set frmToken" line for a 401 or 403, never repeated each pass.
  - Pinned in Task 4 (`SatisDash.Core.FrmQueue.TokenAndCancel`) and Task 8 (`SatisDash.Core.Driver.FrmDown`).
- **The agent leaves Running mid-read** (revoked, config changed):
  - expected: no FRM read is used afterwards, nothing new is queued, and nothing is sent from the old run's parts.
  - Pinned in Task 7 (`SatisDash.Core.FrmBridge.Stop`) and Task 8 (`SatisDash.Core.Driver.FrmStops`).
- **A hand-edited config with `frmPort` and `frmToken`:**
  - examples: `"frmPort": "8080"` (a string), `80.5`, `0`, a token pasted with a trailing newline;
  - expected: a clear `BadField`, and the code-blanking rewrite keeps both fields.
  - Pinned in Task 3 (`SatisDash.Core.Config.Frm`).

## Not in 3A

- **Plan 3B** (written after 3A lands): `mapLive` (`getTrains`, `getTrainStation`), the world sender (`POST /agent/v1/world/:layer`, hashing, backoff, 401 halt) with `rails` and `resourceNodes`, and the world lane's raw-hash skip.
- **Plan 3C:** `conveyors` (`getBelts`, `getLifts`, `getPipes`); the extended `parity` and `tick-cost` runs and the new `frm-down` run; the security review items.
- The world lane exists in `FFrmQueue` from Task 4. 3A only puts live reads on it.

## Owner-facing findings from the lookups for this plan

1. **The enrol run's S13 check will fail once players and factory are sent.**
   - `runs/enrol/pass.ts:37` allows only `agentVersion`, `observedAt`, `reachable`, `paused`, `status`, `power` and `settings`. Every snapshot after this plan also carries `players`, and every 30 s `factory`.
   - Task 10 proposes adding the two keys, which follows the spec's new scope. That's a pass-condition change, so it needs **the owner's ruling before PR 3A-2 merges.**
2. **Evidence would carry player names.**
   - The harness recorder stores each snapshot body in the run's evidence (`harness/src/recorder.ts:136`), hashing only the session name. With `players` in the snapshot, the names of whoever joined the test server would land in public evidence.
   - Task 10 hashes each player's name in the recorder, as it hashes the session name, before any run of this plan's code.
3. **The harness must tell the mod its FRM port.** The test server's FRM listens on the runner config's `frmBaseUrl` port (18081 today), not 8080. Task 10 adds `frmPort` to the config the harness writes.
4. **[NEEDS VERIFICATION] that `TJsonReader<UTF8CHAR>` parses this engine's UTF-8 and numbers as the plan assumes.**
   - The engine's own tests use `TJsonReaderFactory<UTF8CHAR>::CreateFromView` (`Json/Private/Tests/JsonTests.cpp:752`).
   - Task 5's tests pin non-ASCII names, `1e999` and the end-of-input behaviour before anything relies on them.
   - `ReadNext` returns `true` with `EJsonNotation::Error` on bad input, and `false` only at a clean end or after an error (`JsonReader.h:57-152`). `FTokenReader` handles both.

## How the tests run

As in Plan 2A:

- Each task's test step runs locally:
  ```powershell
  pwsh scripts/test-mod.ps1 -Project <sml-project> -Engine <engine-root> -Out <tmp>\mod-test
  cd harness; npm run harness -- judge-mod-test <tmp>\mod-test --min <N>
  ```
  `<sml-project>` holds `FactoryGame.uproject` and `<engine-root>` holds `Engine\`. Both are the executor's own and are never committed. `<N>` is the task's expected test count: 80 today, and each task states its new count.
- The harness tests: `cd harness; npx vitest run <file> --reporter=dot`, then `npx vitest run --reporter=dot` and `npm run typecheck` once at the end of each PR.
- Before each PR merges, the owner dispatches `mod-test` on the branch (`gh workflow run mod-test.yml --ref <branch>` once the queue is empty) and approves its `ue-runner` deployment. PR 3A-2 also gets the core runs `enrol,revoke,command` through `run-backend.ps1`, after the owner's Task 10 ruling.

**PRs:** Tasks 1–4 are **PR 3A-1** (spec step 1); Tasks 5–10 are **PR 3A-2** (spec step 2).

## File structure

| File | New/Modified | Responsibility |
| --- | --- | --- |
| `harness/contract/**` | Modified (re-pin) | The contract at satisfactory-dash main |
| `mod/.../SatisDashCore/Public/SatisDash/Responses.h` | Modified | `MaxRequestResponseBytes` (16 MB) |
| `mod/.../SatisDashCore/Public/SatisDash/Requests.h` | Modified | `FRequestSpec::ResponseCapBytes` |
| `mod/.../SatisDashCore/Public/SatisDash/Reply.h`, `Private/Reply.cpp` | Modified | `FCappedBody::SetLimit` and `Limit` |
| `mod/.../SatisDashGame/Private/CurlTransport.cpp` | Modified | Applies the per-request cap |
| `mod/.../SatisDashCore/Public/SatisDash/Config.h`, `Private/Config.cpp` | Modified | `frmPort` and `frmToken` |
| `mod/.../SatisDashCore/Public/SatisDash/Frm.h`, `Private/Frm.cpp` | New | FRM endpoints and `BuildFrmRequest` |
| `mod/.../SatisDashCore/Public/SatisDash/FrmQueue.h`, `Private/FrmQueue.cpp` | New | Two lanes, one in flight, the token log |
| `mod/.../SatisDashCore/Public/SatisDash/JsonTokens.h`, `Private/JsonTokens.cpp` | New | `FTokenReader` |
| `mod/.../SatisDashCore/Public/SatisDash/FrmPlayers.h`, `Private/FrmPlayers.cpp` | New | `getPlayer` to the players part |
| `mod/.../SatisDashCore/Public/SatisDash/FrmFactory.h`, `Private/FrmFactory.cpp` | New | `getFactory` to the factory part, in slices |
| `mod/.../SatisDashCore/Public/SatisDash/Ports.h` | Modified | `IWorker` |
| `mod/.../SatisDashCore/Public/SatisDash/FrmBridge.h`, `Private/FrmBridge.cpp` | New | Reads, jobs, ready parts, snapshot encoding |
| `mod/.../SatisDashCore/Public/SatisDash/Snapshot.h`, `Private/Snapshot.cpp` | Modified | `PlayersJson` and `FactoryJson` spliced in |
| `mod/.../SatisDashCore/Public/SatisDash/Pusher.h`, `Private/Pusher.cpp` | Modified | `Enqueue(FEncodedBody)` |
| `mod/.../SatisDashCore/Public/SatisDash/Driver.h`, `Private/Driver.cpp` | Modified | The bridge in the sample pass and tick |
| `mod/.../SatisDashGame/Private/TaskWorker.h` | New | `IWorker` over `UE::Tasks` |
| `mod/.../SatisDashGame/Private/SatisDashGameModule.cpp` | Modified | Owns the worker; waits for it at shutdown |
| `mod/.../SatisDashCoreTests/Private/*Test.cpp`, `DriverFakes.h`, `TestUtil.h` | New/Modified | Tests and fakes |
| `mod/.../SatisDashCoreTests/Fixtures/frm-*.json` | New | FRM bodies and the Node agent's output for them |
| `harness/scripts/frm-golden.ts` | New | Writes the Node agent's output for a fixture |
| `harness/src/recorder.ts`, `harness/src/core-run.ts`, `harness/src/mod-config.ts` | Modified | Player names hashed; `frmPort` written |
| `harness/test/*.test.ts` | Modified | Their tests |
| `runs/enrol/pass.ts` | Modified **only after the owner's ruling** | S13 allows `players` and `factory` |
| `api-sources.md`, `README.md` | Modified | Rows; the new config fields |

`mod/...` is `mod/SatisDashAgent/Source`.

---

## PR 3A-1: the FRM client (spec step 1)

### Task 1: Re-pin the contract

**Files:**
- Modify: `harness/contract/manifest.json` and `harness/contract/src/*.ts` (written by the script)

**Interfaces:**
- Produces: the vendored contract at satisfactory-dash main. `map.ts` gains the `conveyors` layer and the `skipped` body, which 3B and 3C need. Later tasks validate fixtures against `SnapshotRequestSchema` from it.

- [ ] **Step 1: Find the commit to pin**

Run: `gh api repos/Sour-Dev-Home/satisfactory-dash/commits/main --jq .sha`
Expected: a 40-hex sha (`3bb0638c5041d1aa12fca667898ee39e28e1d2b2` on 2026-10-23, or newer).

- [ ] **Step 2: Vendor it**

Run: `cd harness; npm run sync-contract -- --commit <sha>`
Expected: `vendored 9 files at <sha7>` (or a different count if the dash added a file).

- [ ] **Step 3: Check what changed**

Run: `git -c core.safecrlf=false diff --stat harness/contract`
Expected: `map.ts` changed, plus `manifest.json`. If `agent.ts` changed too, read its diff:
- satisfactory-dash #621 (optional `settings.pending`) may have landed. An optional field breaks nothing, so continue.
- Anything that removes or tightens a field the mod sends (`snapshots`, `enroll`, `commands`): **stop and report it.**

- [ ] **Step 4: Run the harness checks**

Run: `npx vitest run --reporter=dot` then `npm run typecheck` then `npm run harness -- verify-contract`
Expected: all pass. `verify-contract` prints that the vendored files match the manifest.

- [ ] **Step 5: Commit**

```bash
git add harness/contract
git -c core.safecrlf=false commit -m "Re-pin the contract at satisfactory-dash <sha7> (conveyors layer for the FRM bridge)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 2: A response cap per request

**Files:**
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Responses.h`
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Requests.h`
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Reply.h`
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Private/Reply.cpp`
- Modify: `mod/SatisDashAgent/Source/SatisDashGame/Private/CurlTransport.cpp:156-176, 414-417`
- Test: `mod/SatisDashAgent/Source/SatisDashCoreTests/Private/ReplyTest.cpp`

**Interfaces:**
- Produces:
  - `inline constexpr int64 MaxRequestResponseBytes = 16 * 1024 * 1024;` in `Responses.h`.
  - `int64 FRequestSpec::ResponseCapBytes = MaxResponseBytes;` (64 KB by default).
  - `void FCappedBody::SetLimit(int64 InMaxBytes)`, which clamps to `[0, MaxRequestResponseBytes]`.
  - `int64 FCappedBody::Limit() const`.

- [ ] **Step 1: Write the failing test**

Append to `ReplyTest.cpp` (`feed` is the file's existing helper that serialises N bytes):

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReplyLimitTest, "SatisDash.Core.Reply.Limit", SatisDash::Test::Flags)

bool FReplyLimitTest::RunTest(const FString& Parameters)
{
	{
		FCappedBody CappedBody;
		TestEqual(TEXT("the default is the backend's 64 KB"), CappedBody.Limit(), int64(MaxResponseBytes));
	}
	{
		FCappedBody CappedBody;
		CappedBody.SetLimit(10);
		feed(CappedBody, 10);
		TestFalse(TEXT("exactly the limit is kept"), CappedBody.Overflowed());
		feed(CappedBody, 1);
		TestTrue(TEXT("one byte more overflows"), CappedBody.Overflowed());
		TestTrue(TEXT("and sets the archive error that ends the transfer (S4)"), CappedBody.IsError());
	}
	{
		FCappedBody CappedBody;
		CappedBody.SetLimit(int64(1) << 40);
		TestEqual(TEXT("a limit above 16 MB is held to 16 MB"), CappedBody.Limit(), MaxRequestResponseBytes);
		CappedBody.SetLimit(-5);
		TestEqual(TEXT("a negative limit is 0"), CappedBody.Limit(), int64(0));
	}
	FRequestSpec Spec;
	TestEqual(TEXT("a request's cap defaults to 64 KB"), Spec.ResponseCapBytes, int64(MaxResponseBytes));
	return true;
}
```

- [ ] **Step 2: Run it to see it fail**

Run the mod tests (How the tests run).
Expected: the build fails with `'SetLimit': is not a member of 'SatisDash::FCappedBody'` and `'ResponseCapBytes': is not a member of 'SatisDash::FRequestSpec'`.

- [ ] **Step 3: Implement**

`Responses.h`, after `MaxResponseBytes`:

```cpp
	/** The largest cap any one request may ask for: FRM's large endpoints (FRM bridge spec "Memory"). */
	inline constexpr int64 MaxRequestResponseBytes = 16 * 1024 * 1024;
```

`Requests.h`: add `#include "SatisDash/Responses.h"`, and to `FRequestSpec` after `TimeoutSeconds`:

```cpp
		/** The reply body's cap, enforced while it downloads (S4); the backend's 64 KB unless an FRM read asks for more. */
		int64 ResponseCapBytes = MaxResponseBytes;
```

`Reply.h`, in `FCappedBody`:

```cpp
		FCappedBody();
		/** This request's cap (FRequestSpec::ResponseCapBytes), held to [0, MaxRequestResponseBytes]. Before the first Serialize. */
		void SetLimit(int64 InMaxBytes);
		int64 Limit() const { return MaxBytes; }
```

and the member `int64 MaxBytes = MaxResponseBytes;` before `bool bOverflowed`. Update its comment: "Past its limit it keeps nothing more…".

`Reply.cpp`:

```cpp
void SatisDash::FCappedBody::SetLimit(int64 InMaxBytes)
{
	MaxBytes = InMaxBytes < 0 ? 0 : (InMaxBytes > MaxRequestResponseBytes ? MaxRequestResponseBytes : InMaxBytes);
}
```

and in `Serialize`, replace `Bytes.Num() + Length > MaxResponseBytes` with `Bytes.Num() + Length > MaxBytes`.

`CurlTransport.cpp`, in `Send`, right after `Pending.Url = Spec.Url;`:

```cpp
	Pending.CappedBody.SetLimit(Spec.ResponseCapBytes);
```

and in `Deliver`, replace the oversized line with this, which still logs only the origin (S9):

```cpp
		UE_LOG(LogSatisDash, Warning, TEXT("A response from %s was over %lld bytes; refused it"), *originOf(Pending.Url), Pending.CappedBody.Limit());
```

- [ ] **Step 4: Run the tests**

Expected: `judge-mod-test --min 81` passes.

- [ ] **Step 5: Commit**

```bash
git add mod/SatisDashAgent/Source
git -c core.safecrlf=false commit -m "A response cap per request (FRM bridge Task 2): the backend keeps 64 KB, FRM may ask up to 16 MB" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 3: `frmPort` and `frmToken` in the config

**Files:**
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Config.h`
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Private/Config.cpp:123-162`
- Test: `mod/SatisDashAgent/Source/SatisDashCoreTests/Private/ConfigTest.cpp`
- Modify: `README.md` (the config section)
- Modify: `api-sources.md`

**Interfaces:**
- Produces:
  - `inline constexpr int32 DefaultFrmPort = 8080;`
  - `FAgentConfig::FrmPort` (`TOptional<int32>`, unset = default), `FAgentConfig::FrmToken` (`FString`, empty = none), and `int32 FAgentConfig::GetFrmPort() const`.
  - `bool IsFrmPort(double)` and `bool IsHeaderSafe(const FString&)`.
  - `BadField` values `"frmPort"` and `"frmToken"`.

- [ ] **Step 1: Write the failing test**

Append to `ConfigTest.cpp`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConfigFrmTest, "SatisDash.Core.Config.Frm", SatisDash::Test::Flags)

bool FConfigFrmTest::RunTest(const FString& Parameters)
{
	const FConfigResult Plain = ParseConfig(TEXT("{\"backendUrl\":\"https://api.example.com\"}"));
	TestTrue(TEXT("valid without the FRM fields"), Plain.Status == EConfigStatus::Valid);
	TestEqual(TEXT("FRM's default port"), Plain.Config.GetFrmPort(), 8080);
	TestTrue(TEXT("no token"), Plain.Config.FrmToken.IsEmpty());

	const FConfigResult Set = ParseConfig(TEXT("{\"backendUrl\":\"https://api.example.com\",\"frmPort\":18081,\"frmToken\":\"  tok-123  \"}"));
	TestTrue(TEXT("valid with both"), Set.Status == EConfigStatus::Valid);
	TestEqual(TEXT("the port"), Set.Config.GetFrmPort(), 18081);
	SatisDash::Test::SameText(*this, TEXT("the token, trimmed"), Set.Config.FrmToken, TEXT("tok-123"));

	for (const TCHAR* Bad : { TEXT("0"), TEXT("65536"), TEXT("80.5"), TEXT("\"8080\""), TEXT("true"), TEXT("null"), TEXT("-1") })
	{
		const FConfigResult R = ParseConfig(FString::Printf(TEXT("{\"backendUrl\":\"https://api.example.com\",\"frmPort\":%s}"), Bad));
		TestTrue(FString::Printf(TEXT("frmPort %s is broken"), Bad), R.Status == EConfigStatus::Broken);
		SatisDash::Test::SameText(*this, TEXT("and names frmPort"), R.BadField, TEXT("frmPort"));
	}
	for (const TCHAR* Bad : { TEXT("\"a\\nb\""), TEXT("\"a\\rb\""), TEXT("\"a\\u0007b\""), TEXT("42") })
	{
		const FConfigResult R = ParseConfig(FString::Printf(TEXT("{\"backendUrl\":\"https://api.example.com\",\"frmToken\":%s}"), Bad));
		SatisDash::Test::SameText(*this, TEXT("a bad token names frmToken"), R.BadField, TEXT("frmToken"));
	}
	TestTrue(TEXT("a trailing newline is trimmed, not refused"),
		ParseConfig(TEXT("{\"backendUrl\":\"https://api.example.com\",\"frmToken\":\"tok\\n\"}")).Status == EConfigStatus::Valid);

	SatisDash::Test::FTempDir Dir;
	const FString Path = Dir.File(TEXT("SatisDashAgent.cfg"));
	FAgentConfig Config = Set.Config;
	Config.EnrollmentCode = TEXT("AB3D-7XQ2-K5MN");
	TestTrue(TEXT("blanking writes"), BlankConfigCode(Path, Config));
	const FConfigResult Back = LoadConfigFile(Path);
	TestEqual(TEXT("blanking keeps the port"), Back.Config.GetFrmPort(), 18081);
	SatisDash::Test::SameText(*this, TEXT("and the token"), Back.Config.FrmToken, TEXT("tok-123"));
	TestTrue(TEXT("and blanks the code"), Back.Config.EnrollmentCode.IsEmpty());
	TestFalse(TEXT("a default port isn't written"), ConfigToJson(Plain.Config).Contains(TEXT("frmPort")));
	return true;
}
```

- [ ] **Step 2: Run it to see it fail**

Expected: build error, `'GetFrmPort': is not a member of 'SatisDash::FAgentConfig'`.

- [ ] **Step 3: Implement**

`Config.h`:

```cpp
	/** FRM's own default HTTP port (FicsitRemoteMonitoring.cpp:258) and the Node agent's (store.ts:29). */
	inline constexpr int32 DefaultFrmPort = 8080;

	struct FAgentConfig
	{
		/** Checked by IsAllowedBackendUrl, with no trailing slash. */
		FString BackendUrl;
		/** Empty, or a valid code in upper case. */
		FString EnrollmentCode;
		/** FRM's port on 127.0.0.1 (FRM bridge spec "Configuration"); unset = DefaultFrmPort, and not written back. */
		TOptional<int32> FrmPort;
		/** Sent only as X-FRM-Authorization; empty = none. The host's own FRM secret: never logged. */
		FString FrmToken;

		int32 GetFrmPort() const { return FrmPort.Get(DefaultFrmPort); }
	};
```

Update the comment on `EConfigStatus::Broken` to list `"frmPort"` and `"frmToken"`. Then add:

```cpp
	/** A whole number from 1 to 65535. */
	SATISDASHCORE_API bool IsFrmPort(double Value);
	/** No control character (below 0x20, or 0x7F), so it can't end a header line or start another; at most 512 characters. */
	SATISDASHCORE_API bool IsHeaderSafe(const FString& Value);
```

`Config.cpp`:

```cpp
bool SatisDash::IsFrmPort(double Value)
{
	return FMath::IsFinite(Value) && Value >= 1.0 && Value <= 65535.0 && double(int32(Value)) == Value;
}

bool SatisDash::IsHeaderSafe(const FString& Value)
{
	if (Value.Len() > 512)
	{
		return false;
	}
	for (const TCHAR C : Value)
	{
		if (C < TEXT(' ') || C == 127)
		{
			return false;
		}
	}
	return true;
}
```

In `ParseConfig`, before `Result.Status = EConfigStatus::Valid;`:

```cpp
	TOptional<int32> FrmPort;
	if (Object->HasField(TEXT("frmPort")))
	{
		const TSharedPtr<FJsonValue> PortValue = SatisDash::Json::Field(Object, TEXT("frmPort"), EJson::Number);
		if (!PortValue.IsValid() || !IsFrmPort(PortValue->AsNumber()))
		{
			Result.BadField = TEXT("frmPort");
			return Result;
		}
		FrmPort = int32(PortValue->AsNumber());
	}
	FString FrmToken;
	if (Object->HasField(TEXT("frmToken")))
	{
		const TSharedPtr<FJsonValue> TokenValue = SatisDash::Json::Field(Object, TEXT("frmToken"), EJson::String);
		FrmToken = TokenValue.IsValid() ? TokenValue->AsString().TrimStartAndEnd() : FString();
		if (!TokenValue.IsValid() || !IsHeaderSafe(FrmToken))
		{
			Result.BadField = TEXT("frmToken");
			return Result;
		}
	}
```

After `Result.Config.EnrollmentCode = Code;`:

```cpp
	Result.Config.FrmPort = FrmPort;
	Result.Config.FrmToken = FrmToken;
```

In `ConfigToJson`, after the two existing fields:

```cpp
	if (Config.FrmPort.IsSet())
	{
		Object->SetNumberField(TEXT("frmPort"), Config.FrmPort.GetValue());
	}
	if (!Config.FrmToken.IsEmpty())
	{
		Object->SetStringField(TEXT("frmToken"), Config.FrmToken);
	}
```

`api-sources.md`, after the `TOptional::GetValue` row:

```
| TOptional::Get | Engine/Source/Runtime/Core/Public/Misc/Optional.h:389 | header only (the default-value overload) |
```

`README.md`, in the config section: `frmPort` (FRM's HTTP port on this server, default 8080) and `frmToken` (only if this server's FRM requires one; sent only to FRM on 127.0.0.1). The mod logs "FRM requires a token" when FRM refuses it.

- [ ] **Step 4: Run the tests and check-api**

Expected: `--min 82` passes; `npm run harness -- check-api` prints `0 missing, 0 bad rows`.

- [ ] **Step 5: Commit**

```bash
git add mod/SatisDashAgent/Source api-sources.md README.md
git -c core.safecrlf=false commit -m "Config: frmPort (default 8080) and an optional frmToken (FRM bridge Task 3)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 4: The FRM client and its queue

**Files:**
- Create: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Frm.h`
- Create: `mod/SatisDashAgent/Source/SatisDashCore/Private/Frm.cpp`
- Create: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/FrmQueue.h`
- Create: `mod/SatisDashAgent/Source/SatisDashCore/Private/FrmQueue.cpp`
- Test: `mod/SatisDashAgent/Source/SatisDashCoreTests/Private/FrmQueueTest.cpp`

**Interfaces:**
- Consumes: `FRequestSpec::ResponseCapBytes` (Task 2); `ITransport` (`Ports.h`); `IsAllowedRequestUrl` (`Config.h`).
- Produces:
  ```cpp
  enum class EFrmLane : uint8 { Live, World };
  struct FFrmEndpoint { const TCHAR* Name; EFrmLane Lane; bool bLarge; bool bMayPassLarge; int64 CapBytes; float TimeoutSeconds; };
  inline constexpr FFrmEndpoint FrmGetPlayer;   // getPlayer: Live, small, may pass, 4 MB, 5 s
  inline constexpr FFrmEndpoint FrmGetFactory;  // getFactory: Live, large, 16 MB, 15 s
  FRequestSpec BuildFrmRequest(int32 Port, const FString& Token, const FFrmEndpoint& Endpoint);
  struct FFrmRead { const FFrmEndpoint* Endpoint; FReplyHandler OnReply; };
  class FFrmQueue { explicit FFrmQueue(ITransport&); bool Enqueue(FFrmRead); void Pump(int32 Port, const FString& Token, bool bLargeBusy);
                    void Cancel(); bool InFlight() const; int32 Queued() const; };
  ```

- [ ] **Step 1: Write the failing test**

`FrmQueueTest.cpp`:

```cpp
#include "DriverFakes.h"
#include "SatisDash/Frm.h"
#include "SatisDash/FrmQueue.h"

using namespace SatisDash;

namespace
{
	// A world-lane endpoint for the tests (3B adds the real ones).
	constexpr FFrmEndpoint FrmQueueTestWorld{ TEXT("getTestWorld"), EFrmLane::World, true, false, FrmSmallCapBytes, 5.f };

	FFrmRead FrmQueueRead(const FFrmEndpoint& Endpoint, TArray<int32>* Codes)
	{
		return FFrmRead{ &Endpoint, [Codes](const FHttpReply& Reply) { if (Codes) Codes->Add(Reply.Code); } };
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmRequestTest, "SatisDash.Core.Frm.Request", SatisDash::Test::Flags)

bool FFrmRequestTest::RunTest(const FString& Parameters)
{
	const FRequestSpec Plain = BuildFrmRequest(8080, FString(), FrmGetPlayer);
	SatisDash::Test::SameText(*this, TEXT("GET"), Plain.Verb, TEXT("GET"));
	SatisDash::Test::SameText(*this, TEXT("loopback URL"), Plain.Url, TEXT("http://127.0.0.1:8080/getPlayer"));
	bool bPlain = false;
	TestTrue(TEXT("S1 allows it"), IsAllowedRequestUrl(Plain.Url, bPlain) && bPlain);
	TestEqual(TEXT("no header without a token"), Plain.Headers.Num(), 0);
	TestEqual(TEXT("getPlayer's cap"), Plain.ResponseCapBytes, FrmSmallCapBytes);
	TestEqual(TEXT("getPlayer's timeout"), Plain.TimeoutSeconds, 5.f);
	const FRequestSpec Token = BuildFrmRequest(18081, TEXT("tok"), FrmGetFactory);
	SatisDash::Test::SameText(*this, TEXT("factory URL"), Token.Url, TEXT("http://127.0.0.1:18081/getFactory"));
	TestEqual(TEXT("one header"), Token.Headers.Num(), 1);
	SatisDash::Test::SameText(*this, TEXT("FRM's header name"), Token.Headers[0].Key, TEXT("X-FRM-Authorization"));
	SatisDash::Test::SameText(*this, TEXT("the token"), Token.Headers[0].Value, TEXT("tok"));
	TestEqual(TEXT("getFactory's cap"), Token.ResponseCapBytes, MaxRequestResponseBytes);
	for (const TPair<FString, FString>& Header : Token.Headers)
	{
		TestFalse(TEXT("never the backend's Authorization"), Header.Key.Equals(TEXT("Authorization"), ESearchCase::IgnoreCase));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmQueueLanesTest, "SatisDash.Core.FrmQueue.Lanes", SatisDash::Test::Flags)

bool FFrmQueueLanesTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FFakeTransport Net;
	FFrmQueue Queue{ Net };
	TArray<int32> Codes;
	TestTrue(TEXT("world queued"), Queue.Enqueue(FrmQueueRead(FrmQueueTestWorld, &Codes)));
	TestTrue(TEXT("factory queued"), Queue.Enqueue(FrmQueueRead(FrmGetFactory, &Codes)));
	TestFalse(TEXT("one of each endpoint at a time"), Queue.Enqueue(FrmQueueRead(FrmGetFactory, &Codes)));
	TestTrue(TEXT("players queued"), Queue.Enqueue(FrmQueueRead(FrmGetPlayer, &Codes)));

	Queue.Pump(8080, FString(), false);
	TestEqual(TEXT("the live lane first, in order: factory"), Net.pending(TEXT("/getFactory")), 1);
	Queue.Pump(8080, FString(), false);
	TestEqual(TEXT("one in flight"), Net.Open.Num(), 1);
	TestFalse(TEXT("an endpoint in flight can't be queued again"), Queue.Enqueue(FrmQueueRead(FrmGetFactory, &Codes)));
	Net.answer(TEXT("/getFactory"), SatisDash::Test::Reply(200, TEXT("[]")));
	TestEqual(TEXT("its handler ran"), Codes.Num(), 1);

	// A large body is parsing: only getPlayer may pass it, and the world lane waits.
	Queue.Pump(8080, FString(), true);
	TestEqual(TEXT("players passes the parse"), Net.pending(TEXT("/getPlayer")), 1);
	Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, TEXT("[]")));
	Queue.Pump(8080, FString(), true);
	TestEqual(TEXT("a large world read waits for the parse"), Net.pending(TEXT("/getTestWorld")), 0);
	Queue.Pump(8080, FString(), false);
	TestEqual(TEXT("then goes"), Net.pending(TEXT("/getTestWorld")), 1);
	Queue.Enqueue(FrmQueueRead(FrmGetPlayer, &Codes));
	Net.answer(TEXT("/getTestWorld"), SatisDash::Test::Reply(200, TEXT("[]")));
	Queue.Enqueue(FrmQueueRead(FrmQueueTestWorld, &Codes));
	Queue.Pump(8080, FString(), false);
	TestEqual(TEXT("live before world"), Net.pending(TEXT("/getPlayer")), 1);
	TestEqual(TEXT("world still queued"), Queue.Queued(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmQueueTokenAndCancelTest, "SatisDash.Core.FrmQueue.TokenAndCancel", SatisDash::Test::Flags)

bool FFrmQueueTokenAndCancelTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	SatisDash::Test::FFakeTransport Net;
	FFrmQueue Queue{ Net };
	TArray<int32> Codes;
	for (int32 I = 0; I < 3; ++I)
	{
		Queue.Enqueue(FrmQueueRead(FrmGetPlayer, &Codes));
		Queue.Pump(8080, TEXT("secret-token"), false);
		Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(I == 1 ? 403 : 401, FString()));
	}
	int32 TokenLines = 0;
	for (const FString& Line : Log.Take())
	{
		TokenLines += Line.Contains(TEXT("FRM requires a token: set frmToken in SatisDashAgent.cfg")) ? 1 : 0;
		TestFalse(TEXT("the token is never logged"), Line.Contains(TEXT("secret-token")));
	}
	TestEqual(TEXT("one line for a streak of 401/403"), TokenLines, 1);
	TestEqual(TEXT("the handler still sees each reply"), Codes.Num(), 3);

	Queue.Enqueue(FrmQueueRead(FrmGetPlayer, &Codes));
	Queue.Pump(8080, FString(), false);
	Queue.Enqueue(FrmQueueRead(FrmGetFactory, &Codes));
	Queue.Cancel();
	TestEqual(TEXT("nothing queued after a cancel"), Queue.Queued(), 0);
	Queue.Enqueue(FrmQueueRead(FrmGetFactory, &Codes));
	Queue.Pump(8080, FString(), false);
	TestEqual(TEXT("the cancelled read still counts as in flight"), Net.pending(TEXT("/getFactory")), 0);
	Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, TEXT("[]")));
	TestEqual(TEXT("its reply is dropped"), Codes.Num(), 3);
	Queue.Pump(8080, FString(), false);
	TestEqual(TEXT("then the next read starts"), Net.pending(TEXT("/getFactory")), 1);
	return SatisDash::Test::NoErrorLines(*this, Log);
}
```

- [ ] **Step 2: Run it to see it fail**

Expected: build error, `Cannot open include file: 'SatisDash/Frm.h'`.

- [ ] **Step 3: Implement**

`Frm.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Requests.h"

namespace SatisDash
{
	/** FRM bridge spec "Load spreading": live reads always go first; world reads only when no live read is due. */
	enum class EFrmLane : uint8 { Live, World };

	inline constexpr int64 FrmSmallCapBytes = 4 * 1024 * 1024;

	/** One FRM endpoint and how it is read. */
	struct FFrmEndpoint
	{
		const TCHAR* Name;
		EFrmLane Lane;
		/** A large body: parsed in slices, and at most one held at a time (spec "Memory"). */
		bool bLarge;
		/** May start while a large body is being parsed (only getPlayer, spec "Load spreading"). */
		bool bMayPassLarge;
		int64 CapBytes;
		float TimeoutSeconds;
	};

	// FRM serves getPlayer on the game thread and getFactory on its own server thread (FicsitRemoteMonitoring.cpp:839, :896).
	inline constexpr FFrmEndpoint FrmGetPlayer{ TEXT("getPlayer"), EFrmLane::Live, false, true, FrmSmallCapBytes, 5.f };
	inline constexpr FFrmEndpoint FrmGetFactory{ TEXT("getFactory"), EFrmLane::Live, true, false, MaxRequestResponseBytes, 15.f };

	/** GET http://127.0.0.1:<Port>/<Endpoint.Name>, with X-FRM-Authorization only when Token isn't empty. */
	SATISDASHCORE_API FRequestSpec BuildFrmRequest(int32 Port, const FString& Token, const FFrmEndpoint& Endpoint);
}
```

`Frm.cpp`:

```cpp
#include "SatisDash/Frm.h"

SatisDash::FRequestSpec SatisDash::BuildFrmRequest(int32 Port, const FString& Token, const FFrmEndpoint& Endpoint)
{
	FRequestSpec Spec;
	Spec.Verb = TEXT("GET");
	Spec.Url = FString::Printf(TEXT("http://127.0.0.1:%d/%s"), Port, Endpoint.Name);
	if (!Token.IsEmpty())
	{
		Spec.Headers.Add({ TEXT("X-FRM-Authorization"), Token });
	}
	Spec.TimeoutSeconds = Endpoint.TimeoutSeconds;
	Spec.ResponseCapBytes = Endpoint.CapBytes;
	return Spec;
}
```

`FrmQueue.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Frm.h"
#include "SatisDash/Ports.h"

namespace SatisDash
{
	/** One read to make. OnReply runs once, later, on the game thread, unless Cancel came first. */
	struct FFrmRead
	{
		const FFrmEndpoint* Endpoint = nullptr;
		FReplyHandler OnReply;
	};

	/**
	 * Every FRM read (FRM bridge spec "Load spreading"): at most one request in flight, because FRM answers on one
	 * thread and a second request would only wait there while holding another body here. The live lane goes first. While
	 * a large body is parsing (bLargeBusy), only a read marked bMayPassLarge (getPlayer) may start. One read of each
	 * endpoint at a time. Bounded: at most one queued read per endpoint.
	 */
	class SATISDASHCORE_API FFrmQueue
	{
	public:
		explicit FFrmQueue(ITransport& InTransport);
		FFrmQueue(const FFrmQueue&) = delete;
		FFrmQueue& operator=(const FFrmQueue&) = delete;

		/** False when a read of that endpoint is already queued or in flight. */
		bool Enqueue(FFrmRead Read);
		/** Starts the next allowed read if none is in flight. */
		void Pump(int32 Port, const FString& Token, bool bLargeBusy);
		/** Drops every queued read. A read in flight stays counted until its reply, which is then dropped. */
		void Cancel();
		bool InFlight() const { return bInFlight; }
		int32 Queued() const { return Live.Num() + World.Num(); }

	private:
		bool Has(const FFrmEndpoint* Endpoint) const;
		void NoteCode(int32 Code);

		ITransport& Transport;
		TArray<FFrmRead> Live;
		TArray<FFrmRead> World;
		const FFrmEndpoint* Current = nullptr;
		bool bInFlight = false;
		uint32 Generation = 0;
		/** The "set frmToken" line was written in this streak of 401/403 answers. */
		bool bTokenLogged = false;
	};
}
```

`FrmQueue.cpp`:

```cpp
#include "SatisDash/FrmQueue.h"

#include "SatisDash/Log.h"

SatisDash::FFrmQueue::FFrmQueue(ITransport& InTransport)
	: Transport{InTransport}
{
}

bool SatisDash::FFrmQueue::Has(const FFrmEndpoint* Endpoint) const
{
	if (bInFlight && Current == Endpoint)
	{
		return true;
	}
	for (const FFrmRead& Each : Live)
	{
		if (Each.Endpoint == Endpoint)
		{
			return true;
		}
	}
	for (const FFrmRead& Each : World)
	{
		if (Each.Endpoint == Endpoint)
		{
			return true;
		}
	}
	return false;
}

bool SatisDash::FFrmQueue::Enqueue(FFrmRead Read)
{
	if (Read.Endpoint == nullptr || Has(Read.Endpoint))
	{
		return false;
	}
	(Read.Endpoint->Lane == EFrmLane::Live ? Live : World).Add(MoveTemp(Read));
	return true;
}

void SatisDash::FFrmQueue::NoteCode(int32 Code)
{
	if (Code == 401 || Code == 403)
	{
		if (!bTokenLogged)
		{
			UE_LOG(LogSatisDash, Warning, TEXT("FRM requires a token: set frmToken in SatisDashAgent.cfg"));
			bTokenLogged = true;
		}
	}
	else if (Code >= 200 && Code < 300)
	{
		bTokenLogged = false;
	}
}

void SatisDash::FFrmQueue::Pump(int32 Port, const FString& Token, bool bLargeBusy)
{
	if (bInFlight)
	{
		return;
	}
	for (TArray<FFrmRead>* Lane : { &Live, &World })
	{
		for (int32 I = 0; I < Lane->Num(); ++I)
		{
			if (bLargeBusy && !(*Lane)[I].Endpoint->bMayPassLarge)
			{
				continue;
			}
			FFrmRead Read = MoveTemp((*Lane)[I]);
			Lane->RemoveAt(I);
			Current = Read.Endpoint;
			bInFlight = true;
			const uint32 SentIn = Generation;
			Transport.Send(BuildFrmRequest(Port, Token, *Read.Endpoint), [this, SentIn, OnReply = Read.OnReply](const FHttpReply& Reply)
			{
				bInFlight = false;
				Current = nullptr;
				if (SentIn != Generation)
				{
					return; // cancelled (the agent left Running): this reply belongs to nobody
				}
				NoteCode(Reply.Code);
				OnReply(Reply);
			});
			return;
		}
	}
}

void SatisDash::FFrmQueue::Cancel()
{
	Live.Reset();
	World.Reset();
	++Generation;
}
```

- [ ] **Step 4: Run the tests and check-api**

Expected: `--min 85` passes; `check-api` prints `0 missing, 0 bad rows`.

- [ ] **Step 5: Commit**

```bash
git add mod/SatisDashAgent/Source
git -c core.safecrlf=false commit -m "FRM client and queue: two lanes, one request in flight, getPlayer passes a large parse (FRM bridge Task 4)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 6: Open PR 3A-1**

Run the full harness suite, typecheck and check-api, then push the branch and open a PR to `main` titled "FRM bridge 3A-1: contract re-pin, per-request cap, config, FRM client and queue". Ask the owner to dispatch `mod-test` on it. Nothing in 3A-1 is wired into the driver yet, so the core runs are unchanged.

---

## PR 3A-2: players and factory (spec step 2)

### Task 5: The token reader, and players

**Files:**
- Create: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/JsonTokens.h`
- Create: `mod/SatisDashAgent/Source/SatisDashCore/Private/JsonTokens.cpp`
- Create: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/FrmPlayers.h`
- Create: `mod/SatisDashAgent/Source/SatisDashCore/Private/FrmPlayers.cpp`
- Create: `mod/SatisDashAgent/Source/SatisDashCoreTests/Fixtures/frm-getPlayer.json` and `frm-getPlayer.expected.json`
- Create: `harness/scripts/frm-golden.ts`
- Modify: `mod/SatisDashAgent/Source/SatisDashCoreTests/Private/TestUtil.h` (`SameJson` and `Fixture` move here from `SnapshotTest.cpp`)
- Modify: `mod/SatisDashAgent/Source/SatisDashCoreTests/Private/SnapshotTest.cpp` (uses the moved helpers)
- Test: `mod/SatisDashAgent/Source/SatisDashCoreTests/Private/FrmPlayersTest.cpp`
- Modify: `api-sources.md`

**Interfaces:**
- Produces:
  ```cpp
  class FTokenReader { explicit FTokenReader(const TArray<uint8>& Bytes); bool Next(EJsonNotation& Out); bool SkipValue(EJsonNotation Started);
                       const FString& Name() const; FString String() const; double Number() const; bool Boolean() const;
                       bool Failed() const; bool AtCleanEnd(); };
  bool IsNonNegativeNoise(double Value);   // finite and >= -0.001 (rawSchemas.ts NEGATIVE_NOISE)
  struct FPlayerReading { FString Name; bool bOnline = false; };
  struct FPlayersPart { bool bAvailable = false; TArray<FPlayerReading> Players; };
  inline constexpr int32 MaxPlayers = 256;
  TOptional<FPlayersPart> ParsePlayers(const TArray<uint8>& Body);
  FPlayersPart PlayersUnavailable();
  FString PlayersToJson(const FPlayersPart& Part);
  bool SameJson(const FString& A, const FString& B);  // TestUtil.h, objects or arrays
  FString Fixture(const TCHAR* Name);                  // TestUtil.h
  ```

- [ ] **Step 1: Add the golden script**

`harness/scripts/frm-golden.ts` runs the Node agent's own mapping and conform code on a fixture, so the expected output is machine-written (CLAUDE.md rule 4), never typed by hand:

```ts
// Writes what satisfactory-dash's Node agent sends for one FRM body (FRM bridge Plan 3A, Tasks 5-6). It imports the
// agent's own code from a satisfactory-dash checkout with node_modules installed, so the expected output is the Node
// agent's, byte for byte in meaning. Usage:
//   npx tsx scripts/frm-golden.ts --dash <satisfactory-dash checkout> --kind players|factory --in <body.json> --out <expected.json>
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
const body = JSON.parse(readFileSync(arg("in"), "utf8"));
const adapterModule = await import(pathToFileURL(`${dash}/packages/game-adapter/src/index.ts`).href);
const conform = await import(pathToFileURL(`${dash}/agent/src/conform.ts`).href);
const adapter = new adapterModule.SatisfactoryServerAdapter(
  { call: async () => { throw new Error("no vanilla API in a golden"); } },
  { get: async () => body },
);
const out =
  kind === "factory"
    ? conform.conformFactory({ buildings: (await adapter.getFactoryBuildings()).map(adapterModule.mapFactoryBuilding) }).value
    : conform.conformPlayers(await adapterModule.readPlayers(() => adapter.getPlayers())).value;
writeFileSync(arg("out"), `${JSON.stringify(out, null, 2)}\n`);
console.log(`wrote ${kind} for ${arg("in")}`);
```

- [ ] **Step 2: Make the players fixture and its expected output**

1. Copy the JSON after the `---` line of satisfactory-dash `docs-vault/raw-sources/captured-responses/frm-getPlayer-2026-09-27-full.json` into `Fixtures/frm-getPlayer.json`.
2. Replace `"REDACTED-PLAYER-NAME"` with `"Player One"`, and `"Char_Player_C_REDACTED"` with `"Char_Player_C_1"`.
3. Append a second element `{"ID":"Char_Player_C_2","Name":"Zoë Ünïcode","Online":true,"Extra":{"nested":[1,2,{"x":null}]}}`. It's a non-ASCII name and an unknown nested field to skip.
4. Keep the first element's `location`, `Inventory` and the other fields: they are what the reader must skip.

Run: `cd harness; npx tsx scripts/frm-golden.ts --dash <satisfactory-dash checkout> --kind players --in ../mod/SatisDashAgent/Source/SatisDashCoreTests/Fixtures/frm-getPlayer.json --out ../mod/SatisDashAgent/Source/SatisDashCoreTests/Fixtures/frm-getPlayer.expected.json`
Expected: `wrote players for …`, and a file holding `{"available": true, "players": [{"name": "Player One", "online": false}, {"name": "Zoë Ünïcode", "online": true}]}`.

- [ ] **Step 3: Write the failing tests**

First move `SameJson` and `Fixture` from `SnapshotTest.cpp`'s anonymous namespace into `TestUtil.h`'s `SatisDash::Test`, with `SameJson` generalised to arrays:

```cpp
	/** Same JSON value, compared as data: field order and 5 vs 5.0 don't matter. Objects or arrays. */
	inline bool SameJson(const FString& A, const FString& B)
	{
		const TSharedPtr<FJsonObject> Left = SatisDash::Json::ParseObject(TEXT("{\"v\":") + A + TEXT("}"));
		const TSharedPtr<FJsonObject> Right = SatisDash::Json::ParseObject(TEXT("{\"v\":") + B + TEXT("}"));
		return Left.IsValid() && Right.IsValid() && FJsonValue::CompareEqual(FJsonValueObject(Left), FJsonValueObject(Right));
	}

	/** A file in Source/SatisDashCoreTests/Fixtures, as text ("" when missing). */
	inline FString Fixture(const TCHAR* Name)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SatisDashAgent"));
		FString Text;
		if (Plugin.IsValid())
		{
			FFileHelper::LoadFileToString(Text, *(Plugin->GetBaseDir() / TEXT("Source/SatisDashCoreTests/Fixtures") / Name));
		}
		return Text;
	}

	inline TArray<uint8> Utf8(const FString& Text)
	{
		const FTCHARToUTF8 Converted(*Text);
		return TArray<uint8>(reinterpret_cast<const uint8*>(Converted.Get()), Converted.Length());
	}
```

`TestUtil.h` gains `#include "Interfaces/IPluginManager.h"`, `#include "Dom/JsonValue.h"` and `#include "SatisDash/JsonUtil.h"`. `SnapshotTest.cpp` calls `SatisDash::Test::Fixture` and `SatisDash::Test::SameJson`. Its `Fixture` and `SameJson` are deleted, along with its now unused includes.

`FrmPlayersTest.cpp`:

```cpp
#include "SatisDash/FrmPlayers.h"
#include "SatisDash/JsonTokens.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	TOptional<FPlayersPart> FrmPlayersOf(const FString& Json) { return ParsePlayers(SatisDash::Test::Utf8(Json)); }

	/** Walks every token; true when the body reads cleanly to its end. */
	bool JsonTokensWalk(const FString& Json)
	{
		const TArray<uint8> Bytes = SatisDash::Test::Utf8(Json);
		FTokenReader Reader{ Bytes };
		EJsonNotation N;
		while (Reader.Next(N))
		{
		}
		return !Reader.Failed();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJsonTokensWalkTest, "SatisDash.Core.JsonTokens.Walk", SatisDash::Test::Flags)

bool FJsonTokensWalkTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("a body"), JsonTokensWalk(TEXT("[{\"a\":[1,2,{\"b\":null}],\"c\":\"d\"}]")));
	TestFalse(TEXT("empty input"), JsonTokensWalk(FString()));
	TestFalse(TEXT("trailing input"), JsonTokensWalk(TEXT("[] []")));
	TestFalse(TEXT("unclosed"), JsonTokensWalk(TEXT("[{\"a\":1}")));
	TestFalse(TEXT("deeper than 32 is refused before any recursion"), JsonTokensWalk(FString::ChrN(10000, TEXT('[')) + FString::ChrN(10000, TEXT(']'))));
	TestTrue(TEXT("32 deep is fine"), JsonTokensWalk(FString::ChrN(32, TEXT('[')) + FString::ChrN(32, TEXT(']'))));

	{
		// [NEEDS VERIFICATION pinned here] the reader either refuses 1e999 or reads it as infinite; both refuse the body
		// downstream, as zod refuses Infinity.
		const TArray<uint8> Huge = SatisDash::Test::Utf8(TEXT("[1e999]"));
		FTokenReader HugeReader{ Huge };
		EJsonNotation H;
		HugeReader.Next(H);
		const bool bNumber = HugeReader.Next(H) && H == EJsonNotation::Number;
		TestTrue(TEXT("1e999 is refused or not finite"), !bNumber || !FMath::IsFinite(HugeReader.Number()));
	}
	const TArray<uint8> Bytes = SatisDash::Test::Utf8(TEXT("[\"Zoë\", true]"));
	FTokenReader Reader{ Bytes };
	EJsonNotation N;
	TestTrue(TEXT("array"), Reader.Next(N) && N == EJsonNotation::ArrayStart);
	TestTrue(TEXT("a string"), Reader.Next(N) && N == EJsonNotation::String);
	SatisDash::Test::SameText(*this, TEXT("UTF-8 is decoded"), Reader.String(), TEXT("Zoë"));
	TestTrue(TEXT("a boolean"), Reader.Next(N) && N == EJsonNotation::Boolean && Reader.Boolean());
	TestTrue(TEXT("the end"), Reader.Next(N) && N == EJsonNotation::ArrayEnd);
	TestTrue(TEXT("and nothing after it"), Reader.AtCleanEnd());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmPlayersGoldenTest, "SatisDash.Core.FrmPlayers.Golden", SatisDash::Test::Flags)

bool FFrmPlayersGoldenTest::RunTest(const FString& Parameters)
{
	const TOptional<FPlayersPart> Part = FrmPlayersOf(SatisDash::Test::Fixture(TEXT("frm-getPlayer.json")));
	if (!TestTrue(TEXT("the capture parses"), Part.IsSet()))
	{
		return true;
	}
	const FString Expected = SatisDash::Test::Fixture(TEXT("frm-getPlayer.expected.json"));
	const FString Actual = PlayersToJson(Part.GetValue());
	TestTrue(FString::Printf(TEXT("the Node agent's output; got %s"), *Actual), SatisDash::Test::SameJson(Actual, Expected));
	TestFalse(TEXT("nothing but name and online"), Actual.Contains(TEXT("Inventory")) || Actual.Contains(TEXT("location")) || Actual.Contains(TEXT("Char_Player")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmPlayersRulesTest, "SatisDash.Core.FrmPlayers.Rules", SatisDash::Test::Flags)

bool FFrmPlayersRulesTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("no players is valid"), FrmPlayersOf(TEXT("[]")).IsSet());
	for (const TCHAR* Bad : {
		TEXT("{}"), TEXT("[1]"), TEXT("[{\"Name\":\"a\"}]"), TEXT("[{\"Online\":true}]"), TEXT("[{\"Name\":null,\"Online\":true}]"),
		TEXT("[{\"Name\":\"a\",\"Online\":\"yes\"}]"), TEXT("[{\"Name\":5,\"Online\":true}]"), TEXT("[{\"Name\":\"a\",\"Online\":true}] x") })
	{
		TestFalse(FString::Printf(TEXT("refused like zod: %s"), Bad), FrmPlayersOf(Bad).IsSet());
	}
	FString Many = TEXT("[");
	for (int32 I = 0; I < 300; ++I)
	{
		Many += FString::Printf(TEXT("%s{\"Name\":\"%s\",\"Online\":true}"), I ? TEXT(",") : TEXT(""), *FString::ChrN(250, TEXT('n')));
	}
	Many += TEXT("]");
	const TOptional<FPlayersPart> Capped = FrmPlayersOf(Many);
	TestTrue(TEXT("valid"), Capped.IsSet());
	TestEqual(TEXT("at most 256 kept"), Capped->Players.Num(), 256);
	TestEqual(TEXT("names cut to 200"), Capped->Players[0].Name.Len(), 200);
	TestTrue(TEXT("last key wins, as JSON.parse"), FrmPlayersOf(TEXT("[{\"Name\":\"a\",\"Name\":\"b\",\"Online\":false}]"))->Players[0].Name == TEXT("b"));
	SatisDash::Test::SameText(*this, TEXT("FRM absent"), PlayersToJson(PlayersUnavailable()), TEXT("{\"available\":false,\"players\":[]}"));
	return true;
}
```

- [ ] **Step 4: Run them to see them fail**

Expected: build error, `Cannot open include file: 'SatisDash/FrmPlayers.h'`.

- [ ] **Step 5: Implement the token reader**

`JsonTokens.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Serialization/JsonReader.h"

namespace SatisDash
{
	/**
	 * FRM bridge spec "Memory": a body read token by token, in place, with no FJsonValue tree. UTF-8 bytes go straight
	 * to TJsonReader<UTF8CHAR> (CreateFromView, no copy). Nesting deeper than Json::MaxDepth (32) is refused as it is
	 * met. The reader's GetValueAs* check() the token type (JsonReader.h:166-208): call String, Number or Boolean only
	 * right after Next returned that notation.
	 */
	class SATISDASHCORE_API FTokenReader
	{
	public:
		/** Bytes must outlive the reader, which reads them in place. */
		explicit FTokenReader(const TArray<uint8>& Bytes);

		/** The next notation; false at the end of input or on a refusal (then Failed()). */
		bool Next(EJsonNotation& Out);
		/** After Next returned Started: skips that value. An object or array is read to its end; a scalar needs nothing. */
		bool SkipValue(EJsonNotation Started);
		/** The current value's name inside an object. */
		const FString& Name() const { return Reader->GetIdentifier(); }
		FString String() const { return Reader->GetValueAsString(); }
		double Number() const { return Reader->GetValueAsNumber(); }
		bool Boolean() const { return Reader->GetValueAsBoolean(); }
		bool Failed() const { return bFailed; }
		/** True when the input ends cleanly here (after the root value). */
		bool AtCleanEnd();

	private:
		TSharedRef<TJsonReader<UTF8CHAR>> Reader;
		int32 Level = 0;
		bool bFailed = false;
	};

	/** rawSchemas.ts nonNegative: a finite number no lower than -0.001 (float noise, then raised to 0). */
	SATISDASHCORE_API bool IsNonNegativeNoise(double Value);
}
```

`JsonTokens.cpp`:

```cpp
#include "SatisDash/JsonTokens.h"

#include "SatisDash/JsonUtil.h"

SatisDash::FTokenReader::FTokenReader(const TArray<uint8>& Bytes)
	: Reader{ TJsonReaderFactory<UTF8CHAR>::CreateFromView(TStringView<UTF8CHAR>(reinterpret_cast<const UTF8CHAR*>(Bytes.GetData()), Bytes.Num())) }
{
}

bool SatisDash::FTokenReader::Next(EJsonNotation& Out)
{
	if (bFailed)
	{
		return false;
	}
	// ReadNext: true with Error on bad input; false at a clean end, or after an error (JsonReader.h:57-152).
	if (!Reader->ReadNext(Out))
	{
		bFailed = !Reader->GetErrorMessage().IsEmpty();
		return false;
	}
	if (Out == EJsonNotation::Error)
	{
		bFailed = true;
		return false;
	}
	if (Out == EJsonNotation::ObjectStart || Out == EJsonNotation::ArrayStart)
	{
		if (++Level > SatisDash::Json::MaxDepth)
		{
			bFailed = true;
			return false;
		}
	}
	else if (Out == EJsonNotation::ObjectEnd || Out == EJsonNotation::ArrayEnd)
	{
		--Level;
	}
	return true;
}

bool SatisDash::FTokenReader::SkipValue(EJsonNotation Started)
{
	if (Started != EJsonNotation::ObjectStart && Started != EJsonNotation::ArrayStart)
	{
		return true;
	}
	// Our own walk, not TJsonReader::SkipObject: the depth bound holds inside skipped values too.
	const int32 Until = Level - 1;
	EJsonNotation N;
	while (Level > Until)
	{
		if (!Next(N))
		{
			return false;
		}
	}
	return true;
}

bool SatisDash::FTokenReader::AtCleanEnd()
{
	EJsonNotation N;
	return !Next(N) && !bFailed;
}

bool SatisDash::IsNonNegativeNoise(double Value)
{
	return FMath::IsFinite(Value) && Value >= -0.001;
}
```

- [ ] **Step 6: Implement players**

`FrmPlayers.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

namespace SatisDash
{
	inline constexpr int32 MaxPlayers = 256;

	/** ADR-0029: a name and whether the player is online. Nothing else is read. */
	struct FPlayerReading
	{
		FString Name;
		bool bOnline = false;
	};

	/** AgentPlayersSchema. */
	struct FPlayersPart
	{
		bool bAvailable = false;
		TArray<FPlayerReading> Players;
	};

	/**
	 * getPlayer's 2xx body, as the Node agent reads it (rawSchemas.ts RawFrmPlayerSchema, conform.ts conformPlayers): an
	 * array of objects, each with a string Name and a boolean Online; every other field is skipped unread. Unset when the
	 * body is invalid (the Node agent's invalid_response: the part is left out). The first 256 are kept, names cut to 200.
	 */
	SATISDASHCORE_API TOptional<FPlayersPart> ParsePlayers(const TArray<uint8>& Body);
	/** FRM unreachable, timed out, or an HTTP error (playersMapper.ts isFrmAbsent): {available: false, players: []}. */
	SATISDASHCORE_API FPlayersPart PlayersUnavailable();
	SATISDASHCORE_API FString PlayersToJson(const FPlayersPart& Part);
}
```

`FrmPlayers.cpp`:

```cpp
#include "SatisDash/FrmPlayers.h"

#include "Policies/CondensedJsonPrintPolicy.h"
#include "SatisDash/JsonTokens.h"
#include "SatisDash/Snapshot.h"
#include "Serialization/JsonWriter.h"

TOptional<SatisDash::FPlayersPart> SatisDash::ParsePlayers(const TArray<uint8>& Body)
{
	FTokenReader Reader{ Body };
	FPlayersPart Part;
	Part.bAvailable = true;
	EJsonNotation N;
	if (!Reader.Next(N) || N != EJsonNotation::ArrayStart)
	{
		return {};
	}
	while (true)
	{
		if (!Reader.Next(N))
		{
			return {};
		}
		if (N == EJsonNotation::ArrayEnd)
		{
			break;
		}
		if (N != EJsonNotation::ObjectStart)
		{
			return {};
		}
		TOptional<FString> Name;
		TOptional<bool> bOnline;
		while (true)
		{
			if (!Reader.Next(N))
			{
				return {};
			}
			if (N == EJsonNotation::ObjectEnd)
			{
				break;
			}
			const FString& Key = Reader.Name();
			if (Key.Equals(TEXT("Name"), ESearchCase::CaseSensitive))
			{
				if (N != EJsonNotation::String)
				{
					return {};
				}
				Name = Reader.String();
			}
			else if (Key.Equals(TEXT("Online"), ESearchCase::CaseSensitive))
			{
				if (N != EJsonNotation::Boolean)
				{
					return {};
				}
				bOnline = Reader.Boolean();
			}
			else if (!Reader.SkipValue(N))
			{
				return {};
			}
		}
		if (!Name.IsSet() || !bOnline.IsSet())
		{
			return {};
		}
		if (Part.Players.Num() < MaxPlayers)
		{
			Part.Players.Add({ ClampString(Name.GetValue()), bOnline.GetValue() });
		}
	}
	if (!Reader.AtCleanEnd())
	{
		return {};
	}
	return Part;
}

SatisDash::FPlayersPart SatisDash::PlayersUnavailable()
{
	return FPlayersPart();
}

FString SatisDash::PlayersToJson(const FPlayersPart& Part)
{
	FString Out;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("available"), Part.bAvailable);
	Writer->WriteArrayStart(TEXT("players"));
	for (const FPlayerReading& Player : Part.Players)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("name"), Player.Name);
		Writer->WriteValue(TEXT("online"), Player.bOnline);
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();
	Writer->WriteObjectEnd();
	Writer->Close();
	return Out;
}
```

- [ ] **Step 7: Add the rows**

Add to `api-sources.md`, each looked up on 2026-10-23:

```
| TJsonReaderFactory::CreateFromView | Engine/Source/Runtime/Json/Public/Serialization/JsonReader.h:1085 | header only (TJsonStringViewReader, :1020; the engine's own UTF8CHAR use is Json/Private/Tests/JsonTests.cpp:752) |
| TJsonReader::ReadNext | Engine/Source/Runtime/Json/Public/Serialization/JsonReader.h:57 | header only (true with EJsonNotation::Error on bad input; false at a clean end or after an error, :59-91) |
| TJsonReader::GetIdentifier | Engine/Source/Runtime/Json/Public/Serialization/JsonReader.h:164 | header only |
| TJsonReader::GetValueAsString | Engine/Source/Runtime/Json/Public/Serialization/JsonReader.h:166 | header only (check()s the token: called only after a String notation) |
| TJsonReader::GetValueAsNumber | Engine/Source/Runtime/Json/Public/Serialization/JsonReader.h:192 | header only (called only after a Number notation) |
| TJsonReader::GetValueAsBoolean | Engine/Source/Runtime/Json/Public/Serialization/JsonReader.h:204 | header only (called only after a Boolean notation) |
| TJsonReader::GetErrorMessage | Engine/Source/Runtime/Json/Public/Serialization/JsonReader.h:210 | header only |
| EJsonNotation | Engine/Source/Runtime/Json/Public/Serialization/JsonTypes.h:59 | header only |
| EJsonNotation::ObjectStart | Engine/Source/Runtime/Json/Public/Serialization/JsonTypes.h:61 | header only |
| EJsonNotation::ObjectEnd | Engine/Source/Runtime/Json/Public/Serialization/JsonTypes.h:62 | header only |
| EJsonNotation::ArrayStart | Engine/Source/Runtime/Json/Public/Serialization/JsonTypes.h:63 | header only |
| EJsonNotation::ArrayEnd | Engine/Source/Runtime/Json/Public/Serialization/JsonTypes.h:64 | header only |
| EJsonNotation::Boolean | Engine/Source/Runtime/Json/Public/Serialization/JsonTypes.h:65 | header only |
| EJsonNotation::String | Engine/Source/Runtime/Json/Public/Serialization/JsonTypes.h:66 | header only |
| EJsonNotation::Number | Engine/Source/Runtime/Json/Public/Serialization/JsonTypes.h:67 | header only |
| EJsonNotation::Null | Engine/Source/Runtime/Json/Public/Serialization/JsonTypes.h:68 | header only |
| EJsonNotation::Error | Engine/Source/Runtime/Json/Public/Serialization/JsonTypes.h:69 | header only |
| TStringView | Engine/Source/Runtime/Core/Public/Containers/StringView.h:93 | header only (the pointer-and-size constructor, :143) |
| UTF8CHAR | Engine/Source/Runtime/Core/Public/HAL/Platform.h:1087 | header only |
| TJsonWriterFactory::Create | Engine/Source/Runtime/Json/Public/Serialization/JsonWriter.h:697 | header only (the StringType* overload) |
| TJsonWriter::WriteObjectStart | Engine/Source/Runtime/Json/Public/Serialization/JsonWriter.h:109 | header only (the named overload is :127) |
| TJsonWriter::WriteObjectEnd | Engine/Source/Runtime/Json/Public/Serialization/JsonWriter.h:140 | header only |
| TJsonWriter::WriteArrayStart | Engine/Source/Runtime/Json/Public/Serialization/JsonWriter.h:153 | header only (the named overload is :171) |
| TJsonWriter::WriteArrayEnd | Engine/Source/Runtime/Json/Public/Serialization/JsonWriter.h:183 | header only |
| TJsonWriter::WriteValue | Engine/Source/Runtime/Json/Public/Serialization/JsonWriter.h:243 | header only (named; doubles with 17 significant digits, :438-443) |
| TJsonWriter::WriteNull | Engine/Source/Runtime/Json/Public/Serialization/JsonWriter.h:305 | header only (the named overload) |
| TJsonWriter::Close | Engine/Source/Runtime/Json/Public/Serialization/JsonWriter.h:334 | header only |
```

Then run `npm run harness -- check-api` and add any further row it names, looked up the same way.

- [ ] **Step 8: Run the tests**

Expected: `--min 88` passes (three new tests; `SnapshotTest`'s tests unchanged); `check-api` clean.

- [ ] **Step 9: Commit**

```bash
git add mod/SatisDashAgent/Source harness/scripts/frm-golden.ts api-sources.md
git -c core.safecrlf=false commit -m "Token reader (no JSON tree, depth-bounded) and players from getPlayer, against the Node agent's output (FRM bridge Task 5)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 6: The factory, a slice at a time

**Files:**
- Create: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/FrmFactory.h`
- Create: `mod/SatisDashAgent/Source/SatisDashCore/Private/FrmFactory.cpp`
- Create: `mod/SatisDashAgent/Source/SatisDashCoreTests/Fixtures/frm-getFactory-running.json` and `.expected.json`
- Create: `mod/SatisDashAgent/Source/SatisDashCoreTests/Fixtures/frm-getFactory-norecipe.json` and `.expected.json`
- Test: `mod/SatisDashAgent/Source/SatisDashCoreTests/Private/FrmFactoryTest.cpp`
- Modify: `api-sources.md`

**Interfaces:**
- Consumes: `FTokenReader` and `IsNonNegativeNoise` (Task 5); `ClampString` (`Snapshot.h`).
- Produces:
  ```cpp
  inline constexpr int32 MaxBuildings = 20000; inline constexpr int32 MaxRatesPerBuilding = 16; inline constexpr int32 FactorySliceBuildings = 2000;
  class FFactoryParse { explicit FFactoryParse(TArray<uint8> InBody); enum class EStep : uint8 { More, Done, Invalid };
                        EStep Step(int32 MaxPerSlice = FactorySliceBuildings); FString TakeJson();
                        int32 Buildings() const; int32 Dropped() const; };
  ```

- [ ] **Step 1: Make the fixtures and their expected output**

1. Copy the JSON after the `---` line of satisfactory-dash `docs-vault/raw-sources/captured-responses/frm-getFactory-2026-09-22-01-running-trimmed.json` into `Fixtures/frm-getFactory-running.json`.
2. Copy `frm-getFactory-2026-09-22-CJ-norecipe-fuse-trimmed.json` the same way into `Fixtures/frm-getFactory-norecipe.json`.

They are machine ids, names and numbers, with no personal data.

Run, once per fixture:
`cd harness; npx tsx scripts/frm-golden.ts --dash <satisfactory-dash checkout> --kind factory --in ../mod/SatisDashAgent/Source/SatisDashCoreTests/Fixtures/frm-getFactory-running.json --out ../mod/SatisDashAgent/Source/SatisDashCoreTests/Fixtures/frm-getFactory-running.expected.json`
Expected: `wrote factory for …`. The running fixture gives 7 buildings, the first an Assembler with `"recipe": "Reinforced Iron Plate"`, `"isBackedUp": true` and `"inputsFull": true`.

- [ ] **Step 2: Write the failing tests**

`FrmFactoryTest.cpp`:

```cpp
#include "SatisDash/FrmFactory.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	/** Runs a parse to its end; unset when it was invalid. */
	TOptional<FString> FrmFactoryOf(const FString& Json, int32 Slice = FactorySliceBuildings, int32* Steps = nullptr)
	{
		FFactoryParse Parse{ SatisDash::Test::Utf8(Json) };
		FFactoryParse::EStep Step = FFactoryParse::EStep::More;
		int32 Count = 0;
		while (Step == FFactoryParse::EStep::More)
		{
			Step = Parse.Step(Slice);
			++Count;
		}
		if (Steps)
		{
			*Steps = Count;
		}
		return Step == FFactoryParse::EStep::Done ? TOptional<FString>(Parse.TakeJson()) : TOptional<FString>();
	}

	/** One building; Over replaces or adds fields (a JSON fragment such as "\"IsPaused\":true"). */
	FString FrmFactoryOne(const TCHAR* Over)
	{
		return FString::Printf(TEXT("[{\"ID\":\"B_1\",\"Name\":\"Smelter\",\"ClassName\":\"Build_SmelterMk1_C\",\"Recipe\":\"Iron Ingot\",")
			TEXT("\"IsConfigured\":true,\"IsProducing\":true,\"IsPaused\":false%s%s}]"), *Over ? TEXT(",") : TEXT(""), Over);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmFactoryGoldenTest, "SatisDash.Core.FrmFactory.Golden", SatisDash::Test::Flags)

bool FFrmFactoryGoldenTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* Name : { TEXT("frm-getFactory-running"), TEXT("frm-getFactory-norecipe") })
	{
		const TOptional<FString> Actual = FrmFactoryOf(SatisDash::Test::Fixture(*(FString(Name) + TEXT(".json"))));
		if (!TestTrue(FString::Printf(TEXT("%s parses"), Name), Actual.IsSet()))
		{
			continue;
		}
		const FString Expected = SatisDash::Test::Fixture(*(FString(Name) + TEXT(".expected.json")));
		TestTrue(FString::Printf(TEXT("%s is the Node agent's output; got %s"), Name, *Actual.GetValue()), SatisDash::Test::SameJson(Actual.GetValue(), Expected));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmFactoryRulesTest, "SatisDash.Core.FrmFactory.Rules", SatisDash::Test::Flags)

bool FFrmFactoryRulesTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("no buildings is valid"), FrmFactoryOf(TEXT("[]")).IsSet());
	TestTrue(TEXT("a minimal building is valid"), FrmFactoryOf(FrmFactoryOne(TEXT(""))).IsSet());
	// Each refused as the Node agent's zod schema refuses it, so the whole part is left out.
	for (const TCHAR* Bad : {
		TEXT("\"ID\":\"\""), TEXT("\"Recipe\":null"), TEXT("\"Recipe\":\"\""), TEXT("\"IsPaused\":\"no\""),
		TEXT("\"production\":[{\"Name\":\"a\",\"ClassName\":\"c\",\"Amount\":-0.5,\"CurrentProd\":1,\"MaxProd\":1,\"ProdPercent\":1}]"),
		TEXT("\"production\":[{\"Name\":\"a\",\"ClassName\":\"c\",\"Amount\":1,\"CurrentProd\":1e999,\"MaxProd\":1,\"ProdPercent\":1}]"),
		TEXT("\"production\":[{\"Name\":\"a\",\"Amount\":1,\"CurrentProd\":1,\"MaxProd\":1,\"ProdPercent\":1}]"),
		TEXT("\"PowerInfo\":{\"CircuitGroupID\":1.5,\"CircuitID\":1,\"PowerConsumed\":1,\"MaxPowerConsumed\":1}"),
		TEXT("\"location\":{\"x\":1,\"y\":2}"), TEXT("\"OutputInventory\":null") })
	{
		TestFalse(FString::Printf(TEXT("refused: %s"), Bad), FrmFactoryOf(FrmFactoryOne(Bad)).IsSet());
	}
	TestFalse(TEXT("a building without IsPaused"), FrmFactoryOf(TEXT("[{\"ID\":\"B\",\"Name\":\"n\",\"ClassName\":\"c\",\"IsProducing\":true}]")).IsSet());
	TestFalse(TEXT("trailing input"), FrmFactoryOf(TEXT("[] 1")).IsSet());

	// ManuSpeed is tolerant (.catch(undefined)): an odd value is dropped, never a failure.
	TestFalse(TEXT("ManuSpeed as text is dropped"), FrmFactoryOf(FrmFactoryOne(TEXT("\"ManuSpeed\":\"fast\"")))->Contains(TEXT("clockSpeedPercent")));
	TestFalse(TEXT("ManuSpeed 0 is dropped"), FrmFactoryOf(FrmFactoryOne(TEXT("\"ManuSpeed\":0")))->Contains(TEXT("clockSpeedPercent")));
	TestTrue(TEXT("ManuSpeed 160 is kept"), FrmFactoryOf(FrmFactoryOne(TEXT("\"ManuSpeed\":160")))->Contains(TEXT("\"clockSpeedPercent\":160")));

	const FString Unassigned = FrmFactoryOf(FrmFactoryOne(TEXT("\"Recipe\":\"Unassigned\",\"production\":[{\"Name\":\"Unassigned\",\"ClassName\":\"x\",\"Amount\":0,\"CurrentProd\":0,\"MaxProd\":0,\"ProdPercent\":0}]"))).Get(FString());
	TestTrue(TEXT("Unassigned is no recipe"), Unassigned.Contains(TEXT("\"recipe\":null")));
	TestTrue(TEXT("and no production"), Unassigned.Contains(TEXT("\"production\":[]")));
	TestTrue(TEXT("no PowerInfo is group -1"), Unassigned.Contains(TEXT("\"circuitGroupId\":-1")));
	TestTrue(TEXT("float noise is raised to 0"), FrmFactoryOf(FrmFactoryOne(TEXT("\"production\":[{\"Name\":\"a\",\"ClassName\":\"c\",\"Amount\":1,\"CurrentProd\":-0.0005,\"MaxProd\":1,\"ProdPercent\":1}]")))->Contains(TEXT("\"currentPerMinute\":0")));
	TestTrue(TEXT("a yaw is normalised and centimetres become metres"),
		FrmFactoryOf(FrmFactoryOne(TEXT("\"location\":{\"x\":150,\"y\":-200,\"z\":0,\"rotation\":-90,\"pitch\":3}")))->Contains(TEXT("\"location\":{\"xM\":1.5,\"yM\":-2,\"zM\":0,\"rotationDeg\":270}")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmFactorySlicesTest, "SatisDash.Core.FrmFactory.Slices", SatisDash::Test::Flags)

bool FFrmFactorySlicesTest::RunTest(const FString& Parameters)
{
	FString Many = TEXT("[");
	for (int32 I = 0; I < 20005; ++I)
	{
		Many += FString::Printf(TEXT("%s{\"ID\":\"B_%d\",\"Name\":\"n\",\"ClassName\":\"c\",\"IsProducing\":false,\"IsPaused\":false}"), I ? TEXT(",") : TEXT(""), I);
	}
	Many += TEXT("]");
	int32 Steps = 0;
	FFactoryParse Parse{ SatisDash::Test::Utf8(Many) };
	FFactoryParse::EStep Step = FFactoryParse::EStep::More;
	while (Step == FFactoryParse::EStep::More)
	{
		Step = Parse.Step(2000);
		++Steps;
	}
	TestTrue(TEXT("done"), Step == FFactoryParse::EStep::Done);
	TestEqual(TEXT("2,000 buildings a slice: 11 slices"), Steps, 11);
	TestEqual(TEXT("20,000 kept"), Parse.Buildings(), 20000);
	TestEqual(TEXT("5 dropped past the cap"), Parse.Dropped(), 5);
	TestTrue(TEXT("a bad building past the cap still refuses the body, as zod does"),
		!FrmFactoryOf(Many.LeftChop(1) + TEXT(",{\"ID\":\"\"}]")).IsSet());
	const FString Json = Parse.TakeJson();
	TestTrue(TEXT("valid JSON"), SatisDash::Json::ParseObject(Json).IsValid());

	FString Rates = TEXT("[{\"ID\":\"B\",\"Name\":\"n\",\"ClassName\":\"c\",\"Recipe\":\"r\",\"IsProducing\":true,\"IsPaused\":false,\"production\":[");
	for (int32 I = 0; I < 20; ++I)
	{
		Rates += FString::Printf(TEXT("%s{\"Name\":\"i%d\",\"ClassName\":\"c\",\"Amount\":1,\"CurrentProd\":1,\"MaxProd\":1,\"ProdPercent\":1}"), I ? TEXT(",") : TEXT(""), I);
	}
	Rates += TEXT("]}]");
	const TSharedPtr<FJsonObject> Out = SatisDash::Json::ParseObject(FrmFactoryOf(Rates).Get(FString()));
	TestEqual(TEXT("at most 16 rates, the first kept"), Out.IsValid() ? Out->GetArrayField(TEXT("buildings"))[0]->AsObject()->GetArrayField(TEXT("production")).Num() : -1, 16);
	return true;
}
```

- [ ] **Step 3: Run them to see them fail**

Expected: build error, `Cannot open include file: 'SatisDash/FrmFactory.h'`.

- [ ] **Step 4: Implement**

`FrmFactory.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "SatisDash/JsonTokens.h"
#include "Serialization/JsonWriter.h"

namespace SatisDash
{
	inline constexpr int32 MaxBuildings = 20000;
	inline constexpr int32 MaxRatesPerBuilding = 16;
	/** FRM bridge spec "Threading": about 2,000 records per worker slice. */
	inline constexpr int32 FactorySliceBuildings = 2000;

	/**
	 * getFactory's body to the snapshot's factory part, {"buildings":[...]}, as the Node agent makes it:
	 *  - rawSchemas.ts:98-124 (what is valid; one invalid building refuses the whole body);
	 *  - satisfactoryServerAdapter.ts:173-209 (the mapping);
	 *  - factoryMapper.ts (isBackedUp, inputsFull);
	 *  - conform.ts:88-118 (caps, cuts, yaw).
	 * Pure, and touched by one thread at a time (FFrmBridge hands it to the worker a slice at a time). Not copyable or
	 * movable: the writer holds a pointer to Out, and the reader a view of Body.
	 */
	class SATISDASHCORE_API FFactoryParse
	{
	public:
		explicit FFactoryParse(TArray<uint8> InBody);
		FFactoryParse(const FFactoryParse&) = delete;
		FFactoryParse& operator=(const FFactoryParse&) = delete;

		enum class EStep : uint8 { More, Done, Invalid };
		/** Reads at most MaxPerSlice more buildings. After Done or Invalid it does nothing more. */
		EStep Step(int32 MaxPerSlice = FactorySliceBuildings);
		/** After Done: the part's JSON. Empties it. */
		FString TakeJson();
		int32 Buildings() const { return Count; }
		/** Valid buildings past MaxBuildings, read and checked but not sent. */
		int32 Dropped() const { return Over; }

	private:
		struct FRate { FString Name; FString ClassName; double Current = 0; double Max = 0; double Percent = 0; };
		struct FSlot { FString ClassName; double Amount = 0; double MaxAmount = 0; };
		struct FBuilding
		{
			TOptional<FString> Id;
			TOptional<FString> Name;
			TOptional<FString> ClassName;
			TOptional<FString> Recipe;
			TOptional<bool> IsConfigured;
			TOptional<bool> IsProducing;
			TOptional<bool> IsPaused;
			TArray<FRate> Production;
			TArray<FRate> Ingredients;
			TArray<FSlot> Output;
			TOptional<TArray<FSlot>> Input;
			TOptional<int64> CircuitGroupId;
			bool bHasLocation = false;
			double X = 0, Y = 0, Z = 0, Rotation = 0;
			TOptional<double> ManuSpeed;
		};

		EStep Fail();
		bool ReadBuilding(FBuilding& Out);
		bool ReadRates(EJsonNotation Started, bool bIngredients, TArray<FRate>& Out);
		bool ReadSlots(EJsonNotation Started, TArray<FSlot>& Out);
		bool ReadPowerInfo(EJsonNotation Started, TOptional<int64>& OutGroup);
		bool ReadLocation(EJsonNotation Started, FBuilding& Out);
		bool ReadNonNegative(EJsonNotation N, double& Out) const;
		void WriteBuilding(const FBuilding& B);
		void WriteRates(const TCHAR* Field, const TArray<FRate>& Rates);

		TArray<uint8> Body;
		FTokenReader Reader;
		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer;
		EStep State = EStep::More;
		bool bStarted = false;
		int32 Count = 0;
		int32 Over = 0;
	};
}
```

`FrmFactory.cpp`:

```cpp
#include "SatisDash/FrmFactory.h"

#include "SatisDash/Snapshot.h"

namespace
{
	bool FrmFactoryIs(const FString& Key, const TCHAR* Name)
	{
		return Key.Equals(Name, ESearchCase::CaseSensitive);
	}

	/** zod's .int(): a whole, safe integer. */
	bool FrmFactoryWhole(double Value)
	{
		return FMath::IsFinite(Value) && Value >= -9007199254740991.0 && Value <= 9007199254740991.0 && double(int64(Value)) == Value;
	}

	/** conform.ts normalizeYaw: into [0, 360); float noise that lands on 360 is 0. JavaScript's % is fmod. */
	double FrmFactoryYaw(double Degrees)
	{
		const double Yaw = FMath::Fmod(FMath::Fmod(Degrees, 360.0) + 360.0, 360.0);
		return Yaw >= 360.0 ? 0.0 : Yaw;
	}
}

SatisDash::FFactoryParse::FFactoryParse(TArray<uint8> InBody)
	: Body{MoveTemp(InBody)}
	, Reader{Body}
	, Writer{TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out)}
{
}

SatisDash::FFactoryParse::EStep SatisDash::FFactoryParse::Fail()
{
	State = EStep::Invalid;
	Out.Empty();
	return State;
}

SatisDash::FFactoryParse::EStep SatisDash::FFactoryParse::Step(int32 MaxPerSlice)
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
		Writer->WriteObjectStart();
		Writer->WriteArrayStart(TEXT("buildings"));
	}
	for (int32 Done = 0; Done < MaxPerSlice; ++Done)
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
			Writer->WriteArrayEnd();
			Writer->WriteObjectEnd();
			Writer->Close();
			State = EStep::Done;
			return State;
		}
		FBuilding B;
		if (N != EJsonNotation::ObjectStart || !ReadBuilding(B))
		{
			return Fail();
		}
		if (Count < MaxBuildings)
		{
			WriteBuilding(B);
			++Count;
		}
		else
		{
			++Over; // still read and checked: zod validates the whole array before conform cuts it
		}
	}
	return EStep::More;
}

FString SatisDash::FFactoryParse::TakeJson()
{
	return State == EStep::Done ? MoveTemp(Out) : FString();
}

bool SatisDash::FFactoryParse::ReadNonNegative(EJsonNotation N, double& OutValue) const
{
	if (N != EJsonNotation::Number || !IsNonNegativeNoise(Reader.Number()))
	{
		return false;
	}
	OutValue = FMath::Max(0.0, Reader.Number());
	return true;
}

bool SatisDash::FFactoryParse::ReadBuilding(FBuilding& B)
{
	EJsonNotation N;
	while (Reader.Next(N))
	{
		if (N == EJsonNotation::ObjectEnd)
		{
			return B.Id.IsSet() && !B.Id->IsEmpty() && B.Name.IsSet() && B.ClassName.IsSet() && B.IsProducing.IsSet() && B.IsPaused.IsSet();
		}
		const FString& Key = Reader.Name();
		if (FrmFactoryIs(Key, TEXT("ID")) || FrmFactoryIs(Key, TEXT("Name")) || FrmFactoryIs(Key, TEXT("ClassName")) || FrmFactoryIs(Key, TEXT("Recipe")))
		{
			if (N != EJsonNotation::String)
			{
				return false;
			}
			TOptional<FString>& Field = FrmFactoryIs(Key, TEXT("ID")) ? B.Id : FrmFactoryIs(Key, TEXT("Name")) ? B.Name : FrmFactoryIs(Key, TEXT("ClassName")) ? B.ClassName : B.Recipe;
			Field = Reader.String();
			if (&Field == &B.Recipe && B.Recipe->IsEmpty())
			{
				return false; // Recipe: z.string().min(1)
			}
		}
		else if (FrmFactoryIs(Key, TEXT("IsConfigured")) || FrmFactoryIs(Key, TEXT("IsProducing")) || FrmFactoryIs(Key, TEXT("IsPaused")))
		{
			if (N != EJsonNotation::Boolean)
			{
				return false;
			}
			(FrmFactoryIs(Key, TEXT("IsConfigured")) ? B.IsConfigured : FrmFactoryIs(Key, TEXT("IsProducing")) ? B.IsProducing : B.IsPaused) = Reader.Boolean();
		}
		else if (FrmFactoryIs(Key, TEXT("production")) || FrmFactoryIs(Key, TEXT("ingredients")))
		{
			const bool bIngredients = FrmFactoryIs(Key, TEXT("ingredients"));
			if (!ReadRates(N, bIngredients, bIngredients ? B.Ingredients : B.Production))
			{
				return false;
			}
		}
		else if (FrmFactoryIs(Key, TEXT("OutputInventory")))
		{
			if (!ReadSlots(N, B.Output))
			{
				return false;
			}
		}
		else if (FrmFactoryIs(Key, TEXT("InputInventory")))
		{
			TArray<FSlot> Slots;
			if (!ReadSlots(N, Slots))
			{
				return false;
			}
			B.Input = MoveTemp(Slots);
		}
		else if (FrmFactoryIs(Key, TEXT("PowerInfo")))
		{
			if (!ReadPowerInfo(N, B.CircuitGroupId))
			{
				return false;
			}
		}
		else if (FrmFactoryIs(Key, TEXT("location")))
		{
			if (!ReadLocation(N, B))
			{
				return false;
			}
		}
		else if (FrmFactoryIs(Key, TEXT("ManuSpeed")))
		{
			// z.number().optional().catch(undefined): anything but a finite number is dropped, never a failure.
			B.ManuSpeed.Reset();
			if (N == EJsonNotation::Number && FMath::IsFinite(Reader.Number()))
			{
				B.ManuSpeed = Reader.Number();
			}
			else if (!Reader.SkipValue(N))
			{
				return false;
			}
		}
		else if (!Reader.SkipValue(N))
		{
			return false;
		}
	}
	return false;
}

bool SatisDash::FFactoryParse::ReadRates(EJsonNotation Started, bool bIngredients, TArray<FRate>& Rates)
{
	if (Started != EJsonNotation::ArrayStart)
	{
		return false;
	}
	Rates.Reset(); // a repeated key: the last one wins, as JSON.parse
	const TCHAR* CurrentKey = bIngredients ? TEXT("CurrentConsumed") : TEXT("CurrentProd");
	const TCHAR* MaxKey = bIngredients ? TEXT("MaxConsumed") : TEXT("MaxProd");
	const TCHAR* PercentKey = bIngredients ? TEXT("ConsPercent") : TEXT("ProdPercent");
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
		FRate Rate;
		uint8 Seen = 0;
		while (true)
		{
			if (!Reader.Next(N))
			{
				return false;
			}
			if (N == EJsonNotation::ObjectEnd)
			{
				break;
			}
			const FString& Key = Reader.Name();
			double Amount = 0;
			if (FrmFactoryIs(Key, TEXT("Name")) || FrmFactoryIs(Key, TEXT("ClassName")))
			{
				if (N != EJsonNotation::String)
				{
					return false;
				}
				(FrmFactoryIs(Key, TEXT("Name")) ? Rate.Name : Rate.ClassName) = Reader.String();
				Seen |= FrmFactoryIs(Key, TEXT("Name")) ? 1 : 2;
			}
			else if (FrmFactoryIs(Key, TEXT("Amount")))
			{
				if (!ReadNonNegative(N, Amount))
				{
					return false;
				}
				Seen |= 4;
			}
			else if (FrmFactoryIs(Key, CurrentKey) || FrmFactoryIs(Key, MaxKey) || FrmFactoryIs(Key, PercentKey))
			{
				double& Field = FrmFactoryIs(Key, CurrentKey) ? Rate.Current : FrmFactoryIs(Key, MaxKey) ? Rate.Max : Rate.Percent;
				if (!ReadNonNegative(N, Field))
				{
					return false;
				}
				Seen |= FrmFactoryIs(Key, CurrentKey) ? 8 : FrmFactoryIs(Key, MaxKey) ? 16 : 32;
			}
			else if (!Reader.SkipValue(N))
			{
				return false;
			}
		}
		if (Seen != 63)
		{
			return false; // every field of RawFrmProductionItemSchema / RawFrmIngredientItemSchema is required
		}
		Rates.Add(MoveTemp(Rate));
	}
	return false;
}

bool SatisDash::FFactoryParse::ReadSlots(EJsonNotation Started, TArray<FSlot>& Slots)
{
	if (Started != EJsonNotation::ArrayStart)
	{
		return false;
	}
	Slots.Reset();
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
		FSlot Slot;
		uint8 Seen = 0;
		while (true)
		{
			if (!Reader.Next(N))
			{
				return false;
			}
			if (N == EJsonNotation::ObjectEnd)
			{
				break;
			}
			const FString& Key = Reader.Name();
			if (FrmFactoryIs(Key, TEXT("Name")) || FrmFactoryIs(Key, TEXT("ClassName")))
			{
				if (N != EJsonNotation::String)
				{
					return false;
				}
				if (FrmFactoryIs(Key, TEXT("ClassName")))
				{
					Slot.ClassName = Reader.String();
				}
				Seen |= FrmFactoryIs(Key, TEXT("Name")) ? 1 : 2;
			}
			else if (FrmFactoryIs(Key, TEXT("Amount")) || FrmFactoryIs(Key, TEXT("MaxAmount")))
			{
				if (!ReadNonNegative(N, FrmFactoryIs(Key, TEXT("Amount")) ? Slot.Amount : Slot.MaxAmount))
				{
					return false;
				}
				Seen |= FrmFactoryIs(Key, TEXT("Amount")) ? 4 : 8;
			}
			else if (!Reader.SkipValue(N))
			{
				return false;
			}
		}
		if (Seen != 15)
		{
			return false;
		}
		Slots.Add(MoveTemp(Slot));
	}
	return false;
}

bool SatisDash::FFactoryParse::ReadPowerInfo(EJsonNotation Started, TOptional<int64>& OutGroup)
{
	if (Started != EJsonNotation::ObjectStart)
	{
		return false;
	}
	uint8 Seen = 0;
	int64 Group = -1;
	EJsonNotation N;
	while (Reader.Next(N))
	{
		if (N == EJsonNotation::ObjectEnd)
		{
			if (Seen != 15)
			{
				return false; // CircuitGroupID, CircuitID, PowerConsumed and MaxPowerConsumed are required
			}
			OutGroup = Group;
			return true;
		}
		const FString& Key = Reader.Name();
		if (FrmFactoryIs(Key, TEXT("CircuitGroupID")) || FrmFactoryIs(Key, TEXT("CircuitID")))
		{
			if (N != EJsonNotation::Number || !FrmFactoryWhole(Reader.Number()))
			{
				return false;
			}
			if (FrmFactoryIs(Key, TEXT("CircuitGroupID")))
			{
				Group = int64(Reader.Number());
			}
			Seen |= FrmFactoryIs(Key, TEXT("CircuitGroupID")) ? 1 : 2;
		}
		else if (FrmFactoryIs(Key, TEXT("PowerConsumed")) || FrmFactoryIs(Key, TEXT("MaxPowerConsumed")))
		{
			if (N != EJsonNotation::Number || !FMath::IsFinite(Reader.Number()))
			{
				return false;
			}
			Seen |= FrmFactoryIs(Key, TEXT("PowerConsumed")) ? 4 : 8;
		}
		else if (FrmFactoryIs(Key, TEXT("FuseTriggered")))
		{
			if (N != EJsonNotation::Boolean)
			{
				return false;
			}
		}
		else if (!Reader.SkipValue(N))
		{
			return false;
		}
	}
	return false;
}

bool SatisDash::FFactoryParse::ReadLocation(EJsonNotation Started, FBuilding& B)
{
	if (Started != EJsonNotation::ObjectStart)
	{
		return false;
	}
	uint8 Seen = 0;
	B.Rotation = 0;
	EJsonNotation N;
	while (Reader.Next(N))
	{
		if (N == EJsonNotation::ObjectEnd)
		{
			B.bHasLocation = Seen == 7;
			return B.bHasLocation; // x, y and z are required; rotation is optional
		}
		const FString& Key = Reader.Name();
		const bool bAxis = FrmFactoryIs(Key, TEXT("x")) || FrmFactoryIs(Key, TEXT("y")) || FrmFactoryIs(Key, TEXT("z"));
		if (bAxis || FrmFactoryIs(Key, TEXT("rotation")))
		{
			if (N != EJsonNotation::Number || !FMath::IsFinite(Reader.Number()))
			{
				return false;
			}
			double& Field = FrmFactoryIs(Key, TEXT("x")) ? B.X : FrmFactoryIs(Key, TEXT("y")) ? B.Y : FrmFactoryIs(Key, TEXT("z")) ? B.Z : B.Rotation;
			Field = Reader.Number();
			Seen |= FrmFactoryIs(Key, TEXT("x")) ? 1 : FrmFactoryIs(Key, TEXT("y")) ? 2 : FrmFactoryIs(Key, TEXT("z")) ? 4 : 0;
		}
		else if (!Reader.SkipValue(N))
		{
			return false;
		}
	}
	return false;
}

void SatisDash::FFactoryParse::WriteRates(const TCHAR* Field, const TArray<FRate>& Rates)
{
	Writer->WriteArrayStart(Field);
	const int32 Keep = FMath::Min(Rates.Num(), MaxRatesPerBuilding);
	for (int32 I = 0; I < Keep; ++I)
	{
		const FRate& Rate = Rates[I];
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("name"), ClampString(Rate.Name));
		Writer->WriteValue(TEXT("className"), ClampString(Rate.ClassName));
		Writer->WriteValue(TEXT("currentPerMinute"), Rate.Current);
		Writer->WriteValue(TEXT("maxPerMinute"), Rate.Max);
		Writer->WriteValue(TEXT("percent"), Rate.Percent);
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();
}

void SatisDash::FFactoryParse::WriteBuilding(const FBuilding& B)
{
	// satisfactoryServerAdapter.ts:182-183: "Unassigned" or a missing recipe, or IsConfigured false, is no recipe.
	const bool bConfigured = B.IsConfigured.Get(true) && B.Recipe.IsSet() && !B.Recipe->Equals(TEXT("Unassigned"), ESearchCase::CaseSensitive);
	static const TArray<FRate> NoRates;
	const TArray<FRate>& Production = bConfigured ? B.Production : NoRates;
	const TArray<FRate>& Ingredients = bConfigured ? B.Ingredients : NoRates;
	const bool bPaused = B.IsPaused.GetValue();

	// factoryMapper.ts isBackedUp and areInputsFull, on the whole lists (they run before conform cuts them).
	bool bBackedUp = false;
	if (!bPaused && bConfigured)
	{
		for (const FSlot& Slot : B.Output)
		{
			bBackedUp = bBackedUp || (Slot.MaxAmount > 0 && Slot.Amount >= Slot.MaxAmount);
		}
	}
	bool bInputsFull = !bPaused && bConfigured && Ingredients.Num() > 0 && B.Input.IsSet();
	for (int32 I = 0; bInputsFull && I < Ingredients.Num(); ++I)
	{
		bool bFull = false;
		for (const FSlot& Slot : B.Input.GetValue())
		{
			bFull = bFull || (Slot.ClassName.Equals(Ingredients[I].ClassName, ESearchCase::CaseSensitive) && Slot.MaxAmount > 0 && Slot.Amount >= Slot.MaxAmount);
		}
		bInputsFull = bFull;
	}

	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("id"), ClampString(B.Id.GetValue()));
	Writer->WriteValue(TEXT("name"), ClampString(B.Name.GetValue()));
	Writer->WriteValue(TEXT("className"), ClampString(B.ClassName.GetValue()));
	if (bConfigured)
	{
		Writer->WriteValue(TEXT("recipe"), ClampString(B.Recipe.GetValue()));
	}
	else
	{
		Writer->WriteNull(TEXT("recipe"));
	}
	Writer->WriteValue(TEXT("isProducing"), B.IsProducing.GetValue());
	Writer->WriteValue(TEXT("isPaused"), bPaused);
	Writer->WriteValue(TEXT("isBackedUp"), bBackedUp);
	if (B.Input.IsSet())
	{
		Writer->WriteValue(TEXT("inputsFull"), bInputsFull);
	}
	Writer->WriteValue(TEXT("circuitGroupId"), B.CircuitGroupId.Get(-1));
	if (B.bHasLocation)
	{
		// satisfactoryServerAdapter.ts mapLocation: centimetres to metres, the yaw normalised, the pitch dropped.
		Writer->WriteObjectStart(TEXT("location"));
		Writer->WriteValue(TEXT("xM"), B.X / 100.0);
		Writer->WriteValue(TEXT("yM"), B.Y / 100.0);
		Writer->WriteValue(TEXT("zM"), B.Z / 100.0);
		Writer->WriteValue(TEXT("rotationDeg"), FrmFactoryYaw(B.Rotation));
		Writer->WriteObjectEnd();
	}
	if (B.ManuSpeed.IsSet() && B.ManuSpeed.GetValue() > 0)
	{
		Writer->WriteValue(TEXT("clockSpeedPercent"), B.ManuSpeed.GetValue());
	}
	WriteRates(TEXT("production"), Production);
	WriteRates(TEXT("ingredients"), Ingredients);
	Writer->WriteObjectEnd();
}
```

One known difference from the Node agent: a string longer than 200 characters whose 200th unit is a high surrogate. The Node agent's `slice(0, 200)` would split the pair; `ClampString` keeps 199 units so the UTF-8 stays valid. No real FRM name comes near 200 characters.

- [ ] **Step 5: Add the rows**

```
| FMath::Fmod | Engine/Source/Runtime/Core/Public/GenericPlatform/GenericPlatformMath.h:517 | header only (the double overload; Fmod(-2.8, 2) is -0.8, as JavaScript's %, :509) |
```

Then run `npm run harness -- check-api` and add any further row it names (for example `FJsonObject::GetArrayField` or `FJsonValue::AsObject`, which the test uses), looked up the same way.

- [ ] **Step 6: Run the tests**

Expected: `--min 91` passes; `check-api` clean. If the golden test fails, the message prints the mod's output: compare it with the `.expected.json` field by field. **The expected file is never edited by hand**; the mod's mapping is what changes.

- [ ] **Step 7: Commit**

```bash
git add mod/SatisDashAgent/Source api-sources.md
git -c core.safecrlf=false commit -m "Factory from getFactory, 2,000 buildings a slice, against the Node agent's output (FRM bridge Task 6)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 7: The worker port and the bridge

**Files:**
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Ports.h`
- Create: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/FrmBridge.h`
- Create: `mod/SatisDashAgent/Source/SatisDashCore/Private/FrmBridge.cpp`
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Snapshot.h`, `Private/Snapshot.cpp` (parts spliced in)
- Modify: `mod/SatisDashAgent/Source/SatisDashCoreTests/Private/DriverFakes.h` (`FFakeWorker`)
- Test: `mod/SatisDashAgent/Source/SatisDashCoreTests/Private/FrmBridgeTest.cpp`
- Modify: `api-sources.md`

**Interfaces:**
- Consumes: `FFrmQueue`, `FrmGetPlayer`, `FrmGetFactory` (Task 4); `ParsePlayers`, `PlayersToJson`, `PlayersUnavailable` (Task 5); `FFactoryParse` (Task 6); `EncodeBody` (`Requests.h`).
- Produces:
  ```cpp
  class IWorker { virtual void Launch(TUniqueFunction<void()> Work) = 0; virtual bool IsIdle() const = 0; };
  // FSnapshot gains: TOptional<FString> PlayersJson; TOptional<FString> FactoryJson;
  class FFrmBridge { FFrmBridge(ITransport&, IWorker&); void RequestPass(bool bFactoryDue); void Tick(double Now, int32 Port, const FString& Token);
                     TOptional<FString> TakePlayers(double Now, double MaxAgeSeconds); TOptional<FString> TakeFactory(double Now, double MaxAgeSeconds);
                     void EncodeSnapshot(FSnapshot Snapshot); bool EncodePending() const; TOptional<FEncodedBody> TakeEncoded(); void Stop(); };
  struct FFakeWorker : IWorker { bool bHold; int32 Launched; void release(); };   // DriverFakes.h
  ```

- [ ] **Step 1: Write the failing tests**

`DriverFakes.h`, after `FFakeGame`:

```cpp
	/** Runs work inline when launched, or holds it until release() when bHold (to test one task at a time). */
	struct FFakeWorker : public SatisDash::IWorker
	{
		bool bHold = false;
		int32 Launched = 0;
		TUniqueFunction<void()> Held;

		virtual void Launch(TUniqueFunction<void()> Work) override
		{
			++Launched;
			if (bHold)
			{
				Held = MoveTemp(Work);
				return;
			}
			Work();
		}
		virtual bool IsIdle() const override { return !Held; }
		void release()
		{
			TUniqueFunction<void()> Work = MoveTemp(Held);
			Held = TUniqueFunction<void()>();
			if (Work)
			{
				Work();
			}
		}
	};
```

`FrmBridgeTest.cpp`:

```cpp
#include "DriverFakes.h"
#include "SatisDash/FrmBridge.h"

using namespace SatisDash;

namespace
{
	const TCHAR* FrmBridgeOnePlayer = TEXT("[{\"Name\":\"Player One\",\"Online\":true}]");
	const TCHAR* FrmBridgeOneBuilding = TEXT("[{\"ID\":\"B_1\",\"Name\":\"n\",\"ClassName\":\"c\",\"IsProducing\":true,\"IsPaused\":false}]");

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
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmBridgePlayersTest, "SatisDash.Core.FrmBridge.Players", SatisDash::Test::Flags)

bool FFrmBridgePlayersTest::RunTest(const FString& Parameters)
{
	FFrmBridgeFixture F;
	F.Bridge.RequestPass(false);
	F.tick();
	TestEqual(TEXT("getPlayer asked"), F.Net.pending(TEXT("/getPlayer")), 1);
	F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, FrmBridgeOnePlayer));
	TestFalse(TEXT("not ready before the worker parsed it"), F.Bridge.TakePlayers(F.T, 10).IsSet());
	F.tick(2);
	TestEqual(TEXT("parsed on the worker"), F.Worker.Launched, 1);
	const TOptional<FString> Players = F.Bridge.TakePlayers(F.T, 10);
	TestTrue(TEXT("ready"), Players.IsSet() && Players->Contains(TEXT("Player One")));
	TestFalse(TEXT("taken once"), F.Bridge.TakePlayers(F.T, 10).IsSet());

	F.Bridge.RequestPass(false);
	F.tick();
	F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(0, FString()));
	F.tick();
	SatisDash::Test::SameText(*this, TEXT("FRM down: unavailable, without the worker"), F.Bridge.TakePlayers(F.T, 10).Get(FString()), TEXT("{\"available\":false,\"players\":[]}"));
	TestEqual(TEXT("nothing parsed for it"), F.Worker.Launched, 1);

	F.Bridge.RequestPass(false);
	F.tick();
	F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, TEXT("[{\"Name\":1}]")));
	F.tick(2);
	TestFalse(TEXT("an invalid body: no part"), F.Bridge.TakePlayers(F.T, 10).IsSet());

	F.Bridge.RequestPass(false);
	F.tick();
	F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, FrmBridgeOnePlayer));
	F.tick(2);
	TestFalse(TEXT("a part older than its age is dropped"), F.Bridge.TakePlayers(F.T + 11, 10).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmBridgeFactoryTest, "SatisDash.Core.FrmBridge.Factory", SatisDash::Test::Flags)

bool FFrmBridgeFactoryTest::RunTest(const FString& Parameters)
{
	FFrmBridgeFixture F;
	F.Worker.bHold = true;
	F.Bridge.RequestPass(true);
	F.tick();
	TestEqual(TEXT("one FRM request in flight: players first"), F.Net.Open.Num(), 1);
	F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, FrmBridgeOnePlayer));
	F.tick();
	TestEqual(TEXT("then the factory"), F.Net.pending(TEXT("/getFactory")), 1);
	F.Net.answer(TEXT("/getFactory"), SatisDash::Test::Reply(200, FrmBridgeOneBuilding));
	F.tick();
	TestEqual(TEXT("one worker task at a time: the players parse"), F.Worker.Launched, 1);
	F.Bridge.RequestPass(true);
	F.tick();
	TestEqual(TEXT("getPlayer passes the large body"), F.Net.pending(TEXT("/getPlayer")), 1);
	F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, FrmBridgeOnePlayer));
	F.tick();
	TestEqual(TEXT("but no second factory read while one is parsing"), F.Net.pending(TEXT("/getFactory")), 0);
	TestEqual(TEXT("and still one task"), F.Worker.Launched, 1);
	for (int32 I = 0; I < 6; ++I)
	{
		F.Worker.release();
		F.tick();
	}
	TestTrue(TEXT("the factory part is ready"), F.Bridge.TakeFactory(F.T, 30).Get(FString()).Contains(TEXT("\"B_1\"")));
	F.tick(2);
	TestEqual(TEXT("then the next factory read may start"), F.Net.pending(TEXT("/getFactory")), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmBridgeStopTest, "SatisDash.Core.FrmBridge.Stop", SatisDash::Test::Flags)

bool FFrmBridgeStopTest::RunTest(const FString& Parameters)
{
	FFrmBridgeFixture F;
	F.Bridge.RequestPass(true);
	F.tick();
	F.Bridge.Stop();
	F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, FrmBridgeOnePlayer));
	F.tick(3);
	TestFalse(TEXT("a reply after Stop is never used"), F.Bridge.TakePlayers(F.T, 10).IsSet());
	TestEqual(TEXT("and the queued factory read was dropped"), F.Net.sent(TEXT("/getFactory")), 0);

	FSnapshot Snapshot;
	Snapshot.AgentVersion = TEXT("mod/0.1.0");
	Snapshot.ObservedAt = FDateTime(2026, 10, 23, 12, 0, 0);
	Snapshot.FactoryJson = TEXT("{\"buildings\":[]}");
	Snapshot.PlayersJson = PlayersToJson(PlayersUnavailable());
	F.Bridge.EncodeSnapshot(Snapshot);
	TestTrue(TEXT("encoding is pending"), F.Bridge.EncodePending());
	F.tick(2);
	const TOptional<FEncodedBody> Body = F.Bridge.TakeEncoded();
	TestTrue(TEXT("encoded on the worker"), Body.IsSet() && Body->Bytes.Num() > 0);
	TestFalse(TEXT("no longer pending"), F.Bridge.EncodePending());
	const FString Json = SnapshotToJson(Snapshot);
	TestTrue(TEXT("the parts are spliced into the snapshot"), Json.Contains(TEXT(",\"players\":{\"available\":false,\"players\":[]}")) && Json.Contains(TEXT(",\"factory\":{\"buildings\":[]}}")));
	TestTrue(TEXT("and it is one valid object"), SatisDash::Json::ParseObject(Json).IsValid());
	return true;
}
```

- [ ] **Step 2: Run them to see them fail**

Expected: build error, `'IWorker': is not a member of 'SatisDash'`.

- [ ] **Step 3: Implement**

`Ports.h`, after `IGameReader`:

```cpp
	/**
	 * Runs pure work off the game thread (FRM bridge spec "Threading"): UE::Tasks in the game (SatisDashGame's
	 * FTaskWorker), inline in the tests. One task at a time: Launch is only called while IsIdle().
	 */
	class IWorker
	{
	public:
		virtual ~IWorker() = default;
		virtual void Launch(TUniqueFunction<void()> Work) = 0;
		/** True when nothing launched is still running; what it wrote is then safe to read on the game thread. */
		virtual bool IsIdle() const = 0;
	};
```

`Snapshot.h`, in `FSnapshot`:

```cpp
		/** The players part (AgentPlayersSchema) as JSON, made by FFrmBridge; spliced in as is. */
		TOptional<FString> PlayersJson;
		/** The factory part (AgentFactorySchema) as JSON, made off the game thread by FFactoryParse. */
		TOptional<FString> FactoryJson;
```

`Snapshot.cpp`, replacing the last line of `SnapshotToJson`:

```cpp
	FString Json = SatisDash::Json::Write(Root);
	if (Snapshot.PlayersJson.IsSet() || Snapshot.FactoryJson.IsSet())
	{
		// The parts arrive as finished JSON; the root always has fields, so its closing brace is replaced.
		Json.LeftChopInline(1);
		if (Snapshot.PlayersJson.IsSet())
		{
			Json += TEXT(",\"players\":") + Snapshot.PlayersJson.GetValue();
		}
		if (Snapshot.FactoryJson.IsSet())
		{
			Json += TEXT(",\"factory\":") + Snapshot.FactoryJson.GetValue();
		}
		Json.AppendChar(TEXT('}'));
	}
	return Json;
```

`FrmBridge.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "SatisDash/FrmFactory.h"
#include "SatisDash/FrmPlayers.h"
#include "SatisDash/FrmQueue.h"
#include "SatisDash/Ports.h"
#include "SatisDash/RateLimit.h"
#include "SatisDash/Requests.h"
#include "SatisDash/Snapshot.h"

namespace SatisDash
{
	/**
	 * The FRM bridge's live lane (spec "Design"): asks FRM for players and the factory through FFrmQueue, parses the
	 * bodies on the worker, and keeps the newest finished part of each for the driver's next snapshot. It also
	 * serialises and gzips, on the worker, a snapshot that carries the factory part.
	 *
	 * Threading: the worker runs one job at a time, in this order: a snapshot to encode, a players body, the next slice of
	 * the factory body. A job is shared with its task (TSharedPtr), so dropping it here never frees what the task still
	 * reads. What a task wrote is read only once IWorker::IsIdle().
	 *
	 * Memory (it lives for the server's uptime): at most one factory body (16 MB cap) and its output, one players body (4
	 * MB cap), one ready part of each, one snapshot being encoded and its body; the failure log at most two keys.
	 */
	class SATISDASHCORE_API FFrmBridge
	{
	public:
		FFrmBridge(ITransport& InTransport, IWorker& InWorker);
		FFrmBridge(const FFrmBridge&) = delete;
		FFrmBridge& operator=(const FFrmBridge&) = delete;

		/** A sample pass that read the game: queues getPlayer (the status cadence), and getFactory when bFactoryDue. */
		void RequestPass(bool bFactoryDue);
		/** Every driver tick: takes finished work, launches the next job and starts the next FRM read. */
		void Tick(double Now, int32 Port, const FString& Token);
		/** The newest players part finished within MaxAgeSeconds, once. */
		TOptional<FString> TakePlayers(double Now, double MaxAgeSeconds);
		/** The newest factory part finished within MaxAgeSeconds, once. */
		TOptional<FString> TakeFactory(double Now, double MaxAgeSeconds);
		/** Hands a snapshot carrying the factory part to the worker for SnapshotToJson and EncodeBody. */
		void EncodeSnapshot(FSnapshot Snapshot);
		bool EncodePending() const { return Encode.IsValid(); }
		TOptional<FEncodedBody> TakeEncoded();
		/** Leaving Running: no queued read, no reply used, no job or part kept. A running task finishes and is dropped. */
		void Stop();

	private:
		struct FPlayersJob { TArray<uint8> Body; TOptional<FPlayersPart> Result; bool bRan = false; };
		struct FFactoryJob
		{
			explicit FFactoryJob(TArray<uint8> InBody) : Parse{MoveTemp(InBody)} {}
			FFactoryParse Parse;
			FFactoryParse::EStep Last = FFactoryParse::EStep::More;
			bool bRan = false;
		};
		struct FEncodeJob { FSnapshot Snapshot; FEncodedBody Body; bool bRan = false; };
		struct FReady { FString Json; double At = 0; };

		void Harvest(double Now);
		void LaunchNext();
		void NoteFailed(const TCHAR* Part, double Now);
		static TOptional<FString> TakeFresh(TOptional<FReady>& Ready, double Now, double MaxAgeSeconds);

		IWorker& Worker;
		FFrmQueue Queue;
		TSharedPtr<FPlayersJob> Players;
		TSharedPtr<FFactoryJob> Factory;
		TSharedPtr<FEncodeJob> Encode;
		TOptional<FReady> PlayersReady;
		TOptional<FReady> FactoryReady;
		TOptional<FEncodedBody> Encoded;
		bool bPlayersAbsent = false;
		bool bFactoryFailed = false;
		double LastNow = 0;
		/** Bounded: two keys ("players", "factory"), each at most once per 600 s. */
		FRateLimitedLog FailLog;
	};
}
```

`FrmBridge.cpp`:

```cpp
#include "SatisDash/FrmBridge.h"

#include "SatisDash/Log.h"

SatisDash::FFrmBridge::FFrmBridge(ITransport& InTransport, IWorker& InWorker)
	: Worker{InWorker}
	, Queue{InTransport}
{
}

void SatisDash::FFrmBridge::NoteFailed(const TCHAR* Part, double Now)
{
	if (FailLog.ShouldLog(Part, Now))
	{
		UE_LOG(LogSatisDash, Warning, TEXT("Couldn't read %s from FRM; leaving it out of the snapshot"), Part);
	}
}

void SatisDash::FFrmBridge::RequestPass(bool bFactoryDue)
{
	Queue.Enqueue(FFrmRead{ &FrmGetPlayer, [this](const FHttpReply& Reply)
	{
		if (Reply.Code >= 200 && Reply.Code < 300)
		{
			Players = MakeShared<FPlayersJob>();
			Players->Body = Reply.Body;
		}
		else
		{
			bPlayersAbsent = true; // playersMapper.ts: unreachable, timed out or an HTTP error is "FRM absent"
		}
	} });
	if (bFactoryDue)
	{
		Queue.Enqueue(FFrmRead{ &FrmGetFactory, [this](const FHttpReply& Reply)
		{
			if (Reply.Code >= 200 && Reply.Code < 300)
			{
				Factory = MakeShared<FFactoryJob>(Reply.Body);
			}
			else
			{
				bFactoryFailed = true;
			}
		} });
	}
}

void SatisDash::FFrmBridge::Harvest(double Now)
{
	if (Encode.IsValid() && Encode->bRan)
	{
		Encoded = MoveTemp(Encode->Body);
		Encode.Reset();
	}
	if (Players.IsValid() && Players->bRan)
	{
		if (Players->Result.IsSet())
		{
			PlayersReady = FReady{ PlayersToJson(Players->Result.GetValue()), Now };
		}
		else
		{
			NoteFailed(TEXT("players"), Now);
		}
		Players.Reset();
	}
	if (Factory.IsValid() && Factory->bRan)
	{
		Factory->bRan = false;
		if (Factory->Last == FFactoryParse::EStep::Done)
		{
			FactoryReady = FReady{ Factory->Parse.TakeJson(), Now };
			Factory.Reset();
		}
		else if (Factory->Last == FFactoryParse::EStep::Invalid)
		{
			NoteFailed(TEXT("factory"), Now);
			Factory.Reset();
		}
	}
}

void SatisDash::FFrmBridge::LaunchNext()
{
	if (Encode.IsValid() && !Encode->bRan)
	{
		Worker.Launch([Job = Encode]
		{
			Job->Body = EncodeBody(SnapshotToJson(Job->Snapshot));
			Job->bRan = true;
		});
	}
	else if (Players.IsValid() && !Players->bRan)
	{
		Worker.Launch([Job = Players]
		{
			Job->Result = ParsePlayers(Job->Body);
			Job->Body.Empty();
			Job->bRan = true;
		});
	}
	else if (Factory.IsValid() && !Factory->bRan)
	{
		Worker.Launch([Job = Factory]
		{
			Job->Last = Job->Parse.Step();
			Job->bRan = true;
		});
	}
}

void SatisDash::FFrmBridge::Tick(double Now, int32 Port, const FString& Token)
{
	LastNow = Now;
	if (bPlayersAbsent)
	{
		bPlayersAbsent = false;
		PlayersReady = FReady{ PlayersToJson(PlayersUnavailable()), Now };
	}
	if (bFactoryFailed)
	{
		bFactoryFailed = false;
		NoteFailed(TEXT("factory"), Now);
	}
	if (Worker.IsIdle())
	{
		Harvest(Now);
		LaunchNext();
	}
	Queue.Pump(Port, Token, Factory.IsValid());
}

TOptional<FString> SatisDash::FFrmBridge::TakeFresh(TOptional<FReady>& Ready, double Now, double MaxAgeSeconds)
{
	TOptional<FString> Out;
	if (Ready.IsSet() && Now - Ready->At <= MaxAgeSeconds)
	{
		Out = MoveTemp(Ready->Json);
	}
	Ready.Reset();
	return Out;
}

TOptional<FString> SatisDash::FFrmBridge::TakePlayers(double Now, double MaxAgeSeconds)
{
	return TakeFresh(PlayersReady, Now, MaxAgeSeconds);
}

TOptional<FString> SatisDash::FFrmBridge::TakeFactory(double Now, double MaxAgeSeconds)
{
	return TakeFresh(FactoryReady, Now, MaxAgeSeconds);
}

void SatisDash::FFrmBridge::EncodeSnapshot(FSnapshot Snapshot)
{
	Encode = MakeShared<FEncodeJob>();
	Encode->Snapshot = MoveTemp(Snapshot);
}

TOptional<SatisDash::FEncodedBody> SatisDash::FFrmBridge::TakeEncoded()
{
	TOptional<FEncodedBody> Out = MoveTemp(Encoded);
	Encoded.Reset();
	return Out;
}

void SatisDash::FFrmBridge::Stop()
{
	Queue.Cancel();
	Players.Reset();
	Factory.Reset();
	Encode.Reset();
	PlayersReady.Reset();
	FactoryReady.Reset();
	Encoded.Reset();
	bPlayersAbsent = false;
	bFactoryFailed = false;
}
```

`api-sources.md`:

```
| TUniqueFunction | Engine/Source/Runtime/Core/Public/Templates/Function.h:827 | header only |
```

Then `check-api`, adding whatever else it names.

- [ ] **Step 4: Run the tests**

Expected: `--min 94` passes; `check-api` clean.

- [ ] **Step 5: Commit**

```bash
git add mod/SatisDashAgent/Source api-sources.md
git -c core.safecrlf=false commit -m "The worker port and the FRM bridge: one job at a time, parts kept until the next snapshot (FRM bridge Task 7)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 8: The driver sends players and factory

**Files:**
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Pusher.h`, `Private/Pusher.cpp:32-34`
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Driver.h`
- Modify: `mod/SatisDashAgent/Source/SatisDashCore/Private/Driver.cpp`
- Modify: `mod/SatisDashAgent/Source/SatisDashCoreTests/Private/DriverFakes.h` (`FDriverFixture::Worker`)
- Modify: every `FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock() };` in `DriverTest.cpp` and `DriverCommandsTest.cpp` (22 places)
- Test: `mod/SatisDashAgent/Source/SatisDashCoreTests/Private/DriverFrmTest.cpp`

**Interfaces:**
- Consumes: `FFrmBridge` and `IWorker` (Task 7).
- Produces:
  - `FDriver(FString ConfigPath, FString CredentialPath, ITransport&, IGameReader&, IWorker&, FDriverClock)`.
  - `void FPusher::Enqueue(FEncodedBody Body)`.

- [ ] **Step 1: Write the failing test**

In `DriverFakes.h`, `FDriverFixture` gains `FFakeWorker Worker;` after `FFakeGame Game;`. Replace every `F.Game, F.clock() }` with `F.Game, F.Worker, F.clock() }` in `DriverTest.cpp` and `DriverCommandsTest.cpp`. Their tests still pass unchanged: FRM requests go to `/getPlayer` and `/getFactory`, which they never answer or count.

`DriverFrmTest.cpp`:

```cpp
#include "DriverFakes.h"
#include "SatisDash/Driver.h"

using namespace SatisDash;
using SatisDash::Test::FDriverFixture;

namespace
{
	const TCHAR* DriverFrmSnapshots = TEXT("/agent/v1/snapshots");

	/** Answers every open snapshot post with 200, and returns the bodies it answered. */
	TArray<FString> DriverFrmDrain(FDriverFixture& F)
	{
		TArray<FString> Bodies;
		while (F.Net.pending(DriverFrmSnapshots) > 0)
		{
			Bodies.Add(F.Net.lastBody(DriverFrmSnapshots));
			F.Net.answer(DriverFrmSnapshots, SatisDash::Test::SnapshotOk(false));
		}
		return Bodies;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverFrmPartsTest, "SatisDash.Core.Driver.FrmParts", SatisDash::Test::Flags)

bool FDriverFrmPartsTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	TestEqual(TEXT("no FRM read before enrolling"), F.Net.sent(TEXT("/getPlayer")), 0);
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	F.run(Driver, 1);
	TestEqual(TEXT("the first pass asks for players"), F.Net.pending(TEXT("/getPlayer")), 1);
	F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, TEXT("[{\"Name\":\"Player One\",\"Online\":true}]")));
	F.run(Driver, 1);
	TestEqual(TEXT("then the factory, one FRM request at a time"), F.Net.pending(TEXT("/getFactory")), 1);
	F.Net.answer(TEXT("/getFactory"), SatisDash::Test::Reply(200, TEXT("[{\"ID\":\"B_1\",\"Name\":\"n\",\"ClassName\":\"c\",\"IsProducing\":true,\"IsPaused\":false}]")));
	DriverFrmDrain(F);
	F.run(Driver, 5);
	TArray<FString> Bodies = DriverFrmDrain(F);
	F.run(Driver, 1);
	Bodies.Append(DriverFrmDrain(F));
	bool bPlayers = false;
	bool bFactory = false;
	for (const FString& Body : Bodies)
	{
		bPlayers = bPlayers || Body.Contains(TEXT("\"players\":{\"available\":true,\"players\":[{\"name\":\"Player One\",\"online\":true}]}"));
		bFactory = bFactory || Body.Contains(TEXT("\"factory\":{\"buildings\":[{\"id\":\"B_1\""));
	}
	TestTrue(TEXT("the next snapshot carries players"), bPlayers);
	TestTrue(TEXT("and one carries the factory"), bFactory);
	F.run(Driver, 10);
	TestEqual(TEXT("factory once per 30 s: no second read yet"), F.Net.sent(TEXT("/getFactory")), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverFrmDownTest, "SatisDash.Core.Driver.FrmDown", SatisDash::Test::Flags)

bool FDriverFrmDownTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	F.run(Driver, 1);
	F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(0, FString()));
	F.run(Driver, 1);
	F.Net.answer(TEXT("/getFactory"), SatisDash::Test::Reply(0, FString()));
	DriverFrmDrain(F);
	F.run(Driver, 5);
	const TArray<FString> Bodies = DriverFrmDrain(F);
	bool bUnavailable = false;
	for (const FString& Body : Bodies)
	{
		bUnavailable = bUnavailable || Body.Contains(TEXT("\"players\":{\"available\":false,\"players\":[]}"));
		TestFalse(TEXT("no factory part"), Body.Contains(TEXT("\"factory\"")));
		TestTrue(TEXT("status keeps flowing"), Body.Contains(TEXT("\"status\"")));
	}
	TestTrue(TEXT("players unavailable"), bUnavailable);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverFrmStopsTest, "SatisDash.Core.Driver.FrmStops", SatisDash::Test::Flags)

bool FDriverFrmStopsTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	F.run(Driver, 1);
	const int32 Asked = F.Net.sent(TEXT("/getPlayer"));
	F.Net.answer(DriverFrmSnapshots, SatisDash::Test::Reply(401, FString()));
	F.run(Driver, 1);
	TestTrue(TEXT("halted by the 401"), Driver.GetAgent().GetState() != EAgentState::Running);
	F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, TEXT("[{\"Name\":\"Player One\",\"Online\":true}]")));
	F.run(Driver, 30);
	TestEqual(TEXT("no FRM read while not running"), F.Net.sent(TEXT("/getPlayer")), Asked);
	TestEqual(TEXT("nothing pushed after the halt"), F.Net.pending(DriverFrmSnapshots), 0);
	return true;
}
```

- [ ] **Step 2: Run it to see it fail**

Expected: build error, `'SatisDash::FDriver::FDriver': no overloaded function takes 6 arguments`.

- [ ] **Step 3: Implement**

`Pusher.h`, after `Enqueue(const FString&)`:

```cpp
		/** The same, for a body EncodeBody already made (FFrmBridge encodes a factory snapshot off the game thread). */
		void Enqueue(FEncodedBody Body);
```

`Pusher.cpp`: the body of the old `Enqueue` becomes `Enqueue(FEncodedBody Body)` (from `if (Body.Bytes.Num() > MaxQueuedBytes)` on), and:

```cpp
void SatisDash::FPusher::Enqueue(const FString& SnapshotJson)
{
	Enqueue(EncodeBody(SnapshotJson));
}
```

`Driver.h`:
- Add `#include "SatisDash/FrmBridge.h"`.
- The constructor becomes `FDriver(FString ConfigPath, FString CredentialPath, ITransport& InTransport, IGameReader& InGame, IWorker& InWorker, FDriverClock InClock);`.
- Add the members `FFrmBridge Bridge;` (after `Pusher`, with the comment "Bounded: see FrmBridge.h") and `TOptional<double> LastFactoryAt;` (after `LastPowerAt`).
- Update the class comment's memory paragraph: "the part log at most three keys; FRM reads and parsing as FrmBridge.h bounds them".

`Driver.cpp`:

```cpp
SatisDash::FDriver::FDriver(FString ConfigPath, FString CredentialPath, ITransport& InTransport, IGameReader& InGame, IWorker& InWorker, FDriverClock InClock)
	: Clock{MoveTemp(InClock)}
	, Transport{InTransport}
	, Game{InGame}
	, Agent{MoveTemp(ConfigPath), MoveTemp(CredentialPath), Clock.Random01}
	, Pusher{Clock.Random01}
	, Bridge{InTransport, InWorker}
{
}
```

In `Tick`:
- In the `if (bRunning && !bNowRunning)` block, add `Bridge.Stop();` after `StopCommands();`.
- In the `if (!bRunning && bNowRunning)` block, add `LastFactoryAt.Reset();`.
- Replace the three calls at the end with:

```cpp
	const FAgentConfig& Config = Agent.GetConfig();
	Bridge.Tick(Now, Config.GetFrmPort(), Config.FrmToken);
	if (TOptional<FEncodedBody> Encoded = Bridge.TakeEncoded())
	{
		Pusher.Enqueue(MoveTemp(Encoded.GetValue()));
	}
	TickSample(Now);
	TickPush(Now);
	TickCommands(Now);
```

In `TickSample`:
- At the top, after `if (Now < NextStatusAt) return;`, add:

```cpp
	// A factory snapshot is still being encoded: this pass waits, so snapshots stay in order (Pusher.h).
	if (Bridge.EncodePending())
	{
		return;
	}
```

- Inside the `else` branch (status was read), after the power block:

```cpp
		// FRM bridge spec: players every pass, the factory on its cadence; both arrive in a later pass's snapshot.
		const bool bFactoryDue = !LastFactoryAt.IsSet() || Now - LastFactoryAt.GetValue() >= Cadence.FactorySeconds - DueSlackSeconds;
		if (bFactoryDue)
		{
			LastFactoryAt = Now;
		}
		Bridge.RequestPass(bFactoryDue);
		Snapshot.PlayersJson = Bridge.TakePlayers(Now, 2.0 * Cadence.StatusSeconds);
		Snapshot.FactoryJson = Bridge.TakeFactory(Now, Cadence.FactorySeconds);
```

- Replace `Pusher.Enqueue(SnapshotToJson(Snapshot));` with:

```cpp
	if (Snapshot.FactoryJson.IsSet())
	{
		Bridge.EncodeSnapshot(MoveTemp(Snapshot)); // megabytes of JSON: serialised and gzipped on the worker
	}
	else
	{
		Pusher.Enqueue(SnapshotToJson(Snapshot));
	}
```

- [ ] **Step 4: Run the tests**

Expected: `--min 97` passes. Every existing `Driver.*` test is unchanged and green.

- [ ] **Step 5: Commit**

```bash
git add mod/SatisDashAgent/Source
git -c core.safecrlf=false commit -m "The driver sends players and factory; a factory snapshot is encoded off the game thread (FRM bridge Task 8)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 9: The game's worker

**Files:**
- Create: `mod/SatisDashAgent/Source/SatisDashGame/Private/TaskWorker.h`
- Modify: `mod/SatisDashAgent/Source/SatisDashGame/Private/SatisDashGameModule.cpp`
- Modify: `api-sources.md`

**Interfaces:**
- Consumes: `IWorker` (Task 7); the driver's 6-argument constructor (Task 8).
- Produces: `SatisDash::FTaskWorker`, with `Launch`, `IsIdle` and `Wait`.

- [ ] **Step 1: Implement**

`TaskWorker.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Ports.h"
#include "Tasks/Task.h"

namespace SatisDash
{
	/**
	 * IWorker over UE::Tasks (FRM bridge spec "Threading"): one task at a time, at background-low priority, so parsing
	 * never competes with the game's own work. Waits for its task when destroyed, so nothing it runs outlives the module.
	 */
	class FTaskWorker final : public IWorker
	{
	public:
		FTaskWorker() = default;
		FTaskWorker(const FTaskWorker&) = delete;
		FTaskWorker& operator=(const FTaskWorker&) = delete;
		virtual ~FTaskWorker() override { Wait(); }

		virtual void Launch(TUniqueFunction<void()> Work) override
		{
			Current = UE::Tasks::Launch(TEXT("SatisDashFrm"), MoveTemp(Work), UE::Tasks::ETaskPriority::BackgroundLow);
		}
		virtual bool IsIdle() const override { return Current.IsCompleted(); }
		void Wait()
		{
			if (Current.IsValid())
			{
				Current.Wait();
			}
		}

	private:
		UE::Tasks::FTask Current;
	};
}
```

`SatisDashGameModule.cpp`:
- Add `#include "TaskWorker.h"`.
- Declare `SatisDash::FTaskWorker Worker;` **before** `Driver`, so the driver (and its bridge) is destroyed first and the worker last, after its task.
- The driver's initialiser gains `Worker` between `Reader` and `gameClock()`.
- In `ShutdownModule`, after `FWorldDelegates::OnPostWorldInitialization.Remove(WorldInitHandle);`:

```cpp
		// A parse or encode still running finishes here: its job is shared with the task, never freed under it.
		Worker.Wait();
```

- [ ] **Step 2: Add the rows**

```
| Tasks/Task.h | Engine/Source/Runtime/Core/Public/Tasks/Task.h:1 | header only |
| UE::Tasks::Launch | Engine/Source/Runtime/Core/Public/Tasks/Task.h:299 | header only (the overload without prerequisites) |
| UE::Tasks::FTask | Engine/Source/Runtime/Core/Public/Tasks/Task.h:392 | header only (alias of Private::FTaskHandle, :33) |
| FTaskHandle::IsCompleted | Engine/Source/Runtime/Core/Public/Tasks/Task.h:75 | header only (true when not valid, :77) |
| FTaskHandle::IsValid | Engine/Source/Runtime/Core/Public/Tasks/Task.h:69 | header only |
| FTaskHandle::Wait | Engine/Source/Runtime/Core/Public/Tasks/Task.h:91 | header only (the overload without a timeout) |
| UE::Tasks::ETaskPriority::BackgroundLow | Engine/Source/Runtime/Core/Public/Async/Fundamental/Task.h:25 | header only (LowLevelTasks::ETaskPriority, aliased into UE::Tasks at Tasks/TaskPrivate.h:54) |
```

Then `check-api` until clean.

- [ ] **Step 3: Build and run the tests**

Expected: the game module builds, `--min 97` passes, and `check-api` is clean.

- [ ] **Step 4: Commit**

```bash
git add mod/SatisDashAgent/Source api-sources.md
git -c core.safecrlf=false commit -m "The game's worker: UE::Tasks at background-low priority, waited for at shutdown (FRM bridge Task 9)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 10: The harness, and the owner's ruling on S13

**Files:**
- Modify: `harness/src/recorder.ts` (player names hashed)
- Modify: `harness/src/mod-config.ts:11`, `harness/src/core-run.ts:293-296` (`frmPort`)
- Modify: `harness/test/recorder.test.ts`, `harness/test/core-run.test.ts`, `harness/test/snapshot-fixtures.test.ts`
- Create: `mod/SatisDashAgent/Source/SatisDashCoreTests/Fixtures/snapshot-frm.json`
- Modify **only after the owner's ruling**: `runs/enrol/pass.ts:37`, `harness/test/enrol-pass.test.ts`

**Interfaces:**
- Consumes: the snapshot shape from Task 8.
- Produces: evidence with `players[].name` hashed, and the mod config written with `frmPort`.

- [ ] **Step 1: Write the failing harness tests**

`harness/test/recorder.test.ts`, a new `it` after the Node agent User-Agent test (line 114). It uses the file's own `upstream`, `recorderFor`, `send`, `snapshot` and `MOD_UA`, and `sessionHash` from `../src/status` (already imported):

```ts
  it("hashes every player's name in a recorded snapshot, as it hashes the session (no names in evidence)", async () => {
    const up = await upstream(() => ({ status: 200, body: '{"commandsPending":false}' }));
    const rec = await recorderFor(up.url);
    const body = { ...snapshot, players: { available: true, players: [{ name: "Player One", online: true }] } };
    await send(rec.url, "POST", "/agent/v1/snapshots", { headers: { "user-agent": MOD_UA, "content-type": "application/json" }, body: JSON.stringify(body) });
    const recorded = rec.entries[0]!.snapshot as { players: { players: { name: string }[] } };
    expect(recorded.players.players[0]!.name).toBe(sessionHash("Player One"));
    expect(JSON.stringify(rec.entries)).not.toContain("Player One");
  });
```

`harness/test/snapshot-fixtures.test.ts`, inside the existing `describe`:

```ts
  it("snapshot-frm.json, with players and factory, is a valid snapshot", () => {
    const r = SnapshotRequestSchema.safeParse(load("snapshot-frm.json"));
    expect(r.success ? [] : r.error.issues).toEqual([]);
  });
```

`Fixtures/snapshot-frm.json`: `snapshot-world.json`'s content, plus:
- `"players"`: the content of `frm-getPlayer.expected.json`;
- `"factory"`: the content of `frm-getFactory-running.expected.json`.

`harness/test/core-run.test.ts`: a new `it` using the file's `fakeDeps()` (line 63). It runs a core run whose step calls `ctx.writeConfig({ backendUrl: "http://127.0.0.1:1", enrollmentCode: "" })`, with:
- `deps.config.frmBaseUrl` set to `"http://127.0.0.1:18081"`: the written `FactoryGame/Configs/SatisDashAgent.cfg` under `serverDir` parses to an object with `frmPort: 18081`;
- `deps.config.frmBaseUrl` deleted: the file has no `frmPort`.

Follow the shape of the file's existing tests that drive a `CoreRunSpec` through `fakeDeps()` (read lines 90-130 first). Its `CoreContext.writeConfig` type (`core-run.ts:44`) stays `{ backendUrl: string; enrollmentCode: string }`; only `writeModConfig`'s parameter widens.

- [ ] **Step 2: Run them to see them fail**

Run: `cd harness; npx vitest run test/recorder.test.ts test/snapshot-fixtures.test.ts test/core-run.test.ts --reporter=dot`
Expected: the recorder test fails (the name is recorded as is); the core-run test fails (no `frmPort`); the fixture test passes once the file exists.

- [ ] **Step 3: Implement**

`recorder.ts`, next to `hashSession`:

```ts
/** ADR-0029: player names are personal data about third parties; evidence keeps a hash of each, never the name. */
function hashPlayers(snapshot: unknown): unknown {
  const players = (snapshot as { players?: { players?: unknown } } | null)?.players;
  const list = players?.players;
  if (!Array.isArray(list)) return snapshot;
  const hashed = list.map((p) => (p && typeof p === "object" && typeof (p as { name?: unknown }).name === "string" ? { ...p, name: sessionHash((p as { name: string }).name) } : p));
  return { ...(snapshot as object), players: { ...players, players: hashed } };
}
```

and where it records (`entry.snapshot = hashSession(...)`, both places, lines 131 and 136), wrap: `hashSession(hashPlayers(...))`.

`mod-config.ts:11`: `values: Record<string, string | number>`.

`core-run.ts:293-296`:

```ts
        writeConfig: (v) => {
          if (v.enrollmentCode) codes.push(v.enrollmentCode);
          // FRM bridge: the test server's FRM listens on the runner config's frmBaseUrl port, not FRM's default 8080.
          const frmPort = deps.config.frmBaseUrl ? Number(new URL(deps.config.frmBaseUrl).port) : undefined;
          writeModConfig(deps.config.serverDir, MOD, frmPort ? { ...v, frmPort } : v);
        },
```

`CoreContext.writeConfig` (`core-run.ts:44`) keeps its type; the spread adds the number.

- [ ] **Step 4: Run the harness tests**

Run: `npx vitest run --reporter=dot` then `npm run typecheck`
Expected: all pass.

- [ ] **Step 5: Ask the owner for the S13 ruling**

Stop here and ask the owner. Quote `runs/enrol/pass.ts:37` and give the reason: the FRM bridge spec adds `players` and `factory` to the snapshot, so the check would fail every round. The proposed change:

```ts
// Owner ruling (<date>, FRM bridge Plan 3A Task 10): the FRM bridge adds players and factory (spec "What the Node agent
// sends"). mapLive is added with Plan 3B.
const SNAPSHOT_KEYS = new Set(["agentVersion", "observedAt", "reachable", "paused", "status", "power", "settings", "players", "factory"]);
```

plus a test in `harness/test/enrol-pass.test.ts`: a round whose snapshot has `players` and `factory` passes; one with `mapLive` still fails as `S13: unexpected snapshot field mapLive`.

**Only after the owner rules:**
1. Apply the change and its test.
2. Run `npx vitest run test/enrol-pass.test.ts --reporter=dot`, which must pass.
3. Commit with the ruling's date in the message.

- [ ] **Step 6: Commit the harness changes**

```bash
git add harness mod/SatisDashAgent/Source/SatisDashCoreTests/Fixtures/snapshot-frm.json
git -c core.safecrlf=false commit -m "Harness: player names hashed in evidence, frmPort in the mod config, the FRM snapshot fixture checked (FRM bridge Task 10)" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 7: Open PR 3A-2 and run it**

1. Run the full harness suite, typecheck, `check-api` and the mod tests (`--min 97`).
2. Push, then open a PR to `main` titled "FRM bridge 3A-2: players and factory from FRM, parsed off the game thread". Its body lists the owner's S13 ruling, and the evidence privacy change.
3. Ask the owner to dispatch `mod-test` on the branch.
4. Then ask the owner to run `run-backend.ps1 -Branch <branch> -Runs enrol,revoke,command`. All three must pass, and the enrol evidence must show `players` with hashed names and a `factory` part.
