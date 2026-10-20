# The `ue` runner (owner's PC)

The self-hosted runner that `spike.yml` uses. It builds a spike with the pinned toolchain and runs it on a **separate
test install** of the dedicated server. Spec: `docs/specs/2026-10-19-g2-harness-design.md`, "The runner".

## Before you start

- G2-prep is done: the toolchain is installed exactly as `sml-getting-started` says, and the SML starter project builds.
- Your real server can keep running: the test server uses its own ports (below).
- S5 runs only from a local session with Docker Desktop (owner, 2026-10-20). `satis-runner` is not in `docker-users`, so `spike.yml` doesn't offer S5.

## 1. A low-privilege account and folders

1. Create a local standard (non-admin) Windows user, `satis-runner`.
2. Create `D:\satis-test` (the test server) and `D:\satis-build` (build output). Give `satis-runner` Modify on both,
   and on the SML project `D:\SatisfactoryModding\SatisfactoryModLoader` (a build writes its `Mods\`, `Saved\` and
   `Intermediate\`), and no other grants. Evidence goes to the runner's own temp folder. It must not be able to read
   your real server's folder, your profile or `~/.lanes`.
3. **Check the drive's defaults.** On a second drive, Windows usually gives `Authenticated Users` Modify on the
   whole drive, so every folder on it, your real server included, is writable by `satis-runner` unless you deny
   it. Check with `(Get-Acl D:\).Access`. If it's there, deny the account on each folder it doesn't need, from an
   admin PowerShell:

       foreach ($d in 'D:\satisfactoryds', 'D:\SteamLibrary', 'D:\SteamCMD') {
         icacls $d /deny "satis-runner:(OI)(CI)(F)"
       }

   List every folder at the drive's root and add your own (backups, other games). Leave out the folders above and
   the runner's own folder. Folders Windows already locks down (`Program Files`, `WindowsApps`) refuse the change
   and don't need it. Found on the owner's PC on 2026-10-19: `D:\` grants `Authenticated Users` Modify.

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

## S3's world and FRM (owner's go-ahead, 2026-10-20)

S3 compares the mod's power readings with Ficsit Remote Monitoring (FRM) on a copy of the owner's save. Facts from
satisfactory-dash's G0 addendum 2 (#462): `docs-vault/raw-sources/dedicated-server-api-saves-2026-10-20.md`,
`dedicated-server-save-files-2026-10-20.md` and `frm-config-port-1.5.4-2026-10-20.md`.

- **The world.** A copy of the owner's newest save is staged at `D:\satis-test\saves\s3-world.sav`, and the config
  names it as `"s3World"`. Each run uploads it with `UploadSaveGame` and `LoadSaveGame: true` (saves capture E4).
  The test server runs with insecure local access, so no token is needed.
- **Only as `satis-runner`.** The server writes the uploaded save, and its autosaves named after the save's session,
  into the running account's `%LocalAppData%\FactoryGame\Saved\SaveGames\server` (save-files capture E4). The
  owner's real server keeps its saves in the owner's folder, so a run as the owner would add autosaves to its
  rotation. The harness refuses S3 when that folder holds another server's `ServerSettings.<port>.sav`.
- **FRM.** FRM 1.5.4 is installed on the test server: the folder is copied from the real server's
  `FactoryGame\Mods\GameFeatures\FicsitRemoteMonitoring`. S0–S2 now boot with it too.
- **FRM's port.** The key is `FicsitRemoteMonitoring.Server.uWS.Port` (FRM capture E3), in the test server's
  `FactoryGame\Saved\Config\WindowsServer\GameUserSettings.ini` (FRM capture E2). The line syntax is the one FRM
  wrote into the real server's file, `mIntValues=(("Key", value),…)` under `[/Script/FactoryGame.FGGameUserSettings]`:

      mIntValues=(("FicsitRemoteMonitoring.Server.uWS.Port", 18081),("FicsitRemoteMonitoring.Server.uWS.Autostart", 1))

  The config names `"frmBaseUrl": "http://127.0.0.1:18081"`. FRM's web server didn't start on the empty entry map
  (one boot, 2026-10-20), so the S3 run itself is the first check of the port.

S6 and S7 load the same staged save and follow the same rule: they run only as `satis-runner`, and the harness
refuses them when another server's settings share the saves folder. They don't need FRM, only `s3World`. S6 reads
`QueryServerState` and `HealthCheck`; S7 reads `GetServerOptions` and keeps only `FG.DSAutoPause`. Both put
auto-pause back as they found it.

That restore runs in the harness's own cleanup, so a run that is killed (a cancelled workflow, a runner crash) after
S7's mod changed the setting leaves it changed, and so does S3's after it turned auto-pause off. After a killed world
run, start the test server once, read `FG.DSAutoPause` with `GetServerOptions`, and set it back to what the test server
had before (`ApplyServerOptions`, or the in-game server manager) before S2 or S3 measures anything.

S6 also boots the server once first to clear its auto-load save (`SetAutoLoadSessionName` with an empty name), so the
measured boot starts with no game, and sets it back at the end. A killed S6 run can leave it empty: the server then
boots to the empty entry map until a save is loaded, which the world spikes do themselves.

## 5. Check it

Actions → spike → the latest `main` push: `selftest` waits for your approval, then passes. Then run S0 with
"Run workflow".

## Core mod tests

The core mod's unit tests (`mod/SatisDashAgent`, UE automation tests) run through `mod-test.yml`, which calls
`harness test-mod`.

- The runner config gains `"modTest": { "project": "<SML project folder>", "engine": "<engine root>" }`: the same two
  folders `buildCommand` already passes to `build-spike.ps1`. The owner adds it once.
- `mod-test.yml` needs the `ue-runner` deployment approved, like `spike.yml`. It runs on pushes to `main` that touch
  `mod/` and on "Run workflow", never on pull requests.

## Removing the runner

Stop the service, run `config.cmd remove` with the token GitHub shows, delete `D:\actions-runner`, then delete the
`satis-runner` account and its folders.
