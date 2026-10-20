# Core mod v1, Plan 2B: the mod in the game, and the runs that prove it Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire Plan 2A's pure logic into a dedicated server (native readers, UE's HTTP module, the core ticker), then prove the finished mod with the spec's harness runs, a 30-minute parity run against the Node agent and a security review.

**Architecture:** The plugin `mod/SatisDashAgent` gains a third module, `SatisDashGame` (Runtime, Server and Editor targets), which is thin: an `ITransport` over `FHttpModule`, an `IGameReader` over the game's server subsystem and circuit subsystem, and a module that ticks one `FDriver` every 0.5 s. Everything with a decision in it stays in `SatisDashCore`: the two interfaces, the reply rules (64 KB cap, redirect refusal) and `FDriver`, which runs config checks, enrolment, sampling, pushing and commands against those interfaces, so the pairing contracts in 2A's headers are unit-tested headless with a fake transport and a fake game. The TypeScript harness gains a `core-run` command with one scenario per spec run, a stub backend for the HTTP probe, a recording proxy in front of the throwaway backend, and a backend host that lets the runner's account use that backend without joining `docker-users`.

**Tech Stack:** Unreal Engine 5.6.1-CSS, SML 3.12.0 (`harness/toolchain.json`), C++20, UE automation tests, UE's HTTP module (libcurl on Windows), FactoryGame and FactoryDedicatedServer headers; the TypeScript harness (Node, zod 4, Vitest 5); satisfactory-dash's compose stack at the pinned commit for the backend.

**Spec:** `docs/specs/2026-10-20-core-mod-v1-design.md` (approved, mod PR #19; Security section mod PR #24). The build decision is satisfactory-dash ADR-0042 (#479). The contract is satisfactory-dash `packages/shared/src/agent.ts` at `ab4626f`, vendored in `harness/contract/`. Plan 2A (`docs/plans/2026-10-20-core-mod-v1-2a-logic.md`, merged as #26 and #27) produced every interface this plan consumes; read its Global Constraints too.

## Global Constraints

Every constraint in Plan 2A's Global Constraints still holds. In particular: protocol strings compare case-sensitively; no `check()`, `ensure()` or `verify()` on outside data; logs only to `LogSatisDash` at `Display` or `Warning`, never `Error`, one line per state change, never the secret, the code or `Bearer`; every engine, SML or FactoryGame symbol, include and module has a row in `api-sources.md`, looked up in the pinned checkout (`npm run harness -- check-api` prints `0 missing, 0 bad rows`). The 2A conventions for check-api also hold: write the plugin's nested namespaces in full (`SatisDash::Json::`), give local lambdas lowercase names, declare receivers with the mod's own type name rather than `auto`.

New in 2B:
- `SatisDashCore` still depends only on `Core` and `Json`. `ITransport`, `IGameReader`, the reply rules and `FDriver` live there.
- `SatisDashGame` is `Runtime` with `"TargetAllowList": ["Server", "Editor"]` (FactoryDedicatedServer exists only for those targets, `api-sources.md` row `module:FactoryDedicatedServer`). Its `StartupModule` does nothing unless `IsRunningDedicatedServer()`: the editor loads it for the unit-test build and must not write a config into the SML project.
- **Clock:** monotonic `FPlatformTime::Seconds()` for every `Now` (Agent.h, Pusher.h). **Dates:** `FDateTime::UtcNow()` for `ObservedAt` and every command `Now` (Commands.h, Snapshot.h).
- **Pairing:** every `StartEnroll` that returns a request gets exactly one `FinishEnroll`, every `StartSend` that returns a snapshot exactly one `FinishSend`, including timeouts and cancels. `FPusher::Clear()` is never called while a send is in flight. `GetCredential()` is called again before every authenticated request, and every 401 goes to `OnAuthRejected()`.
- **Transport:** `ITransport::Send` calls its handler exactly once, later, on the game thread, never from inside `Send`. The 64 KB cap is enforced while the body downloads (UE's receive stream), never after buffering it.
- Paths: config `FPaths::ProjectDir() / "Configs/SatisDashAgent.cfg"`, credential `FPaths::ProjectSavedDir() / "SatisDash/credential.json"` (spec "Shape"). On the test server these are `<serverDir>/FactoryGame/Configs/SatisDashAgent.cfg` (`modConfigPath`, `harness/src/mod-config.ts:6`) and `<serverDir>/FactoryGame/Saved/SatisDash/credential.json`.
- Harness runs talk only to `127.0.0.1` (CLAUDE.md rule 5). Codes, secrets and tokens are generated per run, written only to the test server or the throwaway database, and removed after every run, failed ones included. No run's evidence carries the secret, the code, an `Authorization` value or a raw session name (hash it, as S6 does).
- A run's pass conditions live in `runs/<run>/pass.ts` and follow CLAUDE.md rule 3 exactly as `spikes/S#/pass.ts` do: a failed run stops the work, and a pass condition is never weakened to get past one. Evidence is machine-written (`writeCoreEvidence`).
- The test counts below are the C++ automation tests `judge-mod-test --min <N>` must see. 2A ends at 42.

## Rulings from the engine lookups (2026-10-20)

1. **Windows uses libcurl.** `FWindowsPlatformHttp::CreatePlatformHttpManager` returns `new FCurlHttpManager()` (`Engine/Source/Runtime/Online/HTTP/Private/Windows/WindowsPlatformHttp.cpp:90-93`), and `HTTP.Build.cs:25` picks libcurl on Windows unless `bUseXCurl`. That resolves 2A's first `[NEEDS VERIFICATION]`: `CURLOPT_FOLLOWLOCATION` is 1 for every request (`Private/Curl/CurlHttp.cpp:127`).
2. **Redirects: defence in depth, then measure.** The transport refuses any answer whose `IHttpBase::GetEffectiveURL()` (`Public/Interfaces/IHttpBase.h:104`) differs from the URL it asked for, so a redirected answer never reaches the agent (it becomes a Code 0 reply and one log line). That can't stop curl from contacting the other port. Task 1's probe measures whether it does, and whether the `Authorization` header went along. **Gate A** is that result.
3. **User-Agent is kept.** `FHttpRequestCommon` sets UE's default User-Agent only when the header is empty (`Private/GenericPlatform/HttpRequestCommon.cpp:148-150`); 2A sets `satisfactory-dash-mod/0.1.0` (`Requests.cpp:17`). The probe checks the header the stub receives.
4. **TLS floor.** Nothing in the HTTP module sets `CURLOPT_SSLVERSION` (no match under `Online/HTTP/Private`), so the floor is libcurl's default. The probe records the versions the mod offers in its ClientHello; the backend's own minimum is asked of the dash session (Task 1, Step 1).
5. **The 64 KB cap while downloading.** `IHttpRequest::SetResponseBodyReceiveStream` (`Public/Interfaces/IHttpRequest.h:256`) streams the body into an `FArchive`; when its `Serialize` sets an error, "this http request will fail and quit" (`:245-247`; checked at `HttpRequestCommon.cpp:675-677`). `FCappedBody` keeps at most 64 KB and errors past it. Serialize runs on the HTTP thread, so the archive locks.
6. **Delegates come back on the game thread.** That is the default thread policy; `SetDelegateThreadPolicy` (`IHttpRequest.h:418`) is never called.
7. **Power is read per circuit group.** Circuits joined by a power switch report the same figures (S3, owner's amendment after run #11), and FRM lists one row per group. `UFGCircuit::GetCircuitGroupID()` (`Source/FactoryGame/Public/FGCircuit.h:71`) gives the group; the lowest circuit ID in a group speaks for it. Circuits are found as S3 found them, `FindCircuit<UFGPowerCircuit>` over IDs 0-9999 (S3 ruling 5; the map is private, `FGCircuitSubsystem.h:248-251`). Battery percent is computed by the mod (`GetBatterySumPowerStorePercent` divides by capacity unguarded, `FGPowerCircuit.h:223`). `[NEEDS VERIFICATION]` that FRM's `CircuitGroupID`, `PowerMaxConsumed`, `BatteryCapacity`, `BatteryPercent` and `BatteryDifferential` are these same values: Task 6's enrol run compares every field with `getPower`.
8. **The driver is pure.** 2A's headers put the pairing rules on the caller. Putting the caller in `SatisDashCore` behind two interfaces makes every rule a headless unit test, and leaves `SatisDashGame` about 300 lines with nothing to decide.
9. **Task 1 wires enrolment and pushing directly** (about 60 lines in the module), only so the probe can measure the HTTP layer before anything else is built on it. Task 6 replaces that wiring with `FDriver`.
10. **The backend host (owner sign-off at plan review).** Runs that load the staged world must run as `satis-runner` (`savesConflict`, `run.ts:167-170`: the owner's account shares its saves folder with the real server). Runs that need the backend need Docker, and `satis-runner` stays out of `docker-users` (S3-S5 spec, owner decision 3). So the owner's session runs `harness backend-host`, which brings the throwaway stack up under the owner's account and serves a small control API on `127.0.0.1` (seed a code, revoke, queue a command, read state, stop and start the relay), behind a random token in a file only the owner and `satis-runner` can read. The runner's job drives everything else. Without this, enrol could run without a world, but power, command, tick cost and parity couldn't run at all.
11. **"Stored snapshots" means the snapshots the backend accepted.** The backend sends snapshots to in-memory ingest, not a table (S3-S5 plan, ruling 3), so the harness can't read them back. A recording proxy between the agents and the relay keeps every snapshot the backend answered `200`. Task 9 asks the dash session to confirm, or to name a read route.

## Review Focus

1. **The game world changes under the mod** (entry map, then the save loads; a save switch from the dashboard). A person expects status to keep flowing, power to appear once a game runs and vanish without one, and no stale `UWorld*` to be touched. Pinned in Task 4 (`SatisDash.Core.Driver.Parts`: no power without a running game; a failed status read leaves only that part out) and Task 6's enrol run (no world, then world).
2. **A slow backend while the queue is full** (every send takes 10 s; snapshots pile up). A person expects one "queue is full" line per episode, the next episode logged again, and never a second send while one is out. Pinned in Task 4 (`SatisDash.Core.Pusher.FullAgain`, `SatisDash.Core.Driver.OneAtATime`).
3. **The config is edited while a request is out** (a new backend URL or a new code typed during an enrol or a send). A person expects nothing sent to the new backend with the old secret, the queue cleared only after the send in flight answers, and the new code tried. Pinned in Task 4 (`SatisDash.Core.Driver.Rebound`).
4. **A revoked server that is re-enrolled** with a new code. A person expects the old queue and commands dropped, the new secret on every request after it, and command IDs already run never run again. Pinned in Task 4 (`SatisDash.Core.Driver.Reenrol`), Task 5 (`SatisDash.Core.Commands.Halt`) and Task 7's revoke run.
5. **A hostile or broken backend answer at the transport** (a redirect to another port, a 64 KB + 1 body, a body that never ends, a 200 from the redirect target). A person expects each one refused, logged once, and retried with backoff, never accepted. Pinned in Task 1 (`SatisDash.Core.Reply.Make`, `.CappedBody`) and the probe run.

## Not in 2B

- Linux builds and the `0600` mode check (S11), the deep-nesting JSON check on Linux (2A final, `[NEEDS VERIFICATION]`): sub-project 4. The security review records them as open.
- Whether players' clients need the mod (SML's client/server requirement fields): sub-project 4, from SML's headers then.
- Publishing, ficsit.app, the host guide, the cut-over and deprecating the Node agent: sub-projects 4 and 5.
- From 2A's deferred list: "a config that's locked for a moment counts as broken" stays deferred. It heals at the next minute's check (`Agent.cpp:65-80` remembers the failed read's empty text, so the next readable text differs), sends nothing meanwhile (S10 holds), and a headless test can't hold a file lock portably.

## How the tests run

- C++, after each task that changes `mod/`:
  ```powershell
  pwsh scripts/test-mod.ps1 -Project <sml-project> -Engine <engine-root> -Out <tmp>\mod-test
  cd harness; npm run harness -- judge-mod-test <tmp>\mod-test --min <N>
  ```
  The two paths are the executor's own (the runner config's `modTest` values), on the command line only, never committed. Call `pwsh` through the PowerShell tool.
- TypeScript: `cd harness; npx vitest run test/<file>.test.ts --reporter=dot` while iterating, `npm test` once at the end of a task, plus `npm run typecheck` and `npm run harness -- check-api`.
- Harness runs: `npm run harness -- core-run <run> --out <tmp>` with `SATIS_HARNESS_CONFIG` set, or the `core-run.yml` dispatch (Task 1) on the runner. The owner approves every `ue-runner` deployment; never approve one yourself.
- Before each PR merges: `gh workflow run mod-test.yml --ref <branch>`, and the owner approves its deployment. Standing permission: merge once that run passes.

## File structure

```
mod/SatisDashAgent/
  SatisDashAgent.uplugin                          Task 1  adds SatisDashGame
  Source/SatisDashCore/
    Public/SatisDash/Ports.h                      Task 1  ITransport, IGameReader, FReplyHandler
    Public/SatisDash/Reply.h                      Task 1  FCappedBody, FRawReply, MakeReply
    Public/SatisDash/GameValues.h                 Task 2  circuit groups, auto-pause option, tick health
    Public/SatisDash/Driver.h                     Task 4  FDriver, FDriverClock (commands: Task 5)
    Private/Reply.cpp, GameValues.cpp, Driver.cpp, DriverCommands.cpp
    Private/Agent.cpp, Pusher.cpp                 Task 4  2A deferred fixes
  Source/SatisDashCoreTests/Private/
    ReplyTest.cpp                                 Task 1
    GameValuesTest.cpp, TestUtil.h                Task 2
    DriverFakes.h, DriverTest.cpp, PusherTest.cpp Task 4
    DriverCommandsTest.cpp                        Task 5
  Source/SatisDashGame/
    SatisDashGame.Build.cs                        Task 1
    Private/HttpTransport.h, HttpTransport.cpp    Task 1
    Private/GameReader.h, GameReader.cpp          Task 3
    Private/SatisDashGameModule.cpp               Task 1 (probe wiring), Task 6 (FDriver)
scripts/build-mod.ps1                             Task 1
harness/src/core-run.ts                           Task 1  runner, evidence, common checks
harness/src/probe-stub.ts                         Task 1  stub backend, ClientHello capture, untrusted TLS
harness/src/core-runs/http-probe.ts               Task 1
harness/src/recorder.ts                           Task 6
harness/src/backend-host.ts                       Task 6  host (owner side) and client (runner side)
harness/src/core-runs/enrol.ts                    Task 6
harness/src/core-runs/revoke.ts, command.ts       Task 7
harness/src/core-runs/outage.ts, tick-cost.ts     Task 8  (outbound.ts: S12)
harness/src/core-runs/parity.ts, node-agent.ts    Task 9
runs/<run>/pass.ts                                one per run
harness/test/<module>.test.ts                     one per harness module, and one per pass.ts
.github/workflows/core-run.yml                    Task 1
docs/reviews/2026-10-core-mod-v1-security.md      Task 10
```

---

### Task 1: The transport, the game module, and the HTTP probe (S2, S3, User-Agent) — Gate A

The handoff's three questions are answered here, before anything else is built on the HTTP layer.

**Files:**
- Create: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Ports.h`, `Public/SatisDash/Reply.h`, `Private/Reply.cpp`
- Create: `mod/SatisDashAgent/Source/SatisDashCoreTests/Private/ReplyTest.cpp`
- Create: `mod/SatisDashAgent/Source/SatisDashGame/SatisDashGame.Build.cs`, `Private/HttpTransport.h`, `Private/HttpTransport.cpp`, `Private/SatisDashGameModule.cpp`
- Create: `scripts/build-mod.ps1`, `harness/src/core-run.ts`, `harness/src/probe-stub.ts`, `harness/src/core-runs/http-probe.ts`, `runs/http-probe/pass.ts`
- Create: `harness/test/probe-stub.test.ts`, `harness/test/core-run.test.ts`, `harness/test/http-probe-pass.test.ts`, `.github/workflows/core-run.yml`
- Modify: `mod/SatisDashAgent/SatisDashAgent.uplugin`, `harness/src/evidence.ts` (extract `writeBundle`), `harness/src/cli.ts` (`core-run`), `harness/test/workflows.test.ts` (covers the new workflow), `api-sources.md`, `docs/runner.md`, `CLAUDE.md` (rule 3 names `runs/*/pass.ts` too)

**Interfaces:**
- Consumes (2A): `FRequestSpec` (Requests.h), `FHttpReply`, `MaxResponseBytes` (Responses.h), `FStatusReading`, `FCircuitReading`, `FSnapshot`, `SnapshotToJson` (Snapshot.h), `FAgent` (Agent.h), `FPusher` (Pusher.h), `BuildSnapshotRequest` (Requests.h), `FRateLimitedLog` (RateLimit.h), `AgentVersion()` (Version.h).
- Produces:
  - `using SatisDash::FReplyHandler = TFunction<void(const FHttpReply&)>;`
  - `class SatisDash::ITransport { virtual void Send(const FRequestSpec& Spec, FReplyHandler OnReply) = 0; }`
  - `class SatisDash::IGameReader { virtual TOptional<FStatusReading> ReadStatus() = 0; virtual TOptional<bool> ReadAutoPause() = 0; virtual TOptional<TArray<FCircuitReading>> ReadPower() = 0; virtual bool ApplyAutoPause(bool bEnabled) = 0; }`
  - `class SatisDash::FCappedBody : public FArchive` with `bool Overflowed() const`, `TArray<uint8> Take()`
  - `struct SatisDash::FRawReply { bool bProcessed; int32 Code; FString ContentType, RetryAfter, EffectiveUrl, Failure; }`, `enum class SatisDash::EReplyRefusal : uint8 { None, NoAnswer, Redirected, Oversized }`
  - `FHttpReply SatisDash::MakeReply(const FRawReply& Raw, const FString& RequestUrl, FCappedBody& Body, EReplyRefusal& OutRefusal)`
  - `class SatisDash::FHttpTransport : public ITransport` with `void CancelAll()` (module-private)
  - harness: `CORE_RUN_IDS`, `CoreRunSpec`, `CoreContext`, `CoreDeps`, `runCore(id, deps)`, `realCoreDeps(...)`, `writeCoreEvidence`, `commonReasons(e)`; `harness core-run <id> --out <dir>`
  - harness: `startProbeStub()`, `startHelloCapture()`, `startUntrustedTls(dir)`, `parseClientHello(buf)`, `FAKE_SECRET`

- [ ] **Step 1: Ask the dash session about the backend's TLS floor**

ListAgents, find the dash and portfolio session, and send:
"satisfactory-dash-mod Plan 2B, spec S2: what is the minimum TLS version api.satis-manager.com accepts (Cloudflare's Minimum TLS setting, or the origin's)? Reply with the value and where it's set; no change is asked for."
Record the answer verbatim (with its date) in this task's PR description and in `runs/http-probe/pass.ts`'s header comment. Don't wait on it to start Step 2.

- [ ] **Step 2: Write the failing C++ tests for the reply rules**

`mod/SatisDashAgent/Source/SatisDashCoreTests/Private/ReplyTest.cpp`:

```cpp
#include "SatisDash/Reply.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	void feed(FCappedBody& Body, int32 Bytes)
	{
		TArray<uint8> Chunk;
		Chunk.SetNumZeroed(Bytes);
		Body.Serialize(Chunk.GetData(), Chunk.Num());
	}

	FRawReply answered(const TCHAR* Url)
	{
		FRawReply Raw;
		Raw.bProcessed = true;
		Raw.Code = 200;
		Raw.ContentType = TEXT("application/json; charset=utf-8");
		Raw.RetryAfter = TEXT("7");
		Raw.EffectiveUrl = Url;
		return Raw;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReplyCappedBodyTest, "SatisDash.Core.Reply.CappedBody", SatisDash::Test::Flags)

bool FReplyCappedBodyTest::RunTest(const FString& Parameters)
{
	{
		FCappedBody Body;
		feed(Body, MaxResponseBytes - 1);
		feed(Body, 1);
		TestFalse(TEXT("exactly 64 KB fits"), Body.Overflowed());
		TestFalse(TEXT("and is no error"), Body.IsError());
		TestEqual(TEXT("all of it kept"), Body.Take().Num(), MaxResponseBytes);
	}
	{
		FCappedBody Body;
		feed(Body, MaxResponseBytes);
		feed(Body, 1);
		TestTrue(TEXT("one byte more overflows"), Body.Overflowed());
		TestTrue(TEXT("and fails the request"), Body.IsError());
		TestTrue(TEXT("nothing past the cap is kept"), Body.Take().Num() <= MaxResponseBytes);
	}
	{
		FCappedBody Body;
		feed(Body, MaxResponseBytes * 4);
		TestTrue(TEXT("one huge chunk overflows at once"), Body.Overflowed());
		TestEqual(TEXT("and keeps nothing of it"), Body.Take().Num(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReplyMakeTest, "SatisDash.Core.Reply.Make", SatisDash::Test::Flags)

bool FReplyMakeTest::RunTest(const FString& Parameters)
{
	const FString Url = TEXT("https://example.com/agent/v1/snapshots");
	{
		FCappedBody Body;
		feed(Body, 3);
		EReplyRefusal Refusal = EReplyRefusal::Oversized;
		const FHttpReply Reply = MakeReply(answered(*Url), Url, Body, Refusal);
		TestTrue(TEXT("a direct answer is kept"), Refusal == EReplyRefusal::None);
		TestEqual(TEXT("its code"), Reply.Code, 200);
		SatisDash::Test::SameText(*this, TEXT("its content type"), Reply.ContentType, TEXT("application/json; charset=utf-8"));
		SatisDash::Test::SameText(*this, TEXT("its Retry-After"), Reply.RetryAfter, TEXT("7"));
		TestEqual(TEXT("its body"), Reply.Body.Num(), 3);
	}
	{
		// S3: curl followed a redirect; the answer came from somewhere else and is refused.
		FCappedBody Body;
		EReplyRefusal Refusal = EReplyRefusal::None;
		const FHttpReply Reply = MakeReply(answered(TEXT("https://example.com:4443/agent/v1/snapshots")), Url, Body, Refusal);
		TestTrue(TEXT("redirected"), Refusal == EReplyRefusal::Redirected);
		TestEqual(TEXT("is no answer"), Reply.Code, 0);
	}
	{
		// The URL compares exactly: a case change in the path is another resource.
		FCappedBody Body;
		EReplyRefusal Refusal = EReplyRefusal::None;
		MakeReply(answered(TEXT("https://example.com/agent/v1/Snapshots")), Url, Body, Refusal);
		TestTrue(TEXT("case-sensitive"), Refusal == EReplyRefusal::Redirected);
	}
	{
		FCappedBody Body;
		FRawReply Raw = answered(*Url);
		Raw.bProcessed = false;
		EReplyRefusal Refusal = EReplyRefusal::None;
		TestEqual(TEXT("not processed: no answer"), MakeReply(Raw, Url, Body, Refusal).Code, 0);
		TestTrue(TEXT("as NoAnswer"), Refusal == EReplyRefusal::NoAnswer);
	}
	{
		// S4: the cap wins even when UE calls the request processed.
		FCappedBody Body;
		feed(Body, MaxResponseBytes + 1);
		EReplyRefusal Refusal = EReplyRefusal::None;
		TestEqual(TEXT("oversized: no answer"), MakeReply(answered(*Url), Url, Body, Refusal).Code, 0);
		TestTrue(TEXT("as Oversized"), Refusal == EReplyRefusal::Oversized);
	}
	return true;
}
```

- [ ] **Step 3: Run them to see them fail**

Run the C++ test command with `--min 44`. Expected: the build fails on the missing `SatisDash/Reply.h`.

- [ ] **Step 4: Write `Ports.h`, `Reply.h` and `Reply.cpp`**

`Public/SatisDash/Ports.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Requests.h"
#include "SatisDash/Responses.h"
#include "SatisDash/Snapshot.h"

namespace SatisDash
{
	using FReplyHandler = TFunction<void(const FHttpReply&)>;

	/** How a request reaches the backend: UE's HTTP module in the game (SatisDashGame), a fake in the tests. */
	class ITransport
	{
	public:
		virtual ~ITransport() = default;
		/**
		 * Sends Spec and calls OnReply exactly once, later, on the game thread, and never from inside Send. A timeout, a
		 * refused connection, a TLS failure, a redirect or a body over MaxResponseBytes arrives as a Code 0 reply.
		 */
		virtual void Send(const FRequestSpec& Spec, FReplyHandler OnReply) = 0;
	};

	/** The game's state, read natively on the game thread (spec "Data flow"): a few lookups each, no I/O. */
	class IGameReader
	{
	public:
		virtual ~IGameReader() = default;
		/** Unset when the server's state can't be read (no game world, or no server subsystem in it yet). */
		virtual TOptional<FStatusReading> ReadStatus() = 0;
		/** FG.DSAutoPause; unset when it can't be read. */
		virtual TOptional<bool> ReadAutoPause() = 0;
		/** One reading per power circuit group; unset when there is no circuit subsystem. */
		virtual TOptional<TArray<FCircuitReading>> ReadPower() = 0;
		/** Sets FG.DSAutoPause and reads it back: true only when the game now reports bEnabled. */
		virtual bool ApplyAutoPause(bool bEnabled) = 0;
	};
}
```

`Public/SatisDash/Reply.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Serialization/Archive.h"
#include "SatisDash/Responses.h"

namespace SatisDash
{
	/**
	 * The response body as UE's HTTP thread streams it in (IHttpRequest::SetResponseBodyReceiveStream). Past
	 * MaxResponseBytes it keeps nothing more and sets an error, which ends the request while it downloads (S4).
	 */
	class SATISDASHCORE_API FCappedBody final : public FArchive
	{
	public:
		FCappedBody();
		virtual void Serialize(void* Data, int64 Length) override;
		bool Overflowed() const;
		TArray<uint8> Take();

	private:
		mutable FCriticalSection Guard;
		TArray<uint8> Bytes;
		bool bOverflowed = false;
	};

	/** What UE reported for one request, before the mod's rules (filled by SatisDashGame's transport). */
	struct FRawReply
	{
		/** The completion delegate's bProcessedSuccessfully, with a response present. */
		bool bProcessed = false;
		int32 Code = 0;
		FString ContentType;
		FString RetryAfter;
		/** IHttpBase::GetEffectiveURL(): where the answer came from after any redirect curl followed. */
		FString EffectiveUrl;
		/** LexToString of the failure reason, for the log line only. */
		FString Failure;
	};

	enum class EReplyRefusal : uint8 { None, NoAnswer, Redirected, Oversized };

	/**
	 * The reply the agent sees. Only a complete answer from exactly the URL asked is passed on; anything else is a
	 * Code 0 reply (S3, S4), which the agent retries with backoff, and OutRefusal says why.
	 */
	SATISDASHCORE_API FHttpReply MakeReply(const FRawReply& Raw, const FString& RequestUrl, FCappedBody& Body, EReplyRefusal& OutRefusal);
}
```

`Private/Reply.cpp`:

```cpp
#include "SatisDash/Reply.h"

#include "Misc/ScopeLock.h"

SatisDash::FCappedBody::FCappedBody()
{
	SetIsSaving(true);
}

void SatisDash::FCappedBody::Serialize(void* Data, int64 Length)
{
	FScopeLock Lock(&Guard);
	if (bOverflowed || Length < 0 || Bytes.Num() + Length > MaxResponseBytes)
	{
		bOverflowed = true;
		SetError();
		return;
	}
	Bytes.Append(static_cast<const uint8*>(Data), static_cast<int32>(Length));
}

bool SatisDash::FCappedBody::Overflowed() const
{
	FScopeLock Lock(&Guard);
	return bOverflowed;
}

TArray<uint8> SatisDash::FCappedBody::Take()
{
	FScopeLock Lock(&Guard);
	return MoveTemp(Bytes);
}

SatisDash::FHttpReply SatisDash::MakeReply(const FRawReply& Raw, const FString& RequestUrl, FCappedBody& Body, EReplyRefusal& OutRefusal)
{
	FHttpReply Reply;
	if (Body.Overflowed())
	{
		OutRefusal = EReplyRefusal::Oversized;
		return Reply;
	}
	if (!Raw.bProcessed)
	{
		OutRefusal = EReplyRefusal::NoAnswer;
		return Reply;
	}
	if (!Raw.EffectiveUrl.Equals(RequestUrl, ESearchCase::CaseSensitive))
	{
		OutRefusal = EReplyRefusal::Redirected;
		return Reply;
	}
	OutRefusal = EReplyRefusal::None;
	Reply.Code = Raw.Code;
	Reply.ContentType = Raw.ContentType;
	Reply.RetryAfter = Raw.RetryAfter;
	Reply.Body = Body.Take();
	return Reply;
}
```

Add `api-sources.md` rows, each looked up in the engine checkout: `Serialization/Archive.h` (`:1`), `FArchive` (`:1180`), `FArchive::Serialize` (`:1662`), `FArchive::SetError` (`:1714`, `using FArchiveState::SetError`), `FArchive::IsError` (`:1972`), `FArchive::SetIsSaving` (`:2195`), `HAL/CriticalSection.h`, `Misc/ScopeLock.h` and anything else the build or check-api names.

- [ ] **Step 5: Run the tests to see them pass**

Run the C++ test command with `--min 44`. Expected: 44 tests, all pass.

- [ ] **Step 6: Add the game module with the transport**

`SatisDashAgent.uplugin`, `Modules` becomes:

```json
"Modules": [
	{ "Name": "SatisDashCore", "Type": "Runtime", "LoadingPhase": "Default" },
	{ "Name": "SatisDashCoreTests", "Type": "Editor", "LoadingPhase": "Default" },
	{ "Name": "SatisDashGame", "Type": "Runtime", "LoadingPhase": "Default", "TargetAllowList": [ "Server", "Editor" ] }
],
```

`Source/SatisDashGame/SatisDashGame.Build.cs`:

```csharp
using UnrealBuildTool;

public class SatisDashGame : ModuleRules
{
	public SatisDashGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		// The thin layer over the game (spec "Runtime"): every decision is in SatisDashCore, behind Ports.h.
		PrivateDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "HTTP", "Json", "SatisDashCore", "FactoryGame", "FactoryDedicatedServer" });
	}
}
```

`Private/HttpTransport.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "SatisDash/Ports.h"
#include "SatisDash/RateLimit.h"
#include "SatisDash/Reply.h"

