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

## Core runs

`harness core-run <run> --out <dir>` (workflow `core-run.yml`, "Run workflow" only) puts the core mod on the test
server and judges it by `runs/<run>/pass.ts`. Each run builds the mod with `scripts/build-mod.ps1` (so `modTest` must
be set, as above) into `<out>-build`, beside the uploaded out dir and never inside it. It installs the mod, writes its
config, starts the server, drives the run and stops the server. Afterwards it always deletes the mod's config and
credential and uninstalls the mod, failed runs included. Every run also fails if the mod logs at
Error, or if the server log carries the run's enrolment code, the agent secret or the string `Bearer`.

- `http-probe` talks only to stubs it starts on `127.0.0.1`, and needs `openssl` to make a throwaway certificate. It looks
  for it on the runner account's `PATH`, in the file named by `SATIS_OPENSSL` (checked first), then in Git for
  Windows' usual folders (`C:\Program Files\Git\usr\bin`, `...\mingw64\bin`). With none of them it refuses before
  building the mod.
- `core-run.yml` offers every run the harness drives; one that needs the backend host refuses unless the owner has
  started it (below). It needs the `ue-runner` deployment approved, like `spike.yml`.
- `enrol` needs the backend host (below) and the staged world (`s3World`, `frmBaseUrl`), so it runs only as
  `satis-runner`. It boots the server once without the mod to clear the auto-load save (as S6 does) and puts it back at
  the end, then enrols the mod through a recorder on `127.0.0.1` that sits between the mod and the relay. It also
  records the server's memory (`memory.json`). Each sample's label and `game` field say whether a game was running when
  it was taken, as `QueryServerState` said then, with the session's hash: the no-mod sample comes from that first boot,
  which runs the auto-loaded save (`noModWithGame`), and `withModWithGame` is taken at the same stage (a save just loaded,
  healthy). Compare those two only when their session hashes match. It is evidence only, not a pass condition yet.
- `revoke` needs the backend host and no world (it clears the auto-load save like `enrol`). It enrols, revokes the
  server, watches for the halt line and two quiet minutes, then enrols the same server again with a new code
  (`revoke.json`). `command` needs the backend host and the staged world: it flips auto-pause from the dashboard, sends a
  command the mod must refuse, and restores the value (`command.json`). Both record `memory.json` with `enrol`'s labels.
- `outage` needs the backend host and the staged world. With the world running and the mod enrolled, it samples ticks
  for 3 minutes (after the usual warmup), stops the relay for 3 minutes, starts it again and waits 3 minutes for the
  catch-up (`outage.json`). It always starts the relay again, even when the run fails.
- `tick-cost` needs the backend host and the staged world, and takes about twice as long as the others: it boots once
  without the mod (world loaded, auto-pause off) and once with it, and measures each for the warmup plus 10 minutes. Every
  30 s it samples the outbound TCP connections and the working set of the server's process tree, by the spawned pid
  (`tick-cost.json`). No FRM poller runs during it.
- `parity` needs the backend host, the staged world (`s3World`, `frmBaseUrl`) and `parity.agentCjs` (below). With the
  world running and auto-pause off, it enrols the mod through the recorder, then makes a game API token on the test
  server (`server.GenerateAPIToken`, through the HTTPS API's `RunCommand` on loopback) and sets up satisfactory-dash's
  Node agent with it, as `satis-runner`, in a throwaway `SD_AGENT_HOME` under that account's temp folder: `set-tokens`
  (the tokens on its stdin, never on a command line), `check`, `enroll` with the `node` server's code (seeded just
  before) against the same recorder, then `run`. Both agents run side by side for 30 minutes and are judged against
  each other (`parity.json`): status and auto-pause by nearest pair, power against the Node agent's readings just
  before and just after each mod sample (the owner's bracket). A Node agent snapshot over the recorder's 2 MB cap is kept
  without its factory, players and map parts and counted; a mod snapshot that large fails the run, as in every other run. At the end it stops the Node agent, deletes its folder (a failed stop
  or a folder left behind fails the run) and prunes the test server's application tokens
  (`server.InvalidateAPITokens`). `parity.json` also records `agent.cjs`'s sha256 and the contract pin. It takes about
  45 minutes plus the build.
- `native-parity` needs the backend host, the staged world and `frmBaseUrl`. It writes `playersSource` and `resourceNodesSource`
  as `native` into the mod's config (`modConfig`, recorded in `mod-config.json`), loads the world with auto-pause off, starts
  reading the test server's FRM as a black box (`getPower` back to back, `getPlayer` every 2 s with names hashed before they
  are stored, `getResourceNode` every minute and right after each mod post), then enrols the mod through the recorder. After a
  one-minute settle it judges 30 minutes: the mod's one resource-node post (about 8 minutes after enrolment, then every 30)
  falls inside it. Each reader is judged on its own (`runs/native-parity/pass.ts`: power within 3% of FRM's reads either side
  of each mod sample; players equal; the resource-node layer equal and contract-valid) and the three verdicts go to
  `readers.json`; the run is red if any reader is. Fields the world never exercised are reported as `NOT EXERCISED` and do not
  fail (`NOT_EXERCISED_FAILS` in the pass file flips that). It takes about 45 minutes plus the build. To exercise "online"
  players, join the test server with a game client during the window.
