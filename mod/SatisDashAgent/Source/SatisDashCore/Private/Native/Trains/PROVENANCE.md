Reader: Trains
Code: mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Native/Trains/NativeTrains.h
Code: mod/SatisDashAgent/Source/SatisDashCore/Private/Native/Trains/NativeTrains.cpp
Code: mod/SatisDashAgent/Source/SatisDashGame/Private/GameReader.cpp
Code: mod/SatisDashAgent/Source/SatisDashCore/Private/Driver.cpp
Headers: Task 9 adds the game-side read (GameReader.cpp BeginTrainsRead/StepTrainsRead) and the driver's mapLive pass: FGRailroadSubsystem.h:117 (Get), FGTrain.h:386, :511, :519, :527, :709 (mConsistData, in the public: section at :702; FTrainConsist :103), FGVehicle.h:347 (GetVehicleLocation), FGRailroadTimeTable.h:70, FGTrainStationIdentifier.h:84, :88, FGFreightWagon.h:47; the log line carries counts and times only, never an id or a name
Headers: Source/FactoryGame/Public/FGRailroadSubsystem.h:174, :207
Headers: Source/FactoryGame/Public/FGTrain.h:64-70, :460, :523, :614
Headers: Source/FactoryGame/Public/FGTrainStationIdentifier.h:84, :88
Headers: Source/FactoryGame/Public/FGVehicle.h:339
Headers: Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h:2505
Headers: this task (R3, pure mapping) calls no game symbol; the rows above are the ones the game-side read of Task 9 uses, opened and confirmed in the pinned checkout. ETrainStatus is TS_Parked, TS_ManualDriving, TS_SelfDriving, TS_Derailed with DisplayNames "Parked", "Manual Driving", "Self Driving", "Derailed" (FGTrain.h:66-69); the header's wording is not used for the status text
Survey: mod-native-readers-s2-headers-2026-10-26.md
Captures: frm-getTrains-2026-09-27-full.json (satisfactory-dash captured-responses; FRM HTTP output only)
Captures: frm-getTrains-2026-10-21-trimmed.json
Captures: frm-getTrainStation-2026-09-27-full.json
Captures: frm-getTrains.json, frm-getTrainStation.json and frm-mapLive.expected.json (the mod's test fixtures; the same FRM HTTP output and the bridge's own mapLive output)
Facts: the train's Status is the string "Self-Driving" in every capture (hyphen, where the header's DisplayName has a space); that is the one word the native text must equal. Parked, Manual and Derailed are not in any capture: they are our words (owner decision 2026-10-26), parity reports a mismatch on them and does not fail it
Facts: a train record carries a top-level location {x,y,z,...} in cm (frm-getTrains-2026-09-27-full.json: x -102587.95567750931, y -147300); the per-vehicle entries of Vehicles[] carry no location in any capture, so which vehicle gives x,y (ETrainPositionSource, FirstVehicle for now) cannot be settled offline and is left to the parity run
Facts: a station record carries a location {x,y,z,...} in cm (frm-getTrainStation-2026-09-27-full.json: x -104300, y -147300); positions go to whole metres with Math.round semantics and out-of-range ones are dropped, by the bridge's own helpers; trains and stations are written by the bridge's own AppendTrainRecord and AppendStationRecord
Author: Task 8 implementer subagent (Sprint S2, 2026-10-26)
Date: 2026-10-26

FRM source was not opened
