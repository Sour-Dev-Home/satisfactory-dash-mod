# Linux server build plan: build, run and validate the mod on a Linux dedicated server

> **For agentic workers:** use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans, task by task. Steps use checkbox (`- [ ]`) syntax.

Status: **owner asked for it, 2026-11-06** ("yes plan it… Docker seems fine. use D drive if possible for stuff. Lets do L3 too"). D1-D6 below take the recommended defaults unless the owner changes them.
Why: 0.2.0 ships Windows only (`docs/releases/0.2.0.md`). A ficsit.app listing, and every host on Linux, needs a `LinuxServer` build that has been tested on a real Linux server.

**Goal:** the mod builds for `LinuxServer`, runs on the game's Linux dedicated server in Docker on the runner PC, and passes the same core runs as on Windows: enrol, http-probe, tick-cost-native and soak-native.

**Architecture:**
- **L1, build:** cross-compile from the Windows runner with UE's Linux toolchain. Alpakit's `PackagePlugin` with `-serverplatform=Win64+Linux` gives one archive per platform.
- **L2, run:** an image of our own (Debian slim, SteamCMD, app 1690800) runs the Linux server, with its files on D:. The harness gains a `docker` server host next to the Windows one.
- **L3, cost and soak:** the existing tick-cost-native and soak-native judges, unchanged, run against that host.

**Tech stack:**
- UE 5.6.1 (CSS installed build), SML 3.12.0, Alpakit;
- toolchain `v25_clang-18.1.0-rockylinux8`;
- Docker Desktop (Linux containers);
- the TypeScript harness (Vitest, zod).

**Spec:** none separate. This plan is the design. It argues from ADR-0058 (the native readers, the ficsit.app listing after R7) and the harness spec `docs/specs/2026-10-19-g2-harness-design.md` (the core runs and their judges).

## Facts this plan stands on (read-only pass, 2026-11-06)

- **Toolchain:** `Engine/Config/Linux/Linux_SDK.json` pins `MainVersion = MinVersion = MaxVersion = v25_clang-18.1.0-rockylinux8` (AutoSDK platform `Linux_x64`). UBT reads `LINUX_MULTIARCH_ROOT` (`Engine/Source/Programs/UnrealBuildTool/Platform/Linux/LinuxPlatformSDK.cs:221`), and falls back to the in-tree SDK (:226-233), which isn't present.
  - `LINUX_MULTIARCH_ROOT` isn't set in any scope.
  - No toolchain is installed.
- **Engine:** `Engine/Binaries/Linux` already holds the FactoryServer Linux Shipping binaries (`libFactoryServer-*-Linux-Shipping.so`), so the engine can link a Linux server target.
- **Alpakit:** its targets are `Windows`, `WindowsServer` and `LinuxServer` (`Mods/Alpakit/Source/Alpakit/Private/AlpakitSettings.cpp:64`). Server platforms are joined with `+` into `-server -serverplatform=A+B` (`AlpakitProfile.cpp:49`). SML's own CI packages with `-serverplatform=Win64+Linux` (`SatisfactoryModLoader/.github/workflows/build.yml:192`).
- **Our build:** `scripts/build-mod.ps1:36-38` packages with `-serverplatform=Win64 -noclient`. `mod-test.yml` runs on `[self-hosted, windows, ue]`.
- **Our code:**
  - The only Windows code is `SatisDashGameModule.cpp:14-17,27-36`: `QueryThreadCycleTime`, under `#if PLATFORM_WINDOWS`, used only when `stepDiagnostics` is on.
  - `Driver.h:28` already allows `ThreadCycles` to be empty.
  - No module has a platform allow or deny list.
  - libcurl, OpenSSL, nghttp2 and zlib come from the engine (`SatisDashGame.Build.cs:16`).
- **TLS trust:** the transport takes its trusted roots from UE's SSL module (`CurlTransport.cpp:133-134,243`). How the Linux build gets the test backend's root is unknown, and Task 5 finds out.
- **Harness, Windows-only pieces** (`harness/src/server-control.ts`):
  - `spawnServer` (:52) runs `serverDir/serverExe`, defaulting to `FactoryServer.exe` (`config.ts:22`);
  - `killTree` (:63) uses `taskkill`;
  - `readProcessMemory` (:163) reads PowerShell CIM `WorkingSetSize`.
  - Mods go to `<serverDir>/FactoryGame/Mods/<name>` (:11-14). The log is `<serverDir>/FactoryGame/Saved/Logs/FactoryGame.log` (`config.ts:51-53`).
