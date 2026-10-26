Reader: TrainRoutes
Code: mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Native/TrainRoutes/NativeTrainRoutes.h
Code: mod/SatisDashAgent/Source/SatisDashCore/Private/Native/TrainRoutes/NativeTrainRoutes.cpp
Code: mod/SatisDashAgent/Source/SatisDashGame/Private/GameReader.cpp
Code: mod/SatisDashAgent/Source/SatisDashCore/Private/WorldSender.cpp
Headers: Task 9 adds the game-side read (GameReader.cpp BeginTrainsRead/StepTrainsRead, the platform walk) and the world sender's native-only layer: Buildables/FGBuildableRailroadStation.h:56 (GetStationOutputConnection), FGTrainPlatformConnection.h:42 (GetConnectedTo), :53 (GetPlatformOwner), Buildables/FGBuildableTrainPlatform.h:66 (GetConnectionInOppositeDirection), Buildables/FGBuildableTrainPlatformCargo.h:37, :57, FGInventoryComponent.h:451; the walk is bounded at 256 hops with a visited set; the log line carries counts and times only, never an id or a name
Headers: Source/FactoryGame/Public/FGRailroadSubsystem.h:174, :207
Headers: Source/FactoryGame/Public/FGTrain.h:460, :511
Headers: Source/FactoryGame/Public/FGRailroadTimeTable.h:15-21, :70
Headers: Source/FactoryGame/Public/FGTrainStationIdentifier.h:84, :88
Headers: Source/FactoryGame/Public/Buildables/FGBuildableRailroadStation.h:49
Headers: Source/FactoryGame/Public/Buildables/FGBuildableTrainPlatformCargo.h:37, :57
Headers: Source/FactoryGame/Public/FGFreightWagon.h:47
Headers: this task (R3, pure mapping) calls no game symbol; the rows above are the ones the game-side read of Task 9 uses, opened and confirmed in the pinned checkout
Survey: mod-native-readers-s2-headers-2026-10-26.md
Captures: frm-getTrainStation-2026-10-21-trimmed.json (satisfactory-dash captured-responses; FRM HTTP output only; the oracle of the Golden test)
Captures: frm-getTrains-2026-10-21-trimmed.json (the same, for the train record)
Captures: frm-getTrainStation-2026-09-27-full.json and frm-getTrains-2026-09-27-full.json (the same two stations and train, untrimmed)
Captures: frm-train-gate-2026-10-21.md (the gate note: platform ids, trailing spaces in names)
Facts: a station's platforms are its CargoInventory[] entries; the platform id is CargoInventory[].ID (Build_TrainDockingStation_C_...); LoadingMode is "Loading" or "Unloading" (two stations, four platforms, in both captures), which are "load" and "unload"; a platform's items are the ClassName of its Inventory[] entries, the contents it holds at the read (whether FRM lists held or handled classes is not shown, so the parity run settles it)
Facts: an empty platform is a CargoInventory[] entry too: the S2 native-parity run 40378567334 (FRM HTTP output only) listed Build_TrainPlatformEmpty_02_C_... with LoadingMode other than "Loading" and no Inventory, so the walk keeps every non-cargo platform that is not a station as unload with no items; a second station in a chain stays out (no capture shows one)
Facts: a train's stops are TimeTable[].StationName in order; its cargo is the ClassName of the Inventory[] entries of Vehicles[] (the locomotive's is empty); a station name keeps the trailing space the player typed ("Silica/Crstal Out ") and so does the native text
Facts: the bridge never sent this layer, so the contract's TrainRoutesLayerDataSchema and TRAIN_ROUTES_MAX_* (harness/contract/src/map.ts, vendored at dash 48ef758) are the layout and the caps; no field name is taken from FRM
Author: Task 8 implementer subagent (Sprint S2, 2026-10-26)
Date: 2026-10-26

FRM source was not opened
