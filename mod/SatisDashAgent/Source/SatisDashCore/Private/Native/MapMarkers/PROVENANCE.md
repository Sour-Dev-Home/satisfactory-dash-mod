Reader: MapMarkers
Code: mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Native/MapMarkers/NativeMapMarkers.h
Code: mod/SatisDashAgent/Source/SatisDashCore/Private/Native/MapMarkers/NativeMapMarkers.cpp
Code: mod/SatisDashAgent/Source/SatisDashGame/Private/GameReader.cpp
Code: mod/SatisDashAgent/Source/SatisDashCore/Private/WorldSender.cpp
Headers: Source/FactoryGame/Public/FGMapManager.h:70 (AFGMapManager), :95 (Get), :111 (GetNumMapMarkers), :118 (GetMapMarkers: public, returns a copy), :299 (mMaxNumMapMarkers = 250)
Headers: Source/FactoryGame/Public/FGMapMarker.h:13 (FMapMarker), :37 (MarkerGUID), :40 (Location, centimetres), :43 (Name), :49 (MapMarkerType), :52 (IconID), :55 (Color, FLinearColor), :71 (HasValidID)
Headers: Engine/Source/Runtime/Core/Public/Math/Color.h:47 (FLinearColor), :123 (FromSRGBColor), :381 (ToFColor), :494 (FColor), :706 (ToHex)
Survey: mod-native-readers-s4a-headers-2026-10-27.md
Captures: frm-getMapMarkers-2026-09-27-full.json (satisfactory-dash captured-responses; FRM HTTP output only; the fixture is synthetic in its shape)
Facts: the contract (MapMarkerSchema, MAP_MARKERS_MAX_ITEMS, MAP_MARKER_NAME_MAX_CHARS in harness/contract/src/map.ts) and the dash mapMarkersMapper.ts (our own code) are the rules for the name, the kind, the sort and the layout; no field name is taken from FRM source
Facts: the colour conversion is the constant NativeMarkerColorSRGB (survey question Q1); the parity run judges it
Author: Task 4 implementer subagent (Sprint S4-A, 2026-10-27)
Date: 2026-10-27

FRM source was not opened