- **Runner:** Docker reports `Server.Os = linux` (Docker Desktop). D: has about 726 GiB free.
- **Linux server:** SteamCMD app **1690800**, launched with `./FactoryServer.sh -log -unattended`. Don't run it as root.

## Global constraints

- Every repo rule holds (`CLAUDE.md`): no API from memory (`api-sources.md` rows), never weaken a pass condition, evidence is machine-written, loopback and the test server only, plain PRs merged by the owner, no PII or local paths committed, and the clean room (ADR-0058 rule 7, including **not** using any third-party Satisfactory server image or mod manager that bundles FRM).
- **Everything big lives on D:** (owner 2026-11-06):
  - the toolchain at `D:\UnrealToolchains\v25_clang-18.1.0-rockylinux8`;
  - the Linux server files and saves under `D:\satis-linux\`;
  - Docker Desktop's disk image moved to D: (D2).
- **Never stop `D:\satisfactoryds`.** The Linux container uses its own ports, its own name (`satis-linux-ds`) and its own folder.
- **The Windows path stays the default:** `serverHost` defaults to `windows`, and every existing run and test behaves as today.
- **Server cost:** the shipped mod gains no work on Linux. Any Linux diagnostic stays behind `stepDiagnostics` (off by default), as on Windows.
- **No local UE build or test-mod while a runner job runs.**
- Edits to `D:\satis-runner-config\` (`harness.json`, `run-backend.ps1`) and machine environment variables are the owner's to approve, and each is asked for when its task reaches it.
- Subagents run on Sonnet.

## Decisions (recommended defaults)

- **D1, server image:** our own `docker/linux-server/Dockerfile` (`debian:bookworm-slim`, SteamCMD from Valve's archive, a non-root `steam` user). Not a third-party server image: we control what's in it, and the clean room holds.
- **D2, storage:** move Docker Desktop's disk image to D: (Settings → Resources → Advanced → Disk image location; the owner does this). The server install goes in a named volume (`satis-linux-server`), because a bind mount from a Windows drive is slow for a game server and would skew L3's timing.
  - Bind-mounted from `D:\satis-linux\`:
    - `Saved/` (logs, the credential, saves), so the harness reads logs and files with plain paths, as on Windows;
    - `Mods/SatisDashAgent/`, so the harness installs the mod with a plain copy.
- **D3, networking:**
  - Publish the game ports to loopback only: `-p 127.0.0.1:27777:27777/tcp`, `-p 127.0.0.1:27777:27777/udp`, `-p 127.0.0.1:28888:28888/tcp`. These are new ports, so the Windows test server's 17777 and 18888 are untouched.
  - Inside the container, a `socat` forward maps `127.0.0.1:<backend port>` to `host.docker.internal:<backend port>`. The mod's backend URL stays `https://127.0.0.1:<port>`, so rule 5 holds and the traffic never leaves the PC.
- **D4, Linux step diagnostics:** add a Linux thread CPU clock (`clock_gettime(CLOCK_THREAD_CPUTIME_ID)`, behind `stepDiagnostics`) so a soak on Linux can tell a wait from work, as Windows does with cycles. The judge's waited-step rule gains a Linux form: CPU ms per wall ms under 25% of the run's median, and the CPU estimate at most 2 ms.
  - **This is a new pass condition, so it needs the owner's ruling before Task 9 starts.**
  - Without it, a Linux step over 2 ms simply fails. That's stricter, never weaker.
- **D5, which runs validate Linux:** enrol and http-probe (L2), then tick-cost-native and one 16 h soak-native (L3), each on the Linux host. Each result records `serverHost`, so a Linux pass is never read as a Windows one.
- **D6, release:** the first release with Linux is **0.3.0**. It packages both platforms and its notes drop "Windows only". The ficsit.app listing is a separate owner decision after it.

## Review focus (the failure modes the tasks' tests must pin)

