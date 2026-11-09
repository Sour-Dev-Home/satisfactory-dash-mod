# End-to-end release checklist

A manual test of a release the way a server owner meets it: a fresh server, the release zip, the install steps in the
release notes, and the real dashboard. The harness runs prove the mod's behaviour on the test server, but they build
from source, copy files in and talk to a test backend; this checklist covers what they don't.

**When:** after the release PR's harness runs pass and before the tag, against the release-candidate zip (the same file
the GitHub release will carry). Again before a ficsit.app listing if anything changed, and through ficsit.app's own
install path.

**Ground rules**
- Use a **fresh** server: never the dashboard test server you keep running, and never a server with an earlier install
  of the mod or SML. Do one **Windows** and one **Linux** server for any release that ships both.
- Follow **only** the release notes and README for the install. Anything you had to know that they don't say is a
  finding: write it down.
- Note each failure with the time, the server's `LogSatisDash` lines and a screenshot, then stop and report it. Don't
  fix things by hand mid-test.

## 0. Before you start

- [ ] Release-candidate zip downloaded; note its SHA-256 (`Get-FileHash <zip>`).
- [ ] Release notes say which SML version is needed; that version is at hand.
- [ ] The dashboard you test against is up, and you can sign in and create an enrolment code.
- [ ] Note the server's memory **before** the mod (game running, a save loaded, no mod): Task Manager's working set on
      Windows, `docker stats` or `/proc/<pid>/status` VmRSS on Linux.

## 1. Fresh server

**Windows:** a new dedicated server install (SteamCMD app 1690800, or the Epic/Steam "Satisfactory Dedicated Server"
tool) in an empty folder, on ports that don't clash with any server you already run.

**Linux:** a new container from `docker/linux-server/` with its **own** volume and name, e.g.
`docker volume create satis-e2e`, then `docker run` as in `docker/linux-server/README.md` with `--name satis-e2e`,
`-v satis-e2e:/srv/satis/server` and its own loopback ports (e.g. 37777 and 38888).

- [ ] The server starts, is claimed in the game client or the server API, and a new or test save is running.
- [ ] Vanilla works: you can join it from the game client.

## 2. Install SML and the mod

- [ ] Install SML as the release notes say, start the server, and check its log loads SML with no errors.
- [ ] Unzip the server's platform (the per-platform zip, or its folder from the multi-target `SatisDashAgent.zip`) to
      `FactoryGame/Mods/SatisDashAgent/`, exactly as the notes describe. Try both zips across the two servers.
- [ ] Start the server. The log has `LogSatisDash` lines and no `Error` line from the mod.
- [ ] `FactoryGame/Configs/SatisDashAgent.cfg` was written with defaults on first start
      (`backendUrl`, `enrollmentCode` empty).
- [ ] The mod logs "Not enrolled: add an enrolment code from the dashboard to SatisDashAgent.cfg" and sends nothing.

## 3. Pair

