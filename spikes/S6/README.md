# S6: read every status field natively (claim C8)

Part of the core mod's spec (`docs/specs/2026-10-20-core-mod-v1-design.md`, "Spikes before the build").

**What the mod does.** On each new game world it logs one line every 10 s, 15 times:
`satis-mod s6 running= paused= players= limit= duration= tick= health= session=`. The values come from
`UFGServerSubsystem::GetServerGameState()` and from the game's own `Handler_HealthCheck`, called in-process on a
controller the mod creates under the server's API manager. The session is the first 12 hex characters of the name's
SHA-1 over UTF-8; the name itself is never logged.

**What the harness does.** The test server auto-loads its last save, so a first boot clears that setting (run 1
found every reading already had a game); the setting is put back at the end. Then it pairs each line with `QueryServerState` and `HealthCheck` called right after, first in
the entry map (no world), then after loading the staged save. It discards the first batch of each phase, which was
logged before it started reading.

**Pass:** at least 10 judged pairs without a world and 10 with one; running, paused, players, limit, health and
session equal; game duration within 10 s (one round); tick rate within 2 or 10%, whichever is larger.

Runs only as `satis-runner` (it loads the S3 save; see `docs/runner.md`).
