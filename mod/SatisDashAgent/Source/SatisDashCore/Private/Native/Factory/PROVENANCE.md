Reader: Factory
Code: mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Native/Factory/NativeFactory.h
Code: mod/SatisDashAgent/Source/SatisDashCore/Private/Native/Factory/NativeFactory.cpp
Code: mod/SatisDashAgent/Source/SatisDashGame/Private/GameReader.cpp
Headers: Source/FactoryGame/Public/Buildables/FGBuildableFactory.h:27, :92, :108, :151, :184, :225
Headers: Source/FactoryGame/Public/Buildables/FGBuildableManufacturer.h:85, :157, :161, :165
Headers: Source/FactoryGame/Public/Buildables/FGBuildable.h:824 (mDisplayName)
Headers: Source/FactoryGame/Public/FGRecipe.h:55, :59, :63
Headers: Source/FactoryGame/Public/FGInventoryComponent.h:326, :443, :451
Headers: Source/FactoryGame/Public/FGCircuit.h:71
Headers: Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h:2505, :2511
Headers: Source/FactoryGame/Public/FGBuildableSubsystem.h:272, :360, :363 (the list is re-fetched for every element and never kept between ticks)
Headers: Source/FactoryGame/Public/FGPowerInfoComponent.h:80
Headers: Source/FactoryGame/Public/Resources/FGItemDescriptor.h:18, :158, :191 (fluid = RF_LIQUID or RF_GAS; counts scaled /1000 in Core, MakeSlotReading)
Headers: Source/FactoryGame/Public/FGRecipe.h:63, :75
Headers: Source/FactoryGame/Public/FGInventoryComponent.h:45, :95, :493
Headers: slot capacity (MaxAmount) is GetSlotSize(index, ItemClass), FGInventoryComponent.h:493, in place of the survey's GetStackSize (Resources/FGItemDescriptor.h:251): its header comment is "the arbitrary slot size if one is set, otherwise the stack size", the slot's own capacity (a fluid buffer's arbitrary size included); the parity run judges it
Headers: the static GetIngredients(worldContext, class) (FGRecipe.h:59) is used, not the raw instance list (:127); the parity run judges it
Headers: the game reads are in GameReader.cpp (BeginFactoryRead, StepFactoryRead); only non-empty slots are read; no id, name or recipe is logged, the log line carries counts and times only
Survey: mod-native-readers-s2-headers-2026-10-26.md
Captures: frm-getFactory-2026-09-22-01-running-trimmed.json (satisfactory-dash captured-responses; FRM HTTP output only)
Captures: frm-getFactory-2026-10-25-id-stability.md (satisfactory-dash captured-responses; FRM HTTP output only)
Author: Task 5 implementer subagent (Sprint S2, 2026-10-26)
Date: 2026-10-26

FRM source was not opened
