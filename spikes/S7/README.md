# S7: read and change auto-pause natively (claim C9)

Part of the core mod's spec (`docs/specs/2026-10-20-core-mod-v1-design.md`, "Spikes before the build").

**What the mod does.** 60 s after the staged save loads, and then every 15 s, it runs one step and logs it:
`read` (its value of `FG.DSAutoPause` and whether a change is pending), `set` (the opposite value), `check` (read
again), `restore` (the first value) and `final` (read again), then `satis-mod s7 done`. Reads call the game's own
`Handler_GetServerOptions`, and writes call `Handler_ApplyServerOptions`, in-process on controllers the mod creates
under the server's API manager. It logs only `FG.DSAutoPause`: the options also hold FRM's token.

**What the harness does.** It reads auto-pause through `GetServerOptions` before the mod acts, pairs each step with
`GetServerOptions` read right after it, and puts the first value back at the end whatever happened.

**Pass:** all five steps logged; GetServerOptions equals the mod's value at each; `set` and `check` are the opposite
of `read`; `restore` and `final` equal it; nothing pending at `read`, `check` or `final`.

Runs only as `satis-runner` (it loads the S3 save; see `docs/runner.md`).
