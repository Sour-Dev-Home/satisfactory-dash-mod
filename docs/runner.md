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

## S3's world (owner's go-ahead, 2026-10-20)

R7 steps 3 and 4 (2026-11): **nothing in this repository depends on or reads FRM.** The mod has no FRM client (step 3), and the
harness no longer reads it: the `parity`, `native-parity`, `s4-capture` and `node-agent` runs, the runner config's `frmBaseUrl`
and `parity` keys, and the FRM legs of `enrol`, `manual` and `tick-cost` are gone (step 4). The record of what FRM-era
verification showed stays in the dash repo's `docs-vault/raw-sources/mod-native-readers/` (S1 to S4-B, copied byte for byte) and in
the CI artifacts of those runs. The runner config must drop `frmBaseUrl` and `parity`: the schema is strict and refuses them. A
run's mod config no longer carries `frmPort`, `frmToken` or the `*Source` keys; an old file that still has them is ignored by the mod,
which logs one `Config: ignored removed keys ...` line per config load (names only). The test server may still carry its own FRM
install; the harness neither uses nor needs it, and removing the folder is the owner's choice.

Spike S3 was retired 2026-11-05 with R7 (owner decision): its power reference was FRM, and the native power reader's parity is on
record in `native-parity` run 41340686528. Its runner wiring, judge and tests are deleted (they stay in git history). The staged save is still used by S6, S7 and the
core runs. Facts from satisfactory-dash's G0 addendum 2 (#462): `docs-vault/raw-sources/dedicated-server-api-saves-2026-10-20.md`
and `dedicated-server-save-files-2026-10-20.md`.

- **The world.** A copy of the owner's newest save is staged at `D:\satis-test\saves\s3-world.sav`, and the config
  names it as `"s3World"`. Each run uploads it with `UploadSaveGame` and `LoadSaveGame: true` (saves capture E4).
  The test server runs with insecure local access, so no token is needed.
- **Only as `satis-runner`.** The server writes the uploaded save, and its autosaves named after the save's session,
  into the running account's `%LocalAppData%\FactoryGame\Saved\SaveGames\server` (save-files capture E4). The
  owner's real server keeps its saves in the owner's folder, so a run as the owner would add autosaves to its
  rotation. The harness refuses S6 and S7 when that folder holds another server's `ServerSettings.<port>.sav`.

S6 and S7 load the same staged save and follow the same rule: they run only as `satis-runner`, and the harness
refuses them when another server's settings share the saves folder. They need only `s3World`. S6 reads
`QueryServerState` and `HealthCheck`; S7 reads `GetServerOptions` and keeps only `FG.DSAutoPause`. Both put
auto-pause back as they found it.

That restore runs in the harness's own cleanup, so a run that is killed (a cancelled workflow, a runner crash) after
S7's mod changed the setting leaves it changed. After a killed world
run, start the test server once, read `FG.DSAutoPause` with `GetServerOptions`, and set it back to what the test server
had before (`ApplyServerOptions`, or the in-game server manager) before S2 measures anything.

S6 also boots the server once first to clear its auto-load save (`SetAutoLoadSessionName` with an empty name), so the
measured boot starts with no game, and sets it back at the end. A killed S6 run can leave it empty: the server then
boots to the empty entry map until a save is loaded, which the world spikes do themselves.

## The S2 world (Test2)

The S2 native runs (ADR-0058; `native-parity` is retired, see above) loaded the owner's `Test2` save, not S3's. The
runner config names it as `"s2World"`, beside `"s3World"`; no remaining run uses it, and the key stays valid:

```json
{ "s3World": "<path to the S3 save>", "s2World": "<path to the Test2 copy>" }
```

- Every remaining run (`enrol`, `tick-cost`, the spikes) loads `s3World`, and `tick-cost-native` and `soak-native` load
  `sprint3World`.
- A run that needs `s2World` and finds none in the config is refused before the build: `<id> needs s2World in the runner
  config`.
