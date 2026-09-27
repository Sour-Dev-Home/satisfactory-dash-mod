# S1b — one HTTPS request with certificate verification on

Tests claim **C3**: GET `https://api.satis-manager.com/api/health` with verification on, and log the status.

**Pass:** a 200 is logged. A deliberately wrong hostname fails verification, proving the check is actually on.

This is the only spike that talks to a real, non-loopback host, and only a read-only health check — nothing else in
this repository ever reaches production.

Not started. No source in the main repo's `docs-vault/raw-sources/` documents certificate-verification behaviour for
this API (`ue-http-timer-json-2026-09-27.md`, recorded as not found), so this spike's result is evidence, not a design
basis, until it is recorded in `mod-claims.md`.
