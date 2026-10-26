# Native readers: status and the source switch

ADR-0058 (satisfactory-dash) replaces FicsitRemoteMonitoring (FRM) reader by reader. Rules for writing a reader are in
`docs/clean-room.md`.

| Reader | Sprint | Source today | Notes |
| --- | --- | --- | --- |
| Status, auto-pause | core v1 | native | proven (S6, S7) |
| Power (R0) | S1 | native | read natively since core v1; S1 judges every field against FRM (`native-parity`) |
| Players (R1) | S1 | `native` (default) | passed `native-parity` (runs 40244496806, 40253178598: 351 and 357 rounds equal to FRM; NOT EXERCISED: any online player) and `tick-cost-native` (run 40262414493); offline players come from the save's player cache (`GetAllPlayerInfoData`), not `GetOfflineCachedPlayerInfosInSession`, which crashed the dedicated server |
| Resource nodes (R4) | S1 | `native` (default) | a sliced scan of the world's actors on the game thread (2,000 per tick, once per 30 minutes), the sort and the JSON built on the worker; a scan that finds nothing posts nothing and retries after 30 s. Passed `native-parity` (runs 40244496806, 40253178598: 608 items, 31 exploited, equal to FRM's) and `tick-cost-native` (run 40262414493). Scan cost measured at about 4.7 ms a scan and 0.3 ms per step on the game thread; the longest-step ceiling is still the owner's to set |

## The hidden source override (a stopgap, not a setting)

Until the bridge is removed (ADR-0058, R7) a host can force a reader's source in the mod's config file
(`SatisDashAgent.cfg`, the same JSON as `backendUrl`):

    "playersSource": "native"
    "resourceNodesSource": "bridge"

The value is exactly `bridge` or `native` (anything else makes the config Broken, as a bad `frmPort` does). With no key a
reader uses its default (`DefaultPlayersSource`, `DefaultResourceNodesSource` in `Config.h`). `bridge` needs FRM installed.
The keys are deleted with the bridge in the removal release; do not build on them. There is no key for power: it has no
bridge path.
