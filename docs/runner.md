# The `ue` runner (owner's PC)

The self-hosted runner that `spike.yml` uses. It builds a spike with the pinned toolchain and runs it on a **separate
test install** of the dedicated server. Spec: `docs/specs/2026-10-19-g2-harness-design.md`, "The runner".

## Before you start

- G2-prep is done: the toolchain is installed exactly as `sml-getting-started` says, and the SML starter project builds.
- Your real server is stopped whenever a spike runs, until the test server's ports are moved (below). Both servers
  would otherwise want the same ports.

## 1. A low-privilege account and folders

1. Create a local standard (non-admin) Windows user, `satis-runner`.
2. Create `D:\satis-test` (the test server) and `D:\satis-build` (build output). Give `satis-runner` Modify on both and
   no other grants. It must not be able to read your real server's folder, your profile or `~/.lanes`.

## 2. The test server

Install a separate dedicated server into `D:\satis-test\server` with SteamCMD, app 1690800, the same way ADR-0040's
server manager does [NEEDS VERIFICATION: copy the exact SteamCMD command from ADR-0040 once it merges]. Don't point it
at your real server's folder.

**Ports:** the G0 addendum lane task captures which ports the server uses and the arguments that move them. Put those
arguments in `serverArgs` and the API port in `apiBaseUrl`. Until then, `serverArgs` stays `[]` and your real server
must be off during runs.

## 3. Register the runner

1. GitHub → this repo → Settings → Actions → Runners → New self-hosted runner → Windows. Follow the commands shown there
   (they carry the current version and a one-time token), installing under `D:\actions-runner`.
2. When `config.cmd` asks: labels `windows,ue` (it adds `self-hosted` itself); run as a service; log on as
   `satis-runner`.
3. Settings → Environments → New environment `ue-runner` → Required reviewers: you. Every spike run then waits for
   your approval.
4. Settings → Actions → General → "Fork pull request workflows from outside collaborators": "Require approval for all
   outside collaborators". `spike.yml` never runs on PRs anyway; this is a second lock.

## 4. The harness config

Create `D:\satis-runner-config\harness.json` (readable by `satis-runner`; it holds no secret), and add a line to
`D:\actions-runner\.env`: `SATIS_HARNESS_CONFIG=D:\satis-runner-config\harness.json`. Restart the runner service.

    {
      "serverDir": "D:/satis-test/server",
      "apiBaseUrl": "https://127.0.0.1:<API port from the G0 addendum>",
      "serverArgs": [],
      "buildCommand": [],
      "builtPluginDir": "D:/satis-build/{spike}/<target folder from the Alpakit page>",
      "installed": { "engine": "5.6.1", "sml": "3.12.0" }
    }

`buildCommand` (the Alpakit command line as an argv list, with `{spike}` where the spike's name goes) comes from SML's
project-setup docs page. Capture that page into satisfactory-dash raw-sources first; the harness refuses to run while
it's empty. `installed` is what you actually installed, and the harness refuses any difference from
`harness/toolchain.json`.

## 5. Check it

Actions → spike → the latest `main` push: `selftest` waits for your approval, then passes. Then run S0 with
"Run workflow".

## Removing the runner

Stop the service, run `config.cmd remove` with the token GitHub shows, delete `D:\actions-runner`, then delete the
`satis-runner` account and its folders.
