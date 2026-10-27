Reader: PowerLines
Code: mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Native/PowerLines/NativePowerLines.h
Code: mod/SatisDashAgent/Source/SatisDashCore/Private/Native/PowerLines/NativePowerLines.cpp
Headers: Task 10 (R10, pure mapping) calls no game symbol; the game-side read is Task 12, which adds its header rows here
Survey: mod-native-readers-s4a-headers-2026-10-27.md
Captures: frm-getCables-2026-09-27-trimmed.json (satisfactory-dash captured-responses; FRM HTTP output only; the shape of the Golden test, whose ids are synthetic)
Facts: the contract (PowerLineSchema and POWER_LINES_MAX_ITEMS at harness/contract/src/map.ts) and the dash powerLinesMapper.ts (our own code, at 27911fb) are the rules for the item shape, the whole-metre ends, the drops, the sort and the cap; no field name is taken from FRM source
Facts: the two ends are written in the order the game gives them (not sorted); two ends that round to the same metre are kept; a wire with an empty id is dropped and counted, as is one with an end outside +/-1,000,000 m
Author: Task 10 implementer subagent (Sprint S4-A, 2026-10-28)
Date: 2026-10-28

FRM source was not opened
