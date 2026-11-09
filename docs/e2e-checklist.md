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

## 3. Enrol

- [ ] Create an enrolment code in the dashboard.
- [ ] Put it in `enrollmentCode` (and `backendUrl` if you test against a backend other than the default). Restart the
      server, or wait if the notes say the mod reloads the file.
- [ ] The log shows "Enrolling with the code from SatisDashAgent.cfg", then "Enrolled; reporting to the backend".
- [ ] `SatisDashAgent.cfg`'s `enrollmentCode` is now blank, and `FactoryGame/Saved/SatisDash/credential.json` exists.
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
| 0.3.0 | | | | | |
