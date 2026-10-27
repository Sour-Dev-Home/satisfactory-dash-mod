# Native readers: status and the source switch

ADR-0058 (satisfactory-dash) replaces FicsitRemoteMonitoring (FRM) reader by reader. Rules for writing a reader are in
`docs/clean-room.md`.

| Reader | Sprint | Source today | Notes |
| --- | --- | --- | --- |
| Status, auto-pause | core v1 | native | proven (S6, S7) |
| Power (R0) | S1 | native | read natively since core v1; S1 judges every field against FRM (`native-parity`) |
| Players (R1) | S1 | `native` (default) | passed `native-parity` (runs 40244496806, 40253178598: 351 and 357 rounds equal to FRM; NOT EXERCISED: any online player) and `tick-cost-native` (run 40262414493); offline players come from the save's player cache (`GetAllPlayerInfoData`), not `GetOfflineCachedPlayerInfosInSession`, which crashed the dedicated server |
| Resource nodes (R4) | S1 | `native` (default) | a sliced scan of the world's actors on the game thread (2,000 per tick, once per 30 minutes), the sort and the JSON built on the worker; a scan that finds nothing posts nothing and retries after 30 s. Passed `native-parity` (runs 40244496806, 40253178598: 608 items, 31 exploited, equal to FRM's) and `tick-cost-native` (run 40262414493). Scan cost measured at about 4.7 ms a scan and 0.3 ms per step on the game thread; the owner set the ceiling at 2 ms per step (2026-10-26) and `tick-cost-native` judges it. The reader now also carries `className` (`GetResourceClass()->GetName()`); R4's S1 parity pass predates className, so it is judged again in S2's `native-parity` run |
| Rates (R2b) | S2 | `native` (default; follows `factorySource`) | `maxPerMinute`, `currentPerMinute` and `percent` per machine, built on the worker with the factory part; native in the same read as Factory, `DefaultRatesMode` Averaged; products include the Somersloop production boost. Passed `native-parity` (run 40403583987: 0 of 12,840 item-class checks missed, 100% of building-rounds within 5%) |
| TrainRoutes | S2 | native only (no key; scheduled only while `trainsSource` is `native`) | a new world layer the bridge never sent; first scan 480 s (+/-10%, as resource nodes) after Running, so the first post is inside tick-cost-native's window, then every 600 s. Passed `native-parity` (run 40403583987: 2 posted layers equal to FRM's, 16 records) |
| Factory (R2) | S2 | `native` (default) | passed `native-parity` (run 40403583987: flags matched in 99.44% of 33,660 machine-rounds) and `tick-cost-native` (run 40396146628: longest step 1 ms): a sliced read of the buildable list on the game thread (one budgeted step per driver tick, 1 ms), one reading per manufacturer, the sort, rates and JSON built on the worker. With `native`, the bridge never sends `getFactory`; a pass that cannot read (no world, a world swapped mid-read) is abandoned and leaves the part out, a pass that finishes with no machines sends `{"buildings":[]}`, and neither falls back to FRM. Its `native-parity` and `tick-cost-native` judgement is the S2 run (Task 10) |
| Trains and stations (R3) | S2 | `native` (default) | passed `native-parity` (run 40403583987: 60 rounds; NOT EXERCISED: a parked train, a stationary train) and `tick-cost-native` (run 40396146628: longest step 0.1 ms): a sliced read of the railroad subsystem's trains and stations on the game thread (one budgeted step per driver tick, 1 ms), joined into the `mapLive` part on the worker at the factory cadence. The same reader also feeds a new, native-only world layer, `trainRoutes` (stations with their cargo and empty platforms, trains with their timetable stops and cargo; first read 8 minutes (+/-10%, as resource nodes) after Running, then every 10 minutes), which is never requested from FRM and is not scheduled at all while `trainsSource` is `bridge`. A world with no trains and no stations sends empty arrays; only an abandoned read (world swapped, no railroad subsystem) leaves the part out. With `native`, the bridge never starts the `getTrains`/`getTrainStation` pair |
| Rails (R5) | S3 | `bridge` (default; `railsSource`) | the switch exists and the mod logs `rails=` on its `Readers:` line; the shared logistics pass (port, sender, driver wiring) is in and a native rails layer waits 480 s (x jitter) for its first read and logs one `Logistics read (game thread only):` line per pass; the game side is a sliced pass over the buildable list (tracks filtered from it, each sampled a metre apart along its spline); native rails follow `railsSource`, first scan 480 s; not yet judged by a parity run, so keep `railsSource` on `bridge` |
| Conveyors (R6) | S3 | `bridge` (default; `conveyorsSource`) | a new world layer the bridge never sent, so `bridge` means the layer is not sent (no schedule, no FRM request, no log) and `native` sends it; the switch exists and the mod logs `conveyors=` after `rails=` on its `Readers:` line. A native conveyors layer rides the same logistics pass as rails (a pass carries whichever of the two are native; either one falling due takes the other with it), first read 480 s (x jitter) after Running, then every 10 minutes. The pass logs `layers rails=%d conveyors=%d` on its one `Logistics read` line, and, when conveyors rode in it, a `Conveyor kinds:` and a `Conveyor drone ports:` line, plus (first pass after Running only) one `Conveyor tier census:` line per belt, lift and pipe tier (game class names and tier display names, no ids). The game-side read (belts and lifts through the conveyor base, pipes through the pipeline class so hypertubes cannot enter; one weak pointer per element, end ids from the connected building's name, a class-tree cross-check of each end kind that never changes the output) is in; not yet judged by a parity run, so keep `conveyorsSource` on `bridge` |

## The hidden source override (a stopgap, not a setting)

Until the bridge is removed (ADR-0058, R7) a host can force a reader's source in the mod's config file
(`SatisDashAgent.cfg`, the same JSON as `backendUrl`):

    "playersSource": "native"
    "resourceNodesSource": "bridge"
    "factorySource": "bridge"
    "trainsSource": "bridge"
    "railsSource": "bridge"
    "conveyorsSource": "bridge"

The value is exactly `bridge` or `native` (anything else makes the config Broken, as a bad `frmPort` does). With no key a
reader uses its default (`DefaultPlayersSource`, `DefaultResourceNodesSource`, `DefaultFactorySource`, `DefaultTrainsSource`, `DefaultRailsSource`, `DefaultConveyorsSource` in `Config.h`; the first four are `native`, the factory and trains ones since the S2 switch; `DefaultRailsSource` and `DefaultConveyorsSource` are `bridge` until their switch PRs). `bridge` needs FRM installed.
The keys are deleted with the bridge in the removal release; do not build on them. There is no key for power: it has no
bridge path.

## Where each reader stands (S2, after Task 11)

Status, auto-pause and power are native. Every reader with a bridge path (players, resource nodes, factory with rates, trains
with stations) is native by default. FRM is still needed for the rails world layer (`getTrainRails`), which has no native
reader yet. The S2 switch followed `native-parity` run
40403583987 and `tick-cost-native` run 40396146628 on `Test2` (owner, 2026-10-26). TrainRoutes has no bridge path: it is
native-only and runs only when `trainsSource` is `native`, so a host that sets `trainsSource` to `bridge` turns it off.

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
