# Native readers: status and the source switch

ADR-0058 (satisfactory-dash) replaces FicsitRemoteMonitoring (FRM) reader by reader. Rules for writing a reader are in
`docs/clean-room.md`.

| Reader | Sprint | Source today | Notes |
| --- | --- | --- | --- |
| Status, auto-pause | core v1 | native | proven (S6, S7) |
| Power (R0) | S1 | native | read natively since core v1; S1 judges every field against FRM (`native-parity`) |
| Players (R1) | S1 | `native` (default) | passed `native-parity` (runs 40244496806, 40253178598: 351 and 357 rounds equal to FRM; NOT EXERCISED: any online player) and `tick-cost-native` (run 40262414493); offline players come from the save's player cache (`GetAllPlayerInfoData`), not `GetOfflineCachedPlayerInfosInSession`, which crashed the dedicated server |
| Resource nodes (R4) | S1 | `native` (default) | a sliced scan of the world's actors on the game thread (2,000 per tick, once per 30 minutes), the sort and the JSON built on the worker; a scan that finds nothing posts nothing and retries after 30 s. Passed `native-parity` (runs 40244496806, 40253178598: 608 items, 31 exploited, equal to FRM's) and `tick-cost-native` (run 40262414493). Scan cost measured at about 4.7 ms a scan and 0.3 ms per step on the game thread; the owner set the ceiling at 2 ms per step (2026-10-26) and `tick-cost-native` judges it. The reader now also carries `className` (`GetResourceClass()->GetName()`); R4's S1 parity pass predates className, so it is judged again in S2's `native-parity` run |
| Rates (R2b) | S2 | `bridge` (default; follows `factorySource`) | `maxPerMinute`, `currentPerMinute` and `percent` per machine, built on the worker with the factory part; native in the same read as Factory, not yet judged. If only `current` misses parity the owner rules on `DefaultRatesMode` (Instantaneous) and the tolerance |
| TrainRoutes | S2 | native only, off (no key; scheduled only while `trainsSource` is `native`) | a new world layer the bridge never sent; first scan 480 s (+/-10%, as resource nodes) after Running, so the first post is inside tick-cost-native's window, then every 600 s; off until Trains switches, and not yet judged |
| Factory (R2) | S2 | `bridge` (default) | the switch `factorySource` exists and the native reader is in (R2a and R2b), not yet judged: a sliced read of the buildable list on the game thread (one budgeted step per driver tick, 1 ms), one reading per manufacturer, the sort, rates and JSON built on the worker. With `native`, the bridge never sends `getFactory`; a pass that cannot read (no world, a world swapped mid-read) is abandoned and leaves the part out, a pass that finishes with no machines sends `{"buildings":[]}`, and neither falls back to FRM. Its `native-parity` and `tick-cost-native` judgement is the S2 run (Task 10) |
| Trains and stations (R3) | S2 | `bridge` (default) | the switch `trainsSource` exists and the native reader is in (R3), not yet judged: a sliced read of the railroad subsystem's trains and stations on the game thread (one budgeted step per driver tick, 1 ms), joined into the `mapLive` part on the worker at the factory cadence. The same reader also feeds a new, native-only world layer, `trainRoutes` (stations with their cargo platforms, trains with their timetable stops and cargo; first read 8 minutes (+/-10%, as resource nodes) after Running, then every 10 minutes), which is never requested from FRM and is not scheduled at all while `trainsSource` is `bridge`. A world with no trains and no stations sends empty arrays; only an abandoned read (world swapped, no railroad subsystem) leaves the part out. With `native`, the bridge never starts the `getTrains`/`getTrainStation` pair |

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

## Where each reader stands (S2, after Task 11)

Status, auto-pause and power are native. Of the readers with a bridge path, only players and resource nodes are native by
default. Factory, Rates and Trains are built and tested but have not passed `native-parity` or `tick-cost-native` yet: their
default stays `bridge` until each one's switch PR (after the owner dispatches the one S2 `native-parity` run and the one S2
`tick-cost-native` run on `Test2`), so they still need FRM installed. TrainRoutes has no bridge path: it is native-only and
runs only when `trainsSource` is `native`, so it is off until Trains switches.

## The step ceiling (owner, 2026-10-26)

No single native game-thread step may take over 2 ms (`MaxNativeStepMs = 2.0`). Every sliced read (node scan, factory, trains,
train routes) stops at a 1 ms budget (`NativeStepBudgetSeconds = 0.001`) and logs `<Name> (game thread only): ... longest
step X ms, total Y ms`. `tick-cost-native` fails when any logged longest step is over 2.0 ms (2.00 passes), when a reader that
the config made native logged no line, and, for the S2 readers, when no line falls inside the measured window. One-shot reads
that are not sliced (status, auto-pause, players, power's circuit-id probe) are S0/S1 code outside this rule. The total time is
recorded, not judged.

## Before reading the S2 results

Four things to know first. They are reported, not rules; none of them changes a pass condition.

- A trainRoutes failure can be only cargo drift: items change between the mod's read and FRM's sample.
- The moving-train rule passes a position anywhere on FRM's track in the window. Read the "a stationary train" NOT EXERCISED line before switching Trains.
- The 5% FRM read-failure rule is new and was not set by the owner.
- In one game tick the native steps can add up to about 3-4 ms in total (each step stays under 2 ms), and `tick-cost-native` measures the median tick.

## The Test2 note

The S2 runs (`native-parity`, `tick-cost-native`) load the owner's `Test2` save through the runner config's `s2World`, not S3's
(see `docs/runner.md`). `tick-cost-native` writes all four keys (`NATIVE_SOURCES_S2`). The train-routes layer's first scan is 480
s (+/-10%, the resource-node layer's first-read rule) after the agent is Running and then every 600 s, so its first post lands
3 to 13 minutes into `tick-cost-native`'s measurement, inside the measured window, and `native-parity`'s 30-minute window
holds posts at about 8, 18 and 28 minutes. The judge requires the `Train routes read` line inside the window. Test2 must hold the trains, stations and machines the checklist in
`docs/runner.md` lists.
