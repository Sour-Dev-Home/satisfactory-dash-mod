# G2 harness: automated, evidence-first mod spikes

Status: draft for the owner's review (2026-10-19).
Implements: [ADR-0039](https://github.com/Sour-Dev-Home/satisfactory-dash/blob/main/docs-vault/wiki/decisions/0039-mod-as-end-state-research.md)
gate G2, against the claims in
[`mod-claims.md`](https://github.com/Sour-Dev-Home/satisfactory-dash/blob/main/docs-vault/wiki/mod-claims.md).

## Goal

A server-side SML mod that people renting a game server can install, replacing the edge agent, is the end state.
This spec covers only the path to the decision: run spikes S0 to S5 automatically on a real dedicated server,
write their evidence by machine, and end with a G3 build ADR for the owner to approve. It builds no product mod.

The owner's two requirements:
- **Hardened against hallucination.** No mod-API statement reaches a design without a captured source and a
  passing run on a real server, and the code that uses an API is checked against that source by a machine.
- **Automated testing.** Each spike builds, runs and is judged by a workflow, not by a person reading logs.

## Decisions (owner, 2026-10-19)

- **Where tests run: a self-hosted runner on the owner's PC** (option A). Hosted runners can't fit a UE build.
  The repo stays public; the runner is locked down (see "The runner").
- **How code lands: plain PRs in this repo**, opened by an agent session and merged by the owner. satisfactory-dash
  still changes only through lane task issues (the claims table, evidence and the G3 ADR).
- **Tick sampling: 20 min without the mod, then 20 min with it** (C4's pass condition changes from the table's
  30 min to match; the evidence lane task updates the table).
- Spikes run against a **separate test install** of the dedicated server, never the owner's real server or world.

## Shape

```
satisfactory-dash-mod (public, plain PRs)          satisfactory-dash (lanes only)
  spikes/S0..S5/      one small SML mod each          docs-vault/wiki/mod-claims.md  (C1–C7 status)
  harness/            Node/TS: runs a spike, checks   docs-vault/raw-sources/mod-spikes/<spike>-<date>/
                      its claim, writes the evidence  wiki/decisions/00xx G3 build ADR (at the end)
  api-sources.md      every SML/game symbol → source
  .github/workflows/
    ci.yml            PII check + harness unit tests (hosted runner)
    spike.yml         build + run one spike (self-hosted `ue` runner)
```

A spike run uploads an evidence bundle as a workflow artifact. A lane task in satisfactory-dash commits a batch of
bundles to `raw-sources/mod-spikes/` verbatim and flips the matching claim rows. Evidence stays beside the ADRs
(ADR-0039 D3) without a lane task per run.

## The harness

`harness/` is a Node/TypeScript package. `spike.yml` runs `harness run <S#>`:

1. **Build** the spike with the pinned toolchain (Coffee Stain's UE 5.6.1 and SML 3.12.0, per
   `raw-sources/sml-getting-started-2026-09-27.adoc`). The versions live in one `harness/toolchain.json`; the
   harness refuses to run if the installed engine or SML differs.
2. **Test server.** A dedicated server installed by SteamCMD (app 1690800) under a folder on `D:\` set in the
   runner's config, on ports that differ from the owner's real server. Which ports the server uses, and the flags
   to move them, come from the SML and dedicated-server sources [NEEDS VERIFICATION: capture the port page before
   S0]. The harness installs the built mod, starts the server, waits for readiness, and always stops it, including
   on failure or timeout.
3. **Probes**, each a small module with unit tests:
   - **log watcher:** tails the server's `FactoryGame.log` (location per `sml-dedicated-servers`, verified in S0)
     and matches expected lines; also reads the game build and engine version from the startup lines.
   - **echo server:** a loopback HTTP server that records each request's method, path, headers and body.
   - **tick sampler:** polls the dedicated server API's `QueryServerState` (`averageTickRate`; captured in
     satisfactory-dash `raw-sources/captured-responses/vanilla-QueryServerState-*`) for 20 min with the mod
     disabled, then 20 min with it enabled, and reports both series and their medians.
   - **contract check:** the posted JSON must parse with satisfactory-dash's `SnapshotRequestSchema`
     (`packages/shared/src/agent.ts`), vendored at a pinned commit with a SHA-256 check; the harness refuses a
     drifted copy.
   - **local backend (S5 only):** satisfactory-dash's docker-compose stack on a throwaway database. Never
     production.
4. **Verdict.** Each spike has `spikes/S#/pass.ts`, its pass condition as code, copied from the spike table in
   `mod-claims.md`. The harness writes `result.json` (pass or fail, the claim ID, the game build, engine and SML
   versions read from the server, start and end times, and log and echo excerpts) plus the raw captures. Every
   value in it is written by the harness; nothing is typed by hand.

The harness's own logic runs in `ci.yml` on a hosted runner with fixtures (canned logs, recorded requests, sample
`QueryServerState` responses), so a broken probe can't produce a false pass.

## Guards against hallucination

1. **The claims gate (ADR-0039), unchanged.** Only `verified` rows may appear in the G3 ADR.
2. **The API source map, enforced.** Every SML, FactoryGame or engine symbol and every `#include` used under
   `spikes/` has a row in `api-sources.md`: the symbol, the header path and line in the pinned SML or engine
   checkout, and the raw-source document. `harness check-api` scans the spike sources and fails on any symbol or
   include without a row. An invented symbol fails to compile; a real one nobody looked up fails this check. It runs
   in both `ci.yml` and `spike.yml`.
3. **Compile and run are the evidence.** A claim moves only on a harness-written `result.json`. The evidence lane
   task copies bundles verbatim.
4. **Rules for agents** in this repo's `CLAUDE.md`: no API from memory; cite the header path and line in
   `api-sources.md` before using a symbol; where docs and header disagree, the header wins and both are recorded;
   a failed spike stops the work and is reported, never worked around.
5. **Sources before spikes.** C5 (power) has no captured source, so S3 waits for a G0 addendum in satisfactory-dash
   (the SML PowerNetwork page and the headers it names).

## The runner

- A GitHub Actions runner on the owner's PC, under its own low-privilege Windows account, labels
  `self-hosted, windows, ue`, registered to this repository only.
- `spike.yml` triggers on `workflow_dispatch` (input: spike ID) and on `push` to `main` for harness changes. It
  never triggers on `pull_request` or `pull_request_target`, so fork code can't reach the runner.
- Jobs run in a `ue-runner` environment that requires the owner's approval. Workflow permissions are
  `contents: read`; actions are pinned to SHAs.
- The runner account can read and write only the test-server folder, the build folder and loopback. It has no
  access to the owner's real server folder, the owner's profile or `~/.lanes`.
- A runbook (`docs/runner.md`) covers installing, registering and removing the runner, and the account's
  permissions.

## Order of work

0. **Owner (G2-prep):** install the toolchain exactly as `sml-getting-started` says: Visual Studio 2022 (beside 2026)
   with the listed components, the CSS engine 5.6.1, the Epic and GitHub account link. Pass: the SML starter
   project builds.
1. **PR:** the harness skeleton, probes with unit tests, `api-sources.md` and `check-api`, `toolchain.json`, the
   vendored contract, and `CLAUDE.md`. Tests run on the hosted runner.
2. **PR:** `spike.yml` and `docs/runner.md`. The owner registers the runner and approves the environment.
3. **Spikes, smallest first:** S0, S1, S1b, S2, S4, S5; S3 after its G0 addendum. Any fail stops the sequence and is
   reported, and the design is reconsidered before the next spike (`mod-claims.md` rules).
4. **Lane tasks in satisfactory-dash:** the G0 addendum (power, ports); evidence batches that commit bundles and flip
   claim rows (C4's duration updated to 20 + 20 min); then the G3 build ADR, for the owner's approval.

## Out of scope

- The product mod, its packaging, its config UI and hosting-provider testing (G3 and after).
- Linux server builds (the owner's server is Windows; a Linux target is a G3 question).
- Any change to the satisfactory-dash backend: the mod is another agent on the existing contract.

## Risks

- **Toolchain friction** (30+ GB, VS 2022, account linking) is the first blocker and is the owner's step.
- **A self-hosted runner on a public repo** is safe only while no PR-triggered workflow can reach it; the
  "never `pull_request`" rule is checked by a test in `ci.yml` that parses `spike.yml`.
- **Game or SML updates** may break the pinned toolchain mid-sequence; `toolchain.json` makes that a refusal, not a
  silent drift.