1. **The test backend's certificate isn't trusted on Linux.** UE's SSL module may load different roots on Linux than on Windows. Expected: enrol passes because the test root was added the way the engine reads it on Linux (Task 5 finds that path in the engine source). The fix must never disable verification. http-probe's S1 (an untrusted certificate is refused) must still pass on Linux.
2. **Case-sensitive paths.** Linux paths are case-sensitive: `Mods/SatisDashAgent`, `Saved/SatisDash/credential.json`, `SatisDashAgent.cfg`. Expected: the mod finds its config and writes its credential at exactly the paths the harness reads. Task 6's test asserts the exact names.
3. **Stopping without Shutdown.** `docker rm -f` without the API's `Shutdown` would kill the server mid-write. Expected: stop calls `Shutdown` first and waits the same grace period as on Windows, and only then runs `docker stop`, then `rm -f`. Task 6 tests the order.
4. **Memory is measured differently.** Windows counts the working set of the process tree. On Linux the equivalent is the server process's resident set size (RSS). Expected: the soak's memory rules judge the `FactoryServer-Linux-Shipping` process's RSS, read from `/proc/<pid>/status` `VmRSS` and `VmHWM`. They never use `docker stats`, which also counts the page cache. Task 6 tests the parsing.
5. **The build ships tests or misses the `.so`.** Expected: the Linux archive holds `SatisDashCore` and `SatisDashGame` as Linux shared libraries and no `SatisDashCoreTests`. The Windows archive is unchanged. Task 3's check pins this.

---

## L1: build

### Task 1: install the Linux toolchain (owner action, outside the repo)

**Files:** none in the repo. A new runbook section goes in `docs/runner.md` ("Linux toolchain").

