# Native readers: status and the source switch

ADR-0058 (satisfactory-dash) replaces FicsitRemoteMonitoring (FRM) reader by reader. Rules for writing a reader are in
`docs/clean-room.md`.

| Reader | Sprint | Source today | Notes |
| --- | --- | --- | --- |
| Status, auto-pause | core v1 | native | proven (S6, S7) |
| Power (R0) | S1 | native | read natively since core v1; S1 judges every field against FRM (`native-parity`) |
| Players (R1) | S1 | `native` (default) | passed `native-parity` (runs 40244496806, 40253178598: 351 and 357 rounds equal to FRM; NOT EXERCISED: any online player) and `tick-cost-native` (run 40262414493); offline players come from the save's player cache (`GetAllPlayerInfoData`), not `GetOfflineCachedPlayerInfosInSession`, which crashed the dedicated server |
| Resource nodes (R4) | S1 | `native` (default) | a sliced scan of the world's actors on the game thread (2,000 per tick, once per 30 minutes), the sort and the JSON built on the worker; a scan that finds nothing posts nothing and retries after 30 s. Passed `native-parity` (runs 40244496806, 40253178598: 608 items, 31 exploited, equal to FRM's) and `tick-cost-native` (run 40262414493). Scan cost measured at about 4.7 ms a scan and 0.3 ms per step on the game thread; the owner set the ceiling at 2 ms per step (2026-10-26) and `tick-cost-native` judges it. The reader now also carries `className` (`GetResourceClass()->GetName()`); R4's S1 parity pass predates className, so it is judged again in S2's `native-parity` run |
| Factory (R2) | S2 | `bridge` (default) | the switch `factorySource` exists and the native reader is in (R2a and R2b), not yet judged: a sliced read of the buildable list on the game thread (one budgeted step per driver tick, 1 ms), one reading per manufacturer, the sort, rates and JSON built on the worker. With `native`, the bridge never sends `getFactory`; a pass that cannot read (no world, a world swapped mid-read, no machines) leaves the part out and never falls back to FRM. Its `native-parity` and `tick-cost-native` judgement is the S2 run (Task 10) |
| Trains and stations (R3) | S2 | `bridge` (default) | the switch `trainsSource` exists and the native reader is in (R3), not yet judged: a sliced read of the railroad subsystem's trains and stations on the game thread (one budgeted step per driver tick, 1 ms), joined into the `mapLive` part on the worker at the factory cadence. The same reader also feeds a new, native-only world layer, `trainRoutes` (stations with their cargo platforms, trains with their timetable stops and cargo; first read 3 minutes after Running, then every 10 minutes), which is never requested from FRM and is not scheduled at all while `trainsSource` is `bridge`. A world with no trains and no stations sends empty arrays; only an abandoned read (world swapped, no railroad subsystem) leaves the part out. With `native`, the bridge never starts the `getTrains`/`getTrainStation` pair |

## The hidden source override (a stopgap, not a setting)

Until the bridge is removed (ADR-0058, R7) a host can force a reader's source in the mod's config file
(`SatisDashAgent.cfg`, the same JSON as `backendUrl`):

    "playersSource": "native"
    "resourceNodesSource": "bridge"
    "factorySource": "bridge"
    "trainsSource": "bridge"

The value is exactly `bridge` or `native` (anything else makes the config Broken, as a bad `frmPort` does). With no key a
reader uses its default (`DefaultPlayersSource`, `DefaultResourceNodesSource`, `DefaultFactorySource`, `DefaultTrainsSource` in `Config.h`; the last two are `bridge`). `bridge` needs FRM installed.
The keys are deleted with the bridge in the removal release; do not build on them. There is no key for power: it has no
bridge path.