- `tick-cost-native` is `tick-cost` with the players and resource-node readers set to `native` in the mod's config
  (`mod-config.json`). It boots once without the mod and once with it and measures each for the warmup plus 10 minutes, judged
  by the same limits (tick median at least 95% of the baseline's, S12, same save, memory within 100 MB and no more than 25 MB
  growth) plus proof that the native readers were live in the measured window (`runs/tick-cost-native/pass.ts`). Unlike
  `tick-cost`, the mod enrols after the world is loaded and auto-pause is off, just before the measurement starts, so its first
  resource-node post (8 minutes, +/-10%, after the agent starts Running) falls 3 to 13 minutes into the measurement, which is
  after the warmup. The mod logs one line per resource-node scan (actors, steps, longest step, total, in `result.json`'s
  `modLines`), which is the scan's cost; the owner fixes a ceiling for the longest step after the first measurement. The mod's
  factory, mapLive and rails readers are still on the bridge, as in `tick-cost`. `snapshot-parts.json` (the measured window
  and which parts each accepted snapshot carried; no bodies) is now written by `tick-cost` too.

### The parity run's Node agent

The Node agent must be built from the commit the contract is pinned to (`harness/contract/manifest.json`, `commit`;
81f22aef today), since it checks the enrolment code itself. In your own terminal, in the satisfactory-dash checkout the
runner config's `s5.checkout` names (it is at that commit already, for the backend):

    git fetch origin
    git checkout --detach 81f22aef6d679b5a2b8465673b2abe51dbaf98ef
    npm ci
    npm run build -w agent
    New-Item -ItemType Directory -Force D:\satis-runner-config\node-agent
    Copy-Item agent\dist\agent.cjs D:\satis-runner-config\node-agent\agent.cjs -Force
    icacls D:\satis-runner-config\node-agent /inheritance:r /grant:r "${env:USERNAME}:(OI)(CI)F" "satis-runner:(OI)(CI)RX"

`agent.cjs` is one bundled file (esbuild), so it is all the runner needs. Then add to `D:\satis-runner-config\harness.json`:

    "parity": { "agentCjs": "D:/satis-runner-config/node-agent/agent.cjs" }

Rebuild and copy it again whenever the contract pin moves. The run never touches your live agent or its store: its
own store lives only in the run's `SD_AGENT_HOME`, is DPAPI-protected for `satis-runner`, and is deleted at the end.

### Manual session

`manual` holds the test server up for a person to play on, for the dash's G-B1 and G-B2 checks (whether FRM's belt
`ConnectedTo` goes stale after a rebuild, and the unit of pipe `Speed`). It needs no backend and the staged world, so it
runs only as `satis-runner`, like every other world run: the server must be this account's test server, never the
owner's own.

