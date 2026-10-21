# Core mod v1: security review (Plan 2B, Task 10)

Reviewed at `main` 34207ff (branch `feat/core-mod-2b-security`). This was a read-only review against the spec's
Security section (`docs/specs/2026-10-20-core-mod-v1-design.md`, S1-S14, OWASP ASVS 5.0 L2). The reviewer read the
code, the unit tests, the pass conditions and the seven latest passing run bundles. It ran no game and no workflow.

## Summary

- **S1-S14: 11 MET, 3 PARTLY, 0 NOT MET.** PARTLY: S4 (deeply nested JSON is unverified), S11 (Linux `0600`
  is unproven and has a write-then-chmod window) and S14 (the release dependency list is deferred to sub-project 4).
- **Highest finding: medium (F1).** No test, on any platform, covers a deeply nested JSON response. A hostile or
  compromised backend can send 64 KB of nesting into the game thread's JSON parse and free. Nobody has checked whether
  that overflows the stack and takes the server down. A depth pre-scan before parsing fixes it cheaply on every
  platform.
- Everything else is low or info. The transport is solid:
  - redirects are off and https-only, with a TLS 1.2 floor and peer and host verification;
  - the URL allow-list is checked twice;
  - headers are checked for injection;
  - the body cap is enforced during the download;
  - the secret goes only in the header and only to the backend that issued it (an exact `BackendUrl` binding);
  - a 401 deletes the credential;
  - commands are allow-listed, run once each via the ledger, and checked by reading the setting back.
- Mandatory checks:
  - PII and local-path scan of tracked files (fixed-string): one hit, `NOTICE:1`, the copyright line. That is the
    deliberate, CI-documented exception (`.github/workflows/ci.yml:34-38`, commit 7dbfd6e), so not a finding.
  - `npm audit` in `harness/`: 0 vulnerabilities.
  - The seven bundles were scanned for enrolment-code-shaped strings, `agentSecret`, `Bearer `, local paths and the
    owner's name: none found.

## S1-S14

