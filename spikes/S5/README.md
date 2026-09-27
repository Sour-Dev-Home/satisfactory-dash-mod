# S5 — enrol and push a minimal snapshot to a LOCAL backend

Tests claim **C7**: against a local backend (a dev or `satis_load` database, never production), enrol with a code,
then POST a snapshot with `status` only, as `agentVersion: "mod/0.0"`.

**Pass:** the backend accepts it and the dashboard shows the server online.

This spike never targets production. It depends on S1 (C2, the outbound POST) and, for anything beyond loopback, S1b
(C3, certificate verification).

Not started. See `mod-claims.md` in the main repo; the contract this spike posts against is
`packages/shared/src/agent.ts` (`SnapshotRequestSchema`) in satisfactory-dash.