- The staged file lives outside the repository, like `s3World`. The same `satis-runner` rules apply: each run uploads it
  with `UploadSaveGame` and `LoadSaveGame: true`, and only runs as `satis-runner` (the harness refuses when another
  server's settings share the saves folder).

> **Owner checklist: `Test2` must contain, before the S2 runs** (if something is missing, add it in the save and stage the new copy; nothing in the harness can build it):
> 1. Production machines of **several kinds** (at least assembler, constructor and one more recipe machine), some with a **non-100% clock**, at least one **unconfigured** machine, at least one machine in standby/paused (all are checked, not required to pass if absent: absent ones are reported NOT EXERCISED).
> 2. **At least one oil refinery** (or packager) running a **fluid** recipe, so liquid slots and rates are compared.
> 3. **Power**: at least one connected circuit with production and consumption.
> 4. **Trains running a timetable between at least two stations** (self-driving), each station with at least one cargo platform, wagons carrying items. A parked or manual train is welcome, not required.
> 5. The save loads on the test server (the same dedicated-server version as `harness/toolchain.json`) and is the drones-and-trucks save used for the 3B captures.

## The Sprint 3 world (Test3)

The S3 native runs (`tick-cost-native`, `soak-native` and the one-off `s3-census`; ADR-0058 Sprint S3) load the owner's `Test3` save. The
runner config names it as `"sprint3World"`, beside `"s2World"` and `"s3World"`:

```json
{ "s3World": "<path to the S3 save>", "s2World": "<path to the Test2 copy>", "sprint3World": "<path to the Test3 copy>" }
```

- `tick-cost-native`, `soak-native` and the one-off `s3-census` use `sprint3World` (the `sprint3` world key). The spikes
  keep `s3World`.
- A run that needs `sprint3World` and finds none in the config is refused before the build: `<id> needs sprint3World in
  the runner config`.
- The staged file lives outside the repository, like `s3World`. The same `satis-runner` rules apply: each run uploads it
  with `UploadSaveGame` and `LoadSaveGame: true`, and only runs as `satis-runner`.

> **Owner checklist: `Test3` must contain, before the S3 runs** (it is `Test2` plus the items below; if something is missing, add it in the save and stage the new copy; nothing in the harness can build it):
> 1. Everything the `Test2` checklist asks (machines of several kinds, a fluid recipe, power, self-driving trains between two stations with cargo platforms).
> 2. **Belts of two tiers** (for example Mk.1 and Mk.2), a **splitter**, a **merger**, and at least one **lift**.
> 3. **Pipes** with a **junction or a pump**.
> 4. **Belts into and out of the two paired drone ports** (labelled `FUEL 5` and `TestLand1` in the owner's save), and a **battery belt into one port**.
> 5. **Rail tracks**, including at least one curved track. A train that may have crashed is welcome, not required.
> 6. The save loads on the test server (the dedicated-server version of `harness/toolchain.json`). A byte copy is staged on the runner as `sprint3-test3.sav`; the owner adds `"sprint3World": "<path to that copy>"` to the runner config (outside the repository).
> Absent items are reported `NOT EXERCISED`, never failed.

## 5. Check it

Actions → spike → the latest `main` push: `selftest` waits for your approval, then passes. Then run S0 with
"Run workflow".

## Core mod tests

The core mod's unit tests (`mod/SatisDashAgent`, UE automation tests) run through `mod-test.yml`, which calls
`harness test-mod`.

- **Rule: one Unreal build at a time.** A local `test-mod` and the self-hosted runner both mirror into the same SML project
  folder (`modTest.project`, on `D:`, such as `D:/SatisfactoryModding/...`). Never run a local `test-mod` while a runner
  `mod-test` is in progress; run 40599303052 failed at link for this reason.
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
- `enrol` needs the backend host (below) and the staged world (`s3World`), so it runs only as
  `satis-runner`. It boots the server once without the mod to clear the auto-load save (as S6 does) and puts it back at
  the end, then enrols the mod through a recorder on `127.0.0.1` that sits between the mod and the relay. Every accepted
  snapshot is compared with the game's own APIs (status, auto-pause and its pending value). **The mod's power circuits are no
  longer compared with FRM: that equality is retired by owner ruling 2026-11-05 (R7, D5).** The record is `native-parity`: power
  R0 passed 356 rounds in run 41340686528, plus the earlier sprint runs, in the dash repo's
  `docs-vault/raw-sources/mod-native-readers/`. In its place each accepted power circuit must be internally consistent,
  and a failing circuit is named in the reason: no negative `productionMW`, `consumptionMW`, `capacityMW` or
  `maxConsumptionMW`; `consumptionMW` not above `maxConsumptionMW`; `batteryPercent` within 0-100. Enrolment, the credential,
  the posting and the contract validation of the power part are unchanged. It also
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
  (`tick-cost.json`). For S12 (since R7 step 4, owner 2026-11-05, the recorder's loopback port is the only endpoint allowed
  without a DNS-name match; the 2026-10-26 allowance for a second loopback port ended with the FRM bridge), after the with-mod window the harness also forward-resolves
  (system resolver) each name the baseline's DNS cache gave to an address it contacted, and records it as
  `outboundForwardWithMod`; a new with-mod endpoint passes when its address is in the forward set of a name the baseline
  contacted on the same port (owner, 2026-10-27; a failed lookup matches nothing).
- `parity` and `native-parity` (and `s4-capture`, and the Node-agent driver) were retired in R7 step 4 (owner, D5): they
  existed to compare the mod with FRM or to capture it. Their record is in the dash repo's
  `docs-vault/raw-sources/mod-native-readers/` and in the CI artifacts (power R0 passed 356 rounds in run 41340686528; every S1
  to S4-B reader passed `native-parity`, last in that run). `docs/native-readers.md` keeps the reader table. The dash's Node
  agent stays in the dash repo; if the dash wants to exercise it against a backend it needs its own copy of that driver.
- `tick-cost-native` also writes `"stepDiagnostics": true` into the mod's config (the mod's thread-cycle and conveyor call-timing
  diagnostics are off otherwise; the judge treats their lines as report-only). It writes no other mod config key: every
  reader is native and unconditional, so the judge requires them all, explicitly.

- `tick-cost-native` is `tick-cost` with every reader native (they all are, unconditionally, since R7), loading the `Test3`
  save (`sprint3World`) in both boots. The judge needs the mod's `Readers:` line to say native for all thirteen readers and a
  step line, inside the measured window, for each sliced read (`Resource node scan`, `Factory read`, `Trains read`,
  `Train routes read`, `Logistics read` with `powerLines=1`, `Map markers read`, `Drone routes read`, `Buildables read`) plus a
  clean `Station rates` counts line, each longest step at 2.0 ms or less. It boots once without the mod and once with it and measures each for the warmup plus 10 minutes, judged
  by the same limits (tick median at least 95% of the baseline's, S12, same save, memory within 100 MB and no more than 25 MB
  growth) plus proof that the native readers were live in the measured window (`runs/tick-cost-native/pass.ts`). Unlike
  `tick-cost`, the mod enrols after the world is loaded and auto-pause is off, just before the measurement starts, so its first
  resource-node post (8 minutes, +/-10%, after the agent starts Running) falls 3 to 13 minutes into the measurement, which is
  after the warmup. The mod logs one line per resource-node scan (actors, steps, longest step, total, in `result.json`'s
  `modLines`), which is the scan's cost. Since the owner's ruling of 2026-10-26 the longest step of every sliced native read
  (node scan, `Factory read`, `Trains read`, `Train routes read`) is judged: over 2.0 ms fails, and so does a native reader
  whose line is missing. When a longest step passes 2.0 ms the mod also logs one diagnostic line, `<Name> read longest step: <x.xx> ms
  over <n> elements, phase <a> to <b>` (the judge does not read it; the phase is 0 Collect, 1 ReadRails, 2 ReadConveyors,
  3 ReadPowerLines for `Logistics`, the level index for `Resource node`, 0 for the others). One element at about 9 ms
  means a stall outside the mod's code, not a slow element of ours. A step over 2.0 ms still passes as a wait (owner rulings
  2026-11-03 and 2026-11-05) when the diagnostic line has the cycles tail (`stepDiagnostics`), its thread ran at under 25% of
  the **run-wide median** of the "other steps" cycles per ms over every diagnostic line in the log, and its CPU estimate (wall
  time x that share) is within 2 ms; the verdict reports each such step with the median used. A single diagnostic line is its
  own median; without the cycles tail the plain ceiling holds. The routes layer follows the
  resource-node layer's first-read rule (first scan 8 minutes, +/-10%, after Running, then every 10 minutes), so its first post
  also falls 3 to 13 minutes into the measurement, inside the window. `snapshot-parts.json` (the measured window and which parts
  each accepted snapshot carried; no bodies) is written by `tick-cost` too.