| # | Verdict | Implemented at | Proven by | Gap |
| --- | --- | --- | --- | --- |
| S1 https + verification; http only for loopback | MET | `SatisDashCore/Private/Config.cpp:16-94` (`IsAllowedBackendUrl`, `IsAllowedRequestUrl`); `SatisDashGame/Private/CurlTransport.cpp:182` (checked again before any curl call), `:227` (protocols), `:230-235` (VERIFYPEER 1, VERIFYHOST 2, engine roots via `AddCertificatesToSslContext`, `:138-142`); `:12-14` (no build without WITH_SSL) | Unit: `SatisDash.Core.Config.BackendUrl` (look-alike host, userinfo, query/fragment, http elsewhere). Run: http-probe 39516817799, `runs/http-probe/pass.ts:76-77` (an untrusted certificate is refused and no data arrives) | The probe's certificate is wrong in both chain and name at once, so VERIFYHOST alone (a trusted chain with the wrong name) has no test. Info only, because the option is set and can't be skipped (`bSet` fails closed, `:267`). |
| S2 TLS 1.2 floor, no maximum | MET | `CurlTransport.cpp:232` (`CURL_SSLVERSION_TLSv1_2`, no MAX) | Run: http-probe 39516817799, `runs/http-probe/pass.ts:66-73` (ClientHello offers 1.3 and 1.2, has supported_versions, nothing older) | None. Self-hosters: see the rulings. |
| S3 no redirects; the target receives nothing | MET | `CurlTransport.cpp:225-228` (FOLLOWLOCATION 0, MAXREDIRS 0, REDIR_PROTOCOLS https); `SatisDashCore/Private/Reply.cpp:67-73` (3xx, or an effective URL that isn't the requested one, is refused) | Unit: `SatisDash.Core.Reply.Make`. Run: http-probe 39516817799, `runs/http-probe/pass.ts:51-63` (no request, Authorization header or body at the target; a snapshot is re-sent after the redirect; a redirected commands poll was tested (Task 6 addition, `harness/src/probe-stub.ts:126-127`); the S9 line was logged) | The enrol POST isn't redirected in the probe. It uses the same code path, so info only. |
| S4 64 KB cap, `application/json`, strict schema | **PARTLY** | `Reply.cpp:30-40` (cap during download), `CurlTransport.cpp:101-107` (a short write aborts the transfer); `SatisDashCore/Private/Responses.cpp:23-41` (cap, content type), `:43-158` (typed fields; wrong type or out of range rejects the whole response) | Unit: `SatisDash.Core.Reply.CappedBody`, `SatisDash.Core.Responses.Limits` (64 KB and 64 KB+1, html, no content type, malformed, array, empty), `.Enroll`, `.Snapshot`, `.Commands`, `.Cadence` | **Deep nesting has no test anywhere** (F1). The 2A note `[NEEDS VERIFICATION]` is open on Windows too, not just Linux. |
| S5 cadence clamped to 1-3600 s | MET | `Responses.cpp:43-67` (clamped as a double, rejected if not finite); also Retry-After capped at 300 s (`SatisDashCore/Private/Pusher.cpp:12-25`) and backoff capped at 60 s (`:5-10`) | Unit: `SatisDash.Core.Responses.Cadence` (0, 10^6, 1e300, -5, string, null), `SatisDash.Core.Driver.Cadence`, `SatisDash.Core.Pusher.RetryAfter` | None |
| S6 only `set_auto_pause` with a boolean; once per ID; IDs length-checked | MET | `SatisDashCore/Private/Commands.cpp:76-119` (one field exactly, boolean, case-sensitive type), `:39-60` (ledger); `Responses.cpp:146` (1-100 chars), `:152` (20 per poll); `DriverCommands.cpp:7` (the ledger survives a halt) | Unit: `SatisDash.Core.Commands.Plan`, `.Ledger`, `.CaseSensitive`, `.Unsupported`, `SatisDash.Core.Responses.Commands` (101-char ID). Run: command 39531378852 (`{ enabled: "yes" }` is unsupported and changes nothing; one accepted result per command) | Ledger eviction at 1000 entries (F7, info) |
| S7 secret only in the Authorization header | MET | `SatisDashCore/Private/Requests.cpp:21-24`; URLs are built from config and constants only (`:77-117`) | Unit: `SatisDash.Core.Requests.Authenticated` ("the secret is never in a URL"). The recorder stores Authorization only as a boolean (`harness/src/recorder.ts:115`) | None |
| S8 secret and code never logged | MET | Every `UE_LOG` in the mod logs states, codes and origins only; CURLOPT_VERBOSE and DEBUGFUNCTION are never set (`CurlTransport.cpp:220-221`); the failure word is fixed text (`:66-98`); the origin, not the path, goes in the log (`:412-415`) | Unit: `SatisDash.Core.Log.NoSecrets` (whole secret, 8-character prefix, codes). Runs: `leaks.json` is `{code:false, secret:false, bearer:false}` in all 7 bundles (`harness/src/core-run.ts:354`, `commonReasons` `:140-144`) | The harness matches whole values only. Partial matches are unit-tested only. Info. |
| S9 security events logged once | MET | Enrolled or rejected: `SatisDashCore/Private/Agent.cpp:212`, `:218-219`; 401 and halt: `:239`; TLS failure: `CurlTransport.cpp:410-416`; refused response: `:402-409` (each rate-limited) | Runs: enrol 39614789400 (`runs/enrol/pass.ts:129`); revoke 39523768471 (halt line exactly once, `runs/revoke/pass.ts:57-62`); http-probe 39516817799 (redirect line `:63`, failed-TLS line `:79`). Unit: `SatisDash.Core.Agent.CodeRejected`, `SatisDash.Core.Log.OncePerStreak` | Evidence gaps (F6, info): no harness run sees the "code rejected" line; the TLS check matches the "Couldn't reach the backend at (origin)" prefix and doesn't assert `TlsError` |
| S10 fail closed | MET | `Agent.cpp:14-17` (a credential is given out only when Running and bound to the configured URL), `:112-118` (broken config means idle), `LoadConfigFile` writes no code when the file is missing (`Config.cpp:164-173`); `Driver.cpp:37-63` (no sending when not running; queue cleared); `SatisDashGame/Private/GameReader.cpp:57-71` (read-back; `DriverCommands.cpp:121-127`) | Unit: `SatisDash.Core.Agent.FailClosed`, `.Boot`, `.BackendBinding`, `.EditDuringEnrol`, `SatisDash.Core.Commands.ApplyFails`, `SatisDash.Core.Driver.Halt`, `.StaleAuth`. Runs: enrol 39614789400 (S10 in its requirements), revoke 39523768471 (no request more than 1 s after the first 401; credential deleted) | None |
| S11 credential only in `Saved/SatisDash/credential.json`, atomic; `0600` on Linux | **PARTLY** | `SatisDashGame/Private/SatisDashGameModule.cpp:85-86` (path); `SatisDashCore/Private/Files.cpp:13-50` (temp, then rename; Linux chmod `:25-31`); `SatisDashCore/Private/Credential.cpp:79-108` (recovery, delete) | Unit: `SatisDash.Core.Files.Atomic`, `SatisDash.Core.Credential.Store`, `.Crash`. Run: enrol 39614789400 (`credentialWritten`, code blanked) | Linux is unbuilt and untested (sub-project 4). The chmod comes **after** the secret is written, a TOCTOU window (F2) |
| S12 contacts only the backend | MET | One `CURLOPT_URL`, built from `Config.BackendUrl` (`Requests.cpp:79-113`, `CurlTransport.cpp:222`); no telemetry code; the plugin has no other network module (`SatisDashGame.Build.cs`) | Run: tick-cost 39600584441, S12 (exactly one new endpoint, the recorder; `harness/src/outbound.ts`) | The check samples TCP connections only (no UDP or DNS; anything shorter than a sampling interval is missed). Environment proxy variables are honoured (F3). Both info or low. |
| S13 game data only | MET | `SatisDashCore/Private/Snapshot.cpp:61-122` (contract fields only; the session name is a contract field) | Unit: `SatisDash.Core.Snapshot.Fixtures`, `.Shape`. Run: enrol 39614789400, `runs/enrol/pass.ts:47` (top-level keys allow-listed in every round) | The harness checks top-level keys only; nested fields rely on the unit fixture. Info. |
| S14 reproducible, pinned, dependencies listed, no test code shipped | **PARTLY** | `harness/toolchain.json` (engine 5.6.1, SML 3.12.0); `mod/SatisDashAgent/SatisDashAgent.uplugin` (tests are an `Editor` module); `scripts/build-mod.ps1:47-48` (packaging fails if test code is present); mod-test builds in CI | mod-test run 39655793147 (74/74 at 34207ff) | The release dependency list (libcurl 8.12.1, OpenSSL 1.1.1t, nghttp2, zlib) and release CI are sub-project 4. The uplugin's SML requirement is `^3.12.0`, a range, not the pin. |

## Further findings (ranked)

**F1: medium. A deeply nested JSON response is unverified and could crash the game thread.**
`SatisDashCore/Private/Responses.cpp:39-40` and `SatisDashCore/Private/JsonUtil.cpp:8-17` hand up to 64 KB from the
backend to `FJsonSerializer::Deserialize` on the game thread.

- **Scenario.** A compromised backend, an impostor holding a valid certificate, or a self-hoster's broken proxy sends
  `{"cadence":` followed by about 65,000 `[`.
  - Whether UE's parser, or the recursive destruction of the resulting `FJsonValue` tree, overflows the stack was
    never checked. There is no test and no engine source was available to this review.
  - If it does, the dedicated server crashes on every snapshot answer. That defeats the "game never notices the mod"
    asset and the threat table's hostile-response row.
- **Fix:**
  1. In `ReadJsonBody`, before parsing, scan the bytes once, tracking strings and escapes. Reject the body when the
     `[`/`{` depth exceeds a small bound: the contract's deepest legitimate shape is about 4, so 32 is generous.
  2. Add a unit test with 64 KB of `[` and of `{"a":` nesting, at 33 levels and at the cap.
  3. Apply the same guard to `ParseObject`'s other callers (config and credential files: local, but cheap).

  The check is platform-independent, so it also closes the Linux half of the open item.

**F2: low (Linux only, sub-project 4). The credential's temp file holds the secret before it is made `0600`.**
`SatisDashCore/Private/Files.cpp:21-31` writes the text with the process umask (typically `0644`), then calls `chmod`.

- **Scenario.** On a shared Linux host, another local account opens `credential.json.tmp` during that window and
  keeps the file descriptor. It then holds the agent secret and can post snapshots and poll commands as this server
  until revoked.
- **Fix:** create the file with `open(path, O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC, 0600)`, or create it empty, `chmod`
  it, then write. Don't use `umask`: it is process-wide and the game is multithreaded. The harness's Linux mode check
  should run the file through an enrolment, not only a unit write.

**F3: low. libcurl honours the environment's proxy variables.**
`CurlTransport.cpp:220-221` sets no `CURLOPT_PROXY`/`CURLOPT_NOPROXY`, so libcurl's defaults read `https_proxy`,
`HTTPS_PROXY`, `http_proxy`, `ALL_PROXY` and `NO_PROXY`.

- For **https** this is a CONNECT tunnel: TLS still runs end to end with the backend, verified as configured. The
  proxy sees the host name and timing, never the secret or the body. S12 then holds as "the backend, via the
  admin's proxy".
- For **plain-http loopback** (harness, or a self-hoster using `http://127.0.0.1`), an `http_proxy` without a
  matching `NO_PROXY` sends the request, **including `Authorization: Bearer`**, in clear to the proxy. This review
  did not verify libcurl 8.12.1's own loopback exemption.
- Only someone who sets the server process's environment can cause this, which already means the host's admin.
- **Fix:**
  1. When `bPlainHttp`, set `CURLOPT_PROXY` to `""` (proxy off).
  2. For https, decide explicitly: keep honouring the environment (corporate hosts may need it) and say so in the
     host guide, or turn it off the same way. Either way, record the choice.

**F4: low. A command ID of `.` or `..` changes the result URL's path.**
`SatisDashCore/Private/Requests.cpp:63-75` keeps `.` (Node parity), and `CurlTransport.cpp` doesn't set
`CURLOPT_PATH_AS_IS`. libcurl's URL parser therefore removes dot segments: `/agent/v1/commands/../result` becomes
`/agent/v1/result`.

- **Scenario.** A hostile backend issues ID `..`. The mod POSTs the result with its Bearer header to a different
  path on the **same** origin (no cross-host leak). `MakeReply`'s same-place check then refuses the answer as
  "redirected" (`Reply.cpp:69`). The run logs a misleading S9 redirect line and retries 3 times.
- **Fix:** reject the poll (or skip and log) when an ID is all dots. A stricter alternative is to require
  `[A-Za-z0-9_-]{1,100}`, if the backend's IDs are UUIDs, as the harness host assumes (`backend-host.ts:107`).
  Either keeps Node parity for every real ID.

**F5: low (verify in sub-project 4). DNS resolution on the game thread.**
`curl_multi_perform` runs on the game thread (`CurlTransport.cpp:281`). If a platform's libcurl build uses a
synchronous resolver, a slow or failing DNS lookup stalls the game tick for up to the connect timeout (15 s; 35 s for
a poll).

- **Scenario.** The resolver times out and the server freezes for a long poll's timeout. That breaks "the mod never
  stalls the server".
- The outage run cut the relay but kept DNS working, so this case was never exercised. The Windows build appears
  fine (tick-cost passed), but this review didn't check whether the resolver is asynchronous on either platform.
- **Fix:** at startup, read `curl_version_info()` and require `CURL_VERSION_ASYNCHDNS`; stay idle with one log line
  if it's absent. Add a "DNS unreachable" outage variant to sub-project 4's Linux runs.

**F6: info. S9 evidence gaps.**
- `runs/http-probe/pass.ts:79` doesn't assert `(TlsError)`. A TLS failure misclassified as `ConnectionError` would
  still pass.
- The "code rejected" line (`Agent.cpp:218-219`) is unit-tested only.
- **Fix:** tighten the regex. Optionally seed a bad code in the enrol run.

**F7: info. Ledger eviction.**
`Commands.cpp:54-58` evicts the oldest entry after 1,000. With 20 commands per poll at 1 s spacing, a hostile backend
can evict an ID in about 50 s and replay it within its 600 s lifetime. A backend can always issue a fresh ID, so
at-most-once only guards against honest retries. **Accept.**

**F8: info. Stale doc comment.**
`SatisDashCore/Public/SatisDash/Config.h:43` still describes the old four-dash-four code format. Fix the comment.

## Known open items (rulings)

| Item | Ruling | Reasoning |
| --- | --- | --- |
| S11 `0600` on Linux; deep-nesting JSON on Linux | **0600: defer** to sub-project 4, with F2's fix. **Nesting: fix now** (F1) | The mode only matters once Linux builds exist, but the write-then-chmod order should be fixed when they do. The nesting risk isn't Linux-specific: there's no test on Windows either, and the depth pre-scan is cheap and platform-independent. |
| Gate A outcome for S3 (own libcurl: FOLLOWLOCATION 0, TLS 1.2 floor, VERIFYPEER 1 / VERIFYHOST 2, http only for loopback) | **Closed. No action** | Implemented as described (`CurlTransport.cpp:182`, `:225-235`). http-probe 39516817799 proves it for both the snapshot POST and the commands GET, with nothing at the target. Every option failing to set fails the request (`:267`). |
| Harness backend host's control API (ruling 10) | **Accept** | Bound to `127.0.0.1` (`harness/src/backend-host.ts:138`). The token is a 256-bit random Bearer compared with `timingSafeEqual` (`:81-86`). Bodies are capped. Routes are fixed and schema-checked. A browser on the runner can't send a custom Authorization header cross-origin without a preflight the server never answers. Note: `mode: 0o600` (`:142`) does nothing on Windows; the runner account's profile ACL protects the host file. Harness-only, and the token is deleted on close. |
| Recorder's redaction rules | **Accept** | It's an allow-list, not a deny-list (`harness/src/recorder.ts:103-147`): Authorization is stored as a boolean; enrol bodies and answers are never recorded; only snapshot bodies, command IDs and result codes are kept, with paths stripped of their queries. The bundle scan found no codes, secrets, `Bearer` or paths. Minor: `sessionHash` is an unsalted 12-hex SHA-1 (`harness/src/status.ts:6`), so a guessable session name can be recovered. Salt it per run if session names are ever sensitive (both sides are hashed in the harness, so a per-run salt keeps the comparison). |
| PercentEncode leaves `.` in command IDs | **Accept, with F4's guard** | `.` is unreserved (RFC 3986) and Node's `encodeURIComponent` keeps it, so parity holds. Only the all-dots IDs are a problem, and the guard handles them without breaking parity for real IDs. |
| OpenSSL 1.1.1t (engine's copy, end of life) | **Accept as a risk; track it** | The mod can't ship a different OpenSSL without forking the engine's SSL module, and the game's own HTTP uses the same library, so the mod adds no new exposure class. Mitigations in place: TLS 1.2 floor, full verification, one host, client-only. List it in the release dependency list (S14, sub-project 4) and revisit when the game's engine moves. |
| No certificate pinning; engine pins not applied | **Accept** | The backend is behind Cloudflare, whose leaf and intermediate rotate, so pinning would cause outages. The engine's pins cover Epic's endpoints, not this one. Trust comes from the engine's root store (`AddCertificatesToSslContext`). There's no revocation checking either; accepted on the same basis. |
| Environment proxy variables | **Fix the loopback case; document https** (F3) | See F3. The secret stays inside TLS for https. For plain-http loopback it would go to a proxy in clear. |
| S2 for self-hosted backends | **Accept; document in the host guide** (sub-project 4) | The mod enforces its own 1.2 floor whatever the backend offers, so S2 holds for self-hosters. They need a publicly trusted certificate (there's no custom-CA option), or `http://127.0.0.1` when the backend is on the same machine. |
| Enrolment ignores Retry-After | **Accept** (optional small fix later) | Backoff is 0.5-60 s with jitter (`Pusher.cpp:5-10`), codes expire in 10 minutes, and the backend rate-limits. Worst case is about 10-20 extra enrol attempts per code. To be polite, reuse `ParseRetryAfterSeconds` in `Agent::FinishEnroll` the way the pusher and poll do. |
| Ticker lifetime; CancelAll at shutdown or unload: use after free? | **No use after free found. Accept** | `ShutdownModule` (`SatisDashGameModule.cpp:63-77`) removes the driver's ticker, then the world delegate, then `CancelAll`, which removes the transport's ticker, releases every handle and drops handlers unrun. Only then does `curl_global_cleanup` run. Members are destroyed driver first, then reader, then transport (`:80-86`). The destructor's second `CancelAll` touches no curl state. `Tick` takes finished requests out of `InFlight` before delivering them and stops on a generation change (`CurlTransport.cpp:319-339`), so a handler that sends or cancels is safe. Everything is single-threaded on the game thread. Cheap hardening: guard `RemoveTicker(TickHandle)` with a validity check for the path where `StartupModule` returned early (the semantics of an invalid handle weren't checked against `Ticker.h:66`). |
| Circuit scan up to 10,000 IDs | **Accept** (not a security issue) | It's a bounded loop over game data with no backend input. Circuits with IDs of 10,000 or more would be silently missed, a completeness question for the parity work, and the tick cost on very large saves is unmeasured. If big-save tick cost matters, revisit when sub-project 5 replaces the Node agent. |
| Flaky tick-cost test "stops and rejects when the server exits (review M1)" | **No security relevance** | `harness/test/tick-cost.test.ts:79-93` races `setTimeout(0)` against the fake sampler. It guards measurement validity (C4), not any S row, and a flake can only fail a test, never pass a run. Make the exit deterministic as test hygiene; it doesn't block v1. |
| Revoke, command and outage predate #39 (code format) | **Doesn't matter** | Since probe-7's harness commit (7ce421d), the only production change in `mod/` is `IsEnrollmentCode` (`Config.cpp:106-121`). Transport, driver, commands and ledger are unchanged. The new 14-character code path is proven end to end by enrol 39614789400 and parity 39634443490 (leak checks included). The older runs' re-enrol and halt paths are code-identical. |

## Evidence used

Run bundles (`.superpowers/sdd/2026-10-20-core-mod-v1-2b-game/`, read only, all `pass: true`, all `leaks.json`
false/false/false):

| Run | Bundle | Harness commit | Requirements |
| --- | --- | --- | --- |
| http-probe 39516817799 | `probe-7/` | 7ce421d | S1, S2, S3, S9, user-agent |
| enrol 39614789400 | `run-39614789400/` | 948f22f | enrol, C8, C9, power, S10, S13 |
| revoke 39523768471 | `revoke-1/` | 4ac26dd | revoke, S9 |
| command 39531378852 | `command-2/` | 4ac26dd | command, C9 |
| outage 39536478691 | `run-39536478691/` | 5701529 | outage, C4 |
| tick-cost 39600584441 | `run-39600584441/` | 77064ba | tick-cost, C4, S12, memory |
| parity 39634443490 | `runs/parity-39634443490/` | e0cc1ab | parity |

Other evidence:
- mod-test run 39655793147 at 34207ff: 74/74 in-game C++ tests, all 74 listed under `SatisDashCoreTests/Private/`.
- Mandatory checks:
  - fixed-string PII and local-path grep on `git ls-files`, CLAUDE.md files and `.github/` excluded: one hit,
    `NOTICE`, a documented exception;
  - `npm audit` in `harness/`: 0 vulnerabilities;
  - bundle scan for code-shaped strings, `agentSecret`, `Bearer `, local paths and the name: none.
- Code read: everything under `mod/SatisDashAgent/Source/SatisDashCore/Private/` that touches the network, files,
  parsing or commands; `SatisDashGame/Private/` in full; the build files; the uplugin;
  `harness/src/{backend-host,recorder,outbound,core-run}.ts`; `harness/src/core-runs/http-probe.ts`;
  `runs/{http-probe,enrol,revoke}/pass.ts`.
- Not verifiable here: engine sources (`FJsonSerializer`, `FTSTicker`, libcurl's build options), so F1, F3 and F5
  state what must be checked against the pinned checkout.