- [ ] Read the `Pairing code` line in the server console (it is also in `FactoryGame.log`). Enter the code at the
      production `/pair` page (https://satis-manager.com/pair) and approve it onto an existing server, until
      self-serve is on.
- [ ] The log shows "Paired with the dashboard; reporting to the backend".
- [ ] **One player:** with the code still open (or after restarting the server), join alone with the game client. The
      `Pairing code` line appears in the in-game chat, and a client with SML only (no SatisDashAgent) renders it. A
      vanilla client can't join: a server with SML refuses it ("cannot connect a modded server to a vanilla client").
- [ ] **Two players:** with a second player online, no `Pairing code` line appears in chat. The console line still does.
- [ ] `FactoryGame/Saved/SatisDash/credential.json` exists, and `SatisDashAgent.cfg` was not edited.
- [ ] The server appears in the dashboard as online.
- [ ] Search the server's log for the code and the credential: **neither appears**.

## 4. Dashboard: data arrives and stays current

Within a minute of enrolling:
- [ ] **Status:** online, players, game phase/tick; the status updates while you watch (a snapshot every 5 s).
- [ ] **Power:** circuits, generators with fuel, consumption that changes when you build or toggle something.
- [ ] **Players:** join with the game client; you appear, and your position moves on the map.
- [ ] **Factory:** machines, production rates, extractors, storage contents.
- [ ] **Trains and stations:** positions, timetable stops, cargo, transfer rates (use a save that has trains).

About 8 minutes after start (then every 10 or 30 minutes, and only when something changed):
- [ ] **Map layers:** rails, conveyors (belts, lifts, pipes), power lines, map markers, drone routes, resource nodes.
- [ ] Build or remove something on one layer (a belt, a marker); after its next read the map shows the change.

## 5. Commands

- [ ] Toggle **auto-pause** from the dashboard; the server's setting changes (server API or game client), and the
      dashboard shows the new value.

## 6. Restarts and outages

- [ ] Restart the game server. The mod comes back enrolled (no new code needed) and data resumes in the dashboard.
- [ ] Stop the backend or cut the server's network for a few minutes, then restore it. The mod logs the failures
      without `Error` spam, and data resumes on its own.
- [ ] Revoke the server in the dashboard. The mod logs that it was revoked and stops sending. A new code enrols it again.

## 7. Footprint

After about 30 minutes enrolled, with the same save loaded:
- [ ] Server memory vs the no-mod figure from step 0: note both. A difference of tens of MB is expected; hundreds is a
      finding.
- [ ] The game runs smoothly from the client (no hitching you didn't have without the mod), and the log has no
      `Error` line from the mod.

## 8. Remove

- [ ] Delete the mod's folder from `FactoryGame/Mods/` and start the server: it runs as before, with no mod lines.
- [ ] Revoke the test server in the dashboard if it's still enrolled, and delete the fresh server or container and its
      volume (`docker rm -f satis-e2e`, `docker volume rm satis-e2e`).

## Results

| Release | Zip SHA-256 | Windows | Linux | Date | Findings |
|---|---|---|---|---|---|
| 0.3.0 | Linux `99d8a3740413d353bbaeb6b44dfcabc2bba8a1f82c1160c219875f1ae75c4dc6`<br>Windows `10db293754f3e64b20591346ef045d58b64877dd1c041350b2b7df72e0ccbccc`<br>multi-target `366ecc359badf1dd73c5e620a09caf0b204fcefe11e8b42e26962d7f90f1436c` | not run | pairing pass (scope below) | 2026-11-09 | see 0.3.0 notes below |

### 0.3.0 notes

**Scope.** An owner-planned pairing test on Linux only: steps 2 and 3, plus the dashboard check of step 4. Steps 5 to 8
and Windows were not run for this release. The server was the `satis-e2e` container reset for the test: the mod
folder and `SatisDashAgent.cfg` were replaced, but SML 3.12.0 and the save were kept from earlier runs, so it was not
a fresh server in the sense of the ground rules.

**Step 3 results.**
- Pass: the `Pairing code` line is in `FactoryGame.log` at boot (21:09:33 UTC), and a new code is issued every 15
  minutes (four codes from 21:09 to 21:54).
- Pass: one player, a client with SML only, saw the code in chat.
- Not tested: two players. Only one player was available; the owner accepted the risk (2026-11-09). The rule is unit
  tested (`ShouldPostCode` for 0, 1 and 2 players); the live player count is what stays unchecked.
- Pass: paired at 22:04:30 UTC onto an existing dashboard server ("One of my servers"), and every reader is native.
- Pass: `credential.json` was written and the `.cfg` was not edited. The credential's secret is in 0 log lines, and the
  code is only in its own `Pairing code` line.
- Pass: Power and Factory live within a minute; the map layers were replaced on their schedule (see below).

**Findings.**
1. **Players need SML on their game client.** A server with SML refuses a vanilla client. The release notes now say so.
2. **The chat code can't be copied.** Satisfactory's chat text isn't selectable, so the code has to be typed. The four
   codes seen used only `A-Z` and `2-7`, so there's no `0`/`O` or `1`/`I` to confuse, but this wasn't checked in the
   backend.
3. **The container console lags.** `docker logs` stopped at 21:09:25 while `FactoryGame.log` went on; the `Pairing code`
   line reached the console only minutes later. The game's stdout looks block-buffered under Docker. Read the code
   in `FactoryGame.log` on Linux containers.
4. **The map shows the previous server's layers for up to about 9 minutes** after pairing onto an existing dashboard
   server. Power and Factory are live within a minute, but the map layers are first read 2 (rails) and about 8 minutes
   (the rest) after start, and the backend keeps its last copy until then. They were replaced on schedule.
