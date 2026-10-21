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

## Deferred to sub-project 4 (Linux builds and distribution)

- S11: the credential file's `0600` mode on Linux, and F2's fix (create the temp file with mode `0600` before the
  secret is written).
- S14: the release dependency list.
- F5: confirm libcurl's resolver is asynchronous on each platform, and add a "DNS unreachable" outage run.
- F6: tighten the http-probe pass condition to assert the TLS error class, and optionally seed a bad code in the enrol
  run.