- `soak-native` is `tick-cost-native` with a 4 hour measured window (owner 2026-11-08; it was 16 hours) and the extra soak conditions in
  `runs/soak-native/pass.ts`; it needs the same backend host and `sprint3World`.
- `s3-census` is a one-off, read-only run of Sprint S3 (ADR-0058; the owner approves each dispatch). It needs the backend host
  and the staged world (`sprint3World`, `Test3`). It writes no mod config key (every reader is native), loads `Test3`, turns auto-pause off,
  enrols the mod, and waits at most 14 minutes for the mod's first `Logistics read (game thread only):` line (the first pass
  lands about 8 minutes after Running), then for the `Conveyor drone ports:` line and the first tier census line that follow it,
  all inside that one 14-minute bound (a timeout falls through to judging the log as it is). It reads what only the
  running game says: the belt/lift `GetSpeed()` unit, the tier display names, the pipe flow limits and the drone-port ends.
  `census.json` holds the parsed `Conveyor tier census:` lines (kind, name, speed, count), the `Conveyor kinds:` line, the
  `Conveyor drone ports:` line and the counts of the `Logistics read` line. Pass is a `Logistics read` line with
  `layers rails=1 conveyors=1` and at least one census line; nothing else is judged. Every value is printed in `reasons`
  (game class names and tier display names only there and in `census.json`), and each belt/lift tier named `Mk.1` to `Mk.4`
  shows `speed / 2` beside the expected 60/120/270/480 with `match` or `NO MATCH`, each pipe tier `Mk.1`/`Mk.2` shows
  `flow x 60` beside 300/600. If a belt/lift tier prints `NO MATCH`, a small ruling PR sets `NativeConveyorCapacityMode` to
  `ECapacityMode::ClassTable`. Start it through the runner config's `run-backend.ps1` script (in its `scripts` folder) with
  `-Runs s3-census`, like the other backend runs, because it needs the backend host: a bare `gh workflow run core-run.yml -f
  run=s3-census` dispatch refuses with "reading the backend's images".

  **The order of the sprint's runs** (each once, the owner dispatches): `mod-test` on each PR branch, then `s3-census` (above),
  then one `tick-cost-native`, all on `Test3`. Nothing else is run.

