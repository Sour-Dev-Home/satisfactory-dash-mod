Reader: Generators
Code: mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Native/Generators/NativeGenerators.h
Code: mod/SatisDashAgent/Source/SatisDashCore/Private/Native/Generators/NativeGenerators.cpp
Code: mod/SatisDashAgent/Source/SatisDashCore/Private/Native/Factory/NativeFactoryShared.h
Code: mod/SatisDashAgent/Source/SatisDashGame/Private/GameReader.cpp
Headers: Source/FactoryGame/Public/Buildables/FGBuildableGenerator.h:60 (AFGBuildableGenerator), :87 (GetPowerProductionCapacity, not used), :95 (CalcPowerProductionCapacityForPotential), :126 (mPowerProduction, "in MW")
Headers: Source/FactoryGame/Public/Buildables/FGBuildableGeneratorFuel.h:14 (AFGBuildableGeneratorFuel), :54 (GetFuelInventory), :74 (GetRequiresSupplementalResource), :103 (GetCurrentFuelClass), :111 (GetSupplementalResourceClass), :116 (GetFuelInventoryIndex), :117 (GetSupplementalResourceInventoryIndex)
Headers: Source/FactoryGame/Public/FGPowerInfoComponent.h:51 (UFGPowerInfoComponent), :137 (GetBaseProduction), :160 (GetRegulatedDynamicProduction)
Headers: Source/FactoryGame/Public/Buildables/FGBuildableFactory.h:151 (IsProducing), :225 (GetCurrentPotential)
Headers: Source/FactoryGame/Public/FGInventoryComponent.h:326 (GetStackFromIndex), :443 (GetSizeLinear)
Survey: mod-native-readers-s4b-headers-2026-10-27.md
Captures: frm-getGenerators-2026-10-27-test3-trimmed.json (FRM HTTP output only; shape and scale reference, the test fixtures are synthetic)
Facts: the vendored contract (PowerSchema.generators, PowerGeneratorSchema in harness/contract/src/power.ts; AGENT_INPUT_MAX_GENERATORS in agent.ts) is the rule for the key order, the cap, the required fuel and the optional supplement; the building bounds are the factory part's (NativeFactoryShared.h); no field name is taken from FRM source
Author: Task 7 implementer subagent (Sprint S4-B, 2026-10-27)
Date: 2026-10-27

FRM source was not opened
