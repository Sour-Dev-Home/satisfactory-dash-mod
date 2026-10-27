Reader: Extractors
Code: mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Native/Extractors/NativeExtractors.h
Code: mod/SatisDashAgent/Source/SatisDashCore/Private/Native/Extractors/NativeExtractors.cpp
Code: mod/SatisDashAgent/Source/SatisDashCore/Private/Native/Factory/NativeFactoryShared.h
Code: mod/SatisDashAgent/Source/SatisDashGame/Private/GameReader.cpp
Code: mod/SatisDashAgent/Source/SatisDashCore/Private/Driver.cpp
Headers: Source/FactoryGame/Public/Buildables/FGBuildableResourceExtractorBase.h:65 (AFGBuildableResourceExtractorBase), :103 (GetExtractableResource), :117 (GetOutputInventory)
Headers: Source/FactoryGame/Public/Buildables/FGBuildableResourceExtractor.h:13 (AFGBuildableResourceExtractor), :47 (GetExtractionPerMinute), :59 (GetFlowRateSmoothed), :63 (GetMaxFlowRate)
Headers: Source/FactoryGame/Public/Buildables/FGBuildableFactory.h:27 (AFGBuildableFactory), :151 (IsProducing), :184 (IsProductionPaused), :213 (GetProductivity), :225 (GetCurrentPotential)
Survey: mod-native-readers-s4b-headers-2026-10-27.md
Captures: frm-getExtractor-2026-10-27-test3-trimmed.json (FRM HTTP output only; shape and scale reference, the test fixtures are synthetic)
Facts: the vendored contract (FactorySchema.extractors, AGENT_INPUT_MAX_EXTRACTORS in harness/contract/src/factory.ts and agent.ts) is the rule for the shape, the cap and the flags; the record writer is the one the factory part uses (AppendFactoryBuilding); no field name is taken from FRM source
Author: Task 3 implementer subagent (Sprint S4-B, 2026-10-27)
Date: 2026-10-27

FRM source was not opened