### Manual session

`manual` holds the test server up for a person to play on, for the dash's G-B1 and G-B2 checks. It needs no backend and the staged world, so it
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
- The client needs SML installed: the server runs SML, and a client without SML can't join a modded server.
- The verdict is availability only: `QueryServerState` is sampled every 30 s, and a sample that is missing, gets no
  answer within 10 s, or says the game isn't running fails the run. If the server exits, the run stops and fails. Since R7 step 4 the run no longer reads FRM's belt and pipe lists (they were evidence only, never judged); G-B1 and G-B2 are judged by the owner from what they saw in the game.

### Core runs on the Linux test server

`serverHost: "docker"` in the runner config makes core runs drive the Linux test server container
(`docker/linux-server/README.md`) instead of `FactoryServer.exe`. The default is `"windows"`; nothing else changes.
Every run's `result.json` records `serverHost`.

- **Folders.** `serverDir` (for example `D:\satis-linux\root`) holds `FactoryGame\Saved`, `FactoryGame\Mods` and
  `FactoryGame\Configs`. They are the container's three bind-mount sources, so the mod install, the log, the credential
  and `SatisDashAgent.cfg` resolve exactly as on Windows. The game files live in the named volume.
- **Config keys.** `docker.container`, `docker.image`, `docker.volume`; `docker.apiRelay` (`"27778:27777"`, the image's
  `API_RELAY`, so the server sees the harness's API calls as loopback); `docker.publish` (every `docker run -p` value,
  each beginning `127.0.0.1:`); `docker.forwardPorts`. `apiBaseUrl` is the published API port, `serverArgs` carries
  `-Port` and `-ReliablePort`.
