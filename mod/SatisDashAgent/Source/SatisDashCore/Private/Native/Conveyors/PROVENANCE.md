Reader: Conveyors
Code: mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Native/Conveyors/NativeConveyors.h
Code: mod/SatisDashAgent/Source/SatisDashCore/Private/Native/Conveyors/NativeConveyors.cpp
Code: mod/SatisDashAgent/Source/SatisDashGame/Private/GameReader.cpp
Code: mod/SatisDashAgent/Source/SatisDashCore/Private/WorldSender.cpp
Headers: Source/FactoryGame/Public/FGBuildableSubsystem.h:360 (GetAllBuildablesRef), :363 (GetBuildableCount): belts, lifts and pipelines are only reachable through the buildable list
Headers: Source/FactoryGame/Public/Buildables/FGBuildableConveyorBase.h:97 (AFGBuildableConveyorBase), :160 (GetSpeed, no unit stated), :163-164 (GetConnection0, GetConnection1), :327 (ITEM_SPACING 120)
Headers: Source/FactoryGame/Public/Buildables/FGBuildableConveyorBelt.h:22 (AFGBuildableConveyorBelt), Buildables/FGBuildableConveyorLift.h:72 (AFGBuildableConveyorLift)
Headers: Source/FactoryGame/Public/Buildables/FGBuildablePipeline.h:32 (AFGBuildablePipeline), :136 (GetFlowLimit, m^3/s)
Headers: Source/FactoryGame/Public/Buildables/FGBuildablePipeBase.h:68-69 (GetConnection0, GetConnection1)
Headers: Source/FactoryGame/Public/FGFactoryConnectionComponent.h:141 (GetConnectorLocation), FGPipeConnectionComponent.h:69 (GetConnection)
Survey: mod-native-readers-s3-headers-2026-10-26.md
Captures: frm-getBelts-2026-10-21-connections-trimmed.json (satisfactory-dash captured-responses; FRM HTTP output only; the fixture frm-getBelts.json is its array, an oracle of the Golden test)
Captures: frm-getLifts-2026-10-21-trimmed.json (same; the fixture frm-getLifts.json)
Captures: frm-getPipes-2026-10-21-connections-trimmed.json (same; the fixture frm-getPipes.json)
Captures: frm-getDroneStation-2026-10-25-unpaired-trimmed.json (the Build_DroneStation_C_<number> id form of a station end)
Facts: the end-kind table is the dashboard adapter's classifyConveyorEnd (packages/game-adapter/src/map/conveyorsMapper.ts, our own code written against FRM's HTTP output, not FRM's code), ported row for row and in order; the edge layout and key order are the adapter's mapConveyor and the contract's ConveyorEdgeSchema (harness/contract/src/map.ts)
Facts: the tier capacities 60, 120, 270 and 480 a minute and the pipe speeds 5 and 10 m3/s are read from our captures; belt and lift GetSpeed() units are unconfirmed, so the capacity mode (ruling S5, SpeedHalf by default, ClassTable as the fallback) is one constant that the census run settles
Author: Task 7 implementer subagent (Sprint S3, 2026-10-27)
Date: 2026-10-27

FRM source was not opened