namespace SatisDash
{
	/** ITransport over UE's HTTP module (libcurl on Windows, ruling 1). Replies arrive on the game thread (ruling 6). */
	class FHttpTransport : public ITransport
	{
	public:
		virtual ~FHttpTransport() override;
		virtual void Send(const FRequestSpec& Spec, FReplyHandler OnReply) override;
		/** Cancels every request still out. Their handlers never run. For module shutdown. */
		void CancelAll();

	private:
		struct FPending
		{
			FString Url;
			FReplyHandler OnReply;
			TSharedPtr<FCappedBody> Body;
			FHttpRequestPtr Request;
			bool bAnswered = false;
		};
		void Finish(const TSharedRef<FPending>& Pending, const FRawReply& Raw);

		TArray<TSharedRef<FPending>> InFlight;
		/** Replaced by CancelAll: a callback holding the old one knows the transport no longer wants it. */
		TSharedRef<bool> Alive = MakeShared<bool>(true);
		FRateLimitedLog RefusalLog;
	};
}
```

`Private/HttpTransport.cpp`:

```cpp
#include "HttpTransport.h"

#include "Containers/Ticker.h"
#include "HAL/PlatformTime.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "SatisDash/Log.h"

SatisDash::FHttpTransport::~FHttpTransport()
{
	CancelAll();
}

void SatisDash::FHttpTransport::Send(const FRequestSpec& Spec, FReplyHandler OnReply)
{
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetVerb(Spec.Verb);
	Request->SetURL(Spec.Url);
	for (const TPair<FString, FString>& Header : Spec.Headers)
	{
		Request->SetHeader(Header.Key, Header.Value);
	}
	if (Spec.Body.Num() > 0)
	{
		Request->SetContent(Spec.Body);
	}
	Request->SetTimeout(Spec.TimeoutSeconds);

	const TSharedRef<FPending> Pending = MakeShared<FPending>();
	Pending->Url = Spec.Url;
	Pending->OnReply = MoveTemp(OnReply);
	Pending->Body = MakeShared<FCappedBody>();
	Pending->Request = Request;
	// Ruling 5: the cap holds while the body downloads.
	Request->SetResponseBodyReceiveStream(Pending->Body.ToSharedRef());

	const TWeakPtr<bool> alive = Alive;
	Request->OnProcessRequestComplete().BindLambda([this, alive, Pending](FHttpRequestPtr Done, FHttpResponsePtr Response, bool bProcessed)
	{
		if (!alive.IsValid())
		{
			return;
		}
		FRawReply Raw;
		Raw.bProcessed = bProcessed && Response.IsValid();
		if (Response.IsValid())
		{
			Raw.Code = Response->GetResponseCode();
			Raw.ContentType = Response->GetContentType();
			Raw.RetryAfter = Response->GetHeader(TEXT("Retry-After"));
			Raw.EffectiveUrl = Response->GetEffectiveURL();
		}
		Raw.Failure = Done.IsValid() ? FString(LexToString(Done->GetFailureReason())) : FString(TEXT("none"));
		Finish(Pending, Raw);
	});
	InFlight.Add(Pending);
	if (!Request->ProcessRequest())
	{
		// Whether UE also calls the delegate for a request that never started isn't documented; bAnswered lets only
		// the first answer through, and the ticker keeps the answer out of Send (Ports.h).
		FTSTicker::GetCoreTicker().AddTicker(TEXT("SatisDashNotStarted"), 0.0f, [this, alive, Pending](float)
		{
			if (alive.IsValid())
			{
				FRawReply Raw;
				Raw.Failure = TEXT("not started");
				Finish(Pending, Raw);
			}
			return false;
		});
	}
}

void SatisDash::FHttpTransport::Finish(const TSharedRef<FPending>& Pending, const FRawReply& Raw)
{
	if (Pending->bAnswered)
	{
		return;
	}
	Pending->bAnswered = true;
	InFlight.Remove(Pending);
	Pending->Request.Reset(); // the delegate holds Pending; this ends the cycle
	EReplyRefusal Refusal = EReplyRefusal::None;
	const FHttpReply Reply = MakeReply(Raw, Pending->Url, *Pending->Body, Refusal);
	const double Now = FPlatformTime::Seconds();
	// S9: a refused response and a TLS failure are logged, once per kind per ten minutes; never the URL's secret parts
	// (there are none: the secret is only in the Authorization header, S7) and never a header.
	if (Refusal == EReplyRefusal::Redirected && RefusalLog.ShouldLog(TEXT("redirect"), Now))
	{
		UE_LOG(LogSatisDash, Warning, TEXT("The backend redirected a request; its answer was refused (redirects are never accepted)"));
	}
	else if (Refusal == EReplyRefusal::Oversized && RefusalLog.ShouldLog(TEXT("oversized"), Now))
	{
		UE_LOG(LogSatisDash, Warning, TEXT("A backend response was over %d bytes; refused it"), MaxResponseBytes);
	}
	else if (Refusal == EReplyRefusal::NoAnswer && RefusalLog.ShouldLog(Raw.Failure, Now))
	{
		UE_LOG(LogSatisDash, Warning, TEXT("Couldn't reach the backend (%s)"), *Raw.Failure);
	}
	Pending->OnReply(Reply);
}

void SatisDash::FHttpTransport::CancelAll()
{
	Alive = MakeShared<bool>(true);
	const TArray<TSharedRef<FPending>> Out = MoveTemp(InFlight);
	InFlight.Reset();
	for (const TSharedRef<FPending>& Pending : Out)
	{
		Pending->bAnswered = true;
		if (Pending->Request.IsValid())
		{
			Pending->Request->OnProcessRequestComplete().Unbind();
			Pending->Request->CancelRequest();
			Pending->Request.Reset();
		}
	}
}
```

Rows to add, each looked up: `IHttpRequest::SetContent` (`IHttpRequest.h:195`), `IHttpRequest::SetTimeout` (`:317`), `IHttpRequest::SetResponseBodyReceiveStream` (`:256`), `IHttpRequest::CancelRequest` (`:385`), `IHttpBase::GetEffectiveURL` (`IHttpBase.h:104`), `IHttpBase::GetHeader` (`:135`), `IHttpBase::GetContentType` (`:149`), `module:FactoryDedicatedServer`'s row already exists; `module:SatisDashCore` is the plugin's own (check-api excuses `.Build.cs` names in the group).

- [ ] **Step 7: The probe wiring in the module (replaced in Task 6, ruling 9)**

`Private/SatisDashGameModule.cpp`:

```cpp
#include "Containers/Ticker.h"
#include "HAL/PlatformTime.h"
#include "HttpTransport.h"
#include "Misc/CoreMisc.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "SatisDash/Agent.h"
#include "SatisDash/Pusher.h"
#include "SatisDash/Snapshot.h"
#include "SatisDash/Version.h"

namespace
{
	double random01() { return double(FMath::FRand()); }
}

/** Task 1's wiring: enrols and pushes empty snapshots, so the HTTP probe can measure the transport. Task 6 replaces it. */
class FSatisDashGameModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// The editor loads this module for the unit-test build; only a dedicated server runs the agent.
		if (!IsRunningDedicatedServer())
		{
			return;
		}
		Agent = MakeUnique<SatisDash::FAgent>(FPaths::ProjectDir() / TEXT("Configs/SatisDashAgent.cfg"),
			FPaths::ProjectSavedDir() / TEXT("SatisDash/credential.json"), [] { return random01(); });
		Pusher = MakeUnique<SatisDash::FPusher>([] { return random01(); });
		Agent->Boot();
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("SatisDashProbe"), 0.5f, [this](float) { Tick(); return true; });
	}

	virtual void ShutdownModule() override
	{
		FTSTicker::RemoveTicker(TickHandle);
		Transport.CancelAll();
		Pusher.Reset();
		Agent.Reset();
	}

private:
	void Tick()
	{
		const double Now = FPlatformTime::Seconds();
		if (Now >= NextConfigAt)
		{
			NextConfigAt = Now + 60.0;
			Agent->CheckConfig();
		}
		if (TOptional<SatisDash::FRequestSpec> Enroll = Agent->StartEnroll(Now))
		{
			Transport.Send(*Enroll, [this](const SatisDash::FHttpReply& Reply) { Agent->FinishEnroll(Reply, FPlatformTime::Seconds()); });
		}
		const SatisDash::FCredential* Credential = Agent->GetCredential();
		if (Credential == nullptr)
		{
			return;
		}
		if (Now >= NextSnapshotAt)
		{
			NextSnapshotAt = Now + 5.0;
			SatisDash::FSnapshot Snapshot;
			Snapshot.AgentVersion = SatisDash::AgentVersion();
			Snapshot.ObservedAt = FDateTime::UtcNow();
			Pusher->Enqueue(SatisDash::SnapshotToJson(Snapshot));
		}
		if (bSending)
		{
			return;
		}
		if (TOptional<FString> Json = Pusher->StartSend(Now))
		{
			bSending = true;
			Transport.Send(SatisDash::BuildSnapshotRequest(Agent->GetConfig(), *Credential, *Json), [this](const SatisDash::FHttpReply& Reply)
			{
				bSending = false;
				if (Pusher->FinishSend(Reply, FPlatformTime::Seconds()).bHalt)
				{
					Agent->OnAuthRejected();
				}
			});
		}
	}

	SatisDash::FHttpTransport Transport;
	TUniquePtr<SatisDash::FAgent> Agent;
	TUniquePtr<SatisDash::FPusher> Pusher;
	FTSTicker::FDelegateHandle TickHandle;
	double NextConfigAt = 0;
	double NextSnapshotAt = 0;
	bool bSending = false;
};

IMPLEMENT_MODULE(FSatisDashGameModule, SatisDashGame)
```

Rows: `IsRunningDedicatedServer` (`Engine/Source/Runtime/Core/Public/Misc/CoreMisc.h:152`), `Misc/CoreMisc.h`, `FPaths::ProjectDir` (`Misc/Paths.h:251`), `FMath::FRand`, `TUniquePtr`, `MakeUnique`, `HAL/PlatformTime.h`, and any the build or check-api names.

Run the C++ test command (`--min 44`): the editor build now compiles `SatisDashGame` too, and nothing in it runs. Run `npm run harness -- check-api`: `0 missing, 0 bad rows`.

- [ ] **Step 8: `scripts/build-mod.ps1`**

Copy `scripts/build-spike.ps1`'s steps, with these differences: no `-Spike` parameter; `$plugin = 'SatisDashAgent'`; `$source = Join-Path $repo "mod\SatisDashAgent"`; it removes every `SatisMod*` folder from the project's `Mods` (and leaves `SatisDashAgent`, which it mirrors); its header comment cites build-spike.ps1 for the command line. After `Expand-Archive`, it refuses a package that carries test code (S14):

```powershell
$tests = @(Get-ChildItem $Out -Recurse -File | Where-Object { $_.Name -like '*SatisDashCoreTests*' })
if ($tests.Count -ne 0) { throw "the package carries test code: $($tests[0].Name)" }
```

The duplication with build-spike.ps1 (about 25 lines) is deliberate: build-spike's `ValidatePattern` and paths are what its passing runs used.

- [ ] **Step 9: Write the failing harness tests for the probe stub**

`harness/test/probe-stub.test.ts`:

```ts
import { connect } from "node:net";
import { afterEach, describe, expect, it } from "vitest";
import { FAKE_SECRET, parseClientHello, startHelloCapture, startProbeStub, type ProbeStub } from "../src/probe-stub";

/** A minimal ClientHello record: legacy version, empty session id, one suite, null compression, optional supported_versions. */
function hello(legacy: number, versions?: number[]): Buffer {
  const ext = versions
    ? Buffer.concat([Buffer.from([0x00, 0x2b]), u16(1 + versions.length * 2), Buffer.from([versions.length * 2]), ...versions.map(u16)])
    : Buffer.alloc(0);
  const body = Buffer.concat([u16(legacy), Buffer.alloc(32), Buffer.from([0]), u16(2), u16(0x1301), Buffer.from([1, 0]), u16(ext.length), ext]);
  const handshake = Buffer.concat([Buffer.from([0x01]), u24(body.length), body]);
  return Buffer.concat([Buffer.from([0x16, 0x03, 0x01]), u16(handshake.length), handshake]);
}
const u16 = (n: number) => Buffer.from([(n >> 8) & 0xff, n & 0xff]);
const u24 = (n: number) => Buffer.from([(n >> 16) & 0xff, (n >> 8) & 0xff, n & 0xff]);

describe("parseClientHello", () => {
  it("reads supported_versions and drops GREASE values", () => {
    expect(parseClientHello(hello(0x0303, [0x0a0a, 0x0304, 0x0303]))).toEqual({ legacyVersion: 0x0303, supportedVersions: [0x0304, 0x0303] });
  });
  it("falls back to the legacy version without the extension", () => {
    expect(parseClientHello(hello(0x0301))).toEqual({ legacyVersion: 0x0301, supportedVersions: [0x0301] });
  });
  it("refuses what isn't a ClientHello", () => {
    expect(parseClientHello(Buffer.from("GET / HTTP/1.1\r\n\r\n"))).toBeNull();
    expect(parseClientHello(hello(0x0303, [0x0304]).subarray(0, 20))).toBeNull();
  });
});

describe("startProbeStub", () => {
  let stub: ProbeStub | undefined;
  afterEach(async () => void (await stub?.close()));

  it("enrols, answers snapshots, then redirects them to the other port, recording only what's safe", async () => {
    stub = await startProbeStub();
    const enrol = await fetch(`${stub.backendUrl}/agent/v1/enroll`, { method: "POST", headers: { "user-agent": "satisfactory-dash-mod/0.1.0" }, body: "{}" });
    expect(enrol.status).toBe(201);
    expect(((await enrol.json()) as { agentSecret: string }).agentSecret).toBe(FAKE_SECRET);
    const ok = await fetch(`${stub.backendUrl}/agent/v1/snapshots`, { method: "POST", headers: { authorization: `Bearer ${FAKE_SECRET}` }, body: '{"observedAt":"2026-10-20T00:00:00.000Z"}' });
    expect(ok.status).toBe(200);
    stub.startRedirecting();
    const moved = await fetch(`${stub.backendUrl}/agent/v1/snapshots`, { method: "POST", body: "{}", redirect: "manual" });
    expect(moved.status).toBe(302);
    expect(moved.headers.get("location")).toBe(`${stub.elsewhereUrl}/agent/v1/snapshots`);
    await fetch(`${stub.elsewhereUrl}/agent/v1/snapshots`, { method: "POST", headers: { authorization: "Bearer x" }, body: "{}" });
    expect(stub.requests.map((r) => [r.port, r.path, r.authorization])).toEqual([
      ["backend", "/agent/v1/enroll", false],
      ["backend", "/agent/v1/snapshots", true],
      ["backend", "/agent/v1/snapshots", false],
      ["elsewhere", "/agent/v1/snapshots", true],
    ]);
    expect(stub.requests[1]!.observedAt).toBe("2026-10-20T00:00:00.000Z");
    expect(JSON.stringify(stub.requests)).not.toContain(FAKE_SECRET);
  });
});

describe("startHelloCapture", () => {
  it("records each ClientHello and closes the connection", async () => {
    const capture = await startHelloCapture();
    const socket = connect(Number(new URL(capture.url).port), "127.0.0.1");
    await new Promise<void>((r) => socket.on("connect", () => r()));
    socket.write(hello(0x0303, [0x0304, 0x0303]));
    await new Promise<void>((r) => socket.on("close", () => r()));
    expect(capture.hellos.map((h) => h.supportedVersions)).toEqual([[0x0304, 0x0303]]);
    await capture.close();
  });
});
```

Run: `cd harness; npx vitest run test/probe-stub.test.ts --reporter=dot`. Expected: FAIL, `../src/probe-stub` not found.

- [ ] **Step 10: Write `harness/src/probe-stub.ts`**

```ts
import { execFile } from "node:child_process";
import { readFileSync } from "node:fs";
import { createServer as createHttp, type IncomingMessage, type Server, type ServerResponse } from "node:http";
import { type AddressInfo, createServer as createTcp, type Server as TcpServer } from "node:net";
import { join } from "node:path";
import { createServer as createTls } from "node:tls";

/** The contract's secret shape (43 base64url characters). A fixed fake: never a real credential. */
export const FAKE_SECRET = `${"a".repeat(42)}_`;
const CADENCE = { statusSeconds: 5, powerSeconds: 5, factorySeconds: 30 };
const MAX_BODY = 1024 * 1024;