- [ ] **Step 1:** Look up the download for `v25_clang-18.1.0-rockylinux8` on Epic's "Cross-Compiling for Linux" page for UE 5.6. Use the version from `Linux_SDK.json`, not from memory. Record the URL and its published checksum, if Epic gives one, in the runbook.
- [ ] **Step 2 (owner):** Install it to `D:\UnrealToolchains\v25_clang-18.1.0-rockylinux8`.
- [ ] **Step 3 (owner):** Set the machine variable `LINUX_MULTIARCH_ROOT=D:\UnrealToolchains\v25_clang-18.1.0-rockylinux8\`, then restart the runner service so jobs see it.
- [ ] **Step 4:** Verify in a new shell: `[Environment]::GetEnvironmentVariable('LINUX_MULTIARCH_ROOT','Machine')` names the folder, and `<root>\x86_64-unknown-linux-gnu\bin\clang++.exe --version` prints clang 18.1.0.
- [ ] **Step 5:** Commit the runbook section: `docs(runner): Linux cross-compile toolchain`.

### Task 2: `build-mod.ps1` builds a chosen set of server platforms

**Files:**
- Modify: `scripts/build-mod.ps1` (the `RunUAT` line 36-38, the archive step 33-44, the tests check 47-48).
- Test: `harness/test/build-mod-args.test.ts` (new). It checks the command line the script would run, through a `-DryRun` switch that prints it.

**Interfaces:**
- Produces: `build-mod.ps1 -Platforms Win64|Win64+Linux` (default `Win64`, today's behaviour) and `-DryRun`. The output is one archive per platform under `Saved/ArchivedPlugins/SatisDashAgent/`. The script unzips each into `<out>/<Platform>/SatisDashAgent/`, so `Win64` → `WindowsServer`, `Linux` → `LinuxServer`.

- [ ] **Step 1:** Write the failing test. It runs `pwsh -File scripts/build-mod.ps1 -Project X -Engine Y -Out Z -DryRun -Platforms Win64+Linux` and asserts:
  - the printed `RunUAT` line contains `-serverplatform=Win64+Linux` and `-noclient`, and no `-platform=`;
  - with no `-Platforms`, the line contains `-serverplatform=Win64`, exactly as today.
- [ ] **Step 2:** Run `npx vitest run harness/test/build-mod-args.test.ts --reporter=dot`. Expected: FAIL (no `-DryRun`).
- [ ] **Step 3:** Add `[ValidateSet('Win64','Win64+Linux')][string]$Platforms = 'Win64'` and `[switch]$DryRun`. Build the `RunUAT` arguments from `$Platforms`. With `-DryRun`, print them and exit 0 before any build.
- [ ] **Step 4:** Make the archive step loop over every zip in `ArchivedPlugins\SatisDashAgent\`. Each zip's platform comes from its name (record the real names Alpakit writes, from the first real build in Task 3, in a comment). Unzip each into `<out>/<Platform>/`. Run the `SatisDashCoreTests` check on every unzipped tree.
- [ ] **Step 5:** Run the test. Expected: PASS. Then run the full Vitest suite. Expected: all pass.
- [ ] **Step 6:** Commit: `build-mod: -Platforms (Win64 default, Win64+Linux) and -DryRun`.

### Task 3: a `linux-build` workflow and an archive check

**Files:**
- Create: `.github/workflows/linux-build.yml`.
- Create: `harness/src/check-build.ts` (exported `checkBuild(root: string): string[]`, the problems found; empty means good), plus a `check-build` CLI command in the harness entry point, next to `check-api`.
- Test: `harness/test/check-build.test.ts`, with fixture trees under `harness/test/fixtures/build/`.

**Interfaces:**
- Consumes: Task 2's `<out>/WindowsServer/` and `<out>/LinuxServer/` trees.
- Produces: `npm run harness -- check-build <out>`. It exits 1 and lists the problems if any are found.

- [ ] **Step 1:** Write the failing tests. Make the fixture trees with empty files of the right names:
  - **good:** `LinuxServer/SatisDashAgent/Binaries/Linux/` holds a `.so` for `SatisDashCore` and one for `SatisDashGame`, with `WindowsServer/.../Binaries/Win64/*.dll` beside it → `[]`;
  - **no Linux `.so`** → `["LinuxServer: no SatisDashGame library"]`;
  - **a `SatisDashCoreTests` file in either tree** → a problem naming the file;
  - **a `.uplugin` whose `SemVersion` differs between the two trees** → a problem.
  
  Take the exact `.so` file names from the first real build (Step 5). Until then, match `*SatisDashGame*.so` and `*SatisDashCore*.so`, and tighten to the real names in Step 6.
- [ ] **Step 2:** Run `npx vitest run harness/test/check-build.test.ts --reporter=dot`. Expected: FAIL (no module).
- [ ] **Step 3:** Implement `checkBuild` with `readdirSync` recursion and those four rules. Wire up the CLI command.
- [ ] **Step 4:** Add `linux-build.yml`: `workflow_dispatch`, plus a push to `main` touching `mod/**` or `scripts/build-mod.ps1`; runner `[self-hosted, windows, ue]`; concurrency group `ue-runner`. Its steps:
  1. `npm ci`;
  2. `npm run harness -- check-api`;
  3. `pwsh -File scripts/build-mod.ps1 -Platforms Win64+Linux -Out $RUNNER_TEMP/build`;
  4. `npm run harness -- check-build $RUNNER_TEMP/build`;
  5. upload `LinuxServer/` as artifact `SatisDashAgent-LinuxServer`, kept 14 days.
- [ ] **Step 5:** Push and dispatch `gh workflow run linux-build.yml --ref <branch>` after Task 1 is done. Iterate on compile and link errors here, each fix its own commit. A source change for Linux must keep the Windows mod-test passing: dispatch `mod-test` after any `mod/` change.
- [ ] **Step 6:** Tighten `check-build`'s names to what the real build wrote, then run the test and the full suite.
- [ ] **Step 7:** Commit: `linux-build workflow and check-build (L1)`.

**L1 is done when** `linux-build` passes on the branch and its artifact holds the Linux `.so` files.

---

## L2: run on a Linux server

### Task 4: the Linux server image and its first boot (vanilla, no mods)

**Files:**
- Create: `docker/linux-server/Dockerfile`, `docker/linux-server/entrypoint.sh`, `docker/linux-server/README.md`.
- Modify: `docs/runner.md` ("Linux test server").

**Interfaces:**
- Produces:
  - the image `satis-linux-ds:local`;
  - the volume `satis-linux-server` (the game install);
  - bind mounts `D:\satis-linux\Saved` → `/srv/satis/server/FactoryGame/Saved` and `D:\satis-linux\Mods` → `/srv/satis/server/FactoryGame/Mods`;
  - the env `BACKEND_PORT` (for the socat forward);
  - extra arguments passed through to `FactoryServer.sh`.

- [ ] **Step 1:** Write the `Dockerfile`:
  - Base: `debian:bookworm-slim`, with packages `ca-certificates lib32gcc-s1 socat curl procps`.
  - SteamCMD from `https://steamcdn-a.akamaihd.net/client/installer/steamcmd_linux.tar.gz`, unpacked into `/srv/satis/steamcmd`.
  - User `steam` (uid 1000), owning `/srv/satis` (container paths stay off `/home`, which the PII check flags).
  - Volume `/srv/satis/server`; `ENTRYPOINT ["/entrypoint.sh"]`.
- [ ] **Step 2:** Write `entrypoint.sh`, with `set -euo pipefail`:
  1. If `/srv/satis/server/FactoryServer.sh` is missing, or `UPDATE=1`, run `steamcmd +force_install_dir /srv/satis/server +login anonymous +app_update 1690800 validate +quit`.
  2. If `BACKEND_PORT` is set, start `socat TCP-LISTEN:${BACKEND_PORT},bind=127.0.0.1,fork,reuseaddr TCP:host.docker.internal:${BACKEND_PORT} &`.
  3. `exec /srv/satis/server/FactoryServer.sh -log -unattended "$@"`.
- [ ] **Step 3 (owner):** Move Docker Desktop's disk image location to D: (D2). Create `D:\satis-linux\Saved` and `D:\satis-linux\Mods`.
- [ ] **Step 4:** Build and boot it vanilla:
  ```
  docker build -t satis-linux-ds:local docker/linux-server
  docker run -d --name satis-linux-ds -v satis-linux-server:/srv/satis/server -v D:\satis-linux\Saved:/srv/satis/server/FactoryGame/Saved -p 127.0.0.1:27777:27777/tcp -p 127.0.0.1:27777:27777/udp -p 127.0.0.1:28888:28888/tcp satis-linux-ds:local -Port=27777 -ReliablePort=28888
  ```
  Expected: `D:\satis-linux\Saved\Logs\FactoryGame.log` appears. The HTTPS API on `https://127.0.0.1:27777` answers `HealthCheck`. Check the API's route and payload from the existing `ServerApi` in the harness, not from memory.
- [ ] **Step 5:** Install SML 3.12.0's `LinuxServer` build into `D:\satis-linux\Mods\SML`. Use the SML GitHub release asset for 3.12.0, recording the asset name and the URL in the runbook. Restart the container. Expected: the log shows SML loaded.
- [ ] **Step 6:** Stop it cleanly with the API `Shutdown`, then `docker stop satis-linux-ds`, then `docker rm satis-linux-ds`. Commit: `docker: Linux dedicated server image (L2)`.

### Task 5: the mod's TLS trust on Linux (investigation, then the smallest fix)

**Files:**
- Read only first: the engine's SSL module (`Engine/Source/Runtime/Online/SSL/Private/`, the certificate manager and its Linux branch).
- Modify: `api-sources.md` (rows for whatever symbol or config the fix uses), `docker/linux-server/entrypoint.sh` and/or the harness backend setup, plus `docs/runner.md`.

- [ ] **Step 1:** Find how the Windows runs trust the loopback backend's certificate today (`docs/runner.md`, the backend-host setup: Windows store or engine bundle). Write the answer in the runbook.
- [ ] **Step 2:** Read the SSL module's certificate loading for Linux: which files or directories it reads (the engine's bundled `cacert.pem`, system paths, the project's `Certificates` folder, and so on). Cite `path:line`, and add an `api-sources.md` row for anything we'll depend on.
- [ ] **Step 3:** Choose the smallest change that adds **only the test root**, in a place the engine reads, inside the container or the test server's files. Never `VERIFYPEER 0`, never a mod-side bypass, and nothing that changes what the shipped mod trusts on a real host.
  - If the only working option is a mod change, stop and bring it to the owner.
- [ ] **Step 4:** Commit: `Linux test server trusts the test backend's root (L2)`.

### Task 6: the harness's `docker` server host

**Files:**
- Modify: `harness/src/config.ts`. Add `serverHost: z.enum(["windows", "docker"]).default("windows")` and `docker: z.strictObject({ container: z.string().min(1), image: z.string().min(1), volume: z.string().min(1), savedDir: z.string().min(1), modsDir: z.string().min(1), ports: z.array(z.string().min(1)) }).optional()`, with a refine: `serverHost: "docker"` requires `docker`.
- Create: `harness/src/server-host-docker.ts`.
- Modify: `harness/src/server-control.ts` (choose the host), `harness/src/core-run.ts` (`realCoreDeps` takes the host; the `result.json` evidence gains `serverHost`).
- Test: `harness/test/server-host-docker.test.ts`, `harness/test/config.test.ts`.

**Interfaces:**
- Consumes: Task 4's image, container, volume and mounts, and Task 5's trust setup.
- Produces, in `server-host-docker.ts`:
  ```ts
  export type Exec = (cmd: string, args: string[]) => Promise<{ code: number; stdout: string; stderr: string }>;
  /** docker run -d --name <container> -v <volume>:/srv/satis/server -v <savedDir>:…/Saved -v <modsDir>:…/Mods -p … -e BACKEND_PORT=<port> <image> -ini:… <serverArgs> */
  export function dockerRunArgs(cfg: RunnerConfig, backendPort: number | null): string[];
  export const dockerSpawner: (exec: Exec, cfg: RunnerConfig, backendPort: number | null) => Spawner;
  /** After the API Shutdown and its grace period: docker stop, then docker rm -f. */
  export const dockerKiller: (exec: Exec, cfg: RunnerConfig) => Killer;
  /** VmRSS and VmHWM (kB) from /proc/<pid>/status of FactoryServer-Linux-Shipping, through docker exec. */
  export function parseProcStatus(text: string): { rssBytes: number; peakBytes: number } | null;
  export const readContainerMemory: (exec: Exec, cfg: RunnerConfig) => Promise<ProcessMemory | null>;
  ```
  - `ServerProcess.pid` for docker is the container's server PID from `docker inspect -f {{.State.Pid}}`. It's only used for the killer and memory, so `exited` resolves from `docker wait <container>`.
  - The mod install (`installMod`, `modTarget`) works unchanged with `serverDir` set to a folder whose `FactoryGame/Mods` and `FactoryGame/Saved` are the bind mounts. Use a junction `D:\satis-linux\root\FactoryGame\{Mods,Saved}` → the mount folders, so `logPathOf` and `credential.json` also resolve unchanged.

- [ ] **Step 1:** Write the failing tests, with a fake `Exec` recording its calls:
  - `dockerRunArgs` publishes only `127.0.0.1:` ports, includes `INSECURE_LOCAL_ARG` and `cfg.serverArgs` in order, and passes `BACKEND_PORT` only when a port is given.
  - Stop order: a test drives `stopServer` with the docker host and asserts the API `Shutdown` comes before `docker stop`, which comes before `docker rm -f` (review focus 3).
  - `parseProcStatus("VmHWM:\t 812345 kB\nVmRSS:\t 700000 kB\n")` → `{ rssBytes: 716800000, peakBytes: 831841280 }`. Text without them → `null` (review focus 4).
  - Config: `serverHost: "docker"` without `docker` fails to parse. Omitting `serverHost` gives `"windows"`, and every existing config fixture still parses.
  - Paths: with the docker config, `logPathOf` and the credential path end in exactly `FactoryGame/Saved/Logs/FactoryGame.log` and `FactoryGame/Saved/SatisDash/credential.json` (review focus 2).
- [ ] **Step 2:** Run the two test files. Expected: FAIL.
- [ ] **Step 3:** Implement `server-host-docker.ts` and the config keys. In `server-control.ts`/`realCoreDeps`, pick the spawner, killer and memory reader by `cfg.serverHost`. Leave the Windows functions untouched.
- [ ] **Step 4:** Record `serverHost` in each run's evidence. Then add a judge-side guard in the shared verdict writer: the verdict's reasons begin with `Linux (docker) host:` when the evidence says docker. That's added text, never a changed condition.
- [ ] **Step 5:** Run the narrow tests, then the full suite and `tsc`. Expected: all pass, and the existing 692 still pass.
- [ ] **Step 6:** Commit: `harness: docker server host for the Linux test server (L2)`.

### Task 7: the first Linux core runs, enrol and http-probe (L2 validation)

**Files:** `docs/runner.md` ("Running core runs on the Linux server"). Outside the repo, with the owner's approval when asked: a second config `D:\satis-runner-config\harness-linux.json` (`serverHost: "docker"`, `serverDir: D:\satis-linux\root`, ports 27777 and 28888), and a `-Config` switch for `run-backend.ps1`.

- [ ] **Step 1:** Ask the owner to approve the two edits outside the repo. Make them.
- [ ] **Step 2:** Pre-stage the run for the owner. Each run needs its own config, so build the mod with `-Platforms Win64+Linux` and point `builtPluginDir` at the `LinuxServer` tree:
  ```
  pwsh -File D:\satis-runner-config\scripts\run-backend.ps1 -Branch <branch> -Config harness-linux.json -Runs enrol,http-probe
  ```
- [ ] **Step 3:** Judge the results as they come. Expected: both pass unchanged, with `serverHost: "docker"` in the evidence.
  - A failure stops the work and gets reported with its bundle.
  - A Linux-only mod fix goes through mod-test (Windows) and linux-build before rerunning.
- [ ] **Step 4:** Record the run IDs in this plan's ledger and the runbook. Commit.

**L2 is done when** enrol and http-probe both pass on the Linux host.

---

## L3: cost and soak on Linux

### Task 8: tick-cost-native on Linux

- [ ] **Step 1:** Run tick-cost-native with `harness-linux.json`. Expected: it passes under the plain 2 ms ceiling.
  - With diagnostics off, the waited rule never applies on either platform.
  - A step over the ceiling is a failure, reported with its bundle.
- [ ] **Step 2:** Record the run ID and the step percentiles next to the Windows run, in the runbook. Commit.

### Task 9: Linux thread CPU clock for step diagnostics (only if D4 is ruled "yes")

**Files:**
- Modify: `mod/SatisDashAgent/Source/SatisDashGame/Private/SatisDashGameModule.cpp` (a `#elif PLATFORM_LINUX` branch beside `:27-36`), `Driver.h:28` (the comment, and the field if it has to say ns versus cycles), plus the run judge that holds the waited rule and its tests.
- Modify: `api-sources.md` (the `clock_gettime` / `CLOCK_THREAD_CPUTIME_ID` header row, from the toolchain's sysroot `time.h`, `path:line`).

- [ ] **Step 1:** Write the failing judge tests. A Linux step at 3 ms wall with 0.1 ms CPU, where the run's median is 0.8 CPU ms per wall ms, passes as waited. The same step with 2.5 ms CPU fails. A step with no CPU field is judged by the plain ceiling.
- [ ] **Step 2:** Implement the mod branch. It runs only when `stepDiagnostics` is on: read `CLOCK_THREAD_CPUTIME_ID` before and after the step, and report it in the step line's existing diagnostic shape, with a unit the judge can tell apart from cycles.
- [ ] **Step 3:** Run mod-test (Windows; the branch doesn't compile there), linux-build, then the narrow judge tests and the full suite.
- [ ] **Step 4:** Commit: `Linux thread CPU time in step diagnostics (off by default; owner ruling D4)`.

### Task 10: soak-native on Linux (16 h)

- [ ] **Step 1:** Run soak-native with `harness-linux.json`, with `stepDiagnostics` on, as the Windows soak had it. The judge is unchanged except for D4's Linux form, if ruled. Memory comes from the server process's RSS (Task 6).
- [ ] **Step 2:** Pass: record it, and close L3. Fail: stop and report the bundle. The waits on a Docker Desktop VM can differ from a native Linux host, so a wait-driven failure goes to the owner, who decides whether it's the host or the mod.

---

## Release

### Task 11: 0.3.0 with Linux

- [ ] **Step 1:** Make the release packaging produce Alpakit's multi-target archive (`-merge`, `AlpakitProfile.cpp:89`) with WindowsServer and LinuxServer. `check-build` passes on it.
- [ ] **Step 2:** Bump to `0.3.0`: the `.uplugin`, the judges' `mod/0.3.0` (`runs/enrol/pass.ts`, `runs/http-probe/pass.ts`; `mod-version.test.ts` guards them), and `docs/releases/0.3.0.md` ("Linux servers are supported", with the Linux run IDs).
- [ ] **Step 3:** Run mod-test and linux-build, then open the PR. After the owner merges: tag `v0.3.0` and create the GitHub release with both platform zips. The ficsit.app listing is the owner's next call.

## Order and owner actions

| When | Owner action |
|---|---|
| Task 1 | Install the toolchain on D:, set `LINUX_MULTIARCH_ROOT`, restart the runner service |
| Task 4 | Move Docker Desktop's disk image to D: |
| Task 7 | Approve `harness-linux.json` and the `-Config` switch |
| Task 7, 8, 10 | Start the Linux runs (pre-staged commands) |
| Before Task 9 | Rule on D4 (the Linux waited-step rule) |
| Task 11 | Merge, then the ficsit.app decision |

L1 (Tasks 1-3) can start now: it touches only the build. L2's harness work (Task 6) can run beside it. The runner-bound steps wait for the runner to be free.
