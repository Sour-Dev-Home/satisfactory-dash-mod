# G3, sub-project 2: the core mod, v1

Status: approved by the owner 2026-10-20 (mod PR #19); the build decision is ADR-0042 (satisfactory-dash #479). The
"Security" section was added at the owner's request on 2026-10-20 and is in review with this amendment.

## What this is for

The owner wants the dashboard's agent to run inside the game server instead of on their PC (owner, 2026-10-20: "I
want to be able to move the agent from my computer to the mod"). G2 proved the pieces (C1–C7): the mod loads on a
dedicated server, runs a timer without touching tick rate, makes HTTPS calls, reads power, keeps a credential
server-side, and enrols and pushes a snapshot to the real backend.

v1 is the smallest mod that can replace the Node agent for status, power and auto-pause.

## Owner decisions (2026-10-20)

1. **Other players install it too.** It ships through ficsit.app and has to work on rented hosts, so it can't rely on
   anything only the owner's PC has.
2. **It replaces the Node agent,** which is deprecated once the mod reaches parity (sub-project 5).
3. **Hybrid data source.** Status, power and auto-pause are read natively in v1. Factory, map and world layers come
   from FRM in a later sub-project, then move to native readers one release at a time.
4. **Status and auto-pause are native, after spikes C8 and C9 pass** (below).
5. **The secret lives in its own file,** `FactoryGame/Saved/SatisDash/credential.json`. The mod's config holds only
   the backend URL and a one-time enrolment code, which the mod blanks after enrolling. Revocation stays in the
   dashboard.

## The sub-projects, for context

1. G3 prep (lane tasks in satisfactory-dash): the GPL/AGPL reading, Coffee Stain's terms on mods, SML's breakage
   history, and whether hosts allow custom mods and outbound HTTPS.
2. **The core mod, v1 (this spec).**
3. The FRM bridge: factory, map and world layers through FRM.
4. Distribution: Windows and Linux builds in CI, ficsit.app, a host guide.
5. Switchover: the owner's server moves to the mod; the Node agent is deprecated.
6. Native readers that replace FRM, one layer per release.

## The contract it speaks

Unchanged from the Node agent's, in satisfactory-dash `packages/shared/src/agent.ts` at a commit the plan pins:

- `POST /agent/v1/enroll` with `{code, agentVersion}` returns 201 `{agentSecret, serverId, cadence}`. Codes match
  `^[A-Z2-7]{4}-[A-Z2-7]{4}$`.
- `POST /agent/v1/snapshots` with `Authorization: Bearer <agentSecret>`; the body may be gzipped. It requires
  `agentVersion`, `observedAt`, `reachable` and `paused`; parts are optional. The response carries the current cadence.
- `GET /agent/v1/commands?waitSeconds=25`, then `POST /agent/v1/commands/:id/result` with `{ok, code?}`. Codes are
  only `unsupported`, `upstream_unreachable`, `upstream_auth_rejected` and `upstream_error`; never free text.
- `agentVersion` is at most 32 characters. The mod sends `mod/<version>`, e.g. `mod/0.1.0`, so the dashboard can tell
  the mod from the Node agent.

No backend change is needed for v1.

## Shape

One SML mod, `SatisDashAgent`. Each part does one job and mirrors a part of the Node agent:

| Part | Job | Node agent equivalent |
| --- | --- | --- |
| `Config` | Reads `FactoryGame/Configs/SatisDashAgent.cfg`: `backendUrl` (default `https://api.satis-manager.com`) and `enrollmentCode` | `store.json` |
| `CredentialStore` | Reads and writes `FactoryGame/Saved/SatisDash/credential.json` (`agentSecret`, `serverId`) | the DPAPI blob |
| `Enroller` | With a code and no credential: enrols, saves the credential, blanks the code | `enroll` |
| `Sampler` | Builds a snapshot from native readers on the backend's cadence: **Status** (C8), **Power** (proven by S3), **AutoPause** (C9) | `sampler.ts` |
| `Conform` | Clamps values to the contract's bounds | `conform.ts` |
| `Pusher` | A 120-snapshot in-memory queue, oldest first; backoff from 1 s doubling to 60 s with jitter; honours `Retry-After` up to 300 s; drops a snapshot on any other 4xx; gzips bodies over 1 KB | `pusher.ts`, `queue.ts`, `backoff.ts` |
| `CommandRunner` | Long-polls for commands, runs `set_auto_pause` natively (C9), reports results, and keeps a ledger of command IDs so none runs twice | `commandRunner.ts`, `ledger.ts` |
| `Halt` | On a 401: stops pushing and polling and logs once; a new code in the config re-enrols | `halted.ts` |

**Runtime.** Everything runs on the game thread through `FTSTicker`, with UE's asynchronous HTTP, as S1 and S2 proved.
Nothing blocks a frame. Logs go to the mod's own `LogSatisDash` category.

**Portability.** No Windows-only API, so the same code compiles for Linux servers in sub-project 4.

**Out of v1:** factory, map and world layers; release builds for Linux; publishing to ficsit.app.

## Data flow

Each pass, on the backend's cadence (today status 5 s, power 5 s):

1. The `Sampler` checks which parts are due. Auto-pause is read with status.
2. It reads them natively on the game thread. Each read is a few field lookups, with no I/O.
3. `Conform` clamps the values, and the pass builds one snapshot: `agentVersion`, `observedAt`, `reachable: true`,
   `paused`, and the parts that are due.
4. The `Pusher` queues it and sends it in the background.

**No world loaded** (the server sits in `DedicatedserverEntry`): status goes out with `isGameRunning: false` and no
power part, so the dashboard can show "server up, no game".

**Server shuts down:** snapshots stop with it, and the backend's staleness rule marks the server offline, as it does
today when the agent can't reach the API. The mod never sends `reachable: false`.

**The queue is in memory only,** like the agent's. A restart loses at most two minutes of unsent snapshots; a
persisted queue would add disk I/O and a file format for no dashboard benefit.

## Spikes before the build: C8 and C9

They run in the G2 harness on the runner, like S3, and follow its rules: harness-written evidence only, and each
symbol's header line goes in `api-sources.md` before use.

| Claim | The mod can… | How it's checked | Pass |
| --- | --- | --- | --- |
| C8 | read every status field natively: session name, game running, paused, players and limit, tick rate, total game duration, tick health | The spike plugin logs native readings each round while the harness calls `QueryServerState` at the same moment; 12 rounds with a world loaded and 12 without | Every field matches; tick rate and game duration within one round's drift |
| C9 | read and change auto-pause natively, and the vanilla API sees the change | Read it natively and compare with `GetServerOptions`; change it natively and check `GetServerOptions` agrees; restore it | All three agree, and the original value is restored even when the run fails |

If a field can't be read natively, the fallback is for the mod to call the server's own HTTP API on loopback for that
field only. That changes the design rather than blocking it, and goes back to the owner before it's used.

## Errors and edge cases

| Case | What the mod does | What the owner sees |
| --- | --- | --- |
| Missing config (first boot) | Writes `SatisDashAgent.cfg` with the default URL and an empty code, then idles. The game runs normally | One log line: add an enrolment code from the dashboard |
| Broken config (bad JSON; URL not `https://`) | Ignores the file and idles, without overwriting it. `http://` is allowed only for `127.0.0.1` and `localhost`, which the harness needs | One log line naming the bad field |
| Code rejected (400, 404) | Keeps the code and doesn't retry it. Tries again only when the config file's modification time changes (checked each minute) | One log line: get a new code |
| Enrolment fails on the network (5xx, timeout) | Retries with the push backoff, 1 s to 60 s | Nothing until it succeeds |
| Credential file broken or unreadable | Treated as not enrolled: idles until there's a code. Never sends an empty Bearer | One log line |
| Credential written, code not yet blanked (a crash in between) | On the next boot the credential wins and the code is blanked then. One code never enrols twice | Nothing |
| 401 on any call (revoked) | Deletes the credential, stops pushing and polling | One log line: revoked in the dashboard |
| Backend down for a long time | The queue fills to 120 and drops the oldest. Commands pause too | One line when failures start and one when they stop |
| Unknown command type | Reports `{ok: false, code: "unsupported"}` | The dashboard's command result |
| `set_auto_pause` write fails | Reports `{ok: false, code: "upstream_error"}`; the ledger still records the ID, so a retry doesn't run it twice | The dashboard's command result |
| A reader fails | That part is left out of the snapshot, never the whole snapshot; nothing propagates into the game's tick | A rate-limited log line naming the part |

Two rules cover all of it:

- **The game never notices the mod.** No path in it can crash, stall or pause the server.
- **Logs are quiet and safe.** One line per state change, never one per attempt. Never the secret or the code, only
  present or absent, and HTTP status codes.

## Security

The owner asked for a security pass against OWASP (2026-10-20). The reference is the **OWASP Application Security
Verification Standard 5.0.0** (May 2025, github.com/OWASP/ASVS, `5.0/en`), at Level 2 where a requirement applies.
The mod is an outbound HTTPS client inside the game process: it opens no port, serves nothing and has no UI. So the
chapters for browsers, sessions, OAuth and WebRTC (V3, V7, V10, V17) don't apply. The ones that do are mapped below.

### What it protects, and from whom

| Asset | Why it matters |
| --- | --- |
| The agent secret | Whoever holds it can push snapshots as this server and receive its commands |
| The enrolment code | Single use for 10 minutes; whoever uses it first owns the enrolment |
| The game's state | `set_auto_pause` changes the server; nothing else may |
| The server's uptime | The mod must never crash, stall or slow the game |

| Threat | Answer |
| --- | --- |
| Someone on the network path (public Wi-Fi at a host, a hostile network) reads or changes traffic | HTTPS with certificate verification on every call; no plain-HTTP fallback (V12) |
| A redirect sends the request, and its Bearer header, to another host | Redirects aren't followed (V15) |
| A compromised or impostor backend sends hostile responses | Every response is parsed against a strict schema with size limits; only known commands with valid parameters run (V2, V4) |
| The secret leaks through logs or crash reports | Logging rules forbid it and the code; a harness check fails the build if either appears (V16) |
| Another player on the server reads the secret | Nothing replicates it (C6); it's in a server-side file outside the config |
| **Accepted:** the host's admin, or another mod in the same game process, reads the credential file or the process memory | They already control the server. Not defendable from inside the process. Answer: revoke the agent in the dashboard. The host guide (sub-project 4) says so |
| **Accepted:** a stolen secret is used from elsewhere | It can only post snapshots for that one server and poll its commands; revoking it in the dashboard ends that |

### Requirements

Each one names its ASVS 5.0 requirement and how it is checked.

| # | Requirement | ASVS 5.0 | Checked by |
| --- | --- | --- | --- |
| S1 | Every call uses `https://` with certificate verification on; a failed check is an error, never a retry over HTTP. `http://` is accepted only for `127.0.0.1` and `localhost` (the harness) | 12.2.1, 12.3.1, 12.3.2 | Unit test on `Config`'s URL rules; harness run against a wrong-host certificate, as S1b did (C3) |
| S2 | The mod relies on the backend's floor. The official backend refuses anything below TLS 1.2 (dash session, 2026-10-20). Self-hosted backends default to 1.2+ (satisfactory-dash #493). The mod offers TLS 1.3 and 1.2, and also 1.1 and 1.0 (libcurl's default; UE has no per-request TLS option). A self-hosted backend that still accepts old TLS is an accepted risk for the security review (Plan 2B Task 10) | 12.1.1 | Harness: the probe captures the mod's ClientHello. It requires 1.3 and 1.2 in the offer (and supported_versions present) and records older versions without failing on them. Measured: the official backend refuses 1.1 at Cloudflare's edge (alert 70, OpenSSL 3.5.6) |
| S3 | A redirected answer is refused, and neither the Bearer header nor any request body reaches another address | 15.3.2 | Harness: a stub backend answers 302 to another loopback port, which answers. Any request there with an Authorization header or a body fails the probe; a snapshot must be re-sent after the redirects began (the refusal) and the refusal logged (S9). A redirected authenticated GET (the commands poll) is added once the driver polls (Plan 2B Task 6) |
| S4 | Every response body is size-capped (64 KB; the agent's largest response is far smaller), must say `Content-Type: application/json`, and is parsed against the contract's schema; unknown fields are ignored, and wrong types or out-of-range values reject the whole response | 2.2.1, 4.1.1, 15.2.2 | Unit tests: oversized, wrong content type, malformed, wrong-type and out-of-range bodies for enrol, snapshot and command responses |
| S5 | Cadence values from the backend are clamped to the contract's bounds (1–3600 s), so a hostile backend can't make the mod spin | 2.2.1, 15.2.2 | Unit test: cadence 0 and 10⁶ are clamped |
| S6 | Commands: only `set_auto_pause` with a boolean `enabled` runs. Anything else reports `unsupported`. A command ID runs at most once (the ledger), and IDs are length-checked | 2.2.1, 2.3.1 | Unit tests: unknown type, missing or wrong-type params, a repeated ID, an over-long ID |
| S7 | The secret is never sent in a URL, only in the `Authorization` header | 14.2.1 | Unit test on the request builder |
| S8 | The secret and code are never logged, not even in part; the mod logs only "present"/"absent" and HTTP status codes | 16.2.5 | Harness check on every run's log, for the secret, the code and the string `Bearer` (S5's `bearerInLog`, extended) |
| S9 | Security events are logged once each: enrolment succeeded or was rejected, 401 and halt, a TLS failure, a refused response | 16.3.1, 16.3.4 | Harness runs (enrol, revoke, wrong-host certificate) check for each line |
| S10 | Any failure fails closed: no snapshot is sent without a valid credential, a broken config doesn't fall back to defaults that send data, and a failed command doesn't half-apply | 16.5.2, 16.5.3 | The error table above, each row a unit test |
| S11 | The credential lives only in `Saved/SatisDash/credential.json`, never in the config or the build. It's written atomically (temp file, then rename). On Linux servers it's created with mode `0600` | 13.3.1, 13.3.2 | Unit test of the atomic write; on Linux, a harness check of the mode once Linux builds exist (sub-project 4) |
| S12 | The mod contacts only the configured backend host; nothing else, including no telemetry of its own | 13.1.1, 13.2.4 | Harness: an outbound-traffic check during the parity run (only the backend's host:port is contacted) |
| S13 | Snapshots carry game data only: no host name, IP, user name, path or player account IDs beyond what the contract defines | 14.1.1, 14.2.3 | Unit test: the snapshot JSON's fields equal the contract fixture's fields |
| S14 | Builds are reproducible from the repository in CI; SML and the engine are pinned (`harness/toolchain.json`); a release lists its dependencies; no test code ships in a release build | 15.1.2, 15.2.3 | Sub-project 4's CI. For v1: the plan keeps test code in test-only modules |

S2 and S3 were amended by the owner on 2026-10-20 after Gate A (Plan 2B Task 1): the probe measured that libcurl follows redirects as a body-less GET without the Authorization header, and offers TLS 1.0-1.3 with no per-request floor in UE.

### The security review

Before v1 is called done, a security review separate from the normal code review checks the finished mod against
the table above, row by row. The workspace's `security-reviewer` agent runs it, and its findings are fixed or
recorded as accepted risks with the owner's say. Its report is kept with the v1 evidence.

## Testing

**1. C++ unit tests,** as UE automation tests run headless in the editor build the runner already makes:

- `Config` and `CredentialStore`: missing, broken and valid files; the crash case where the code isn't blanked yet.
- `Conform`: every bound in the contract, with values just inside and just outside.
- `Pusher`: the backoff steps, the jitter range, `Retry-After` capped at 300 s, the queue dropping the oldest at 121,
  gzip only above 1 KB, a 401 halting.
- `CommandRunner`: the ledger skips a repeated ID; an unknown type reports `unsupported`.
- Snapshot JSON: checked against a fixture generated from satisfactory-dash's schema at the pinned commit, so drift
  fails the build.

The plan's first task proves these tests run headless on the runner. If they can't, the same cases become harness
checks against a stub backend, recorded as a ruling.

**2. Harness runs on the runner,** with a real server and a real backend (S5's local stack):

- **Enrol:** with a seeded code, the mod enrols, writes the credential, blanks the code, and its snapshots are
  accepted.
- **Revoke:** revoking the server halts the mod within one cycle; a new code re-enrols it.
- **Command:** a `set_auto_pause` from the backend changes the setting in the game (checked through
  `GetServerOptions`), and the result is reported once.
- **Backend outage:** the stack stops for 3 minutes; the mod logs once, catches up afterwards, and the game's tick
  rate never drops.
- **Secrets:** no run's log contains the secret or the code (S5's `bearerInLog`, extended to the code).
- **Tick cost:** C4's tick-rate check again, on the finished mod, over 10 minutes with a world loaded.

S5 ran only locally because Docker needs `docker-users` (owner decision 3 of the S3–S5 spec). These runs need the
backend too, so they follow the same rule unless the plan finds a way that keeps `satis-runner` out of that group.

**3. Parity with the Node agent.** Both run side by side on the same server, each enrolled as its own server in the
local backend. Over 30 minutes with a world loaded, the harness compares their stored snapshots: status, power and
auto-pause match in every round, with S3's rule for joined power groups.

v1 is done when all three pass. Deprecating the Node agent comes later, in sub-project 5, after the mod has run on the
owner's real server.