export interface ProbeRequest {
  at: string;
  port: "backend" | "elsewhere";
  method: string;
  path: string;
  userAgent: string | null;
  /** Whether an Authorization header arrived; its value is never kept. */
  authorization: boolean;
  bodyBytes: number;
  /** A snapshot body's observedAt, so a re-sent snapshot shows (Task 1's normal phase). */
  observedAt: string | null;
}
export interface ProbeStub {
  backendUrl: string;
  elsewhereUrl: string;
  requests: ProbeRequest[];
  startRedirecting(): void;
  redirectingSince(): string | null;
  close(): Promise<void>;
}

const listen = (server: Server | TcpServer) =>
  new Promise<number>((resolve) => server.listen(0, "127.0.0.1", () => resolve((server.address() as AddressInfo).port)));
const closeServer = (server: Server | TcpServer) => new Promise<void>((resolve) => server.close(() => resolve()));

function readBody(req: IncomingMessage): Promise<Buffer> {
  return new Promise((resolve) => {
    const chunks: Buffer[] = [];
    let size = 0;
    req.on("data", (c: Buffer) => {
      size += c.length;
      if (size <= MAX_BODY) chunks.push(c);
    });
    req.on("end", () => resolve(Buffer.concat(chunks)));
  });
}

function observedAtOf(body: Buffer): string | null {
  try {
    const v = (JSON.parse(body.toString("utf8")) as { observedAt?: unknown }).observedAt;
    return typeof v === "string" ? v : null;
  } catch {
    return null;
  }
}

const json = (res: ServerResponse, status: number, body: unknown) => {
  res.writeHead(status, { "content-type": "application/json; charset=utf-8" });
  res.end(JSON.stringify(body));
};

/** Task 1's stub backend: answers enrol and snapshots, then on cue redirects snapshots to a second port. */
export async function startProbeStub(now: () => Date = () => new Date()): Promise<ProbeStub> {
  const requests: ProbeRequest[] = [];
  let redirecting: string | null = null;
  const record = (port: ProbeRequest["port"], req: IncomingMessage, body: Buffer) =>
    requests.push({
      at: now().toISOString(),
      port,
      method: req.method ?? "",
      path: req.url ?? "",
      userAgent: req.headers["user-agent"] ?? null,
      authorization: req.headers.authorization !== undefined,
      bodyBytes: body.length,
      observedAt: req.url === "/agent/v1/snapshots" ? observedAtOf(body) : null,
    });

  const elsewhere = createHttp(async (req, res) => {
    record("elsewhere", req, await readBody(req));
    // A valid answer: if the mod accepted a redirected answer, its queue would move on and the next request would show it.
    json(res, 200, { cadence: CADENCE, commandsPending: false });
  });
  const elsewherePort = await listen(elsewhere);
  const elsewhereUrl = `http://127.0.0.1:${elsewherePort}`;

  const backend = createHttp(async (req, res) => {
    const body = await readBody(req);
    record("backend", req, body);
    if (req.method === "POST" && req.url === "/agent/v1/enroll") return json(res, 201, { agentSecret: FAKE_SECRET, serverId: "probe", cadence: CADENCE });
    if (req.method === "POST" && req.url === "/agent/v1/snapshots") {
      if (redirecting) {
        res.writeHead(302, { location: `${elsewhereUrl}/agent/v1/snapshots` });
        return res.end();
      }
      return json(res, 200, { cadence: CADENCE, commandsPending: false });
    }
    json(res, 404, { error: "not_found" });
  });
  const backendPort = await listen(backend);

  return {
    backendUrl: `http://127.0.0.1:${backendPort}`,
    elsewhereUrl,
    requests,
    startRedirecting: () => void (redirecting ??= now().toISOString()),
    redirectingSince: () => redirecting,
    close: async () => void (await Promise.all([closeServer(backend), closeServer(elsewhere)])),
  };
}

/** TLS record type 22 (handshake), message type 1 (ClientHello); extension 43 is supported_versions (RFC 8446 4.2.1). */
export function parseClientHello(buf: Buffer): { legacyVersion: number; supportedVersions: number[] } | null {
  try {
    if (buf[0] !== 0x16 || buf[5] !== 0x01) return null;
    let at = 9;
    const legacyVersion = buf.readUInt16BE(at);
    at += 2 + 32;
    at += 1 + buf[at]!; // session id
    at += 2 + buf.readUInt16BE(at); // cipher suites
    at += 1 + buf[at]!; // compression methods
    if (at >= buf.length) return { legacyVersion, supportedVersions: [legacyVersion] };
    const end = at + 2 + buf.readUInt16BE(at);
    if (end > buf.length) return null;
    at += 2;
    while (at + 4 <= end) {
      const type = buf.readUInt16BE(at);
      const len = buf.readUInt16BE(at + 2);
      if (type === 0x002b) {
        const versions: number[] = [];
        for (let i = at + 5; i + 1 < at + 4 + len; i += 2) versions.push(buf.readUInt16BE(i));
        const real = versions.filter((v) => (v & 0x0f0f) !== 0x0a0a); // GREASE (RFC 8701)
        return { legacyVersion, supportedVersions: real };
      }
      at += 4 + len;
    }
    return { legacyVersion, supportedVersions: [legacyVersion] };
  } catch {
    return null;
  }
}

export interface HelloCapture { url: string; hellos: { at: string; legacyVersion: number; supportedVersions: number[] }[]; close(): Promise<void> }

/** S2: a plain TCP listener that reads the first TLS record, keeps the versions offered, and hangs up. */
export async function startHelloCapture(now: () => Date = () => new Date()): Promise<HelloCapture> {
  const hellos: HelloCapture["hellos"] = [];
  const server = createTcp((socket) => {
    let buf = Buffer.alloc(0);
    socket.on("data", (chunk: Buffer) => {
      buf = Buffer.concat([buf, chunk]);
      const parsed = buf.length >= 5 && buf.length >= 5 + buf.readUInt16BE(3) ? parseClientHello(buf) : null;
      if (parsed) {
        hellos.push({ at: now().toISOString(), ...parsed });
        socket.destroy();
      } else if (buf.length > 16 * 1024) socket.destroy();
    });
    socket.on("error", () => undefined);
  });
  const port = await listen(server);
  return { url: `https://127.0.0.1:${port}`, hellos, close: () => closeServer(server) };
}

export interface UntrustedTls { url: string; handshakeFailures: number; requests: number; close(): Promise<void> }

/**
 * S1 and S9: TLS with a certificate for another name, issued by nobody the mod trusts. The handshake must fail and no
 * request may arrive. The certificate is made per run with openssl (Git for Windows ships it) into a temp dir.
 */
export async function startUntrustedTls(dir: string): Promise<UntrustedTls> {
  const key = join(dir, "untrusted-key.pem");
  const cert = join(dir, "untrusted-cert.pem");
  await new Promise<void>((resolve, reject) =>
    execFile(
      "openssl",
      ["req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", key, "-out", cert, "-days", "1", "-subj", "/CN=wrong.invalid"],
      { env: { ...process.env, MSYS2_ARG_CONV_EXCL: "*" } },
      (err) => (err ? reject(new Error(`openssl failed (is it on PATH?): ${err.message}`)) : resolve()),
    ),
  );
  const state = { handshakeFailures: 0, requests: 0 };
  const server = createTls({ key: readFileSync(key), cert: readFileSync(cert) }, (socket) => {
    socket.on("data", () => void state.requests++);
    socket.on("error", () => undefined);
  });
  server.on("tlsClientError", () => void state.handshakeFailures++);
  const port = await listen(server);
  return {
    url: `https://127.0.0.1:${port}`,
    get handshakeFailures() { return state.handshakeFailures; },
    get requests() { return state.requests; },
    close: () => closeServer(server),
  };
}
```

Run the probe-stub tests. Expected: PASS.

- [ ] **Step 11: The core-run framework and its tests**

`harness/src/evidence.ts`: extract the body of `writeEvidence` after `ResultSchema.parse` into

```ts
/** Writes result.json and the captures under <dir>/<name>-<startedAt>, redacted (shared by spikes and core runs). */
export function writeBundle(dir: string, name: string, startedAt: string, result: unknown, captures: Record<string, unknown>): string
```

so `writeEvidence` becomes `writeBundle(dir, parsed.spike, parsed.startedAt, parsed, captures)`. Behaviour is unchanged (the existing evidence tests stay green).

`harness/src/core-run.ts`:

```ts
import { existsSync, readFileSync, rmSync } from "node:fs";
import { join } from "node:path";
import { z } from "zod";
import { type RunnerConfig } from "./config";
import { redact, writeBundle } from "./evidence";
import { modConfigPath, writeModConfig } from "./mod-config";
import { RefusalError, type RunDeps } from "./run";
import type { ServerProcess } from "./server-control";
import { toolchainMismatches } from "./toolchain";
import type { Verdict } from "./verdict";
import { versionLines } from "./log-watch";

export const CORE_RUN_IDS = ["http-probe", "enrol", "revoke", "command", "outage", "tick-cost", "parity"] as const;
export type CoreRunId = (typeof CORE_RUN_IDS)[number];
export const MOD = "SatisDashAgent";

/** The credential the mod writes (spec "Shape"; FPaths::ProjectSavedDir() on a dedicated server is FactoryGame/Saved). */
export const credentialPath = (serverDir: string) => join(serverDir, "FactoryGame", "Saved", "SatisDash", "credential.json");

export interface CoreEvidence {
  /** Every LogSatisDash line of the boot. */
  lines: string[];
  errorLines: string[];
  /** S8: whether any value that must stay secret shows up anywhere in the server log. secret is null when no credential was written. */
  leaks: { code: boolean; secret: boolean | null; bearer: boolean };
  captures: Record<string, unknown>;
}

export interface CoreContext {
  deps: CoreDeps;
  proc: ServerProcess;
  offset: number;
  /** Writes SatisDashAgent.cfg and remembers the code as a value the log must never carry. */
  writeConfig(values: { backendUrl: string; enrollmentCode: string }): void;
  /** Reads the secret from credential.json now, so it is searched for in the log even if a halt deletes the file later. */
  noteSecret(): void;
  credentialExists(): boolean;
  /** Polls check every second until it returns a value, or fails the run naming what. */
  waitFor<T>(what: string, check: () => Promise<T | undefined> | T | undefined, timeoutMs: number): Promise<T>;
  captures: Record<string, unknown>;
  /** Auto-pause as GetServerOptions said before the run touched it; put back at the end. */
  rememberAutoPause(value: boolean): void;
}

export interface CoreRunSpec {
  requirements: string[];
  /** Needs the backend host (ruling 10). */
  backend: boolean;
  /** Loads the staged save (satis-runner only, savesConflict). */
  world: boolean;
  drive(ctx: CoreContext): Promise<void>;
  evaluate(e: CoreEvidence): Verdict;
}

export type CoreDeps = Pick<RunDeps,
  "config" | "toolchain" | "outDir" | "checkApi" | "serverAnswering" | "install" | "removeSpikeMods" | "start" | "stop" |
  "waitHealthy" | "resetLog" | "readLogSince" | "loadWorld" | "savesConflict" | "readAutoPause" | "setAutoPause" | "sampleTicks" | "now" | "env"> & {
  /** Runs scripts/build-mod.ps1 and returns the unpacked plugin's folder. */
  buildMod(): Promise<string>;
  sleep(ms: number): Promise<void>;
  /** Present when the runner config names the backend host's file (Task 6). */
  backendHost?: () => Promise<import("./backend-host").HostClient>;
};

const ResultSchema = z.strictObject({
  run: z.enum(CORE_RUN_IDS),
  requirements: z.array(z.string().min(1)).min(1),
  pass: z.boolean(),
  reasons: z.array(z.string()).min(1),
  startedAt: z.iso.datetime(),
  endedAt: z.iso.datetime(),
  toolchain: z.strictObject({ required: z.strictObject({ engine: z.string(), sml: z.string() }), declared: z.strictObject({ engine: z.string(), sml: z.string() }) }),
  versionLines: z.array(z.string()),
  modLines: z.array(z.string()),
  harnessCommit: z.string().min(1),
  runUrl: z.string().nullable(),
});
export type CoreResult = z.infer<typeof ResultSchema>;

export function writeCoreEvidence(dir: string, result: CoreResult, captures: Record<string, unknown>): string {
  const parsed = ResultSchema.parse(JSON.parse(redact(JSON.stringify(result))));
  return writeBundle(dir, `core-${parsed.run}`, parsed.startedAt, parsed, captures);
}

/** Lines of LogSatisDash: "[time][frame]LogSatisDash: Display: text". */
export const satisDashLines = (log: string) => log.split(/\r?\n/).filter((l) => /\bLogSatisDash: /.test(l));

/** The rules every core run is judged by, besides its own pass.ts (Global Constraints). */
export function commonReasons(e: CoreEvidence): string[] {
  const reasons: string[] = [];
  if (e.leaks.code) reasons.push("S8: the server log carries an enrolment code");
  if (e.leaks.secret) reasons.push("S8: the server log carries the agent secret");
  if (e.leaks.bearer) reasons.push("S8: the server log carries the string Bearer");
  if (e.errorLines.length) reasons.push(`the mod logged at Error: ${e.errorLines[0]}`);
  return reasons;
}

const message = (e: unknown) => (e instanceof Error ? e.message : String(e));

export async function runCore(id: CoreRunId, spec: CoreRunSpec, deps: CoreDeps): Promise<{ result: CoreResult; dir: string }> {
  const mismatches = toolchainMismatches(deps.toolchain, deps.config.installed);
  if (mismatches.length) throw new RefusalError(`toolchain mismatch: ${mismatches.join("; ")}`);
  const api = deps.checkApi();
  if (api.missing.length || api.badRows.length) throw new RefusalError(`check-api failed: ${[...api.badRows, ...api.missing.map((u) => `${u.path}:${u.line} ${u.name}`)].join(", ")}`);
  if (spec.backend && !deps.backendHost) throw new RefusalError(`${id} needs the backend host (backendHost in the runner config; docs/runner.md)`);
  if (spec.world) {
    if (!deps.loadWorld || !deps.readAutoPause || !deps.setAutoPause || !deps.savesConflict) throw new RefusalError(`${id} needs s3World in the runner config`);
    const shared = deps.savesConflict();
    if (shared.length) throw new RefusalError(`another server keeps its settings in this account's saves folder (${shared.join(", ")}): ${id} runs only as satis-runner`);
  }
  if (await deps.serverAnswering()) throw new RefusalError(`something already answers at ${deps.config.apiBaseUrl}: stop the other server first`);

  const startedAt = deps.now().toISOString();
  let pluginDir: string;
  try {
    pluginDir = await deps.buildMod();
  } catch (e) {
    throw new RefusalError(message(e));
  }
  deps.removeSpikeMods();
  const cfg = modConfigPath(deps.config.serverDir, MOD);
  const cred = credentialPath(deps.config.serverDir);
  const scrub = () => {
    rmSync(cfg, { force: true });
    rmSync(cred, { force: true });
    rmSync(`${cred}.tmp`, { force: true });
  };
  scrub();

  const codes: string[] = [];
  const secrets: string[] = [];
  const captures: Record<string, unknown> = {};
  const reasons: string[] = [];
  let autoPauseBefore: boolean | undefined;
  let log = "";
  // The secret is read only to search the log for it; it is never written anywhere.
  const noteSecret = () => {
    try {
      const s = (JSON.parse(readFileSync(cred, "utf8")) as { agentSecret?: unknown }).agentSecret;
      if (typeof s === "string" && s && !secrets.includes(s)) secrets.push(s);
    } catch {
      // no file, or not yet valid: nothing to note
    }
  };
  try {
    deps.install(pluginDir, MOD);
    const offset = deps.resetLog();
    const proc = deps.start();
    try {
      await deps.waitHealthy();
      const ctx: CoreContext = {
        deps, proc, offset, captures,
        writeConfig: (v) => {
          if (v.enrollmentCode) codes.push(v.enrollmentCode);
          writeModConfig(deps.config.serverDir, MOD, v);
        },
        noteSecret,
        credentialExists: () => existsSync(cred),
        waitFor: async (what, check, timeoutMs) => {
          const end = Date.now() + timeoutMs;
          for (;;) {
            const v = await check();
            if (v !== undefined) return v;
            if (Date.now() > end) throw new Error(`timed out waiting for ${what}`);
            await deps.sleep(1000);
          }
        },
        rememberAutoPause: (v) => void (autoPauseBefore ??= v),
      };
      await spec.drive(ctx);
    } catch (e) {
      reasons.push(message(e));
    } finally {
      if (autoPauseBefore !== undefined) {
        try {
          await deps.setAutoPause!(autoPauseBefore);
        } catch (e) {
          reasons.push(`restoring auto-pause: ${message(e)}`);
        }
      }
      await deps.stop(proc);
      log = deps.readLogSince(offset);
    }
  } finally {
    // The file goes with the config whatever happened.
    noteSecret();
    scrub();
  }

  const lines = satisDashLines(log);
  const evidence: CoreEvidence = {
    lines,
    errorLines: lines.filter((l) => /LogSatisDash: Error:/.test(l)),
    leaks: { code: codes.some((c) => log.includes(c)), secret: secrets.length ? secrets.some((s) => log.includes(s)) : null, bearer: /Bearer/.test(log) },
    captures,
  };
  const verdict = spec.evaluate(evidence);
  const allReasons = [...reasons, ...commonReasons(evidence), ...verdict.reasons];
  const result: CoreResult = {
    run: id,
    requirements: spec.requirements,
    pass: reasons.length === 0 && commonReasons(evidence).length === 0 && verdict.pass,
    reasons: allReasons,
    startedAt,
    endedAt: deps.now().toISOString(),
    toolchain: { required: { engine: deps.toolchain.engine, sml: deps.toolchain.sml }, declared: deps.config.installed },
    versionLines: versionLines(log),
    modLines: lines.slice(0, 200),
    harnessCommit: deps.env.commit,
    runUrl: deps.env.runUrl,
  };
  return { result, dir: writeCoreEvidence(deps.outDir, result, { ...captures, "leaks.json": evidence.leaks }) };
}
```

Add `realCoreDeps(config, toolchain, outDir, repoRoot): CoreDeps` to `core-run.ts`: it spreads the matching fields of `realDeps(...)` (`run.ts:389`), adds `sleep`, and `buildMod` running `pwsh -NoProfile -File scripts/build-mod.ps1 -Project <modTest.project> -Engine <modTest.engine> -Out <outDir>/build/SatisDashAgent` (refusing when `modTest` is unset), resolving to that folder.

`harness/test/core-run.test.ts` builds a `CoreDeps` of fakes (as `harness/test/run.test.ts` does for `RunDeps`) and pins:
- a run whose `drive` throws still stops the server, deletes `SatisDashAgent.cfg`, `credential.json` and `credential.json.tmp`, and restores auto-pause;
- the secret read from `credential.json` is searched in the log, including one noted with `noteSecret()` before the file was deleted, and `leaks.json` holds booleans only (assert the written bundle contains neither the secret nor the code);
- a log line `LogSatisDash: Error: x` fails the run; `Bearer` anywhere in the log fails it;
- a `world` spec refuses when `savesConflict` returns a name; a `backend` spec refuses without `backendHost`.

- [ ] **Step 12: The http-probe run and its pass conditions**

`harness/src/core-runs/http-probe.ts`:

```ts
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import * as Pass from "../../../runs/http-probe/pass";
import type { CoreRunSpec } from "../core-run";
import { randomCode } from "../backend";
import { startHelloCapture, startProbeStub, startUntrustedTls } from "../probe-stub";

const MIN = 60_000;

export const httpProbe: CoreRunSpec = {
  requirements: Pass.requirements,
  backend: false,
  world: false,
  evaluate: Pass.evaluate,
  async drive(ctx) {
    const stub = await startProbeStub();
    const hello = await startHelloCapture();
    const untrusted = await startUntrustedTls(mkdtempSync(join(tmpdir(), "satis-probe-")));
    try {
      // 1. Normal: enrol, then three snapshots answered 200 by the URL asked.
      ctx.writeConfig({ backendUrl: stub.backendUrl, enrollmentCode: randomCode() });
      await ctx.waitFor("an enrol and three snapshots", () =>
        stub.requests.filter((r) => r.path === "/agent/v1/snapshots").length >= 3 ? true : undefined, 3 * MIN);
      // 2. S3: snapshots now get 302 to the other port; the mod retries with backoff (1, 2, 4, 8, 16 s ...).
      stub.startRedirecting();
      await ctx.deps.sleep(MIN);
      // 3. S2: a new backend URL and code make the mod enrol there at the next minute's config check.
      ctx.writeConfig({ backendUrl: hello.url, enrollmentCode: randomCode() });
      await ctx.waitFor("a ClientHello", () => (hello.hellos.length ? true : undefined), 2 * MIN);
      // 4. S1 and S9: a certificate nobody trusts, for another name.
      ctx.writeConfig({ backendUrl: untrusted.url, enrollmentCode: randomCode() });
      await ctx.waitFor("a refused handshake", () => (untrusted.handshakeFailures ? true : undefined), 2 * MIN);
      await ctx.deps.sleep(10_000);
    } finally {
      ctx.captures["probe.json"] = {
        redirectingSince: stub.redirectingSince(),
        requests: stub.requests,
        hellos: hello.hellos,
        untrusted: { handshakeFailures: untrusted.handshakeFailures, requests: untrusted.requests },
      };
      await Promise.all([stub.close(), hello.close(), untrusted.close()]);
    }
  },
};
```

`runs/http-probe/pass.ts`:

```ts
// The HTTP probe (Plan 2B Task 1): S2, S3, User-Agent, and S1/S9 against an untrusted certificate. Gate A.
// Backend TLS minimum, from the dash session: <record the reply and its date here, Task 1 Step 1>.
import type { CoreEvidence } from "../../harness/src/core-run";
import type { ProbeRequest } from "../../harness/src/probe-stub";
import type { Verdict } from "../../harness/src/verdict";

export const requirements = ["S1", "S2", "S3", "S9", "user-agent"];
export const USER_AGENT = "satisfactory-dash-mod/0.1.0";
const TLS12 = 0x0303;

interface Probe {
  redirectingSince: string | null;
  requests: ProbeRequest[];
  hellos: { supportedVersions: number[] }[];
  untrusted: { handshakeFailures: number; requests: number };
}

