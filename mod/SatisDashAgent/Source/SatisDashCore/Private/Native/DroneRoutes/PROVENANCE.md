Reader: DroneRoutes
Code: mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Native/DroneRoutes/NativeDroneRoutes.h
Code: mod/SatisDashAgent/Source/SatisDashCore/Private/Native/DroneRoutes/NativeDroneRoutes.cpp
Headers: Task 7 (R9, pure mapping) calls no game symbol
Headers: Task 9 (game-side read, SatisDashGame GameReader.cpp): Source/FactoryGame/Public/FGDroneSubsystem.h:68 (AFGDroneSubsystem::Get), :81 (GetAllStations); Source/FactoryGame/Public/FGDroneStationInfo.h:190 (GetBuildingTag_Implementation), :201 (GetPairedStation), :208 (GetStationLocation, not used), :252 (GetStation); Source/FactoryGame/Public/FGBuildingTagInterface.h:46 (GetBuildingTag); Source/FactoryGame/Public/Buildables/FGBuildableDroneStation.h:145 (GetInputInventory), :149 (GetOutputInventory), :153 (GetFuelInventory); each has its api-sources.md row
Survey: mod-native-readers-s4a-headers-2026-10-27.md
Captures: frm-getDroneStation-2026-10-25-paired-*-trimmed.json (satisfactory-dash captured-responses; FRM HTTP output only; the shape of the Golden test, whose names are synthetic)
Captures: frm-getDroneStation-2026-10-25-unpaired-trimmed.json (the same, for the unpaired port)
Facts: the contract (DronePortSchema at harness/contract/src/map.ts:219, with the optional nullable pairedId of dash #739, DRONE_ROUTES_MAX_ITEMS, TRAIN_ROUTES_MAX_ITEM_CLASSES, the 200-character boundedString) and the dash droneRoutesMapper.ts (our own code, at 27911fb) are the rules for the key order, the distinct sorted item classes, the drops and the sort; no field name is taken from FRM source
Facts: pairedId is the partner port's id and is written always: null when the port is unpaired, the partner's buildable is gone, or the partner is not in the kept layer (dropped, capped or absent), so it never names a port outside the layer; paired stays the partner's name as the game reports it
Author: Task 7 and Task 9 implementer subagents (Sprint S4-A, 2026-10-28)
Date: 2026-10-28

FRM source was not opened
