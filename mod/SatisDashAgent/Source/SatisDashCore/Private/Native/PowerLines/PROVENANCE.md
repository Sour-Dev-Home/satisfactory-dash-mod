Reader: PowerLines
Code: mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Native/PowerLines/NativePowerLines.h
Code: mod/SatisDashAgent/Source/SatisDashCore/Private/Native/PowerLines/NativePowerLines.cpp
Headers: Task 10 (R10, pure mapping) calls no game symbol; the game-side read of Task 12 (SatisDashGame GameReader.cpp, the shared logistics pass) uses the rows below
Headers: Source/FactoryGame/Public/Buildables/FGBuildableWire.h:33 (AFGBuildableWire, UCLASS( Abstract ) at :32: the filter is Cast on the buildable list), :61 (GetConnection, null when the end is gone), :65 (GetConnectionLocation, cached end location in cm)
Headers: Source/FactoryGame/Public/FGCircuitConnectionComponent.h:18 (UFGCircuitConnectionComponent, the full type for the null check)
Headers: Source/FactoryGame/Public/FGBuildableSubsystem.h:360 (GetAllBuildablesRef), :363 (GetBuildableCount of AFGBuildableWire::StaticClass(): advisory, the sender warns when wires read plus dropped differ from it by more than max(5, 5%); the output never changes); the log carries counts only, never an id
Survey: mod-native-readers-s4a-headers-2026-10-27.md
Captures: frm-getCables-2026-09-27-trimmed.json (satisfactory-dash captured-responses; FRM HTTP output only; the shape of the Golden test, whose ids are synthetic)
Facts: the contract (PowerLineSchema and POWER_LINES_MAX_ITEMS at harness/contract/src/map.ts) and the dash powerLinesMapper.ts (our own code, at 27911fb) are the rules for the item shape, the whole-metre ends, the drops, the sort and the cap; no field name is taken from FRM source
Facts: the two ends are written in the order the game gives them (not sorted); two ends that round to the same metre are kept; a wire with an empty id is dropped and counted, as is one with an end outside +/-1,000,000 m
Facts: a wire with either end missing is not read and counts in Dropped; the id is the wire's GetName(); the body cap (2,000,000 bytes) is applied by the world read that carries the body (FWorldRead::Settle), so an over-cap body is skipped, not sent
Author: Task 10 and Task 12 implementer subagents (Sprint S4-A, 2026-10-28)
Date: 2026-10-28

FRM source was not opened