export function evaluate(e: CoreEvidence): Verdict {
  const p = e.captures["probe.json"] as Probe | undefined;
  if (!p) return { pass: false, reasons: ["no probe capture"] };
  const reasons: string[] = [];
  const atBackend = p.requests.filter((r) => r.port === "backend");
  const elsewhere = p.requests.filter((r) => r.port === "elsewhere");

  const wrongUa = atBackend.filter((r) => r.userAgent !== USER_AGENT);
  if (!atBackend.length) reasons.push("the stub received nothing");
  if (wrongUa.length) reasons.push(`User-Agent: expected ${USER_AGENT}, got ${JSON.stringify(wrongUa[0]!.userAgent)}`);

  // Before the redirects, each answered snapshot is a new one: a refused (re-sent) answer would repeat its observedAt.
  const since = p.redirectingSince;
  const normal = atBackend.filter((r) => r.path === "/agent/v1/snapshots" && (since === null || r.at < since)).slice(0, 3);
  if (normal.length < 3) reasons.push(`only ${normal.length} snapshots before the redirects began`);
  else if (new Set(normal.map((r) => r.observedAt)).size !== 3) reasons.push("a snapshot answered 200 by the URL asked was sent again: the transport refused a direct answer");

  if (elsewhere.length) {
    const withAuth = elsewhere.filter((r) => r.authorization).length;
    reasons.push(`S3: the redirect target received ${elsewhere.length} request(s), ${withAuth} with an Authorization header`);
  }
  if (since !== null && !e.lines.some((l) => /The backend redirected a request; its answer was refused/.test(l))) {
    reasons.push("S9: no line saying a redirected answer was refused");
  }

  if (!p.hellos.length) reasons.push("S2: no ClientHello arrived");
  else {
    const lowest = Math.min(...p.hellos.flatMap((h) => h.supportedVersions));
    if (lowest < TLS12) reasons.push(`S2: the mod offers TLS below 1.2 (0x${lowest.toString(16)})`);
  }

  if (!p.untrusted.handshakeFailures) reasons.push("S1: the untrusted certificate's handshake never failed");
  if (p.untrusted.requests) reasons.push("S1: data arrived over a certificate nobody trusts: verification is off");
  if (!e.lines.some((l) => /Couldn't reach the backend \(/.test(l))) reasons.push("S9: no line for the failed TLS connection");

  return reasons.length
    ? { pass: false, reasons }
    : { pass: true, reasons: ["User-Agent kept; redirect target contacted by nobody; TLS 1.2 or newer offered; an untrusted certificate refused"] };
}
```

`harness/test/http-probe-pass.test.ts` covers `evaluate` with hand-built `CoreEvidence`: a clean probe passes; each of a wrong User-Agent, a request at `elsewhere` (the reason names how many carried Authorization), a repeated `observedAt` in the normal phase, a hello offering `0x0301`, `untrusted.requests > 0`, and a missing redirect line fails with its own reason.

Register the run: `harness/src/core-runs/index.ts` exports `CORE_RUNS: Partial<Record<CoreRunId, CoreRunSpec>> = { "http-probe": httpProbe }`. `cli.ts` gains `core-run <id> --out <dir>`, mirroring its `run` case (`SATIS_HARNESS_CONFIG`, `RefusalError` → exit 3, prints `<id>: PASS|FAIL`, the reasons and the bundle dir) and calling `runCore(id, CORE_RUNS[id]!, realCoreDeps(...))`.

`.github/workflows/core-run.yml`: a copy of `spike.yml`'s `run` job (same pinned actions, `environment: ue-runner`, `concurrency: ue-runner`, `workflow_dispatch` only, no `pull_request` and no `push`), with a `run` choice input whose options are the runs that need no backend: `[http-probe]`, and `npm run harness -- core-run $env:RUN --out "$env:RUNNER_TEMP/core-run"` followed by the same artifact upload step as `spike.yml`. Extend `harness/test/workflows.test.ts` so the new file is held to the same rules.

`docs/runner.md`: a "Core runs" section: what `core-run` does, that `http-probe` needs `openssl` on the runner account's PATH, and that every core run builds the mod with `build-mod.ps1` (so `modTest` must be set). `CLAUDE.md` rule 3 becomes "Never weaken a pass condition (`spikes/S#/pass.ts`, `runs/*/pass.ts`)".

- [ ] **Step 13: Run everything**

C++ (`--min 44`), `npm test`, `npm run typecheck`, `npm run harness -- check-api`. Expected: all green, 44 C++ tests.

- [ ] **Step 14: Commit, PR, and the probe run**

```bash
git -c core.safecrlf=false add mod scripts/build-mod.ps1 harness runs .github/workflows/core-run.yml api-sources.md docs/runner.md CLAUDE.md
git commit -m "Core mod Plan 2B Task 1: the HTTP transport, the game module and the HTTP probe"
```

Open the PR. Because `core-run.yml` must be on `main` before it can be dispatched (2A, "How the tests run"), this task's PR merges after its `mod-test.yml` run passes, then the probe is dispatched from `main`: `gh workflow run core-run.yml --ref main -f run=http-probe`. The owner approves the deployment.

- [ ] **Step 15: Gate A**

Report the probe's verdict and its bundle to the owner (and the coordinator, per the user-level rules), quoting each reason.
- **PASS:** continue with Task 2.
- **S3 fails** (the redirect target received a request): **stop.** Report how many requests reached it and whether they carried `Authorization`, and put these options to the owner:
  1. Amend S3 to "a redirected answer is refused and the Bearer header never reaches another origin", relying on libcurl dropping `Authorization` across origins, which this run measured, plus the effective-URL refusal (ruling 2). Only offerable if no request at the target carried `Authorization`.
  2. Cancel on the 3xx status: bind `OnStatusCodeReceived` (`IHttpRequest.h:380`) with `CompleteOnHttpThread` (`:418`) and call `CancelRequest` there. It may still lose the race with curl's follow; it would need its own probe run.
  3. Leave the engine's HTTP for the agent's calls (a socket-level client): a large change, and out of v1's scope.
- **S2 fails:** report the versions offered and the backend's minimum from Step 1; the owner decides whether the backend's floor is enough (spec S2: "the backend enforces the floor").
- **User-Agent fails:** report the header received; it's a contract-adjacent change, so ask the dash session whether the backend keys on it before changing anything.

---

### Task 2: Game values: circuit groups, the auto-pause option, tick health

**Files:**
- Create: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/GameValues.h`, `Private/GameValues.cpp`
- Create: `mod/SatisDashAgent/Source/SatisDashCoreTests/Private/GameValuesTest.cpp`
- Modify: `mod/SatisDashAgent/Source/SatisDashCoreTests/Private/TestUtil.h` (verbosity capture, `NoErrorLines`), `.github/workflows/mod-test.yml` (`--min`)

**Interfaces:**
- Consumes: `FCircuitReading`, `MaxCircuits` (Snapshot.h).
- Produces:
  - `struct SatisDash::FPowerCircuitSample { int32 CircuitId, GroupId; double ProducedMW, ConsumedMW, CapacityMW, MaxConsumptionMW; bool bFuseTriggered, bHasBatteries; double BatteryStoreMWh, BatteryCapacityMWh, BatteryInputMW; }`
  - `inline constexpr int32 SatisDash::MaxCircuitIdScanned = 10000;`
  - `TArray<FCircuitReading> SatisDash::GroupCircuits(const TArray<FPowerCircuitSample>& Samples)`
  - `inline const TCHAR* SatisDash::AutoPauseOption = TEXT("FG.DSAutoPause");`
  - `TOptional<bool> SatisDash::ParseAutoPauseOption(const FString* Value)`, `FString SatisDash::AutoPauseOptionValue(bool bEnabled)`
  - `bool SatisDash::IsTickHealthy(const FString& Health)`
  - test helpers: `FLogCapture::Verbosities()` (one per line), `bool SatisDash::Test::NoErrorLines(FAutomationTestBase&, FLogCapture&)`

- [ ] **Step 1: Write the failing tests**

`GameValuesTest.cpp`:

```cpp
#include "SatisDash/GameValues.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	FPowerCircuitSample circuit(int32 Id, int32 Group, double Produced)
	{
		FPowerCircuitSample S;
		S.CircuitId = Id;
		S.GroupId = Group;
		S.ProducedMW = Produced;
		S.ConsumedMW = Produced / 2;
		S.CapacityMW = Produced * 2;
		S.MaxConsumptionMW = Produced;
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameValuesGroupsTest, "SatisDash.Core.GameValues.Groups", SatisDash::Test::Flags)

bool FGameValuesGroupsTest::RunTest(const FString& Parameters)
{
	// Ruling 7: circuits 3 and 0 are joined (group 0) and read the same figures; circuit 5 is alone (group 2).
	const TArray<FCircuitReading> Out = GroupCircuits({ circuit(3, 0, 100), circuit(5, 2, 7), circuit(0, 0, 100) });
	TestEqual(TEXT("one row per group"), Out.Num(), 2);
	TestEqual(TEXT("ordered by group id"), Out[0].CircuitGroupId, 0);
	TestEqual(TEXT("then"), Out[1].CircuitGroupId, 2);
	TestEqual(TEXT("figures copied"), Out[1].ProductionMW, 7.0);
	TestEqual(TEXT("consumption"), Out[1].ConsumptionMW, 3.5);
	TestEqual(TEXT("capacity"), Out[1].CapacityMW, 14.0);
	TestEqual(TEXT("max consumption"), Out[1].MaxConsumptionMW, 7.0);

	// The lowest circuit id speaks for a group, whatever order the scan found them in.
	FPowerCircuitSample Low = circuit(1, 4, 10);
	FPowerCircuitSample High = circuit(9, 4, 99);
	TestEqual(TEXT("lowest id wins"), GroupCircuits({ High, Low })[0].ProductionMW, 10.0);

	TestEqual(TEXT("a circuit with no group yet is left out"), GroupCircuits({ circuit(1, -1, 5) }).Num(), 0);
	TestEqual(TEXT("no circuits, no rows"), GroupCircuits({}).Num(), 0);

	TArray<FPowerCircuitSample> Many;
	for (int32 I = 0; I < MaxCircuits + 5; ++I)
	{
		Many.Add(circuit(I, I, 1));
	}
	TestEqual(TEXT("capped at the contract's 1000"), GroupCircuits(Many).Num(), MaxCircuits);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameValuesBatteryTest, "SatisDash.Core.GameValues.Battery", SatisDash::Test::Flags)

bool FGameValuesBatteryTest::RunTest(const FString& Parameters)
{
	FPowerCircuitSample S = circuit(0, 0, 50);
	S.bFuseTriggered = true;
	S.bHasBatteries = true;
	S.BatteryStoreMWh = 25;
	S.BatteryCapacityMWh = 100;
	S.BatteryInputMW = -12.5;
	const FCircuitReading R = GroupCircuits({ S })[0];
	TestTrue(TEXT("fuse"), R.bFuseTriggered);
	TestEqual(TEXT("capacity"), R.BatteryCapacityMWh, 100.0);
	TestEqual(TEXT("percent 0-100"), R.BatteryPercent, 25.0);
	TestEqual(TEXT("differential keeps its sign (discharging)"), R.BatteryDifferentialMW, -12.5);

	S.BatteryCapacityMWh = 0; // the game's own percent getter would divide by this (ruling 7)
	TestEqual(TEXT("no capacity: 0 percent"), GroupCircuits({ S })[0].BatteryPercent, 0.0);

	FPowerCircuitSample None = circuit(0, 0, 50);
	None.BatteryStoreMWh = 5; // stale figures with no batteries are ignored
	None.BatteryCapacityMWh = 10;
	const FCircuitReading N = GroupCircuits({ None })[0];
	TestEqual(TEXT("no batteries: capacity 0"), N.BatteryCapacityMWh, 0.0);
	TestEqual(TEXT("no batteries: percent 0"), N.BatteryPercent, 0.0);
	TestEqual(TEXT("no batteries: differential 0"), N.BatteryDifferentialMW, 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameValuesAutoPauseTest, "SatisDash.Core.GameValues.AutoPauseOption", SatisDash::Test::Flags)

bool FGameValuesAutoPauseTest::RunTest(const FString& Parameters)
{
	const FString True = TEXT("True");
	const FString False = TEXT("False");
	const FString Lower = TEXT("true");
	const FString Junk = TEXT("1");
	TestTrue(TEXT("True"), ParseAutoPauseOption(&True) == TOptional<bool>(true));
	TestTrue(TEXT("False"), ParseAutoPauseOption(&False) == TOptional<bool>(false));
	// The game writes exactly True and False (S7's log); anything else is unread, never guessed.
	TestFalse(TEXT("lower case is unread"), ParseAutoPauseOption(&Lower).IsSet());
	TestFalse(TEXT("junk is unread"), ParseAutoPauseOption(&Junk).IsSet());
	TestFalse(TEXT("missing is unread"), ParseAutoPauseOption(nullptr).IsSet());
	SatisDash::Test::SameText(*this, TEXT("write true"), AutoPauseOptionValue(true), TEXT("True"));
	SatisDash::Test::SameText(*this, TEXT("write false"), AutoPauseOptionValue(false), TEXT("False"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameValuesTickHealthTest, "SatisDash.Core.GameValues.TickHealth", SatisDash::Test::Flags)

bool FGameValuesTickHealthTest::RunTest(const FString& Parameters)
{
	// HealthCheck's values as S6 recorded them ("healthy", "slow"); the backend maps the snapshot's healthy/slow.
	TestTrue(TEXT("healthy"), IsTickHealthy(TEXT("healthy")));
	TestFalse(TEXT("slow"), IsTickHealthy(TEXT("slow")));
	TestFalse(TEXT("case-sensitive"), IsTickHealthy(TEXT("Healthy")));
	TestFalse(TEXT("empty"), IsTickHealthy(FString()));
	return true;
}
```

- [ ] **Step 2: Run them to see them fail**

C++ test command, `--min 48`. Expected: the build fails on the missing `SatisDash/GameValues.h`.

- [ ] **Step 3: Write `GameValues.h` and `GameValues.cpp`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Snapshot.h"

namespace SatisDash
{
	/** One UFGPowerCircuit as the game reports it (FGPowerCircuit.h), before grouping (ruling 7). */
	struct FPowerCircuitSample
	{
		int32 CircuitId = 0;
		/** UFGCircuit::GetCircuitGroupID(); negative while the circuit has no group. */
		int32 GroupId = 0;
		double ProducedMW = 0;
		double ConsumedMW = 0;
		double CapacityMW = 0;
		double MaxConsumptionMW = 0;
		bool bFuseTriggered = false;
		bool bHasBatteries = false;
		double BatteryStoreMWh = 0;
		double BatteryCapacityMWh = 0;
		/** Signed, positive while charging (the game's mBatterySumPowerInput). */
		double BatteryInputMW = 0;
	};

	/** Circuit IDs the reader tries, as S3 did (S3 ruling 5): the game keeps its circuit map private. */
	inline constexpr int32 MaxCircuitIdScanned = 10000;

	/** One reading per group, ordered by group id; the group's lowest circuit id speaks for it. At most MaxCircuits. */
	SATISDASHCORE_API TArray<FCircuitReading> GroupCircuits(const TArray<FPowerCircuitSample>& Samples);

	/** The option's name in GetServerOptions and ApplyServerOptions (S7). */
	inline const TCHAR* AutoPauseOption = TEXT("FG.DSAutoPause");
	/** Exactly "True" or "False"; anything else, or no value, is unread. */
	SATISDASHCORE_API TOptional<bool> ParseAutoPauseOption(const FString* Value);
	SATISDASHCORE_API FString AutoPauseOptionValue(bool bEnabled);

	/** HealthCheck's verdict: "healthy" or "slow" (S6). */
	SATISDASHCORE_API bool IsTickHealthy(const FString& Health);
}
```

```cpp
#include "SatisDash/GameValues.h"

TArray<SatisDash::FCircuitReading> SatisDash::GroupCircuits(const TArray<FPowerCircuitSample>& Samples)
{
	TMap<int32, const FPowerCircuitSample*> Speaker;
	for (const FPowerCircuitSample& S : Samples)
	{
		if (S.GroupId < 0)
		{
			continue;
		}
		const FPowerCircuitSample** Found = Speaker.Find(S.GroupId);
		if (Found == nullptr || S.CircuitId < (*Found)->CircuitId)
		{
			Speaker.Add(S.GroupId, &S);
		}
	}
	TArray<int32> Groups;
	Speaker.GetKeys(Groups);
	Groups.Sort();
	TArray<FCircuitReading> Out;
	for (const int32 Group : Groups)
	{
		if (Out.Num() == MaxCircuits)
		{
			break;
		}
		const FPowerCircuitSample& S = *Speaker[Group];
		FCircuitReading R;
		R.CircuitGroupId = Group;
		R.ProductionMW = S.ProducedMW;
		R.ConsumptionMW = S.ConsumedMW;
		R.CapacityMW = S.CapacityMW;
		R.MaxConsumptionMW = S.MaxConsumptionMW;
		R.bFuseTriggered = S.bFuseTriggered;
		if (S.bHasBatteries)
		{
			R.BatteryCapacityMWh = S.BatteryCapacityMWh;
			R.BatteryPercent = S.BatteryCapacityMWh > 0 ? 100.0 * S.BatteryStoreMWh / S.BatteryCapacityMWh : 0.0;
			R.BatteryDifferentialMW = S.BatteryInputMW;
		}
		Out.Add(R);
	}
	return Out;
}

TOptional<bool> SatisDash::ParseAutoPauseOption(const FString* Value)
{
	if (Value == nullptr)
	{
		return {};
	}
	if (Value->Equals(TEXT("True"), ESearchCase::CaseSensitive))
	{
		return true;
	}
	if (Value->Equals(TEXT("False"), ESearchCase::CaseSensitive))
	{
		return false;
	}
	return {};
}

FString SatisDash::AutoPauseOptionValue(bool bEnabled)
{
	return bEnabled ? TEXT("True") : TEXT("False");
}

bool SatisDash::IsTickHealthy(const FString& Health)
{
	return Health.Equals(TEXT("healthy"), ESearchCase::CaseSensitive);
}
```

`ConformCircuit` (2A) still clamps every value when the snapshot is written, so a battery percent of 100.0000001 from float noise becomes 100.

- [ ] **Step 4: Extend the log capture (2A deferred: "never logs Error")**

In `TestUtil.h`'s `FLogCapture`, keep the verbosity of each line in a parallel `TArray<ELogVerbosity::Type> Levels` (filled in `Serialize` with `Verbosity & ELogVerbosity::VerbosityMask`), with `TArray<ELogVerbosity::Type> Verbosities()` returning a copy under the lock, and add:

```cpp
	/** Global Constraints: Display or Warning, never Error or Fatal. */
	inline bool NoErrorLines(FAutomationTestBase& Test, FLogCapture& Capture)
	{
		const TArray<FString> Lines = Capture.Take();
		const TArray<ELogVerbosity::Type> Levels = Capture.Verbosities();
		for (int32 I = 0; I < Levels.Num(); ++I)
		{
			if (Levels[I] <= ELogVerbosity::Error)
			{
				Test.AddError(FString::Printf(TEXT("logged at Error: %s"), Lines.IsValidIndex(I) ? *Lines[I] : TEXT("?")));
				return false;
			}
		}
		return true;
	}
```

Add one call at the end of 2A's `LogTest.cpp` test body (the whole-suite log check), so 2A's paths are covered too. Rows: `ELogVerbosity::VerbosityMask`, `ELogVerbosity::Error` (look up `Logging/LogVerbosity.h`).

- [ ] **Step 5: Run the tests to see them pass**

C++ test command, `--min 48`. Expected: 48 tests pass.

- [ ] **Step 6: `--min` in mod-test.yml (2A deferred)**

`.github/workflows/mod-test.yml`: `npm run harness -- test-mod --out "$env:RUNNER_TEMP/mod-test" --min 48`. Each later task that adds tests raises it in the same commit (Task 4: 56, Task 5: 64).

- [ ] **Step 7: Commit**

```bash
git -c core.safecrlf=false add mod .github/workflows/mod-test.yml api-sources.md
git commit -m "Core mod Plan 2B Task 2: circuit groups, the auto-pause option and tick health"
```

---

### Task 3: The native game reader (C8, C9, power)

The game's calls are the spikes' (S6, S7, S3), already in `api-sources.md`. This task has no unit test of its own: it needs a running game. It's judged by the build, check-api and review here, and proven by Task 6's enrol run, which compares every field with `QueryServerState`, `HealthCheck`, `GetServerOptions` and FRM's `getPower`.

**Files:**
- Create: `mod/SatisDashAgent/Source/SatisDashGame/Private/GameReader.h`, `Private/GameReader.cpp`
- Modify: `api-sources.md`

**Interfaces:**
- Consumes: `IGameReader` (Ports.h, Task 1); `FPowerCircuitSample`, `GroupCircuits`, `MaxCircuitIdScanned`, `AutoPauseOption`, `ParseAutoPauseOption`, `AutoPauseOptionValue`, `IsTickHealthy` (Task 2).
- Produces: `class SatisDash::FGameReader : public IGameReader` with `void SetWorld(UWorld* InWorld)`.

- [ ] **Step 1: Write the reader**

`GameReader.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Ports.h"

class UWorld;
class UFGServerSubsystem;

namespace SatisDash
{
	/** The game's own readers, in-process, as S6 (status), S7 (auto-pause) and S3 (power) proved them. Game thread only. */
	class FGameReader : public IGameReader
	{
	public:
		/** The newest game world: the entry map at boot, then each loaded save. */
		void SetWorld(UWorld* InWorld) { World = InWorld; }

		virtual TOptional<FStatusReading> ReadStatus() override;
		virtual TOptional<bool> ReadAutoPause() override;
		virtual TOptional<TArray<FCircuitReading>> ReadPower() override;
		virtual bool ApplyAutoPause(bool bEnabled) override;

	private:
		UFGServerSubsystem* Server() const;
		TWeakObjectPtr<UWorld> World;
	};
}
```

`GameReader.cpp`:

```cpp
#include "GameReader.h"

#include "Controller/FGServerManagementController.h"
#include "Controller/FGServerStateController.h"
#include "Engine/World.h"
#include "FGCircuitSubsystem.h"
#include "FGPowerCircuit.h"
#include "FGServerSubsystem.h"
#include "SatisDash/GameValues.h"

UFGServerSubsystem* SatisDash::FGameReader::Server() const
{
	return World.IsValid() ? UFGServerSubsystem::Get(World.Get()) : nullptr;
}

TOptional<SatisDash::FStatusReading> SatisDash::FGameReader::ReadStatus()
{
	UFGServerSubsystem* Subsystem = Server();
	UFGServerAPIManager* Manager = Subsystem != nullptr ? Subsystem->GetServerAPIManager() : nullptr;
	if (Manager == nullptr)
	{
		return {};
	}
	const FServerGameState State = Subsystem->GetServerGameState();
	// HealthCheck's verdict has no getter; the game's own handler computes it, on a controller made under the API
	// manager, which finds the subsystem through its outers (S6).
	FString Health;
	FString CustomData;
	NewObject<UFGServerStateController>(Manager)->Handler_HealthCheck(FString(), Health, CustomData);
	FStatusReading Out;
	Out.bTickHealthy = IsTickHealthy(Health);
	Out.bIsGameRunning = State.IsGameRunning;
	Out.bGamePaused = State.IsGamePaused;
	Out.SessionName = State.ActiveSessionName;
	Out.ConnectedPlayers = State.NumConnectedPlayers;
	Out.PlayerLimit = State.PlayerLimit;
	Out.TickRate = State.AverageTickRate;
	Out.TotalGameDurationSeconds = State.TotalGameDuration;
	return Out;
}

TOptional<bool> SatisDash::FGameReader::ReadAutoPause()
{
	UFGServerSubsystem* Subsystem = Server();
	UFGServerAPIManager* Manager = Subsystem != nullptr ? Subsystem->GetServerAPIManager() : nullptr;
	if (Manager == nullptr)
	{
		return {};
	}
	// The options also hold FRM's token: only FG.DSAutoPause is read, and nothing of them is logged (S7).
	TMap<FString, FString> Options;
	TMap<FString, FString> Pending;
	NewObject<UFGServerStateController>(Manager)->Handler_GetServerOptions(Options, Pending);
	return ParseAutoPauseOption(Options.Find(AutoPauseOption));
}

bool SatisDash::FGameReader::ApplyAutoPause(bool bEnabled)
{
	UFGServerSubsystem* Subsystem = Server();
	UFGServerAPIManager* Manager = Subsystem != nullptr ? Subsystem->GetServerAPIManager() : nullptr;
	if (Manager == nullptr)
	{
		return false;
	}
	TMap<FString, FString> Updated;
	Updated.Add(AutoPauseOption, AutoPauseOptionValue(bEnabled));
	NewObject<UFGServerManagementController>(Manager)->Handler_ApplyServerOptions(Updated);
	// Handler_ApplyServerOptions returns nothing (FGServerManagementController.h:60): reading back is the only check.
	const TOptional<bool> Now = ReadAutoPause();
	return Now.IsSet() && Now.GetValue() == bEnabled;
}

TOptional<TArray<SatisDash::FCircuitReading>> SatisDash::FGameReader::ReadPower()
{
	AFGCircuitSubsystem* Circuits = World.IsValid() ? AFGCircuitSubsystem::GetCircuitSubsystem(World.Get()) : nullptr;
	if (Circuits == nullptr)
	{
		return {};
	}
	TArray<FPowerCircuitSample> Samples;
	for (int32 Id = 0; Id < MaxCircuitIdScanned; ++Id)
	{
		UFGPowerCircuit* Circuit = Circuits->FindCircuit<UFGPowerCircuit>(Id);
		if (Circuit == nullptr)
		{
			continue;
		}
		FPowerCircuitStats Stats;
		Circuit->GetStats(Stats);
		FPowerCircuitSample S;
		S.CircuitId = Circuit->GetCircuitID();
		S.GroupId = Circuit->GetCircuitGroupID();
		S.ProducedMW = Stats.PowerProduced;
		S.ConsumedMW = Stats.PowerConsumed;
		S.CapacityMW = Stats.PowerProductionCapacity;
		S.MaxConsumptionMW = Stats.MaximumPowerConsumption;
		S.bFuseTriggered = Circuit->IsFuseTriggered();
		S.bHasBatteries = Circuit->HasBatteries();
		S.BatteryStoreMWh = Circuit->GetBatterySumPowerStore();
		S.BatteryCapacityMWh = Circuit->GetBatterySumPowerStoreCapacity();
		// The public getters split the signed input into charge and discharge (FGPowerCircuit.h:227, :231).
		S.BatteryInputMW = Circuit->GetBatterySumPowerInput() - Circuit->GetBatterySumPowerOutput();
		Samples.Add(S);
	}
	return GroupCircuits(Samples);
}
```

- [ ] **Step 2: Add the rows**

Each looked up in the SML checkout (`Source/FactoryGame/Public/`), line numbers as found on 2026-10-20 (re-check them):
`UFGCircuit::GetCircuitGroupID` (`FGCircuit.h:71`), `UFGPowerCircuit::IsFuseTriggered` (`FGPowerCircuit.h:163`), `UFGPowerCircuit::HasBatteries` (`:211`), `UFGPowerCircuit::GetBatterySumPowerStore` (`:215`), `UFGPowerCircuit::GetBatterySumPowerStoreCapacity` (`:219`), `UFGPowerCircuit::GetBatterySumPowerInput` (`:227`), `UFGPowerCircuit::GetBatterySumPowerOutput` (`:231`), `FPowerCircuitStats::MaximumPowerConsumption` (`:110`), plus any row check-api asks for (`UFGCircuit::GetCircuitID` if S3's row names only another class, `TWeakObjectPtr`).

- [ ] **Step 3: Build and check**

C++ test command, `--min 48` (the editor build compiles the reader), and `npm run harness -- check-api` (`0 missing, 0 bad rows`).

- [ ] **Step 4: Commit**

```bash
git -c core.safecrlf=false add mod api-sources.md
git commit -m "Core mod Plan 2B Task 3: the native game reader"
```

---

### Task 4: The driver: config, enrolment, sampling, pushing, halt

**Files:**
- Create: `mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Driver.h`, `Private/Driver.cpp`
- Create: `mod/SatisDashAgent/Source/SatisDashCoreTests/Private/DriverFakes.h`, `Private/DriverTest.cpp`
- Modify: `Private/Pusher.cpp` (the "queue is full" line comes back, 2A deferred), `Private/Agent.cpp` (the Settle race, 2A final N2), `SatisDashCoreTests/Private/PusherTest.cpp`, `.github/workflows/mod-test.yml` (`--min 56`)

**Interfaces:**
- Consumes: `FAgent` (Boot, CheckConfig, GetState, GetConfig, GetCadence, SetCadence, GetCredential, StartEnroll, FinishEnroll, OnAuthRejected), `FPusher` (Enqueue, StartSend, FinishSend, Clear, Num, Failures), `FRateLimitedLog`, `BuildSnapshotRequest`, `SnapshotToJson`, `AgentVersion`, `ITransport`, `IGameReader`.
- Produces:
  - `struct SatisDash::FDriverClock { TFunction<double()> Now; TFunction<FDateTime()> UtcNow; TFunction<double()> Random01; }`
  - `class SatisDash::FDriver` with `FDriver(FString ConfigPath, FString CredentialPath, ITransport& InTransport, IGameReader& InGame, FDriverClock InClock)`, `void Boot()`, `void Tick()`, `const FAgent& GetAgent() const`, `int32 Queued() const`
  - constants `SatisDash::ConfigCheckSeconds = 60.0`, `SatisDash::DueSlackSeconds = 0.25`
  - test fakes `FFakeTransport` (`Sent`, `pending(Path)`, `answer(Path, Reply)`), `FFakeGame`, `FDriverFixture` (Task 5 reuses them)

- [ ] **Step 1: The fakes**

`DriverFakes.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "SatisDash/Config.h"
#include "SatisDash/Driver.h"
#include "SatisDash/Ports.h"
#include "TestUtil.h"

namespace SatisDash::Test
{
	/** Holds every request until the test answers it, so the order of replies is the test's to choose. */
	struct FFakeTransport : public SatisDash::ITransport
	{
		struct FCall
		{
			SatisDash::FRequestSpec Spec;
			SatisDash::FReplyHandler OnReply;
		};
		TArray<FCall> Open;
		TArray<SatisDash::FRequestSpec> Sent;

		virtual void Send(const SatisDash::FRequestSpec& Spec, SatisDash::FReplyHandler OnReply) override
		{
			Sent.Add(Spec);
			Open.Add({ Spec, MoveTemp(OnReply) });
		}

		/** Unanswered requests whose URL path ends with Path. */
		int32 pending(const TCHAR* Path) const
		{
			int32 N = 0;
			for (const FCall& Call : Open)
			{
				N += Call.Spec.Url.EndsWith(Path, ESearchCase::CaseSensitive) ? 1 : 0;
			}
			return N;
		}

		int32 sent(const TCHAR* Path) const
		{
			int32 N = 0;
			for (const SatisDash::FRequestSpec& Spec : Sent)
			{
				N += Spec.Url.EndsWith(Path, ESearchCase::CaseSensitive) ? 1 : 0;
			}
			return N;
		}

		/** Answers the oldest open request for Path; false when there is none. */
		bool answer(const TCHAR* Path, const SatisDash::FHttpReply& Reply)
		{
			for (int32 I = 0; I < Open.Num(); ++I)
			{
				if (Open[I].Spec.Url.EndsWith(Path, ESearchCase::CaseSensitive))
				{
					FCall Call = MoveTemp(Open[I]);
					Open.RemoveAt(I);
					Call.OnReply(Reply);
					return true;
				}
			}
			return false;
		}

		/** The body of the newest request for Path, as text ("" when none). */
		FString lastBody(const TCHAR* Path) const
		{
			for (int32 I = Sent.Num() - 1; I >= 0; --I)
			{
				if (Sent[I].Url.EndsWith(Path, ESearchCase::CaseSensitive))
				{
					const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Sent[I].Body.GetData()), Sent[I].Body.Num());
					return FString::ConstructFromPtrSize(Text.Get(), Text.Length());
				}
			}
			return FString();
		}

		/** The Authorization header of the newest request for Path ("" when none). */
		FString lastAuth(const TCHAR* Path) const
		{
			for (int32 I = Sent.Num() - 1; I >= 0; --I)
			{
				if (!Sent[I].Url.EndsWith(Path, ESearchCase::CaseSensitive))
				{
					continue;
				}
				for (const TPair<FString, FString>& Header : Sent[I].Headers)
				{
					if (Header.Key.Equals(TEXT("Authorization"), ESearchCase::CaseSensitive))
					{
						return Header.Value;
					}
				}
				return FString();
			}
			return FString();
		}
	};

	struct FFakeGame : public SatisDash::IGameReader
	{
		TOptional<SatisDash::FStatusReading> Status;
		TOptional<bool> AutoPause = false;
		TOptional<TArray<SatisDash::FCircuitReading>> Power;
		bool bApplyWorks = true;
		TArray<bool> Applied;
		int32 PowerReads = 0;

		FFakeGame()
		{
			SatisDash::FStatusReading S;
			S.bIsGameRunning = true;
			S.SessionName = TEXT("test");
			Status = S;
			Power = TArray<SatisDash::FCircuitReading>{ SatisDash::FCircuitReading() };
		}
		virtual TOptional<SatisDash::FStatusReading> ReadStatus() override { return Status; }
		virtual TOptional<bool> ReadAutoPause() override { return AutoPause; }
		virtual TOptional<TArray<SatisDash::FCircuitReading>> ReadPower() override { ++PowerReads; return Power; }
		virtual bool ApplyAutoPause(bool bEnabled) override
		{
			Applied.Add(bEnabled);
			if (bApplyWorks)
			{
				AutoPause = bEnabled;
			}
			return bApplyWorks;
		}
	};

	/** A config, a fake clock (monotonic and UTC move together), a fake network and a fake game. */
	struct FDriverFixture
	{
		FTempDir Dir;
		FString ConfigPath = Dir.Path / TEXT("Configs/SatisDashAgent.cfg");
		FString CredentialPath = Dir.Path / TEXT("SatisDash/credential.json");
		double T = 1000.0;
		FDateTime Utc = FDateTime(2026, 10, 20, 12, 0, 0);
		FFakeTransport Net;
		FFakeGame Game;

		void writeConfig(const TCHAR* Url, const TCHAR* Code) const
		{
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(ConfigPath), true);
			WriteText(ConfigPath, FString::Printf(TEXT("{\"backendUrl\":\"%s\",\"enrollmentCode\":\"%s\"}"), Url, Code));
		}

		SatisDash::FDriverClock clock()
		{
			SatisDash::FDriverClock Clock;
			Clock.Now = [this] { return T; };
			Clock.UtcNow = [this] { return Utc; };
			Clock.Random01 = [] { return 1.0; };
			return Clock;
		}

		/** Moves both clocks, ticking the driver every half second as the game does. */
		void run(SatisDash::FDriver& Driver, double Seconds)
		{
			for (double Done = 0; Done < Seconds; Done += 0.5)
			{
				T += 0.5;
				Utc += FTimespan::FromMilliseconds(500);
				Driver.Tick();
			}
		}

		/** Ticks until Done says so, for at most Seconds; false if it never did. */
		bool runUntil(SatisDash::FDriver& Driver, const TFunction<bool()>& Done, double Seconds)
		{
			for (double Spent = 0; Spent < Seconds; Spent += 0.5)
			{
				if (Done())
				{
					return true;
				}
				run(Driver, 0.5);
			}
			return Done();
		}

		/** Boots a driver against http://127.0.0.1:1 with a code and answers its enrolment with 201. */
		void enrol(SatisDash::FDriver& Driver, const FString& Secret)
		{
			writeConfig(TEXT("http://127.0.0.1:1"), TEXT("AB3D-7XQ2"));
			Driver.Boot();
			Driver.Tick();
			Net.answer(TEXT("/agent/v1/enroll"), Reply(201, FString::Printf(
				TEXT("{\"agentSecret\":\"%s\",\"serverId\":\"srv-1\",\"cadence\":%s}"), *Secret, CadenceJson)));
		}
	};

	inline FHttpReply SnapshotOk(bool bCommandsPending)
	{
		return Reply(200, FString::Printf(TEXT("{\"cadence\":%s,\"commandsPending\":%s}"), CadenceJson, bCommandsPending ? TEXT("true") : TEXT("false")));
	}
}
```

- [ ] **Step 2: Write the failing driver tests**

`DriverTest.cpp`:

```cpp
#include "DriverFakes.h"
#include "SatisDash/Driver.h"

using namespace SatisDash;
using SatisDash::Test::FDriverFixture;

namespace
{
	const TCHAR* Snapshots = TEXT("/agent/v1/snapshots");
	const TCHAR* Enroll = TEXT("/agent/v1/enroll");
	const TCHAR* Poll = TEXT("/agent/v1/commands?waitSeconds=25");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverEnrollThenPushTest, "SatisDash.Core.Driver.EnrollThenPush", SatisDash::Test::Flags)

bool FDriverEnrollThenPushTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver(F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock());
	F.writeConfig(TEXT("http://127.0.0.1:1"), TEXT("AB3D-7XQ2"));
	Driver.Boot();
	Driver.Tick();
	TestEqual(TEXT("one enrol request"), F.Net.pending(Enroll), 1);
	F.run(Driver, 5);
	TestEqual(TEXT("still one while it's out"), F.Net.sent(Enroll), 1);
	TestEqual(TEXT("nothing pushed before enrolling"), F.Net.sent(Snapshots), 0);
	F.Net.answer(Enroll, SatisDash::Test::Reply(201, SatisDash::Test::EnrollJson()));
	TestTrue(TEXT("running"), Driver.GetAgent().GetState() == EAgentState::Running);
	F.run(Driver, 1);
	TestEqual(TEXT("a snapshot goes out"), F.Net.pending(Snapshots), 1);
	SatisDash::Test::SameText(*this, TEXT("with the secret"), F.Net.lastAuth(Snapshots), TEXT("Bearer ") + SatisDash::Test::FakeSecret());
	F.Net.answer(Snapshots, SatisDash::Test::SnapshotOk(false));
	TestEqual(TEXT("and leaves the queue"), Driver.Queued(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverOneAtATimeTest, "SatisDash.Core.Driver.OneAtATime", SatisDash::Test::Flags)

bool FDriverOneAtATimeTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver(F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock());
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	F.run(Driver, 30);
	// Pusher.h: one FinishSend per StartSend; while the backend is slow, snapshots wait in the queue.
	TestEqual(TEXT("one send out at a time"), F.Net.pending(Snapshots), 1);
	TestTrue(TEXT("the rest are queued"), Driver.Queued() >= 5);
	F.Net.answer(Snapshots, SatisDash::Test::SnapshotOk(false));
	F.run(Driver, 0.5);
	TestEqual(TEXT("the next goes out after the answer"), F.Net.pending(Snapshots), 1);
	F.Net.answer(Snapshots, SatisDash::Test::Reply(0, FString()));
	F.run(Driver, 0.5);
	TestEqual(TEXT("a Code 0 answer (timeout) waits for the backoff"), F.Net.pending(Snapshots), 0);
	F.run(Driver, 2);
	TestEqual(TEXT("then retries"), F.Net.pending(Snapshots), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverPartsTest, "SatisDash.Core.Driver.Parts", SatisDash::Test::Flags)

bool FDriverPartsTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	FDriverFixture F;
	FDriver Driver(F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock());
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	auto answerAll = [&F] { while (F.Net.answer(TEXT("/agent/v1/snapshots"), SatisDash::Test::SnapshotOk(false))) {} };

	// Status every 5 s (the default cadence), power when due and only with a game running. Passes fall at +0.5 s and
	// +5.5 s (each is due 4.75 s after the last, met on the next half-second tick).
	const int32 Before = F.Net.sent(Snapshots);
	for (int32 I = 0; I < 20; ++I) { F.run(Driver, 0.5); answerAll(); }
	TestEqual(TEXT("two passes in 10 s"), F.Net.sent(Snapshots) - Before, 2);
	TestEqual(TEXT("power read with each"), F.Game.PowerReads, 2);
	TestTrue(TEXT("auto-pause goes with status"), F.Net.lastBody(Snapshots).Contains(TEXT("\"autoPause\":false")));

	// No game (the entry map): status says so and no power is read (spec "Data flow").
	F.Game.Status->bIsGameRunning = false;
	const int32 Reads = F.Game.PowerReads;
	for (int32 I = 0; I < 20; ++I) { F.run(Driver, 0.5); answerAll(); }
	TestEqual(TEXT("no power without a game"), F.Game.PowerReads, Reads);
	TestTrue(TEXT("isGameRunning false is sent"), F.Net.lastBody(Snapshots).Contains(TEXT("\"isGameRunning\":false")));
	TestFalse(TEXT("with no power part"), F.Net.lastBody(Snapshots).Contains(TEXT("\"power\"")));

	// A failed reader leaves only its part out, and is logged once, not per pass.
	F.Game.Status->bIsGameRunning = true;
	F.Game.Power.Reset();
	for (int32 I = 0; I < 40; ++I) { F.run(Driver, 0.5); answerAll(); }
	int32 PowerLines = 0;
	for (const FString& Line : Log.Take())
	{
		PowerLines += Line.Contains(TEXT("power")) && Line.Contains(TEXT("leaving it out")) ? 1 : 0;
	}
	TestEqual(TEXT("one line for the power reader"), PowerLines, 1);
	TestTrue(TEXT("snapshots still go out"), F.Net.sent(Snapshots) > Before + 4);
	SatisDash::Test::NoErrorLines(*this, Log);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverHaltTest, "SatisDash.Core.Driver.Halt", SatisDash::Test::Flags)

bool FDriverHaltTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver(F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock());
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	F.run(Driver, 20);
	TestTrue(TEXT("a queue built up"), Driver.Queued() > 1);
	F.Net.answer(Snapshots, SatisDash::Test::Reply(401, TEXT("{}")));
	TestTrue(TEXT("halted"), Driver.GetAgent().GetState() == EAgentState::Halted);
	TestFalse(TEXT("credential deleted"), IFileManager::Get().FileExists(*F.CredentialPath));
	const int32 Pushed = F.Net.sent(Snapshots);
	const int32 Polled = F.Net.sent(Poll);
	F.run(Driver, 120);
	TestEqual(TEXT("queue cleared"), Driver.Queued(), 0);
	TestEqual(TEXT("nothing more pushed"), F.Net.sent(Snapshots), Pushed);
	TestEqual(TEXT("and nothing more polled"), F.Net.sent(Poll), Polled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverReboundTest, "SatisDash.Core.Driver.Rebound", SatisDash::Test::Flags)

bool FDriverReboundTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver(F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock());
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	F.run(Driver, 20);
	TestEqual(TEXT("a send is out"), F.Net.pending(Snapshots), 1);
	// The config now names another backend (no code): the credential belongs to the first one (S10).
	F.writeConfig(TEXT("https://other.example"), TEXT(""));
	F.run(Driver, 61);
	TestTrue(TEXT("no credential for the new backend"), Driver.GetAgent().GetCredential() == nullptr);
	TestTrue(TEXT("the queue waits for the send in flight"), Driver.Queued() > 0);
	F.Net.answer(Snapshots, SatisDash::Test::SnapshotOk(false));
	F.run(Driver, 1);
	TestEqual(TEXT("then it's cleared"), Driver.Queued(), 0);
	for (const SatisDash::FRequestSpec& Spec : F.Net.Sent)
	{
		TestFalse(TEXT("nothing ever went to the new backend"), Spec.Url.StartsWith(TEXT("https://other.example")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverReenrolTest, "SatisDash.Core.Driver.Reenrol", SatisDash::Test::Flags)

bool FDriverReenrolTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver(F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock());
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	F.run(Driver, 1);
	F.Net.answer(Snapshots, SatisDash::Test::Reply(401, TEXT("{}")));
	F.run(Driver, 10);
	// A new code from the dashboard: the next config check enrols again, with a new secret.
	F.writeConfig(TEXT("http://127.0.0.1:1"), TEXT("ZZZZ-2222"));
	F.run(Driver, 61);
	TestEqual(TEXT("a second enrolment"), F.Net.pending(Enroll), 1);
	const FString Second = FString::ChrN(42, TEXT('b')) + TEXT("_");
	F.Net.answer(Enroll, SatisDash::Test::Reply(201, FString::Printf(
		TEXT("{\"agentSecret\":\"%s\",\"serverId\":\"srv-1\",\"cadence\":%s}"), *Second, SatisDash::Test::CadenceJson)));
	F.run(Driver, 1);
	SatisDash::Test::SameText(*this, TEXT("the new secret is used"), F.Net.lastAuth(Snapshots), TEXT("Bearer ") + Second);
	TestTrue(TEXT("only fresh snapshots are queued"), Driver.Queued() <= 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverCadenceTest, "SatisDash.Core.Driver.Cadence", SatisDash::Test::Flags)

bool FDriverCadenceTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver(F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock());
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	F.run(Driver, 1);
	F.Net.answer(Snapshots, SatisDash::Test::Reply(200,
		TEXT("{\"cadence\":{\"statusSeconds\":20,\"powerSeconds\":60,\"factorySeconds\":30},\"commandsPending\":false}")));
	TestEqual(TEXT("the backend's cadence is taken"), Driver.GetAgent().GetCadence().StatusSeconds, 20);
	const int32 Before = F.Net.sent(Snapshots);
	for (int32 I = 0; I < 80; ++I)
	{
		F.run(Driver, 0.5);
		while (F.Net.answer(Snapshots, SatisDash::Test::Reply(200,
			TEXT("{\"cadence\":{\"statusSeconds\":20,\"powerSeconds\":60,\"factorySeconds\":30},\"commandsPending\":false}")))) {}
	}
	TestEqual(TEXT("one pass per 20 s"), F.Net.sent(Snapshots) - Before, 2);
	return true;
}
```

In `PusherTest.cpp`, add:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPusherFullAgainTest, "SatisDash.Core.Pusher.FullAgain", SatisDash::Test::Flags)

bool FPusherFullAgainTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	FPusher Pusher([] { return 1.0; });
	auto fullLines = [&Log]
	{
		int32 N = 0;
		for (const FString& Line : Log.Take()) { N += Line.Contains(TEXT("queue is full")) ? 1 : 0; }
		return N;
	};
	// A slow backend that answers every send: the queue overflows with no failure in between (2A final review).
	for (int32 I = 0; I < 130; ++I) { Pusher.Enqueue(TEXT("{}")); }
	TestEqual(TEXT("logged once"), fullLines(), 1);
	for (int32 I = 0; I < 10; ++I) { Pusher.Enqueue(TEXT("{}")); }
	TestEqual(TEXT("still once while it stays full"), fullLines(), 1);
	while (Pusher.Num() > MaxQueuedSnapshots / 2)
	{
		Pusher.StartSend(0);
		Pusher.FinishSend(SatisDash::Test::Reply(200, SatisDash::Test::SnapshotAnswerJson), 0);
	}
	for (int32 I = 0; I < 100; ++I) { Pusher.Enqueue(TEXT("{}")); }
	TestEqual(TEXT("a second episode is logged again"), fullLines(), 2);
	return true;
}
```

- [ ] **Step 3: Run them to see them fail**

C++ test command, `--min 56`. Expected: the build fails on the missing `SatisDash/Driver.h`.

- [ ] **Step 4: Write `Driver.h` and `Driver.cpp`**

`Driver.h` (Task 5 adds the command members marked there):

```cpp
#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Agent.h"
#include "SatisDash/Commands.h"
#include "SatisDash/Ports.h"
#include "SatisDash/Pusher.h"
#include "SatisDash/RateLimit.h"

namespace SatisDash
{
	inline constexpr double ConfigCheckSeconds = 60.0;
	/** sampler.ts DUE_SLACK_MS: a pass is due this much early, so the ticker's half-second steps don't stretch it. */
	inline constexpr double DueSlackSeconds = 0.25;

	struct FDriverClock
	{
		/** Monotonic seconds: FPlatformTime::Seconds() in the game (Agent.h, Pusher.h). */
		TFunction<double()> Now;
		/** UTC: FDateTime::UtcNow() in the game (Snapshot.h, Commands.h). */
		TFunction<FDateTime()> UtcNow;
		/** Uniform in [0, 1]: FMath::FRand() in the game. */
		TFunction<double()> Random01;
	};

	/**
	 * The Node agent's agentRunner.ts on one game-thread tick: config checks, enrolment, sampling, pushing, commands and
	 * halt. Owns every pairing rule in Agent.h and Pusher.h, so the game module only ticks it. Not copyable: replies
	 * call back into it, so it must outlive every request it sent (the transport's CancelAll ensures that).
	 */
	class SATISDASHCORE_API FDriver
	{
	public:
		FDriver(FString ConfigPath, FString CredentialPath, ITransport& InTransport, IGameReader& InGame, FDriverClock InClock);
		FDriver(const FDriver&) = delete;
		FDriver& operator=(const FDriver&) = delete;

		void Boot();
		/** Call often (the game: every 0.5 s); each call does only what is due. */
		void Tick();

		const FAgent& GetAgent() const { return Agent; }
		int32 Queued() const { return Pusher.Num(); }

	private:
		void TickSample(double Now);
		void TickPush(double Now);
		void NoteFailedPart(const TCHAR* Part, double Now);
		/** Task 5. */
		void TickCommands(double Now);
		void StopCommands();

		FDriverClock Clock;
		ITransport& Transport;
		IGameReader& Game;
		FAgent Agent;
		FPusher Pusher;
		FRateLimitedLog PartLog;
		double NextConfigAt = 0;
		double NextStatusAt = 0;
		TOptional<double> LastPowerAt;
		bool bRunning = false;
		bool bSendInFlight = false;
		/** Leaving Running asks for a Clear; it happens once no send is in flight (Pusher.h). */
		bool bClearPending = false;
		// Task 5 adds the command members here.
	};
}
```

`Driver.cpp`:

```cpp
#include "SatisDash/Driver.h"

#include "SatisDash/Log.h"
#include "SatisDash/Snapshot.h"
#include "SatisDash/Version.h"

SatisDash::FDriver::FDriver(FString ConfigPath, FString CredentialPath, ITransport& InTransport, IGameReader& InGame, FDriverClock InClock)
	: Clock{MoveTemp(InClock)}
	, Transport{InTransport}
	, Game{InGame}
	, Agent{MoveTemp(ConfigPath), MoveTemp(CredentialPath), Clock.Random01}
	, Pusher{Clock.Random01}
{
}

void SatisDash::FDriver::Boot()
{
	Agent.Boot();
	NextConfigAt = Clock.Now() + ConfigCheckSeconds;
}

void SatisDash::FDriver::Tick()
{
	const double Now = Clock.Now();
	if (Now >= NextConfigAt)
	{
		NextConfigAt = Now + ConfigCheckSeconds;
		Agent.CheckConfig();
	}
	if (TOptional<FRequestSpec> Enroll = Agent.StartEnroll(Now))
	{
		Transport.Send(*Enroll, [this](const FHttpReply& Reply) { Agent.FinishEnroll(Reply, Clock.Now()); });
	}

	// S10: "running" means a credential for the backend the config names now, not just the state's name (2A final, N2).
	const bool bNowRunning = Agent.GetCredential() != nullptr;
	if (bRunning && !bNowRunning)
	{
		bClearPending = true;
		StopCommands();
	}
	if (!bRunning && bNowRunning)
	{
		NextStatusAt = Now;
		LastPowerAt.Reset();
	}
	bRunning = bNowRunning;
	if (bClearPending && !bSendInFlight)
	{
		Pusher.Clear();
		bClearPending = false;
	}
	if (!bRunning || bClearPending)
	{
		return;
	}
	TickSample(Now);
	TickPush(Now);
	TickCommands(Now);
}

void SatisDash::FDriver::NoteFailedPart(const TCHAR* Part, double Now)
{
	if (PartLog.ShouldLog(Part, Now))
	{
		UE_LOG(LogSatisDash, Warning, TEXT("Couldn't read %s from the game; leaving it out of the snapshot"), Part);
	}
}

void SatisDash::FDriver::TickSample(double Now)
{
	if (Now < NextStatusAt)
	{
		return;
	}
	const FCadence& Cadence = Agent.GetCadence();
	NextStatusAt = Now + Cadence.StatusSeconds - DueSlackSeconds;
	FSnapshot Snapshot;
	Snapshot.AgentVersion = AgentVersion();
	Snapshot.ObservedAt = Clock.UtcNow();
	Snapshot.Status = Game.ReadStatus();
	if (!Snapshot.Status.IsSet())
	{
		NoteFailedPart(TEXT("status"), Now);
	}
	else
	{
		// Auto-pause is read with status (spec "Data flow").
		Snapshot.AutoPause = Game.ReadAutoPause();
		if (!Snapshot.AutoPause.IsSet())
		{
			NoteFailedPart(TEXT("auto-pause"), Now);
		}
		const bool bPowerDue = !LastPowerAt.IsSet() || Now - LastPowerAt.GetValue() >= Cadence.PowerSeconds - DueSlackSeconds;
		if (Snapshot.Status->bIsGameRunning && bPowerDue)
		{
			LastPowerAt = Now;
			Snapshot.Power = Game.ReadPower();
			if (!Snapshot.Power.IsSet())
			{
				NoteFailedPart(TEXT("power"), Now);
			}
		}
	}
	Pusher.Enqueue(SnapshotToJson(Snapshot));
}

void SatisDash::FDriver::TickPush(double Now)
{
	if (bSendInFlight)
	{
		return;
	}
	// Agent.h: fetched again for every authenticated request, and copied into the request, never kept.
	const FCredential* Credential = Agent.GetCredential();
	if (Credential == nullptr)
	{
		return;
	}
	const TOptional<FString> Json = Pusher.StartSend(Now);
	if (!Json.IsSet())
	{
		return;
	}
	bSendInFlight = true;
	Transport.Send(BuildSnapshotRequest(Agent.GetConfig(), *Credential, *Json), [this](const FHttpReply& Reply)
	{
		bSendInFlight = false;
		const FPusher::FOutcome Outcome = Pusher.FinishSend(Reply, Clock.Now());
		if (Outcome.bHalt)
		{
			Agent.OnAuthRejected();
			return;
		}
		if (Outcome.Response.IsSet())
		{
			Agent.SetCadence(Outcome.Response->Cadence);
		}
	});
}

void SatisDash::FDriver::TickCommands(double Now)
{
	// Task 5.
}

void SatisDash::FDriver::StopCommands()
{
	// Task 5.
}
```

(The two Task 5 bodies are empty only between Task 4 and Task 5, in the same branch; Task 5 fills them before its PR.)

- [ ] **Step 5: The two 2A fixes**

`Pusher.cpp`, in `FinishSend`'s `Ok` path, replace the recovery block with:

```cpp
			PopIfFront(Sent);
			if (FailureCount > 0)
			{
				UE_LOG(LogSatisDash, Display, TEXT("Pushing snapshots recovered after %d attempts (%d queued, %d dropped)"), FailureCount, Queue.Num(), DroppedInStreak);
				FailureCount = 0;
				DroppedInStreak = 0;
			}
			else if (DroppedInStreak > 0 && Queue.Num() <= MaxQueuedSnapshots / 2)
			{
				// A slow backend that never fails: the episode ends once the queue has drained to half (2A final review).
				UE_LOG(LogSatisDash, Display, TEXT("The snapshot queue has room again (%d dropped while the backend was slow)"), DroppedInStreak);
				DroppedInStreak = 0;
			}
			return Out;
```

`Agent.cpp`, in `Settle`, the bound branch becomes:

```cpp
	if (IsBoundHere())
	{
		if (!Config.EnrollmentCode.IsEmpty())
		{
			// One code never enrols twice (spec error table): the credential wins and the code is blanked.
			UE_LOG(LogSatisDash, Display, TEXT("An enrolment code is in SatisDashAgent.cfg, but this server is already enrolled; blanking it (revoke the server in the dashboard to enrol again)"));
			BlankCodeInFile();
		}
		// BlankCodeInFile adopts what the file says now, which may name another backend (2A final review, N2).
		if (IsBoundHere())
		{
			Enter(EAgentState::Running, TEXT("Enrolled; reporting to the backend"), false);
			return;
		}
	}
```

No unit test pins the Agent fix: its window is between two reads of the same file. S10 rests on the driver's `GetCredential()` gate, which `SatisDash.Core.Driver.Rebound` pins.

- [ ] **Step 6: Run the tests to see them pass**

C++ test command, `--min 56`, and update `mod-test.yml` to `--min 56`. Expected: 56 tests pass, 2A's included.

- [ ] **Step 7: Commit**

```bash
git -c core.safecrlf=false add mod .github/workflows/mod-test.yml api-sources.md
git commit -m "Core mod Plan 2B Task 4: the driver (enrol, sample, push, halt)"
```

---

### Task 5: The driver: commands (S6)

**Files:**
- Create: `mod/SatisDashAgent/Source/SatisDashCore/Private/DriverCommands.cpp`, `SatisDashCoreTests/Private/DriverCommandsTest.cpp`
- Modify: `Public/SatisDash/Driver.h` (command members), `Private/Driver.cpp` (remove the two empty bodies; `TickPush`'s handler sets `NextPollAt = 0` on `bCommandsPending`), `.github/workflows/mod-test.yml` (`--min 64`)

**Interfaces:**
- Consumes: `PlanCommand`, `FCommandLedger`, `ShouldRetryResult`, `CommandOk`, `CommandFailed`, `ResultAttempts` (Commands.h); `ParseCommandsResponse`, `ClassifyReply` (Responses.h); `BuildCommandsPoll`, `BuildResultRequest` (Requests.h); `BackoffSeconds`, `ParseRetryAfterSeconds` (Pusher.h); Task 4's fakes.
- Produces: `inline constexpr double SatisDash::MinPollSpacingSeconds = 1.0;` and the private members below.

- [ ] **Step 1: Write the failing tests**

`DriverCommandsTest.cpp`:

```cpp
#include "DriverFakes.h"
#include "SatisDash/Driver.h"

using namespace SatisDash;
using SatisDash::Test::FDriverFixture;

namespace
{
	const TCHAR* Poll = TEXT("/agent/v1/commands?waitSeconds=25");
	const TCHAR* Snapshots = TEXT("/agent/v1/snapshots");

	FString command(const TCHAR* Id, const TCHAR* Type, const TCHAR* Params, const FDateTime& Expires)
	{
		return FString::Printf(TEXT("{\"id\":\"%s\",\"type\":\"%s\",\"params\":%s,\"expiresAt\":\"%s\"}"), Id, Type, Params, *Expires.ToIso8601());
	}

	FHttpReply commands(const FString& Items)
	{
		return SatisDash::Test::Reply(200, FString::Printf(TEXT("{\"commands\":[%s]}"), *Items));
	}

	FString resultPath(const TCHAR* Id) { return FString::Printf(TEXT("/agent/v1/commands/%s/result"), Id); }

	/** Enrolled, with every snapshot answered, until the first poll is out. */
	void ready(FDriverFixture& F, FDriver& Driver)
	{
		F.enrol(Driver, SatisDash::Test::FakeSecret());
		F.run(Driver, 1);
		while (F.Net.answer(Snapshots, SatisDash::Test::SnapshotOk(false))) {}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommandsSetAutoPauseTest, "SatisDash.Core.Commands.SetAutoPause", SatisDash::Test::Flags)

bool FCommandsSetAutoPauseTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver(F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock());
	ready(F, Driver);
	TestEqual(TEXT("a poll is out"), F.Net.pending(Poll), 1);
	const FDateTime Expires = F.Utc + FTimespan::FromMinutes(5);
	F.Net.answer(Poll, commands(command(TEXT("c-1"), TEXT("set_auto_pause"), TEXT("{\"enabled\":true}"), Expires)));
	F.run(Driver, 1);
	TestEqual(TEXT("applied once"), F.Game.Applied.Num(), 1);
	TestTrue(TEXT("with enabled"), F.Game.Applied.Num() == 1 && F.Game.Applied[0]);
	TestEqual(TEXT("its result is posted"), F.Net.pending(*resultPath(TEXT("c-1"))), 1);
	SatisDash::Test::SameText(*this, TEXT("ok"), F.Net.lastBody(*resultPath(TEXT("c-1"))), TEXT("{\"ok\":true}"));
	F.Net.answer(*resultPath(TEXT("c-1")), SatisDash::Test::Reply(204, FString()));
	// The backend hands it out again (its answer was lost): it is reported again, never run again (S6).
	F.run(Driver, 1.5);
	F.Net.answer(Poll, commands(command(TEXT("c-1"), TEXT("set_auto_pause"), TEXT("{\"enabled\":true}"), Expires)));
	F.run(Driver, 1);
	TestEqual(TEXT("not applied twice"), F.Game.Applied.Num(), 1);
	TestEqual(TEXT("but reported again"), F.Net.sent(*resultPath(TEXT("c-1"))), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommandsUnsupportedTest, "SatisDash.Core.Commands.Unsupported", SatisDash::Test::Flags)

bool FCommandsUnsupportedTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver(F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock());
	ready(F, Driver);
	const FDateTime Expires = F.Utc + FTimespan::FromMinutes(5);
	F.Net.answer(Poll, commands(command(TEXT("c-2"), TEXT("SET_AUTO_PAUSE"), TEXT("{\"enabled\":true}"), Expires)
		+ TEXT(",") + command(TEXT("c-3"), TEXT("set_auto_pause"), TEXT("{\"enabled\":\"yes\"}"), Expires)));
	F.run(Driver, 1);
	SatisDash::Test::SameText(*this, TEXT("a type in another case is unsupported"), F.Net.lastBody(*resultPath(TEXT("c-2"))), TEXT("{\"ok\":false,\"code\":\"unsupported\"}"));
	F.Net.answer(*resultPath(TEXT("c-2")), SatisDash::Test::Reply(204, FString()));
	F.run(Driver, 1);
	SatisDash::Test::SameText(*this, TEXT("a wrong-type param is unsupported"), F.Net.lastBody(*resultPath(TEXT("c-3"))), TEXT("{\"ok\":false,\"code\":\"unsupported\"}"));
	TestEqual(TEXT("nothing applied"), F.Game.Applied.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommandsApplyFailsTest, "SatisDash.Core.Commands.ApplyFails", SatisDash::Test::Flags)

bool FCommandsApplyFailsTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	F.Game.bApplyWorks = false;
	FDriver Driver(F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock());
	ready(F, Driver);
	const FDateTime Expires = F.Utc + FTimespan::FromMinutes(5);
	F.Net.answer(Poll, commands(command(TEXT("c-4"), TEXT("set_auto_pause"), TEXT("{\"enabled\":false}"), Expires)));
	F.run(Driver, 1);
	SatisDash::Test::SameText(*this, TEXT("upstream_error"), F.Net.lastBody(*resultPath(TEXT("c-4"))), TEXT("{\"ok\":false,\"code\":\"upstream_error\"}"));
	F.Net.answer(*resultPath(TEXT("c-4")), SatisDash::Test::Reply(204, FString()));
	F.run(Driver, 1.5);
	F.Net.answer(Poll, commands(command(TEXT("c-4"), TEXT("set_auto_pause"), TEXT("{\"enabled\":false}"), Expires)));
	F.run(Driver, 1);
	TestEqual(TEXT("the ledger keeps the ID: tried once"), F.Game.Applied.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommandsResultRetriesTest, "SatisDash.Core.Commands.ResultRetries", SatisDash::Test::Flags)

bool FCommandsResultRetriesTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	FDriverFixture F;
	FDriver Driver(F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock());
	ready(F, Driver);
	const FString Path = resultPath(TEXT("c-5"));
	F.Net.answer(Poll, commands(command(TEXT("c-5"), TEXT("set_auto_pause"), TEXT("{\"enabled\":true}"), F.Utc + FTimespan::FromMinutes(5))));
	for (int32 Attempt = 0; Attempt < 10; ++Attempt)
	{
		F.run(Driver, 20);
		F.Net.answer(*Path, SatisDash::Test::Reply(503, FString()));
	}
	TestEqual(TEXT("five attempts, no more (commandRunner.ts RESULT_ATTEMPTS)"), F.Net.sent(*Path), ResultAttempts);
	int32 Lines = 0;
	for (const FString& Line : Log.Take()) { Lines += Line.Contains(TEXT("Couldn't report")) ? 1 : 0; }
	TestEqual(TEXT("one line when it gives up"), Lines, 1);
	TestTrue(TEXT("polling resumes"), F.Net.pending(Poll) == 1);
	SatisDash::Test::NoErrorLines(*this, Log);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommandsPollBackoffTest, "SatisDash.Core.Commands.PollBackoff", SatisDash::Test::Flags)

bool FCommandsPollBackoffTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	FDriverFixture F;
	FDriver Driver(F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock());
	ready(F, Driver);
	F.Net.answer(Poll, SatisDash::Test::Reply(0, FString()));
	F.run(Driver, 0.5);
	TestEqual(TEXT("no poll during the backoff"), F.Net.pending(Poll), 0);
	F.run(Driver, 1.5);
	TestEqual(TEXT("then one"), F.Net.pending(Poll), 1);
	F.Net.answer(Poll, SatisDash::Test::Reply(502, FString()));
	F.run(Driver, 4.5);
	F.Net.answer(Poll, commands(FString()));
	int32 Failed = 0;
	int32 Recovered = 0;
	for (const FString& Line : Log.Take())
	{
		Failed += Line.Contains(TEXT("Polling for commands failed")) ? 1 : 0;
		Recovered += Line.Contains(TEXT("Polling for commands recovered")) ? 1 : 0;
	}
	TestEqual(TEXT("one line when failures start"), Failed, 1);
	TestEqual(TEXT("one when they stop"), Recovered, 1);
	F.run(Driver, 1.5);
	F.Net.answer(Poll, SatisDash::Test::Reply(401, TEXT("{}")));
	TestTrue(TEXT("a 401 on the poll halts"), Driver.GetAgent().GetState() == EAgentState::Halted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommandsSpacingTest, "SatisDash.Core.Commands.Spacing", SatisDash::Test::Flags)

bool FCommandsSpacingTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver(F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock());
	ready(F, Driver);
	F.Net.answer(Poll, commands(FString()));
	// The next poll is sent; this time it's answered at once, the way an empty long poll can come back.
	F.runUntil(Driver, [&F] { return F.Net.pending(TEXT("/agent/v1/commands?waitSeconds=25")) > 0; }, 5);
	F.Net.answer(Poll, commands(FString()));
	F.run(Driver, 0.5);
	TestEqual(TEXT("an empty answer is spaced by 1 s from the poll's start"), F.Net.pending(Poll), 0);
	F.run(Driver, 0.5);
	TestEqual(TEXT("then polled again"), F.Net.pending(Poll), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommandsPendingHintTest, "SatisDash.Core.Commands.PendingHint", SatisDash::Test::Flags)

bool FCommandsPendingHintTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver(F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock());
	ready(F, Driver);
	for (int32 I = 0; I < 4; ++I)
	{
		F.runUntil(Driver, [&F] { return F.Net.pending(TEXT("/agent/v1/commands?waitSeconds=25")) > 0; }, 30);
		F.Net.answer(Poll, SatisDash::Test::Reply(0, FString()));
	}
	// The fourth failure waits 8 s (2^3 with Random01 = 1), longer than a status cycle.
	F.run(Driver, 0.5);
	TestEqual(TEXT("waiting out a backoff"), F.Net.pending(Poll), 0);
	// commandRunner.ts notifyPending: a snapshot answer saying a command waits ends the wait.
	F.runUntil(Driver, [&F] { return F.Net.pending(TEXT("/agent/v1/snapshots")) > 0; }, 6);
	F.Net.answer(Snapshots, SatisDash::Test::SnapshotOk(true));
	F.run(Driver, 0.5);
	TestEqual(TEXT("polled at once"), F.Net.pending(Poll), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommandsHaltTest, "SatisDash.Core.Commands.Halt", SatisDash::Test::Flags)

bool FCommandsHaltTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver(F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock());
	ready(F, Driver);
	const FDateTime Expires = F.Utc + FTimespan::FromMinutes(5);
	F.Net.answer(Poll, commands(command(TEXT("c-6"), TEXT("set_auto_pause"), TEXT("{\"enabled\":true}"), Expires)
		+ TEXT(",") + command(TEXT("c-7"), TEXT("set_auto_pause"), TEXT("{\"enabled\":false}"), Expires)));
	F.run(Driver, 1);
	F.Net.answer(*resultPath(TEXT("c-6")), SatisDash::Test::Reply(401, TEXT("{}")));
	TestTrue(TEXT("a 401 on a result halts"), Driver.GetAgent().GetState() == EAgentState::Halted);
	F.run(Driver, 60);
	TestEqual(TEXT("the waiting command is dropped, not run"), F.Game.Applied.Num(), 1);
	TestEqual(TEXT("and nothing is polled"), F.Net.pending(Poll), 0);
	// Re-enrolled: the same ID handed out again is reported from the ledger, not run again.
	F.writeConfig(TEXT("http://127.0.0.1:1"), TEXT("ZZZZ-2222"));
	F.run(Driver, 61);
	F.Net.answer(TEXT("/agent/v1/enroll"), SatisDash::Test::Reply(201, SatisDash::Test::EnrollJson()));
	F.run(Driver, 1);
	while (F.Net.answer(Snapshots, SatisDash::Test::SnapshotOk(false))) {}
	F.Net.answer(Poll, commands(command(TEXT("c-6"), TEXT("set_auto_pause"), TEXT("{\"enabled\":true}"), Expires)));
	F.run(Driver, 1);
	TestEqual(TEXT("still applied once"), F.Game.Applied.Num(), 1);
	return true;
}
```

Run (`--min 64`). Expected: the new tests fail (no poll is ever sent).

- [ ] **Step 2: The command members**

In `Driver.h`, replace `// Task 5 adds the command members here.` with:

```cpp
		struct FReport
		{
			FString Id;
			FCommandResult Result;
			FDateTime ExpiresAt;
			int32 Attempts = 0;
			double NextAt = 0;
		};
		void SendPoll(double Now);
		void FinishPoll(const FHttpReply& Reply);
		void RunNext(double Now);
		void SendReport();
		void FinishReport(const FHttpReply& Reply);

		FCommandLedger Ledger;
		TArray<FAgentCommand> Waiting;
		TOptional<FReport> Report;
		bool bPollInFlight = false;
		bool bReportInFlight = false;
		double NextPollAt = 0;
		double PollStartedAt = 0;
		int32 PollFailures = 0;
		FRateLimitedLog SkipLog{60.0};
```

and add above the class: `inline constexpr double MinPollSpacingSeconds = 1.0; // commandRunner.ts MIN_POLL_SPACING_MS`.

In `Driver.cpp`, delete the two empty Task 5 bodies, and in `TickPush`'s handler, after `Agent.SetCadence(...)`, add:

```cpp
			if (Outcome.Response->bCommandsPending)
			{
				NextPollAt = 0; // commandRunner.ts notifyPending: end the wait before the next poll
			}
```

In `Tick()`, where a run starts (`!bRunning && bNowRunning`), add `NextPollAt = Now; PollFailures = 0;`.

- [ ] **Step 3: `DriverCommands.cpp`**

```cpp
#include "SatisDash/Driver.h"

#include "SatisDash/Log.h"

void SatisDash::FDriver::StopCommands()
{
	// The ledger stays: an ID that ran never runs again, even after a halt and a new enrolment (S6).
	Waiting.Reset();
	if (!bReportInFlight)
	{
		Report.Reset();
	}
}

void SatisDash::FDriver::TickCommands(double Now)
{
	// One thing at a time, as commandRunner.ts does: a poll, then each command and its report in turn.
	if (bPollInFlight || bReportInFlight)
	{
		return;
	}
	if (Report.IsSet())
	{
		if (Now >= Report->NextAt)
		{
			SendReport();
		}
		return;
	}
	if (Waiting.Num() > 0)
	{
		RunNext(Now);
		return;
	}
	if (Now >= NextPollAt)
	{
		SendPoll(Now);
	}
}

void SatisDash::FDriver::SendPoll(double Now)
{
	const FCredential* Credential = Agent.GetCredential();
	if (Credential == nullptr)
	{
		return;
	}
	bPollInFlight = true;
	PollStartedAt = Now;
	Transport.Send(BuildCommandsPoll(Agent.GetConfig(), *Credential), [this](const FHttpReply& Reply) { FinishPoll(Reply); });
}

void SatisDash::FDriver::FinishPoll(const FHttpReply& Reply)
{
	bPollInFlight = false;
	if (Agent.GetCredential() == nullptr)
	{
		return; // halted or rebound while the poll was out: what it brought is not ours to run
	}
	const double Now = Clock.Now();
	const EReplyKind Kind = ClassifyReply(Reply.Code, true);
	if (Kind == EReplyKind::AuthRejected)
	{
		Agent.OnAuthRejected();
		return;
	}
	TOptional<TArray<FAgentCommand>> Commands;
	if (Kind == EReplyKind::Ok)
	{
		Commands = ParseCommandsResponse(Reply);
	}
	if (!Commands.IsSet())
	{
		if (PollFailures == 0)
		{
			UE_LOG(LogSatisDash, Warning, TEXT("Polling for commands failed (%d); retrying with backoff"), Reply.Code);
		}
		NextPollAt = Now + FMath::Max(ParseRetryAfterSeconds(Reply.RetryAfter).Get(0.0), BackoffSeconds(PollFailures, Clock.Random01()));
		++PollFailures;
		return;
	}
	if (PollFailures > 0)
	{
		UE_LOG(LogSatisDash, Display, TEXT("Polling for commands recovered after %d attempts"), PollFailures);
		PollFailures = 0;
	}
	Waiting = MoveTemp(Commands.GetValue());
	NextPollAt = PollStartedAt + MinPollSpacingSeconds;
}

void SatisDash::FDriver::RunNext(double Now)
{
	const FAgentCommand Command = Waiting[0];
	Waiting.RemoveAt(0);
	const FCommandPlan Plan = PlanCommand(Command, Ledger, Clock.UtcNow());
	FCommandResult Result = Plan.Result;
	switch (Plan.Action)
	{
	case ECommandAction::Skip:
		if (SkipLog.ShouldLog(TEXT("skipped"), Now))
		{
			UE_LOG(LogSatisDash, Warning, TEXT("Skipped a command that has expired, expires too far ahead or is already running"));
		}
		return;
	case ECommandAction::Report:
		break;
	case ECommandAction::SetAutoPause:
		if (Game.ApplyAutoPause(Plan.bEnabled))
		{
			Result = CommandOk();
			UE_LOG(LogSatisDash, Display, TEXT("Applied set_auto_pause from the dashboard: auto-pause is now %s"), Plan.bEnabled ? TEXT("on") : TEXT("off"));
		}
		else
		{
			// S10: the read-back didn't agree, so nothing is claimed; the ledger still records the ID.
			Result = CommandFailed(TEXT("upstream_error"));
			UE_LOG(LogSatisDash, Warning, TEXT("Couldn't apply set_auto_pause; reporting upstream_error"));
		}
		Ledger.Finish(Command.Id, Result);
		break;
	}
	FReport Next;
	Next.Id = Command.Id;
	Next.Result = Result;
	Next.ExpiresAt = Command.ExpiresAt;
	Next.NextAt = Now;
	Report = Next;
}

void SatisDash::FDriver::SendReport()
{
	const FCredential* Credential = Agent.GetCredential();
	if (Credential == nullptr)
	{
		Report.Reset();
		return;
	}
	bReportInFlight = true;
	++Report->Attempts;
	Transport.Send(BuildResultRequest(Agent.GetConfig(), *Credential, Report->Id, Report->Result), [this](const FHttpReply& Reply) { FinishReport(Reply); });
}

void SatisDash::FDriver::FinishReport(const FHttpReply& Reply)
{
	bReportInFlight = false;
	if (!Report.IsSet() || Agent.GetCredential() == nullptr)
	{
		Report.Reset();
		return;
	}
	const EReplyKind Kind = ClassifyReply(Reply.Code, true);
	if (Kind == EReplyKind::AuthRejected)
	{
		Report.Reset();
		Agent.OnAuthRejected();
		return;
	}
	if (Kind != EReplyKind::Transient)
	{
		Report.Reset(); // taken, or refused for good (commandRunner.ts: a fatal answer isn't retried)
		return;
	}
	const FDateTime UtcNow = Clock.UtcNow();
	if (ShouldRetryResult(Report->Attempts, Kind, Report->ExpiresAt, UtcNow))
	{
		Report->NextAt = Clock.Now() + FMath::Max(ParseRetryAfterSeconds(Reply.RetryAfter).Get(0.0), BackoffSeconds(Report->Attempts - 1, Clock.Random01()));
		return;
	}
	if (UtcNow < Report->ExpiresAt)
	{
		UE_LOG(LogSatisDash, Warning, TEXT("Couldn't report a command's result after %d attempts"), Report->Attempts);
	}
	Report.Reset();
}
```

- [ ] **Step 4: Run the tests to see them pass**

C++ test command, `--min 64`; `mod-test.yml` to `--min 64`. Expected: 64 pass.

- [ ] **Step 5: Commit**

```bash
git -c core.safecrlf=false add mod .github/workflows/mod-test.yml
git commit -m "Core mod Plan 2B Task 5: the driver's commands"
```

Tasks 2-5 can share one PR (Task 4's empty bodies never reach `main` alone). Before it merges: `gh workflow run mod-test.yml --ref <branch>`, owner approval, then merge.

---

### Task 6: The driver in the game, the backend host, the recorder, and the enrol run

**Files:**
- Modify: `mod/SatisDashAgent/Source/SatisDashGame/Private/SatisDashGameModule.cpp` (replaces Task 1's wiring)
- Create: `harness/src/recorder.ts`, `harness/src/backend-host.ts`, `harness/src/core-runs/enrol.ts`, `runs/enrol/pass.ts`
- Create: `harness/test/recorder.test.ts`, `harness/test/backend-host.test.ts`, `harness/test/enrol-pass.test.ts`
- Modify: `harness/src/core-run.ts` (`emptyBoot`), `harness/test/core-run.test.ts`, `harness/src/backend.ts` (several servers; revoke; commands; relay), `harness/test/backend.test.ts`, `harness/src/frm.ts` (`parseGetPowerFull`), `harness/test/frm.test.ts`, `harness/src/config.ts` (`backendHost`), `harness/test/config.test.ts`, `harness/src/cli.ts` (`backend-host`), `harness/src/core-runs/index.ts`, `docs/runner.md`, `api-sources.md`

**Interfaces:**
- Consumes: `FDriver`, `FDriverClock` (Task 4/5), `FGameReader` (Task 3), `FHttpTransport` (Task 1); `runCore`, `CoreRunSpec`, `CoreContext` (Task 1); `startBackend`, `randomCode` (`backend.ts`); `readApiStatus` (`status.ts:28`), `sessionHash` (`status.ts:6`), `readAutoPause`, `setAutoPause` (`frm.ts:70`, `:79`).
- Produces:
  - `startRecorder(upstream: string): Promise<Recorder>`; `interface Recorder { url: string; entries: RecordedCall[]; close(): Promise<void> }`; `interface RecordedCall { at: string; agent: "mod" | "other"; method: string; path: string; status: number; authorization: boolean; snapshot?: unknown; commandIds?: string[]; result?: unknown }`
  - `type ServerKey = "mod" | "node"`; `interface HostClient { relayUrl: string; seedCode(server: ServerKey, code: string): Promise<void>; revoke(server: ServerKey): Promise<void>; queueCommand(server: ServerKey, params: Record<string, unknown>): Promise<string>; command(id: string): Promise<{ status: string; resultCode: string | null }>; state(server: ServerKey): Promise<{ codeConsumed: boolean; agentVersion: string | null; lastSeen: boolean; revoked: boolean }>; stopRelay(): Promise<void>; startRelay(): Promise<void> }`
  - `serveBackendHost(opts)`, `connectBackendHost(file: string): Promise<HostClient>`; `harness backend-host`
  - `parseGetPowerFull(body: unknown): FrmCircuitGroup[]`
  - `enrolPhase(ctx, host, recorder, opts?)` (reused by Tasks 7-9)

- [ ] **Step 1: The module, driving `FDriver`**

`SatisDashGameModule.cpp` becomes:

```cpp
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "GameReader.h"
#include "HAL/PlatformTime.h"
#include "HttpTransport.h"
#include "Misc/CoreMisc.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "SatisDash/Driver.h"

class FSatisDashGameModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// The editor loads this module for the unit-test build; only a dedicated server runs the agent.
		if (!IsRunningDedicatedServer())
		{
			return;
		}
		// The entry map is a game world too; each loaded save is a new one (S6). The reader always reads the newest.
		WorldInitHandle = FWorldDelegates::OnPostWorldInitialization.AddLambda([this](UWorld* InWorld, const UWorld::InitializationValues)
		{
			if (InWorld != nullptr && InWorld->IsGameWorld())
			{
				Reader.SetWorld(InWorld);
			}
		});
		SatisDash::FDriverClock Clock;
		Clock.Now = [] { return FPlatformTime::Seconds(); };
		Clock.UtcNow = [] { return FDateTime::UtcNow(); };
		Clock.Random01 = [] { return double(FMath::FRand()); };
		Driver = MakeUnique<SatisDash::FDriver>(FPaths::ProjectDir() / TEXT("Configs/SatisDashAgent.cfg"),
			FPaths::ProjectSavedDir() / TEXT("SatisDash/credential.json"), Transport, Reader, MoveTemp(Clock));
		Driver->Boot();
		// The core ticker runs on the game thread (spec "Runtime"); a tick costs a few comparisons unless something is due.
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("SatisDashAgent"), 0.5f, [this](float)
		{
			Driver->Tick();
			return true;
		});
	}

	virtual void ShutdownModule() override
	{
		FTSTicker::RemoveTicker(TickHandle);
		FWorldDelegates::OnPostWorldInitialization.Remove(WorldInitHandle);
		// No reply handler may run into a destroyed driver: cancel first, then destroy.
		Transport.CancelAll();
		Driver.Reset();
	}

private:
	SatisDash::FHttpTransport Transport;
	SatisDash::FGameReader Reader;
	TUniquePtr<SatisDash::FDriver> Driver;
	FDelegateHandle WorldInitHandle;
	FTSTicker::FDelegateHandle TickHandle;
};

IMPLEMENT_MODULE(FSatisDashGameModule, SatisDashGame)
```

C++ test command, `--min 64`; check-api. Expected: green.

- [ ] **Step 2: Extend `backend.ts` (tests first)**

In `harness/test/backend.test.ts`, with the existing fake `Exec`, add cases that assert the SQL each new method runs:
- `seedCode("node", code)` inserts the server `core-node` only if it isn't there (`ON CONFLICT (public_id) DO NOTHING`, `public_id` is `UNIQUE`, `1790294400000_core_tables.sql:71`) and adds the code;
- `revoke("mod")` runs `UPDATE agents.agent_credentials SET revoked_at = now() ...` for `core-mod` (`1790899200000_agents.sql:37`);
- `queueCommand("mod", { enabled: true })` inserts into `agents.commands (server_id, type, params, expires_at)` with `'set_auto_pause'`, the params as a JSON literal, `now() + interval '5 minutes'`, and returns the `RETURNING id` value (`1790985600000_agent_commands.sql`);
- `command(id)` refuses an id that isn't a UUID before any SQL runs;
- `stopRelay()`/`startRelay()` run `docker compose ... stop spike-relay` / `start spike-relay`.

Then change `backend.ts`: `SERVER_PUBLIC_ID` becomes `const PUBLIC_IDS = { mod: "core-mod", node: "core-node", s5: "spike-s5" } as const`, the methods take a `server` key (S5's call sites pass `"s5"`, so S5 is unchanged), params are serialised with `JSON.stringify` and validated as `{ enabled: unknown }` objects of at most 1 key before they're quoted (`'...'::jsonb`, single quotes doubled), and `state(server)` also returns `revoked: revoked_at IS NOT NULL`. Run the backend tests: PASS, S5's included.

- [ ] **Step 3: The backend host (ruling 10)**

`harness/test/backend-host.test.ts` drives `serveBackendHost` with a fake `Backend` and pins:
- it listens on `127.0.0.1` only, and writes the host file `{ relayUrl, controlUrl, token }` with a 64-hex-character token;
- every call without `Authorization: Bearer <token>` gets 401 and touches nothing; a wrong token too (compare with `timingSafeEqual`);
- bodies are strict: `POST /seed { server: "mod"|"node", code }` with `code` matching `^[A-Z2-7]{4}-[A-Z2-7]{4}$`; `POST /revoke { server }`; `POST /command { server, params }` returns `{ id }`; `GET /command/:id` (UUID only); `GET /state/:server`; `POST /relay/stop`, `POST /relay/start`; anything else 404;
- on `close()` it calls `backend.down()` and deletes the host file;
- `connectBackendHost(file)` reads the file and returns a `HostClient` whose calls carry the token.

`harness/src/backend-host.ts`: `serveBackendHost({ backend, file, port })` (a `node:http` server on `127.0.0.1:<port>`, zod bodies, `randomBytes(32).toString("hex")` as the token, the host file written with `writeFileSync(file, ..., { mode: 0o600 })`), and `connectBackendHost(file)` (fetch with `AbortSignal.timeout(10_000)`).

`config.ts`: `backendHost: z.strictObject({ file: z.string().min(1), port: z.number().int().min(1024).max(65535) }).optional()`; a test in `config.test.ts`. `realCoreDeps` sets `backendHost: () => connectBackendHost(config.backendHost.file)` when present.

`cli.ts`: `backend-host` (owner's session only): needs `s5` and `backendHost` in the config, runs `startBackend` (`backend.ts:70`), then `serveBackendHost`, prints `backend host up: relay <url>; Ctrl+C to stop`, and on `SIGINT` closes it (which runs `down -v` and deletes the host file).

`docs/runner.md`, "The backend host": the owner starts it in their own terminal before dispatching a backend run, the host file lives in the runner's config folder (outside the repository), readable by the owner and `satis-runner` only (give the `icacls` line), and it is stopped after the runs. It serves only fixed operations on the throwaway database: no SQL and no shell from the caller.

- [ ] **Step 4: The recorder (ruling 11)**

`harness/test/recorder.test.ts` runs a fake upstream on loopback and pins:
- every request is forwarded (method, path, headers, body) and the upstream's status and body come back unchanged;
- a gzipped snapshot body (`content-encoding: gzip`) is recorded decompressed; a body over 2 MB is forwarded but not recorded;
- `agent` is `"mod"` when the User-Agent starts `satisfactory-dash-mod/`;
- `authorization` is a boolean, and **nothing recorded contains the Authorization value, an enrol request body (the code) or an enrol response body (the secret)** (assert on `JSON.stringify(recorder.entries)`);
- `snapshot.status.sessionName` is replaced by its `sessionHash` (bundles are public);
- `/agent/v1/commands?...` records the command IDs from the response; `/commands/:id/result` records the request body;
- when the upstream refuses the connection, the client's socket is destroyed (the mod sees no answer, as in a real outage), and the entry's `status` is 0.

`harness/src/recorder.ts`: a `node:http` server on `127.0.0.1:0` that pipes each request to the upstream with `node:http.request`, buffers at most 2 MB of each body for recording, and records per the rules above (`zlib.gunzipSync` for gzip).

- [ ] **Step 5: FRM's full power figures**

`frm.ts`: `parseGetPowerFull(body)` reads `CircuitGroupID`, `PowerProduction`, `PowerConsumed`, `PowerCapacity`, `PowerMaxConsumed`, `BatteryCapacity`, `BatteryPercent`, `BatteryDifferential`, `FuseTriggered` (field names as satisfactory-dash `packages/game-adapter/src/rawSchemas.ts:125-136` parses them) into `FrmCircuitGroup { groupId, production, consumption, capacity, maxConsumption, batteryCapacity, batteryPercent, batteryDifferential, fuseTriggered }`. A test with the S3 fixture plus the extra fields.

- [ ] **Step 6: The enrol run's pass conditions (tests first)**

`runs/enrol/pass.ts`:

```ts
// The enrol run (spec "Testing" 2): enrol with a seeded code, write the credential, blank the code, and have the
// snapshots accepted. Every native reading is compared with the game's APIs (C8, C9, power; Task 3, ruling 7).
import type { CoreEvidence } from "../../harness/src/core-run";
import type { Verdict } from "../../harness/src/verdict";

export const requirements = ["enrol", "C8", "C9", "power", "S10", "S13"];
const NEED_WORLD = 10;
const NEED_EMPTY = 3;
const tickClose = (a: number, b: number) => Math.abs(a - b) <= Math.max(2, 0.1 * Math.max(a, b)); // S6
const within = (a: number, b: number) => Math.abs(a - b) <= 0.01 * Math.max(Math.abs(a), Math.abs(b), 1); // S3
const SNAPSHOT_KEYS = new Set(["agentVersion", "observedAt", "reachable", "paused", "status", "power", "settings"]);

export interface ApiStatus { running: boolean; paused: boolean; players: number; limit: number; duration: number; tickRate: number; health: string; session: string }
export interface ModStatus { isGameRunning: boolean; gamePaused: boolean; connectedPlayers: number; playerLimit: number; totalGameDurationSeconds: number; tickRate: number; tickHealth: string; sessionName: string }
export interface Circuit { circuitGroupId: number; productionMW: number; consumptionMW: number; capacityMW: number; maxConsumptionMW: number; fuseTriggered: boolean; batteryCapacityMWh: number; batteryPercent: number; batteryDifferentialMW: number }
export interface Round { at: string; world: boolean; snapshot: Record<string, unknown>; api: ApiStatus | null; autoPause: boolean | null; frm: Array<{ groupId: number; production: number; consumption: number; capacity: number; maxConsumption: number; batteryCapacity: number; batteryPercent: number; batteryDifferential: number; fuseTriggered: boolean }> | null }
export interface Enrol { state: { codeConsumed: boolean; agentVersion: string | null; lastSeen: boolean }; codeBlanked: boolean; credentialWritten: boolean; refused: number; rounds: Round[] }

export function compareRound(r: Round): string[] {
  const out: string[] = [];
  const s = r.snapshot.status as ModStatus | undefined;
  for (const k of Object.keys(r.snapshot)) if (!SNAPSHOT_KEYS.has(k)) out.push(`${r.at}: S13: unexpected snapshot field ${k}`);
  if (r.snapshot.reachable !== true) out.push(`${r.at}: reachable is not true`);
  if (!s || !r.api) return [...out, `${r.at}: no status to compare`];
  const a = r.api;
  if (s.isGameRunning !== a.running) out.push(`${r.at}: isGameRunning ${s.isGameRunning} vs API ${a.running}`);
  if (s.gamePaused !== a.paused) out.push(`${r.at}: gamePaused ${s.gamePaused} vs API ${a.paused}`);
  if (r.snapshot.paused !== s.gamePaused) out.push(`${r.at}: paused doesn't match status.gamePaused`);
  if (s.connectedPlayers !== a.players || s.playerLimit !== a.limit) out.push(`${r.at}: players ${s.connectedPlayers}/${s.playerLimit} vs API ${a.players}/${a.limit}`);
  if (Math.abs(s.totalGameDurationSeconds - a.duration) > 10) out.push(`${r.at}: duration ${s.totalGameDurationSeconds} vs API ${a.duration}`);
  if (!tickClose(s.tickRate, a.tickRate)) out.push(`${r.at}: tick rate ${s.tickRate} vs API ${a.tickRate}`);
  if (s.tickHealth !== a.health) out.push(`${r.at}: tick health ${s.tickHealth} vs API ${a.health}`);
  if (s.sessionName !== a.session) out.push(`${r.at}: session hash differs`);
  const settings = r.snapshot.settings as { autoPause?: boolean } | undefined;
  if (r.autoPause !== null && settings?.autoPause !== r.autoPause) out.push(`${r.at}: autoPause ${settings?.autoPause} vs GetServerOptions ${r.autoPause}`);
  const power = (r.snapshot.power as { circuits?: Circuit[] } | undefined)?.circuits;
  if (!r.world) {
    if (power !== undefined) out.push(`${r.at}: power sent with no game running`);
    return out;
  }
  if (!power || !r.frm) return [...out, `${r.at}: no power to compare`];
  const ids = (xs: number[]) => [...xs].sort((x, y) => x - y).join(",");
  if (ids(power.map((c) => c.circuitGroupId)) !== ids(r.frm.map((g) => g.groupId))) {
    return [...out, `${r.at}: circuit groups ${ids(power.map((c) => c.circuitGroupId))} vs FRM ${ids(r.frm.map((g) => g.groupId))}`];
  }
  for (const c of power) {
    const g = r.frm.find((x) => x.groupId === c.circuitGroupId)!;
    const pairs: Array<[string, number, number]> = [
      ["production", c.productionMW, g.production], ["consumption", c.consumptionMW, g.consumption], ["capacity", c.capacityMW, g.capacity],
      ["maxConsumption", c.maxConsumptionMW, g.maxConsumption], ["batteryCapacity", c.batteryCapacityMWh, g.batteryCapacity],
      ["batteryPercent", c.batteryPercent, g.batteryPercent], ["batteryDifferential", c.batteryDifferentialMW, g.batteryDifferential],
    ];
    for (const [name, mod, frm] of pairs) if (!within(mod, frm)) out.push(`${r.at}: group ${c.circuitGroupId} ${name} ${mod} vs FRM ${frm}`);
    if (c.fuseTriggered !== g.fuseTriggered) out.push(`${r.at}: group ${c.circuitGroupId} fuse ${c.fuseTriggered} vs FRM ${g.fuseTriggered}`);
  }
  return out;
}

export function evaluate(e: CoreEvidence): Verdict {
  const d = e.captures["enrol.json"] as Enrol | undefined;
  if (!d) return { pass: false, reasons: ["no enrol capture"] };
  const reasons: string[] = [];
  if (!d.state.codeConsumed) reasons.push("the code was not consumed");
  if (d.state.agentVersion !== "mod/0.1.0") reasons.push(`agentVersion ${d.state.agentVersion}, expected mod/0.1.0`);
  if (!d.state.lastSeen) reasons.push("the backend never saw an authenticated call");
  if (!d.codeBlanked) reasons.push("the code is still in SatisDashAgent.cfg");
  if (!d.credentialWritten) reasons.push("no credential.json was written");
  if (d.refused) reasons.push(`the backend refused ${d.refused} snapshot(s)`);
  const empty = d.rounds.filter((r) => !r.world);
  const world = d.rounds.filter((r) => r.world);
  if (empty.length < NEED_EMPTY) reasons.push(`only ${empty.length} rounds with no game, need ${NEED_EMPTY}`);
  if (world.length < NEED_WORLD) reasons.push(`only ${world.length} rounds with the world, need ${NEED_WORLD}`);
  for (const r of d.rounds) reasons.push(...compareRound(r));
  if (!e.lines.some((l) => /Enrolled; reporting to the backend/.test(l))) reasons.push("S9: no enrolment line");
  return reasons.length ? { pass: false, reasons } : { pass: true, reasons: [`enrolled; ${empty.length} rounds with no game and ${world.length} with the world match the game's APIs`] };
}
```

`harness/test/enrol-pass.test.ts`: a clean capture passes; each of a consumed-code false, an unblanked code, a power part in a no-game round, a group set that differs from FRM's, a battery percent off by 2%, an extra top-level snapshot field and a tick rate off by 20% fails with its reason; a joined group (two FRM circuits, one group id) compares as one row.

- [ ] **Step 7: The enrol run**

`harness/src/core-runs/enrol.ts` exports `enrolPhase` (reused by Tasks 7-9) and the spec:

```ts
import { readFileSync } from "node:fs";
import * as Pass from "../../../runs/enrol/pass";
import type { CoreContext, CoreRunSpec } from "../core-run";
import { MOD } from "../core-run";
import { randomCode } from "../backend";
import type { HostClient } from "../backend-host";
import { parseGetPowerFull, readAutoPause } from "../frm";
import { modConfigPath } from "../mod-config";
import { startRecorder, type Recorder } from "../recorder";
import { createServerApi } from "../server-api";
import { readApiStatus } from "../status";

const MIN = 60_000;
const accepted = (rec: Recorder) => rec.entries.filter((e) => e.agent === "mod" && e.path === "/agent/v1/snapshots" && e.status === 200);

/** Seeds a code, points the mod at the recorder, and waits for the enrolment and a first accepted snapshot. */
export async function enrolPhase(ctx: CoreContext, host: HostClient, recorder: Recorder): Promise<void> {
  const code = randomCode();
  await host.seedCode("mod", code);
  ctx.writeConfig({ backendUrl: recorder.url, enrollmentCode: code });
  await ctx.waitFor("an accepted snapshot", () => (accepted(recorder).length ? true : undefined), 3 * MIN);
  ctx.noteSecret(); // before any revoke deletes the file
}

/** Waits for each new accepted snapshot and reads the game's APIs right after it. */
async function collect(ctx: CoreContext, recorder: Recorder, world: boolean, want: number, frmBaseUrl: string | undefined): Promise<Pass.Round[]> {
  const api = createServerApi(ctx.deps.config.apiBaseUrl);
  const rounds: Pass.Round[] = [];
  let seen = accepted(recorder).length;
  while (rounds.length < want) {
    await ctx.waitFor("the next accepted snapshot", () => (accepted(recorder).length > seen ? true : undefined), MIN);
    const entry = accepted(recorder)[seen]!;
    seen = accepted(recorder).length;
    const [status, autoPause, frm] = await Promise.all([
      readApiStatus(api).catch(() => null),
      readAutoPause(api).catch(() => null),
      world && frmBaseUrl ? fetch(new URL("/getPower", frmBaseUrl), { signal: AbortSignal.timeout(2000) }).then(async (r) => parseGetPowerFull(await r.json())).catch(() => null) : Promise.resolve(null),
    ]);
    rounds.push({ at: entry.at, world, snapshot: entry.snapshot as Record<string, unknown>, api: status, autoPause, frm });
  }
  return rounds;
}

export const enrol: CoreRunSpec = {
  requirements: Pass.requirements,
  backend: true,
  world: true,
  emptyBoot: true,
  evaluate: Pass.evaluate,
  async drive(ctx) {
    const host = await ctx.deps.backendHost!();
    const recorder = await startRecorder(host.relayUrl);
    try {
      await enrolPhase(ctx, host, recorder);
      const rounds = await collect(ctx, recorder, false, 4, undefined);
      await ctx.deps.loadWorld!();
      await ctx.deps.waitHealthy();
      ctx.rememberAutoPause(await ctx.deps.readAutoPause!());
      await ctx.deps.setAutoPause!(false); // the factory runs, so power moves (S3)
      await ctx.deps.sleep(MIN); // the world settles
      rounds.push(...(await collect(ctx, recorder, true, 12, ctx.deps.config.frmBaseUrl)));
      const cfg = JSON.parse(readFileSync(modConfigPath(ctx.deps.config.serverDir, MOD), "utf8")) as { enrollmentCode?: string };
      ctx.captures["enrol.json"] = {
        state: await host.state("mod"),
        codeBlanked: cfg.enrollmentCode === "",
        credentialWritten: ctx.credentialExists(),
        refused: recorder.entries.filter((e) => e.agent === "mod" && e.path === "/agent/v1/snapshots" && e.status >= 400).length,
        rounds,
      } satisfies Pass.Enrol;
    } finally {
      ctx.captures["recorder.json"] = recorder.entries.map(({ snapshot: _, ...rest }) => rest);
      await recorder.close();
    }
  },
};
```

The snapshot bodies in `enrol.json` carry the session as a hash only (the recorder's rule), and circuit IDs and MW only.

The "no game" rounds need a boot with no game: the test server auto-loads its last save (S6 run 1). Add `emptyBoot?: boolean` to `CoreRunSpec`, `readAutoLoad`/`setAutoLoad` to `CoreDeps`, and to `runCore` the pre-boot that `runSpike` does for S6 (`run.ts:253-266`: boot, read the auto-load session, clear it, stop) with the session put back in the outer `finally`. Set `emptyBoot: true` on `enrol`. A `core-run.test.ts` case pins that the auto-load session is restored when `drive` throws.

Register `enrol` in `core-runs/index.ts`.

- [ ] **Step 8: Redirected poll (S3, Gate A)**

Extend `http-probe` so that, after the driver is in the game, the stub also answers `GET /agent/v1/commands` with a 302 to the redirect target during the redirect phase. The amended S3 rules apply to those requests too (no Authorization, no body).

- [ ] **Step 9: Run the harness tests, then the enrol run**

`npm test`, `npm run typecheck`, check-api: green. Commit:

```bash
git -c core.safecrlf=false add mod harness runs docs/runner.md api-sources.md
git commit -m "Core mod Plan 2B Task 6: the driver in the game, the backend host, the recorder and the enrol run"
```

The run itself: the owner starts `npm run harness -- backend-host` in their own session; then `core-run enrol` runs as `satis-runner` (add `enrol` to `core-run.yml`'s options once this PR is on `main`, and dispatch it there; the owner approves). A failed run stops the work (CLAUDE.md rule 3): report the reasons and the bundle. Ruling 7's `[NEEDS VERIFICATION]` is resolved by this run's power rounds; record the outcome in the PR.

---

### Task 7: The revoke and command runs

**Files:**
- Create: `harness/src/core-runs/revoke.ts`, `harness/src/core-runs/command.ts`, `runs/revoke/pass.ts`, `runs/command/pass.ts`, `harness/test/revoke-pass.test.ts`, `harness/test/command-pass.test.ts`
- Modify: `harness/src/core-runs/index.ts`, `.github/workflows/core-run.yml` (options)

**Interfaces:**
- Consumes: `enrolPhase`, `startRecorder`, `HostClient` (Task 6).
- Produces: the `revoke` and `command` specs.

- [ ] **Step 1: Ask the dash session one question**

"satisfactory-dash-mod Plan 2B: after `agents.agent_credentials.revoked_at` is set for a server, does `POST /agent/v1/enroll` with a new code for that same server replace the credential (the table's key is `server_id`), or must the server be re-created?" Record the answer in `runs/revoke/pass.ts`'s header. If it can't be re-enrolled, the re-enrol half of the run seeds a second server instead, and the PR says so.

- [ ] **Step 2: Pass conditions, tests first**

`runs/revoke/pass.ts` judges a capture `{ revokedAt, halts: string[] (timestamps of the halt line), afterRevoke: RecordedCall[], reenrolled: { enrolStatus: number; acceptedAfter: number }, credentialAfterHalt: boolean }`:
- the halt line (`The backend rejected the credential (401)`) appears exactly once, at most 15 s after `revokedAt` (one status cycle is 5 s; the spec says "within one cycle", plus the one request in flight);
- after the first 401, no snapshot, poll or result request reaches the recorder until the re-enrolment;
- `credential.json` is gone after the halt;
- the re-enrolment answered 201 and at least 3 snapshots were accepted after it.

`runs/command/pass.ts` judges `{ before: boolean, flipped: { status, resultCode, apiAfter: boolean, results: number }, unsupported: { status, resultCode, applied: boolean }, restored: { status, apiAfter } }`:
- the flip command is `succeeded` with no result code, `GetServerOptions` reads `!before` after it, and exactly one result was posted for its ID (`results === 1`);
- the command with `params: { enabled: "yes" }` is `failed` with `unsupported`, and auto-pause didn't change;
- the restore command is `succeeded` and `GetServerOptions` reads `before`;
- the mod logged `Applied set_auto_pause` exactly twice.

Each pass file gets a test file covering a clean capture and each reason.

- [ ] **Step 3: The drives**

`revoke.ts`: no world. `enrolPhase`; wait for 3 accepted snapshots; `host.revoke("mod")` and note the time; wait up to 30 s for the halt line in the log (`ctx.deps.readLogSince(ctx.offset)`); wait 2 min more, recording what reaches the recorder; check `credentialExists()`; seed a new code and write it to the config; wait up to 3 min for a 201 and 3 accepted snapshots. Capture as above.

`command.ts`: world (C9 needs a game: S7 skipped the entry map). `enrolPhase`; load the world; `rememberAutoPause(before)`; `queueCommand("mod", { enabled: !before })`; wait up to 90 s for `command(id).status` to leave `pending`/`sent`; read the API; count the recorder's result posts for that ID; then the `{ enabled: "yes" }` command (the table's CHECK allows any object, `1790985600000_agent_commands.sql`); then a restore command `{ enabled: before }`. Capture as above.

- [ ] **Step 4: Tests, commit, runs**

`npm test`, typecheck, check-api. Commit (`Core mod Plan 2B Task 7: the revoke and command runs`), PR, merge after the mod-test run, then dispatch `revoke` and `command` with the backend host up. Report each verdict.

---

### Task 8: The outage and tick-cost runs, and the outbound-traffic check (S12)

**Files:**
- Create: `harness/src/core-runs/outage.ts`, `harness/src/core-runs/tick-cost.ts`, `harness/src/outbound.ts`, `runs/outage/pass.ts`, `runs/tick-cost/pass.ts`
- Create: `harness/test/outbound.test.ts`, `harness/test/outage-pass.test.ts`, `harness/test/tick-cost-pass.test.ts`
- Modify: `harness/src/core-runs/index.ts`, `.github/workflows/core-run.yml`, `harness/src/core-run.ts` (`CoreRunSpec.baseline?: boolean`)

**Interfaces:**
- Consumes: `sampleTicks`, `median`, `TICK_INTERVAL_MS`, `TICK_WARMUP_MS` (`ticks.ts`), `enrolPhase`, `HostClient.stopRelay/startRelay`.
- Produces: `parseConnections(json: string, listening: number[]): string[]`, `sampleOutbound(rootPid: number): Promise<string[]>`.

- [ ] **Step 1: The outbound sampler, tests first**

`harness/test/outbound.test.ts` feeds `parseConnections` the JSON that `Get-NetTCPConnection | Select-Object LocalAddress,LocalPort,RemoteAddress,RemotePort,State,OwningProcess | ConvertTo-Json` prints, and pins: connections whose local port is one the process listens on are inbound and left out; `Listen` and `TimeWait` rows are left out; the result is the set of `remote:port` strings, sorted; a single object (PowerShell prints one row without an array) parses too.

`harness/src/outbound.ts`: `sampleOutbound(rootPid)` runs `pwsh -NoProfile -Command` with a script that collects the process tree (`Get-CimInstance Win32_Process` by `ParentProcessId`, starting at `rootPid`), its listening ports, and its established or SYN-sent TCP connections as JSON, then calls `parseConnections`.

- [ ] **Step 2: Pass conditions, tests first**

`runs/outage/pass.ts` judges `{ before: TickSeries, during: TickSeries, after: TickSeries, stoppedAt, startedAt, acceptedAfter: string[] (observedAt of snapshots accepted after the restart) }` and the log lines:
- exactly one `Pushing snapshots failed` line and exactly one `Pushing snapshots recovered` line; at most one `Polling for commands failed` and one `recovered`;
- the snapshots observed during the outage are delivered afterwards: at least 80% of `180 / 5` observedAt values inside `[stoppedAt, startedAt]` appear in `acceptedAfter` (the queue holds 120);
- the tick median during the outage is at least 95% of the median before it (S2's tolerance, `spikes/S2/pass.ts:7`).

`runs/tick-cost/pass.ts` judges `{ baseline: TickSeries, withMod: TickSeries, outboundBaseline: string[], outboundWithMod: string[], recorder: string }`:
- C4's rule (S2's): the with-mod median is at least 95% of the baseline's; both windows have samples;
- S12: every endpoint in `outboundWithMod` that isn't in `outboundBaseline` is the recorder's `127.0.0.1:<port>`.

Tests for both, covering a clean capture and each reason.

- [ ] **Step 3: The drives**

`outage.ts`: world, auto-pause off; `enrolPhase`; ticks for 3 min (`sampleTicks` with `warmupMs: 0`, `durationMs: 3 min`); `host.stopRelay()`; ticks for 3 min; `host.startRelay()`; wait 3 min; capture.

`tick-cost.ts` adds `baseline: true` to the spec, and `runCore` boots once without the mod first (as `runSpike` does for S2, `run.ts:228-241`): world loaded, auto-pause off, `TICK_WARMUP_MS` then 10 minutes of samples, with `sampleOutbound(proc.pid)` every 30 s. Then the mod's boot: `enrolPhase`, world, auto-pause off, the same window and sampling. Capture as above. Never `pid` of the harness itself: `proc.pid` is the server's launcher, and the tree walk finds the game process under it.

- [ ] **Step 4: Tests, commit, runs**

As Task 7: `npm test`, typecheck, check-api; commit (`Core mod Plan 2B Task 8: the outage and tick-cost runs`); after merge, dispatch `outage` and `tick-cost` with the backend host up.

---

### Task 9: Parity with the Node agent (30 minutes)

**Files:**
- Create: `harness/src/core-runs/node-agent.ts`, `harness/src/core-runs/parity.ts`, `runs/parity/pass.ts`, `harness/test/node-agent.test.ts`, `harness/test/parity-pass.test.ts`
- Modify: `harness/src/config.ts` (`parity`), `harness/src/core-runs/index.ts`, `.github/workflows/core-run.yml`, `docs/runner.md`

**Interfaces:**
- Consumes: `enrolPhase`, `startRecorder` (both agents behind it), `HostClient.seedCode("node", ...)`, `compareRound`'s comparison rules (Task 6).
- Produces: `startNodeAgent(opts): Promise<{ stop(): Promise<void> }>`, the `parity` spec.

- [ ] **Step 1: Ask the dash session**

Send: "satisfactory-dash-mod Plan 2B, parity run: we run a second Node agent from a satisfactory-dash build at `ab4626f` on the test server (127.0.0.1:17777, FRM on its loopback port), enrolled as its own server in the throwaway backend, side by side with the mod for 30 minutes. (1) Is recording the snapshots each agent got 200 for, through a loopback proxy, acceptable as 'stored snapshots', or is there a read route you'd rather we use? (2) `agent set-tokens` prompts for the game API token: on a test server started with `FG.DedicatedServer.AllowInsecureLocalAccess=1`, what token should it get, and does piping it on stdin work? (3) Anything else the agent needs (`SD_AGENT_HOME` for a throwaway store, the build command)? Nothing touches `satisfactory-dash-deploy` or the live agent." Record the answers in `runs/parity/pass.ts`'s header. **Never touch `ReactApps/satisfactory-dash-deploy`.**

- [ ] **Step 2: The Node agent launcher, tests first**

`config.ts`: `parity: z.strictObject({ agentCjs: z.string().min(1) }).optional()`: the path to a `dist/agent.cjs` built from the pinned checkout, copied where `satis-runner` can read it (docs/runner.md says how). `node-agent.test.ts` pins, with a fake spawner: the agent runs with `SD_AGENT_HOME` set to a fresh temp dir that is deleted on `stop()`; `set-tokens --host 127.0.0.1 --api-port <api port> --frm-port <frm port>` gets its token on stdin and nothing is logged of it; `enroll <code> --url <recorder url>`; then `run`, which `stop()` kills (tree). The exact commands follow the dash session's answers.

- [ ] **Step 3: Pass conditions, tests first**

`runs/parity/pass.ts` pairs each accepted mod snapshot with the Node agent's accepted snapshot nearest in `observedAt`, kept only when they are at most 2.5 s apart (half a status cycle), and judges: at least `30 * 60 / 5 * 0.8` pairs; in every pair, status (exact for running, paused, players, limit, session hash, tick health; S6's tick-rate rule; duration within 10 s), `settings.autoPause` exact, and power by group with S3's rule (`within` 1%), a joined group compared as one row; and both agents' snapshots all accepted. Any mismatch fails the run naming the round and the field: never widen a tolerance to pass (CLAUDE.md rule 3); if production legitimately moves faster than the 2.5 s pairing allows, that goes to the owner.

- [ ] **Step 4: The drive**

World loaded, auto-pause off; both codes seeded (`"mod"`, `"node"`); the recorder in front of the relay; the mod enrolled with `enrolPhase`, the Node agent started with the other code against the recorder; 30 minutes; both stopped; captures: the pairs (hashed sessions, IDs and MW only) and `host.state` for both servers.

- [ ] **Step 5: Tests, commit, run**

`npm test`, typecheck, check-api; commit (`Core mod Plan 2B Task 9: parity with the Node agent`); after merge, dispatch `parity` with the backend host up. Send the verdict to the dash session too (it owns the cut-over plan).

---

### Task 10: The security review, and v1's evidence

**Files:**
- Create: `docs/reviews/2026-10-core-mod-v1-security.md` (the reviewer's report, kept as written)
- Modify: `README.md` (what the mod is now, how it's configured, where the evidence is), `docs/runner.md`, `.github/workflows/mod-test.yml` (final `--min`)

- [ ] **Step 1: The full suite**

C++ test command with `--min 64`; `npm test`; `npm run typecheck`; check-api. Then `gh workflow run mod-test.yml --ref <branch>`; the owner approves.

- [ ] **Step 2: The security review (spec "The security review")**

Dispatch the `security-reviewer` agent, fresh (never a fork), with: the spec's Security section; the instruction to check the finished mod and harness against S1-S14 **row by row**, citing for each the code and the run bundle (or unit test) that proves it; and to write its full report to `docs/reviews/2026-10-core-mod-v1-security.md` and reply in 10 lines or fewer. Give it these known open items to rule on: S11's `0600` on Linux and the deep-nesting JSON check on Linux (sub-project 4); Gate A's outcome for S3; the backend host's control API (ruling 10); the recorder's redaction rules; PercentEncode leaving `.` in command IDs (2A final, Node parity).

- [ ] **Step 3: Fix or accept**

Each finding is fixed in this branch (with its test), or recorded in the report as an accepted risk **with the owner's say** (through the coordinator). Re-run whatever a fix touches: its unit tests, and the core run that covers it.

- [ ] **Step 4: v1's evidence**

The run bundles are machine-written and never edited. List every core run's bundle (run, verdict, `startedAt`, the workflow run URL) in the PR description, with the security report's link. The spike evidence lives in satisfactory-dash (#478); ask the dash session whether v1's bundles go there the same way, and hand them over as it says.

- [ ] **Step 5: Commit and PR**

```bash
git -c core.safecrlf=false add docs/reviews README.md docs/runner.md .github/workflows/mod-test.yml
git commit -m "Core mod Plan 2B Task 10: the security review and v1's evidence"
```

v1 is done when the unit tests, every harness run and the parity run pass, and the security review's findings are fixed or accepted (spec "Testing"). Tell the owner and the dash session; deprecating the Node agent is sub-project 5.

---

## Self-review (done when this plan was written)

**Spec coverage.**
- Shape: Config, CredentialStore, Enroller, Halt are 2A's `FAgent`, driven by Task 4. Sampler is Tasks 3 and 4. Conform is 2A. Pusher is Task 4. CommandRunner is Task 5.
- Runtime: game thread, `FTSTicker`, async HTTP (Tasks 1 and 6); `LogSatisDash` throughout.
- Portability: no Windows-only API in the mod; `outbound.ts` is harness-only.
- Data flow: Task 4 (cadence, parts, auto-pause with status, no world gives no power).
- Error table:
  - rows 1-7 are 2A's, exercised again by Task 4's driver tests;
  - "backend down": Task 8's outage run;
  - "unknown command type" and "write fails": Task 5;
  - "a reader fails": Task 4 (`Parts`).
- Security rows:
  - S1: Task 1's probe (untrusted certificate) plus 2A's URL rules.
  - S2 and S3: Task 1 (Gate A).
  - S4: Task 1 (cap and refusal) plus 2A's parsers.
  - S5 and S6: 2A, plus Task 5.
  - S7: 2A.
  - S8: every core run (`commonReasons`), plus Tasks 4 and 5.
  - S9: the probe's lines and the enrol and revoke runs.
  - S10: Tasks 4 and 5.
  - S11: 2A, with Linux deferred.
  - S12: Task 8.
  - S13: Task 6.
  - S14: `build-mod.ps1`'s package check.
- Testing 2: enrol (Task 6), revoke and command (Task 7), outage and tick cost (Task 8), secrets (all runs).
- Testing 3: parity (Task 9). The security review is Task 10.

**Placeholders.** Three answers come from the dash session at run time: the TLS minimum, re-enrol after revoke, and the Node agent's invocation. Each step says what to ask, where to record the answer, and what changes for each answer. The harness drives for Tasks 7-9 are written as exact steps and pass conditions rather than full listings: they compose Task 6's `enrolPhase`, recorder and host client, which are given in full.

**Type consistency.**
- `ITransport::Send(const FRequestSpec&, FReplyHandler)` and `IGameReader`'s four methods are used the same way in Tasks 1, 3, 4, 5 and 6.
- `FDriver(FString, FString, ITransport&, IGameReader&, FDriverClock)` is the same in Task 4's tests and Task 6's module.
- `MakeReply(const FRawReply&, const FString&, FCappedBody&, EReplyRefusal&)` matches between Task 1's test and its implementation.
- `HostClient` methods match Tasks 6-9: `seedCode`, `revoke`, `queueCommand`, `command`, `state`, `stopRelay`, `startRelay`.
- The capture names are fixed: `probe.json`, `enrol.json`, `recorder.json` and `leaks.json`.

**Review Focus.** Each of the five lines names the test that pins it.
