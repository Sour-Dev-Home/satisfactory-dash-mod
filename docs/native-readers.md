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
| Map markers | S4-A | `bridge` (default; `mapMarkersSource`) | the switch exists and the mod logs `mapMarkers=` after `conveyors=` on its `Readers:` line; `bridge` means the layer is not sent. The small-read path (port, sender scheduling, driver wiring) is in: a native layer waits 480 s (x jitter) for its first read, then reads every 10 minutes, and logs one `Map markers read (game thread only):` line per read (counts and times only); an abandoned read is retried after 30 s. The game-side read is in: one small sliced read of the map manager's markers (copied once, at most 250; id, name, kind, icon, colour and position only; first read 480 s). Not yet judged by `native-parity`, so keep `mapMarkersSource` on `bridge` until it is |
| Drone routes | S4-A | `bridge` (default; `droneRoutesSource`) | the switch exists and the mod logs `droneRoutes=` on its `Readers:` line; `bridge` means the layer is not sent. The pure body builder and the sender's small-read path are in (the same schedule as map markers: first read 480 s x jitter, then every 10 minutes, one `Drone routes read (game thread only):` log line per read with counts and times only, an abandoned read retried after 30 s). The game-side read is in (one station per step iteration from the drone subsystem's list, re-fetched each time; the id is the buildable's name, the name the building tag, the position the actor's; input and output follow the header's fields); not yet judged by a parity run, so keep `droneRoutesSource` on `bridge` |
| Power lines | S4-A | `bridge` (default; `powerLinesSource`) | the switch exists and the mod logs `powerLines=` on its `Readers:` line; `bridge` means the layer is not sent. `native` rides the shared logistics pass (the wires through `AFGBuildableWire`; a wire with a missing end is dropped and counted; a Warning when no wire is read or the count differs from the subsystem's by more than max(5, 5%)); not yet judged by a parity run, so keep `powerLinesSource` on `bridge` |

## The hidden source override (a stopgap, not a setting)

Until the bridge is removed (ADR-0058, R7) a host can force a reader's source in the mod's config file
(`SatisDashAgent.cfg`, the same JSON as `backendUrl`):

    "playersSource": "native"
    "resourceNodesSource": "bridge"
    "factorySource": "bridge"
    "trainsSource": "bridge"
    "railsSource": "bridge"
    "conveyorsSource": "bridge"
    "mapMarkersSource": "native"
    "droneRoutesSource": "native"
    "powerLinesSource": "native"

The value is exactly `bridge` or `native` (anything else makes the config Broken, as a bad `frmPort` does). With no key a
reader uses its default (`DefaultPlayersSource`, `DefaultResourceNodesSource`, `DefaultFactorySource`, `DefaultTrainsSource`, `DefaultRailsSource`, `DefaultConveyorsSource` in `Config.h`; all six are `native`: the factory and trains ones since the S2 switch, the rails and conveyors ones since the S3 switch). The S4-A keys (`mapMarkersSource`, `droneRoutesSource`, `powerLinesSource`; `DefaultMapMarkersSource`, `DefaultDroneRoutesSource`, `DefaultPowerLinesSource`) default to `bridge`, which for these three layers means the layer is not sent. `bridge` needs FRM installed.
The keys are deleted with the bridge in the removal release; do not build on them. There is no key for power: it has no
bridge path.

## Where each reader stands (the S3 switch)

Every reader is native by default. Status, auto-pause and power have no bridge path. Players, resource nodes, factory with
rates, and trains with stations switched in S2; Rails (R5) and Conveyors (R6) switched in S3, after `native-parity` run
40447299909 (every reader passed), `tick-cost-native` run 40442596123 and `s3-census` run 40437494703, all on `Test3`. With
the defaults a host needs FRM for nothing. Conveyors has no bridge path: `conveyorsSource` `bridge` means the layer is not
sent. The next step is R7 (the bridge removal), its own PR on the owner's word only; it is not part of S3.

What changes for a host on the new defaults:

- Rails and conveyors share one logistics pass on the game thread (a sliced read of the buildable list, one budgeted step per
  driver tick). A pass carries whichever of the two layers are native, and either one falling due takes the other with it
  (the pulled-forward rule). It logs one `Logistics read (game thread only):` line per pass.
- S4-A: `powerLinesSource` is a third flag on the same pass. With it `native`, the wires are read in the same pass as rails and
  conveyors (a layer falling due takes the other native layers with it), and the pass line gains `powerLines=` and a wires
  count (`wires N, expected wires M`). With `bridge`, the default, the layer is not sent and the line reads `powerLines=0`.
- Ids of rebuilt belts change.
- The first native post of each layer changes that layer's content hash once.

The S2 switch followed `native-parity` run
40403583987 and `tick-cost-native` run 40396146628 on `Test2` (owner, 2026-10-26). TrainRoutes has no bridge path: it is
native-only and runs only when `trainsSource` is `native`, so a host that sets `trainsSource` to `bridge` turns it off.

## Where each reader stands (S4-A, after Task 17)

Map markers (R8), drone routes (R9) and power lines (R10) are built, and all three are still `bridge`, so by default their
layers are not sent. FRM is not involved in them either way; `bridge` here only means "off". None has been judged by a parity
run yet. The switch PR comes after `native-parity` passes on `Test3`, on the owner's word. A native layer reads on a schedule
of its own (markers and drone routes: first read 480 s x jitter, then every 10 minutes); the logistics pass pulls `powerLines`
forward with rails and conveyors. The first native post of each layer changes its content hash once, ids of rebuilt wires
and ports change, and `droneRoutes` posts carry `pairedId`.

The three snapshot fields below are different: they are sent on every snapshot, whatever the layer sources.

## The snapshot's game-connection fields (S4-A, R7)

Every snapshot carries three top-level fields beside `reachable` (never inside `status`): `gameReachable` (this pass's status
read succeeded), `lastGameReadAt` (UTC time of the last successful status read; left out until the first one, never null) and
`modVersion` (`AgentVersion()`, cut to 64 characters). `reachable` stays `true`. The contract's snapshot schema is strict, so
a backend without dash #740 answers 4xx to a body that carries them: deploy the backend first (plan Open questions 6 to 8).

## The step ceiling (owner, 2026-10-26)

No single native game-thread step may take over 2 ms (`MaxNativeStepMs = 2.0`). Every sliced read (node scan, factory, trains,
train routes, the shared `Logistics read` pass, `Map markers read`, and `Drone routes read`) stops at a 1 ms budget (`NativeStepBudgetSeconds = 0.001`) and logs `<Name> (game thread only): ... longest
step X ms, total Y ms`. `tick-cost-native` fails when any logged longest step is over 2.0 ms (2.00 passes), when a reader that
the config made native logged no line, and, for the S2 readers, when no line falls inside the measured window. One-shot reads
that are not sliced (status, auto-pause, players, power's circuit-id probe) are S0/S1 code outside this rule. The total time is
recorded, not judged.

## Before reading the S2 results

Four things to know first. They are reported, not rules; none of them changes a pass condition.

- Cargo drift in trainRoutes (items change between the mod's read and FRM's sample) no longer fails: since the owner's ruling of
  2026-10-27 (run 40439238142, Test3) a difference only in a platform's or a train's cargo item set is reported (count and
  first three), while stations, platform ids, modes, train ids, names and timetable stops stay exact.
- The moving-train rule passes a position anywhere on FRM's track in the window. Read the "a stationary train" NOT EXERCISED line before switching Trains.
- The 5% FRM read-failure rule is new and was not set by the owner.
- In one game tick the native steps can add up to about 3-4 ms in total (each step stays under 2 ms), and `tick-cost-native` measures the median tick.

## Before reading the S3 results

Reported, not rules; none of them changes a pass condition.

- FRM's rail geometry is rounded to 0.01 m before the 1 m checks. The owner's rule says "unrounded", so the rounding can add at
  most 0.0071 m of slack to a distance (half a centimetre on each axis).
- Two numbers are new in the S3 judges: `S3_GAP_MS` (120 s, reused from S2's routes rule: the FRM sample before and after a post
  must be within it) and the representable rule (an FRM track counts only with at least two distinct whole-metre points inside
  +/-1,000,000 m; unrepresentable tracks are listed and are in neither id set).
- Duplicate ids fail only on the mod's side, as in S2; FRM's duplicates are reported.

## Before reading the S4 results

Reported, not rules; none of them changes a pass condition. The judges are in `runs/native-parity/pass-s4.ts`.

- Id-set exemptions: when both sides are exactly at the layer cap, the ids sorting after the earlier of the two last ids are
  reported, not failed; clamped mod ids are reported too, but no more of them than FRM's derived ids. Any other id difference
  fails.
- `pairedId` rules: an unpaired FRM port needs the mod's `pairedId` null or absent; an ambiguous FRM name allows null, absent
  or one of FRM's candidate ids; a unique FRM partner must equal the mod's id.
- Marker positions are compared after rounding to whole metres, with a 0.01 m boundary tolerance (`S4_MARKER_BOUNDARY_TOL_M`):
  a value that close to a .5 m boundary may round one metre apart.
- An FRM-only wire with a null end shows as FRM-only: the mod drops such a wire and the judge is not told, so check the `wires`
  count in the Logistics read line before blaming the reader.
- Drone input, output and fuel item-class sets are reported when they differ (a count and the first three), never failed,
  because the drones fly during the run; an input/output swap still fails.
- Each layer has NOT EXERCISED lines for what the save does not hold (for example a layer with no records); they are reported,
  never failed.
- An input/output "swapped" hint on drone ports is a reading aid for the fix, printed with the failure.
- Marker colour is compared on a guess at the sRGB conversion; read a colour difference with that in mind.
- The expected-wires warning (`wires N, expected wires M`) may fire if `GetBuildableCount` returns 0 for the abstract wire
  class. It is a Warning in the mod's log, not a verdict.
- The S4 FRM samplers poll every 90 s. The dropped-wire count is not judged (no wires-only dropped parser).

## The Test3 note

The S3 runs (`native-parity`, `tick-cost-native`) load the owner's `Test3` save through the runner config's `sprint3World`
(see `docs/runner.md`). `tick-cost-native` writes all six keys (`NATIVE_SOURCES_S3`) and requires the `Logistics read (game
thread only):` line, with its longest step at 2.0 ms or less, inside the measured window. The native first read is 480 s
(+/-10%) after Running, so it lands 3 to 13 minutes into the measurement. A config with only one logistics layer native still
needs the line, but `tick-cost-native` fails it as an S3 run.

For S4-A both runs write all nine keys (`NATIVE_SOURCES_S4`). The marker, drone-route and wire counts on `Test3` are pending
the `s4-capture` run (PR #67); nothing here is guessed.

## Follow-ups after S4-A

- S4-B: extractors, generators, train transfer rates and storage, once the dash contracts for them land.
- A wires-only dropped parser, so `judgeWires` can apply the dropped-wire check.
- `IsValid` on the subsystem checks across all readers.
- The switch PR for markers, drone routes and power lines, after `native-parity` passes on `Test3`.
- R7, the bridge removal, on the owner's word only.

## The Test2 note

The S2 runs (`native-parity`, `tick-cost-native`) load the owner's `Test2` save through the runner config's `s2World`, not S3's
(see `docs/runner.md`). `tick-cost-native` writes all four keys (`NATIVE_SOURCES_S2`). The train-routes layer's first scan is 480
s (+/-10%, the resource-node layer's first-read rule) after the agent is Running and then every 600 s, so its first post lands
3 to 13 minutes into `tick-cost-native`'s measurement, inside the measured window, and `native-parity`'s 30-minute window
holds posts at about 8, 18 and 28 minutes. The judge requires the `Train routes read` line inside the window. Test2 must hold the trains, stations and machines the checklist in
`docs/runner.md` lists.
