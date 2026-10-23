# Core mod v1 security review: what was decided

The review itself is `2026-10-core-mod-v1-security.md`, kept as the reviewer wrote it. This file records what was
done with each finding, and the owner's decisions on the accepted risks (2026-10-21, in this session: "Accept all").

## Fixed in this branch

| Finding | Fix |
| --- | --- |
| F1 (medium): a deeply nested JSON response could overflow the game thread's stack | Nesting depth is checked before every parse; deeper than the bound is refused as malformed JSON |
| F3 (low): plain-http loopback requests honoured proxy variables, so a Bearer header could reach a proxy in clear | Plain-http requests turn the proxy off |
| F4 (low): an all-dots command ID changed the result URL's path | All-dots IDs are refused like any other invalid ID |
| F8 (info): a stale comment on the enrolment code format | Corrected |

## Accepted risks (owner, 2026-10-21)

| Risk | Why it's accepted |
| --- | --- |
| OpenSSL 1.1.1t, the engine's copy, is end of life | The mod can't ship another OpenSSL without forking the engine's SSL module, and the game's own HTTP uses the same library. TLS 1.2 floor, full verification, one host, client only. Listed for the release dependency list; revisit when the game's engine moves |
| No certificate pinning; the engine's pins aren't applied | The backend is behind Cloudflare, whose certificates rotate, so pinning would cause outages. Trust comes from the engine's root store |
| For https, environment proxy variables stay honoured | TLS still runs end to end through a CONNECT tunnel, so the proxy sees the host name and timing, never the secret or body. Only the host's admin can set them. The host guide (sub-project 4) says so |
| Enrolment ignores Retry-After | Backoff is 0.5-60 s with jitter, codes expire in 10 minutes and the backend rate-limits |
| Ledger eviction after 1,000 IDs lets a hostile backend replay an ID | A backend can always issue a fresh ID; at-most-once only guards against honest retries |
| The harness backend host's control API, the recorder's redaction, PercentEncode keeping `.`, the circuit scan's 10,000-ID bound | Ruled on in the review's "Known open items"; harness-only or not a security issue |

## The final review of Plan 2B (2026-10-21)

- Blocking: enrol, revoke and command re-run on this branch after F1, F3 and F4 (see PR #42's evidence table).
- The enrol run now requires auto-pause to be compared in at least 10 rounds.
- The owner ratified the two controller rulings in `runs/revoke/pass.ts` (the 401 grace) and `runs/command/pass.ts`
  (transient result retries).
- Follow-ups, owner's choice, to start sub-project 3 or 5, the first one first:
  1. The transport's 0 s ticker runs `curl_multi_perform` and rebuilds its arrays every frame for the whole uptime,
     even when idle (footprint).
  2. `ApplyAutoPause` ignores the game's pending server options, so a deferred change reports `upstream_error` and
     still applies later.
  3. Joined circuit groups are never exercised in the game, and the 10,000-ID scan could miss groups on a long-running
     server.
  4. The `0.1.0` version is hard-coded in 7 places, and the old HTTP-module transport left a lock and two
     api-sources rows behind.

  Done on `feat/core-mod-followups` (2026-10-23), pending the owner's mod-test and core runs:
  - 1: the transport's ticker stops when no request is out and restarts on the next Send. The multi handle and its
    connection cache stay. The arrays are rebuilt only on a tick where a request finished.
  - 4: `AgentVersion()` reads the `.uplugin`'s `VersionName`, so the `.uplugin` is the one place the version is
    written. The run judges keep their literals (pass conditions are untouched), and `harness/test/mod-version.test.ts`
    fails if they drift from the `.uplugin`. `FCappedBody`'s lock is gone, because libcurl writes the body on the game
    thread. The 12 api-sources rows only the old transport used, and `HAL/CriticalSection.h`, are removed.
  - 2 and 3 are still open: 2 changes what a deferred auto-pause command reports, and 3 needs a save with joined
    circuits.

## Deferred to sub-project 4 (Linux builds and distribution)

- S11: the credential file's `0600` mode on Linux, and F2's fix (create the temp file with mode `0600` before the
  secret is written).
- S14: the release dependency list.
- F5: confirm libcurl's resolver is asynchronous on each platform, and add a "DNS unreachable" outage run.
- F6: tighten the http-probe pass condition to assert the TLS error class, and optionally seed a bad code in the enrol
  run.
