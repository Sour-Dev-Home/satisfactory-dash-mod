# The `ue` runner (owner's PC)

The self-hosted runner that `spike.yml` uses. It builds a spike with the pinned toolchain and runs it on a **separate
test install** of the dedicated server. Spec: `docs/specs/2026-10-19-g2-harness-design.md`, "The runner".

## Before you start

- G2-prep is done: the toolchain is installed exactly as `sml-getting-started` says, and the SML starter project builds.
- Your real server can keep running: the test server uses its own ports (below).

## 1. A low-privilege account and folders

1. Create a local standard (non-admin) Windows user, `satis-runner`.
2. Create `D:\satis-test` (the test server) and `D:\satis-build` (build output). Give `satis-runner` Modify on both,
   and on the SML project `D:\SatisfactoryModding\SatisfactoryModLoader` (a build writes its `Mods\`, `Saved\` and
   `Intermediate\`), and no other grants. Evidence goes to the runner's own temp folder. It must not be able to read
   your real server's folder, your profile or `~/.lanes`.

## 2. The test server

Install a separate dedicated server into `D:\satis-test\server` with SteamCMD, app 1690800, the same way ADR-0040's
server manager does [NEEDS VERIFICATION: copy the exact SteamCMD command from ADR-0040 once it merges]. Don't point it
at your real server's folder. Boot it once (any spike will do) before running S2: a freshly created world runs at
~30 ticks for its first 10 minutes, longer than the harness's 3-minute warm-up (`harness/src/ticks.ts`).

**Ports:** the server uses the standard port (default 7777: TCP for the HTTPS API, UDP for game traffic) and the
reliable-messaging port (default 8888, TCP). `-Port=` and `-ReliablePort=` move them (satisfactory-dash
`raw-sources/dedicated-server-ports-2026-10-19.md`, #452). The config below puts the test server on 17777 and 18888.
A busy standard port isn't an error: the server quietly takes the next free one, which is why the harness refuses to
start when anything already answers on `apiBaseUrl`. S0 passed this way on 2026-10-19 with the real server up on 7777.

## 3. Lock down, then register the runner

Do steps 1 and 2 **before** registering the runner. GitHub created the `ue-runner` environment, with no protection,
the first time `spike.yml` ran on `main`. Any job still queued for the `ue` label would run unapproved the moment a
runner appears. Also cancel any queued `spike` runs under Actions first.

1. Settings → Environments → `ue-runner` (create it if it isn't there):
   - Required reviewers: you. Every spike run then waits for your approval.
   - Deployment branches and tags: "Selected branches and tags", with only `main`. A job on any other branch,
     including a PR's own edited copy of `spike.yml`, can't use the environment, so it never starts.
2. Settings → Actions → General → "Fork pull request workflows from outside collaborators": "Require approval for all
   outside collaborators". `spike.yml` never runs on PRs anyway; this is a second lock.
3. GitHub → this repo → Settings → Actions → Runners → New self-hosted runner → Windows. Follow the commands shown there
   (they carry the current version and a one-time token), installing under `D:\actions-runner`.
4. When `config.cmd` asks: labels `windows,ue` (it adds `self-hosted` itself); run as a service; log on as
   `satis-runner`.
5. If your GitHub plan offers runner groups for this repo, put the runner in a group restricted to the workflow
   `.github/workflows/spike.yml@refs/heads/main`. Without that, any workflow pushed to a branch of this repo could
   name `runs-on: self-hosted`. The guard test fails CI on such a workflow, but only after the push, and the
   environment gate only stops jobs that use it. So treat push access to this repo, including agent sessions, as
   access to the runner, and keep it to yourself.

## 4. The harness config

Create `D:\satis-runner-config\harness.json` (readable by `satis-runner`; it holds no secret), and add a line to
`D:\actions-runner\.env`: `SATIS_HARNESS_CONFIG=D:\satis-runner-config\harness.json`. Restart the runner service.

    {
      "serverDir": "D:/satis-test/server",
      "apiBaseUrl": "https://127.0.0.1:17777",
      "serverArgs": ["-Port=17777", "-ReliablePort=18888"],
      "buildCommand": ["pwsh", "-NoProfile", "-File", "scripts/build-spike.ps1", "-Spike", "{spike}",
        "-Project", "D:/SatisfactoryModding/SatisfactoryModLoader", "-Engine", "C:/Program Files/Unreal Engine - CSS",
        "-Out", "D:/satis-build/{spike}"],
      "builtPluginDir": "D:/satis-build/{spike}",
      "installed": { "engine": "5.6.1", "sml": "3.12.0" }
    }

Build output comes from `scripts/build-spike.ps1` (Alpakit `PackagePlugin`, Windows Server, Shipping). It builds
`FactoryEditor` first, because the cook loads the spike's editor module. The first build compiles the whole game
target (about 20 minutes); later ones are incremental. `installed` is what you actually installed, and the harness refuses any difference from
`harness/toolchain.json`.

## 5. Check it

Actions → spike → the latest `main` push: `selftest` waits for your approval, then passes. Then run S0 with
"Run workflow".

## Removing the runner

Stop the service, run `config.cmd remove` with the token GitHub shows, delete `D:\actions-runner`, then delete the
`satis-runner` account and its folders.