- **Forward ports.** The recorder and the http-probe stubs listen on 127.0.0.1 ports the container must already forward
  when it starts (`FORWARD_PORTS`). With `docker` the listeners take their ports in turn from `docker.forwardPorts`
  (give it at least 4 free ports; http-probe uses 4) instead of ephemeral ones.
- **Command shape.** The harness runs `docker run -d --name <container> -e API_RELAY=… -e FORWARD_PORTS=… -v <volume>:/srv/satis/server
  -v <serverDir>\FactoryGame\{Saved,Mods,Configs}:/srv/satis/server/FactoryGame/{Saved,Mods,Configs} -p … <image>
  -ini:Engine:[SystemSettings]:FG.DedicatedServer.AllowInsecureLocalAccess=1 <serverArgs>`, after `docker rm -f <container>`.
- **Stopping.** API `Shutdown`, the same 60 s grace as Windows, then `docker stop`, then `docker rm -f`.
- **Memory.** `memory.json` holds the server process's `VmRSS` and `VmHWM` from `/proc/<pid>/status`
  (`FactoryServer-Linux-Shipping`, found with `ps` through `docker exec`), never `docker stats`, which counts page cache.
- **The build.** On the docker host the core run builds both server platforms (`build-mod.ps1 -Platforms Win64+Linux`) and
  installs the `LinuxServer` tree. `Mods\SML` under `serverDir` holds SML's own `SML-LinuxServer.zip` (3.12.0), unpacked.
- **Choosing the host.** The runner's `.env` keeps `SATIS_HARNESS_CONFIG` (the Windows config) and gains
  `SATIS_HARNESS_CONFIG_LINUX` (the Linux one, e.g. `harness-linux.json` beside it); restart the runner service after
  editing it. `core-run.yml`'s `host` input (`windows` by default, or `linux`) picks between them, and a `linux` run
  refuses when `SATIS_HARNESS_CONFIG_LINUX` is unset. By hand: `gh workflow run core-run.yml --ref <branch> -f run=<run> -f host=linux`.

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

## Linux toolchain (installed 2026-11-06)

The `linux-build` workflow cross-compiles the mod for the Linux dedicated server (plan
`docs/plans/2026-11-06-linux-server-build.md`, L1). UE 5.6 pins one toolchain: `Engine/Config/Linux/Linux_SDK.json`
gives `MainVersion = MinVersion = MaxVersion = v25_clang-18.1.0-rockylinux8`, and UBT finds it through the machine
variable `LINUX_MULTIARCH_ROOT` (`Engine/Source/Programs/UnrealBuildTool/Platform/Linux/LinuxPlatformSDK.cs:221`).

- Download: `https://cdn.unrealengine.com/CrossToolchain_Linux/v25_clang-18.1.0-rockylinux8.exe` (Epic's CDN; 946,496,944
  bytes, SHA-256 `C1C7798900B434502091629ADA194AC38CCF8B13A94E0D81F8C464E0C87D03F7`, Authenticode signed by Epic
  Games Inc.). Forum reports say this link once served v23; check `ToolchainVersion.txt` after installing.
- Install, silent, to D:: `v25_clang-18.1.0-rockylinux8.exe /S /D=D:\UnrealToolchains\v25_clang-18.1.0-rockylinux8`.
  The installer sets the machine variable `LINUX_MULTIARCH_ROOT=D:\UnrealToolchains\v25_clang-18.1.0-rockylinux8\`.
- Restart the runner service so jobs see the variable (an administrator PowerShell):
  `Restart-Service -Name 'actions.runner.*'`.
- Check: `ToolchainVersion.txt` reads `v25_clang-18.1.0-rockylinux8`, and
  `x86_64-unknown-linux-gnu\bin\clang++.exe --version` prints clang 18.1.0. The workflow's first step checks both.

Run it with `gh workflow run linux-build.yml --ref <branch>`. It runs `harness build-mod --platforms Win64+Linux`
(`scripts/build-mod.ps1`, one archive per server platform, unzipped to `WindowsServer/` and `LinuxServer/`), then
`harness check-build`: each tree has the plugin, a library per shipped module and no test code, and both carry one
version. The LinuxServer tree is uploaded as an artifact for 14 days.

## Removing the runner

Stop the service, run `config.cmd remove` with the token GitHub shows, delete `D:\actions-runner`, then delete the
`satis-runner` account and its folders.
