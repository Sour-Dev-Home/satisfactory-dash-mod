# Native readers: status and the source switch

ADR-0058 (satisfactory-dash) replaces FicsitRemoteMonitoring (FRM) reader by reader. Rules for writing a reader are in
`docs/clean-room.md`.

| Reader | Sprint | Source today | Notes |
| --- | --- | --- | --- |
| Status, auto-pause | core v1 | native | proven (S6, S7) |
| Power (R0) | S1 | native | read natively since core v1; S1 judges every field against FRM (`native-parity`) |
| Players (R1) | S1 | `bridge` (default) | native reader built; default flips in its switch PR after `native-parity` passes |
| Resource nodes (R4) | S1 | `bridge` (default) | native reader built: a sliced scan of the world's actors on the game thread (2,000 per tick, once per 30 minutes), the sort and the JSON built on the worker; a scan that finds nothing posts nothing and retries after 30 s; default flips after `native-parity` passes |

## The hidden source override (a stopgap, not a setting)

Until the bridge is removed (ADR-0058, R7) a host can force a reader's source in the mod's config file
(`SatisDashAgent.cfg`, the same JSON as `backendUrl`):

    "playersSource": "native"
    "resourceNodesSource": "bridge"

The value is exactly `bridge` or `native` (anything else makes the config Broken, as a bad `frmPort` does). With no key a
reader uses its default (`DefaultPlayersSource`, `DefaultResourceNodesSource` in `Config.h`). `bridge` needs FRM installed.
The keys are deleted with the bridge in the removal release; do not build on them. There is no key for power: it has no
bridge path.
