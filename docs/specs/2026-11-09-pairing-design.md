# Device-code pairing in the mod (0.3.0)

Status: design approved by the owner 2026-11-09 ("approved, write the spec"). This spec awaits the owner's review.

**Decision source:** ADR-0051 §1, in satisfactory-dash `docs-vault/wiki/decisions/0051-public-release-plan.md`, accepted
2026-10-24. The contract is satisfactory-dash `packages/shared/src/pairing.ts` (#676), deployed to production since #678.
The `agentVersion` fix is #871.

**Owner rulings (2026-11-09):**
- pairing goes into 0.3.0, which waits for it;
- the code goes to the console, and to in-game chat when that is feasible;
- chat only when exactly one player is online.

## Why

Players install the mod with the Satisfactory Mod Manager (SMM), which hides the mod's files. Today the only way to
enrol is to paste a code into `FactoryGame/Configs/SatisDashAgent.cfg` on the server (`Agent.cpp:159`), and an SMM user
never sees that file. With pairing, the code goes the other way: the server shows a code, and the player types it into
the dashboard.

**Success:** a fresh SMM install, with no cfg file, gets from "server started" to data on the dashboard with no file
edits. The steps are:
1. read the code in the console or chat;
2. sign in at `/app/pair` and enter it;
3. confirm the server's name.

## Contract (as deployed; the mod vendors it)

| Call | Body | Answer |
|---|---|---|
| `POST /agent/v1/pairings` (no auth) | `{ agentVersion: "mod/0.3.0", serverName? }` | 201 `{ pairingSecret (22 chars base64url), userCode (XXXX-XXXX-XXXX), expiresIn (≤900), interval (≤60) }` |
| `POST /agent/v1/pairings/poll` (no auth) | `{ pairingSecret }` (body only, never the URL) | 200 `{status:"pending"}`, `{status:"expired"}` or `{status:"slow_down"}`; 201 `{status:"approved", agentSecret, serverId, cadence}` |

- An unknown or spent secret reads as `expired`, never 404.
- Rate limits (satisfactory-dash `agents/routes/pairingApi.ts:22-27`):
  - starts: 6 an hour per network and 600 an hour overall;
  - polls: 120 a minute per network.
- "Network" is the client IPv4 address or IPv6 /64 (`agents/services/clientKey.ts`). The counters are in memory and
  reset when the backend restarts. The network limit is hit **before** the body is parsed (`pairingApi.ts:44-53`), so a
  400 still uses one of the 6.
- Servers behind one address (a host's NAT, several servers on one box) share the 6. Restarts also open new windows
  (§1), so a 429 on start is a **normal case**, not an error (§3).
- Both routes require `Content-Type: application/json` (`requireJsonBody`), with bodies capped at 4 kB. Before its
  database loads, the backend answers 503.
- `429` always carries `Retry-After` in whole seconds (`platform/errorResponse.ts:414`).
- Approving onto a **new** server is gated by `SELF_SERVE_ENABLED` (`pairingService.ts:188`). Approving onto an
  existing server isn't gated.
- **Asked of satisfactory-dash (2026-11-09), not blocking:**
  - an optional `approvedBy` on `approved` (branch `feat/pairing-approved-by`). It's 1 to 64 characters, with no
    control, bidi or zero-width characters. It's sent only when the approver has a generated `Player-XXXX` name, so it's
    **absent** when the operator approves, which is what the harness does;
  - a `/pair` → `/app/pair` route.

## 1. When the mod pairs

**New unit:** `Pairing.cpp` / `SatisDash/Pairing.h`. It is pure logic with no engine types, like `FAgent`, so mod-test
covers it fully. `FAgent` owns one `FPairing` and adds the state `EAgentState::Pairing` (`Agent.h:11`). The driver sends
its requests the way it sends enrolment (`Driver.cpp:51-55`).

**Entering pairing.** In `FAgent::Settle` (`Agent.cpp:118`), the branch that today logs "Not enrolled"
(`Agent.cpp:159`) enters `Pairing` instead. That happens when:
- the cfg is `Valid` or `Missing`, and an SMM install has no cfg, so `Missing` counts;
- **and** the cfg has no enrolment code;
- **and** there is no credential at all.

Other cases:
- **A saved credential for a different backend** (`Agent.cpp:153-156`) stays `Idle`. Pairing would silently abandon
  that credential.
- **A broken cfg** stays `ConfigBroken`.

**The cfg code always wins.** If a code appears in the cfg while pairing, the mod drops the pairing (no further polls)
and enrols. This is the advanced, fallback path. Existing installs and the Node agent are unchanged.

**The pairing window:**
- A window opens at boot and lasts 1 hour.
- Inside it, the mod starts a pairing and polls it. When the backend says `expired`, or `expiresIn` has passed, it
  starts a new one. That's at most 4 starts an hour, under the 6-an-hour limit.
- When the hour ends, the mod stops sending anything and logs one line: `No pairing code active: join the server or
  restart it to get a new one`.
- **A player joining reopens a closed window.** This covers an unattended server that the owner comes back to hours
  later. A join during an open window does nothing.
- At most one window opens per rolling hour, so joins can't exceed the start limit.
- **Ruling, extends ADR-0051:** ADR-0051 says "restart the server" only. The join trigger costs one extra start an
  hour at most. If it's wrong, the mod idles until a restart.

**Polling:**
- Every `interval` seconds, starting from the start reply's value.
- `slow_down` adds 5 seconds for the rest of that pairing (`PAIRING_SLOW_DOWN_SECONDS`).
- A status the mod doesn't know counts as `pending`.
- One request is in flight at a time.

**Approval.** The `approved` 201 body is enrol's shape plus `status`. It goes through `ParseEnrollResponse`
(`Responses.cpp:88-105`) and the same steps as a successful enrol (`Agent.cpp:199-219`): save the credential with
`BackendUrl`, adopt the cadence, enter `Running`. The mod logs `Paired with the dashboard; reporting to the backend`,
adding `(approved by "<name>")` when `approvedBy` is present and valid. The name is quoted, logged only, and never acted
on. If it's absent or invalid, the mod logs the line without it. If an `approved` reply fails to parse, it counts as
a transient error.

**Secrets:**
- `pairingSecret` lives only in memory. It is never logged, never written to disk, and sent only in the poll body.
- After a restart, the mod simply starts a new pairing.
- `userCode` is logged on purpose, since the console line is the feature. The harness's "never in the log" rules
  therefore apply to the secret, not the code.

**After a revoke (401):** the mod enters `Halted` (`Agent.cpp:245`) as today, and stays there until the server restarts.
A restart then starts pairing, because the credential was deleted.
- **Ruling, narrows the design presented 2026-11-09**, which said revoke leads straight back to pairing. The revoke
  run's pass rule (`runs/revoke/pass.ts:82-86`) allows no request after the halt except a re-enrol. Pairing straight
  after a revoke would fail it, and that rule is not loosened (repo rule 3).
- A revoke is also the owner saying "stop", so halting until a restart matches its intent.
- The halt line changes to `... Stopped; restart the server to pair it again, or add a code to SatisDashAgent.cfg`.
- **If wrong:** after a revoke, an SMM user has to restart the server to pair again.

## 2. Where the code shows up

**Console and FactoryGame.log:** `UE_LOG(LogSatisDash, Display, ...)` reaches both on Windows and Linux. The mod writes
one line per new code:

```
Pairing code ABCD-EFGH-JKLM: sign in at https://satis-manager.com/pair and enter it (expires in 15 minutes)
```

- The URL is `https://satis-manager.com/pair` only when `backendUrl` is the default `https://api.satis-manager.com`
  (`Config.cpp` `DefaultBackendUrl`). Any other backend gets `on your dashboard's Pair page`.
- The "15 minutes" comes from `expiresIn`, rounded down to whole minutes.

**In-game chat.** The mod posts a system message with the same text through `AFGChatManager::BroadcastChatMessage`
(`FGChatManager.h:79`; message struct `:26`, `CMT_SystemMessage` `:20`). The manager multicasts on the game's own
replicated actor (`:95-96`), so a vanilla client shows it without the mod installed.
- **It is a broadcast only:** the struct has no recipient (`:26-53`). The owner's rule therefore applies: post only
  when exactly one player is connected, counted by the server game state's `NumConnectedPlayers`
  (`FactoryGame/Public/Server/FGDSGameplayTypes.h:42`, the same source as the status snapshot). Correction (2026-11-09,
  final review): an earlier draft named `AGameStateBase::PlayerArray`.
- **When:**
  - when a code is issued and one player is on;
  - when the player count rises to exactly 1 while a code is open, i.e. a player joins an empty server;
  - when the player count rises to 1 with the window closed: the window reopens (§1), and the new code is posted.
- **Never** with 0 or 2+ players. The console line is always written.
- **The rule is a pure function** `ShouldPostCode(players, isNewCode, joined)` in `Pairing.cpp`. The engine side does
  only the count and the call, and it runs on the game thread inside the existing step budget.
- **Unverified at runtime:** that a vanilla client renders it. The end-to-end checklist checks this (§4).

**No `pairing.txt`.** SMM users never see the server's files.

**Server name.** `UFGServerSubsystem::GetServerName()` (`FactoryDedicatedServer/Public/FGServerSubsystem.h:68`, via
`::Get` `:62`; `::Get` is in `api-sources.md`, `GetServerName` needs a row) is the name the owner gave when claiming the server.
- The mod drops control, format and line-separator characters, so it matches the contract's
  `/^[^\p{C}\p{Zl}\p{Zp}]+$/`, and clamps it to 200 characters.
- It sends no `serverName` when the result is empty.
- A name the backend still refuses costs one 400 (§3). In that case the mod retries without the name.

## 3. Errors

| Answer | Start | Poll |
|---|---|---|
| 201 | adopt the pairing, log the code | approved: §1 |
| 200 pending, unknown or slow_down | | wait `interval` (slow_down adds 5 s) |
| 200 expired | | start a new pairing if the window is open |
| 400 | once with a name: retry at once without it. Otherwise warn once (`The backend refused the pairing request (400); retrying in an hour`) and retry in 1 hour | treat as expired |
| 429 | wait `Retry-After` (capped at 1 hour). Logged once as a Display line, not a warning: `Waiting <n> s for a pairing code (the backend's per-address limit)` | wait `Retry-After` |
| 5xx, network, timeout, or a 201 not in the expected form | backoff as enrol (`BackoffSeconds`, `Agent.cpp:233`) | same backoff. The pairing stays open until `expiresIn` passes |

- **The window clock keeps running during waits.** An outage can't stretch a window past an hour, and the backoff
  resets when a new window opens.
- **The start budget across restarts isn't tracked.** A restart part-way through a window can reach 7 or 8 starts in an
  hour, and the extra ones get a 429. The mod relies on `Retry-After` instead of saving start times to disk. The cost:
  after quick restarts, a code can be a few minutes late.
- **Both requests send `Content-Type: application/json`**, which the backend requires.
- **Warning lines are one per state change**, as `FAgent::Enter` does (`Agent.cpp:162-177`), never one per retry.

## 4. Testing

**mod-test (`SatisDashCoreTests`)** gets a new `PairingTest.cpp`, plus cases in `AgentTest.cpp`:
- entering pairing (Missing cfg, empty code, no credential) and every case that doesn't pair;
- start → poll at `interval`, `slow_down` adds 5 s, unknown status counts as pending;
- expiry starts a new code, and the window ends after an hour with no request after it;
- a join reopens a closed window, a join in an open window does nothing, and at most one window per hour;
- a cfg code mid-pairing stops polling and enrols;
- approved: credential saved, `Running`, the approving name logged, and an unparseable approval treated as transient;
- 401 → `Halted`, with no pairing request until a restart;
- every row of the §3 table;
- `ShouldPostCode` for players 0, 1, 2 × new code / join;
- server-name cleaning: control and bidi characters, clamping, empty;
- the secret never appears in any log line the tests capture.

**Harness: no production contact.** `core-run.ts` writes a loopback-only cfg (the run's recorder or stub URL, no code)
**before** each mod boot (today the mod boots at `core-run.ts:335`, before `writeConfig`).
- Without this, a missing cfg means the default production backend (`Config.cpp:220-221`), and pairing would contact it
  (repo rule 5).
- A test pins the order: the cfg is written before `start`, and it never names a non-loopback host.

**Harness: a new core run `pair`** (`harness/src/core-runs/pair.ts`, `runs/pair/pass.ts`), on Windows and Linux:
1. Refresh the vendored contract (`harness/contract/manifest.json`) to a satisfactory-dash commit that includes #871. The
   backend images must descend from it (`core-run.ts:198-214`).
2. Boot with the loopback cfg and no code. The mod starts a pairing against the harness's recorder in front of the
   compose backend.
3. Read the code from the log. Sign in as the operator (`POST /api/auth/login` with the harness `.env` admin user) and
   keep the session cookie. Send `POST /api/pairings/lookup` `{userCode}` (upper case), then `POST /api/pairings/approve`
   `{userCode, target:{kind:"existing", serverId}}`, both as JSON with no `Origin` header.
   - `serverId` is the `mod` server that the backend host seeds codes for (`host.seedCode("mod", ...)`,
     `harness/src/core-runs/enrol.ts:20`). The harness has no HTTP sign-in today. The plan adds the login call and a way to
     read that server's id from the backend host.
   - The compose backend runs with `SELF_SERVE_ENABLED` off (dash: `compose.yaml` doesn't pass it), so `new` would answer
     403. **Ruling:** the run uses `existing` rather than changing the backend's config. It still exercises the whole mod
     side. The `new` path is covered by the end-to-end test once production self-serve is on.

It passes when:
- one start per code, and none before the boot;
- polls no faster than `interval`;
- the poll's secret is never in the log, a URL or a header;
- the code line appears in the log within 30 s of health;
- the credential is written, and the first snapshot is accepted within 60 s of the approval;
- no request after the approval goes to `/agent/v1/pairings*`;
- the "Paired" line appears. `(approved by ...)` is optional, since it's absent when the operator approves.

**End-to-end checklist (`docs/e2e-checklist.md`):** step 3 becomes "pair":
- the owner reads the code from the console and enters it at production `/app/pair`, approving onto an existing server
  until self-serve is on;
- then joins alone with the real client to see the chat line, and once more with a second player online, to see that
  no chat line appears.

## Out of scope

- A `?code=` link (ADR-0051 "Not now").
- `pairing.txt`.
- A chat command.
- Sending chat to one player only (not possible: `FGChatManager.h:26-53`).
- Re-pairing without a restart after a revoke.

## Files

- **Mod:**
  - new: `Pairing.cpp`, `Public/SatisDash/Pairing.h`;
  - changed: `Agent.cpp`, `Agent.h`, `Requests.cpp` (two builders), `Responses.cpp` (two parsers), `Driver.cpp` (send,
    player count, chat), a server-name reader in `SatisDashGame`;
  - `api-sources.md` rows for every new symbol.
- **Harness:**
  - changed: `core-run.ts` (cfg before boot), `harness/contract/` (refresh);
  - new: the `pair` core run and its tests;
  - changed: `.github/workflows/core-run.yml` (add `pair`).
- **Docs:** `docs/releases/0.3.0.md` (install without a file edit), `docs/e2e-checklist.md` step 3.
