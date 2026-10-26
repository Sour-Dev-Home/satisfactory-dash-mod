Reader: Rates
Code: mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Native/Rates/NativeRates.h
Code: mod/SatisDashAgent/Source/SatisDashCore/Private/Native/Rates/NativeRates.cpp
Headers: Source/FactoryGame/Public/FGRecipe.h:75, :129
Headers: Source/FactoryGame/Public/Buildables/FGBuildableFactory.h:197, :213, :217, :225
Headers: Source/FactoryGame/Public/Resources/FGItemDescriptor.h:18, :158, :258
Headers: the rows are the rate rows of "Lookups made" in docs/plans/2026-10-26-native-readers-s2.md; this task (R2b) is pure Core code and calls no game symbol, the game reads come with GameReader.cpp in Task 7
Survey: mod-native-readers-s2-headers-2026-10-26.md
Captures: frm-getFactory-2026-09-22-01-running-trimmed.json (satisfactory-dash captured-responses; FRM HTTP output only)
Captures: frm-getFactory-running.json (the mod's test fixture; the same FRM HTTP output)
Inference: the max is the recipe amount over the base duration times the clock; the capture's Stator at 100% (MaxProd 5) and 160% (MaxProd 8) and the refinery (60 in, 40 out, m3) fit it. That ProdPercent is an averaged measure over a window (the capture shows an idle machine with a non-zero percent) is a hypothesis tested by the parity run. The owner's fallback (2026-10-26) is ERatesMode::Instantaneous, one constant.
Author: Task 6 implementer subagent (Sprint S2, 2026-10-26)
Date: 2026-10-26

FRM source was not opened