- The owner dispatches `core-run` with `run=manual`. The run builds and installs the mod as every core run does
  (`CoreRunSpec` can't skip that), but writes no config, so the mod stays idle. It boots the server, loads the staged
  world, turns auto-pause off, then keeps the server up for about `MANUAL_MINUTES` (default 45, at most 75; the workflow
  times out at 90; the last sample is 30 s before the end) and logs `manual session: N min left` every minute. Then it
  puts auto-pause back, stops the server and cleans up as every run does. Cancelling the workflow kills the runner before
  that cleanup, as for every run, so auto-pause stays off on the test server until the next run puts it back.
- The client joins `127.0.0.1:17777` (the test server's `-Port`), from the same PC. The harness sets no server password
  and no admin claim: it starts the server with insecure local access, which only lifts the token for the HTTPS API on
  loopback. If the game asks for a password or to claim the server, that is the server's own state, not something the
  harness set or knows; the owner does it in the client. No secret belongs in this repository.
- The client needs SML installed: the server runs SML and FRM, and a client without SML can't join a modded server.
- The verdict is availability only: `QueryServerState` is sampled every 30 s, and a sample that is missing, gets no
  answer within 10 s, or says the game isn't running fails the run. If the server exits, the run stops and fails. `manual.json` also holds FRM's `getBelts` and `getPipes` (`frmBaseUrl`) at the start and at the end, trimmed
  to `ID`, `ClassName`, `ConnectedTo0`, `ConnectedTo1` and `ItemsPerMinute` or `Speed`. A failed FRM read is recorded and
  does not fail the run; when both fail, the first reason says `NO FRM EVIDENCE`. G-B1 and G-B2 are judged by the owner from what they saw and from those two reads.

## The backend host

Runs that need a backend (`enrol` and the later core runs) talk to the throwaway satisfactory-dash backend from S5,
which needs Docker, and `satis-runner` is not in `docker-users`. So the owner runs it, and the runner reaches it
through a small host on loopback.

1. The runner config gains `"backendHost": { "file": "D:/satis-runner-config/backend-host/backend-host.json", "port": 13102 }`
   next to `"s5"` (the satisfactory-dash checkout and the relay's port). The file lives in the runner's config folder,
   outside the repository.
2. Once, make the host file's folder readable by the owner and `satis-runner` only (no inherited entries; the file is
   written fresh at every start, so it takes the folder's rights):

       New-Item -ItemType Directory -Force D:\satis-runner-config\backend-host
       icacls D:\satis-runner-config\backend-host /inheritance:r /grant:r "${env:USERNAME}:(OI)(CI)F" "satis-runner:(OI)(CI)R"

3. Before dispatching a backend run, start it in your own terminal (Docker Desktop running):

       $env:SATIS_HARNESS_CONFIG = "D:\satis-runner-config\harness.json"
       cd harness; npm run harness -- backend-host

   It brings the stack up from nothing (`down -v` first), listens on `127.0.0.1:<port>`, writes the host file
   (`relayUrl`, `controlUrl` and a random 64-hex-character token) and prints `backend host up: relay <url>; Ctrl+C to stop`.

   Before anything boots, it checks the images the stack will run (`APP_IMAGE` for the backend, `OPS_IMAGE` for the
   migrations; compose.yaml's defaults are `ghcr.io/sour-dev-home/satisfactory-dash-backend:main` and `...-ops:main`).
   Each image's `org.opencontainers.image.revision` label (`docker image inspect`) must be the contract pin
   (`harness/contract/manifest.json`, `commit`) or a later commit: the checkout's own git answers that
   (`git merge-base --is-ancestor <pin> <revision>`), so nothing needs the network. Otherwise it refuses:
   - an image that is missing, too old or has no revision label: `docker pull <image>` (it names it), then start again;
   - "can't tell whether ... descends": the checkout doesn't know that commit yet, so run `git fetch origin` in it, then
     start again;
   - "git could not be run" or "docker could not be run": put it on the terminal's `PATH`.

   Every backend run records the two images and their revisions in its evidence (`backend.json`), with the run's own
   contract pin beside them.
4. After the runs, press Ctrl+C: it runs `down -v` (the throwaway database is gone) and deletes the host file. A Ctrl+C
   while the stack is still coming up also ends in `down -v`, and a second Ctrl+C is ignored by the host. Docker's own
   commands share the terminal, though, so a Ctrl+C can still cut a `docker compose` step short. If the terminal was
   closed or a step was cut, clean up by hand from the satisfactory-dash checkout:

       docker compose -p satis-s5 down -v

   (The next `backend-host` start also runs `down -v` first.)

It serves only fixed operations on the throwaway database, each behind the token: seed an enrolment code, revoke a
credential, queue a `set_auto_pause` command, read a command, read a server's state, stop or start the relay, and say
which images it runs. No SQL
and no shell come from the caller; codes, ids and params are checked before anything runs. It uses the same compose
project as a local S5 run (`satis-s5`), so never run both at once.

## Removing the runner

Stop the service, run `config.cmd remove` with the token GitHub shows, delete `D:\actions-runner`, then delete the
`satis-runner` account and its folders.
