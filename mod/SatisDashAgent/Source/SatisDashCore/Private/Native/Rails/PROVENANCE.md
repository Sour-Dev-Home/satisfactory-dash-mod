Reader: Rails
Code: mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Native/Rails/NativeRails.h
Code: mod/SatisDashAgent/Source/SatisDashCore/Private/Native/Rails/NativeRails.cpp
Code: mod/SatisDashAgent/Source/SatisDashGame/Private/GameReader.cpp
Code: mod/SatisDashAgent/Source/SatisDashCore/Private/WorldSender.cpp
Headers: Task 6 adds the game-side read (GameReader.cpp, the track walk and sampling); this task (R5, pure sampling plan and layer builder) calls no game symbol. The rows below are the ones that read uses, opened and confirmed in the pinned checkout
Headers: Source/FactoryGame/Public/Buildables/FGBuildableRailroadTrack.h:99, :100 (UCLASS( Abstract ), the class: filter with Cast)
Headers: Source/FactoryGame/Public/Buildables/FGBuildableRailroadTrack.h:150 (GetLength), :156 (GetIsOwnedByPlatform), :171 (GetWorldLocationAndDirectionAtDistance), :247 (GetSplineComponent)
Headers: Engine/Classes/Components/SplineComponent.h:284 (USplineComponent), :731 (GetSplineLength), :763 (GetLocationAtDistanceAlongSpline), :35-41 (ESplineCoordinateSpace)
Survey: mod-native-readers-s3-headers-2026-10-26.md
Captures: frm-getTrainRails-2026-09-27-trimmed.json (satisfactory-dash captured-responses; FRM HTTP output only; the fixture frm-getTrainRails.json is its reduction, the oracle of the Golden test)
Facts: the bridge's rails layer (FrmRails.cpp, written against FRM's HTTP output) and the contract's RailsLayerDataSchema (harness/contract/src/map.ts) are the layout and the caps; no field name is taken from FRM
Facts: a one-metre sample step (survey ruling S2) and the sampler constant NativeRailSampler (ruling M2) are ours; the parity run judges the sampler
Author: Task 4 implementer subagent (Sprint S3, 2026-10-27)
Date: 2026-10-27

FRM source was not opened
