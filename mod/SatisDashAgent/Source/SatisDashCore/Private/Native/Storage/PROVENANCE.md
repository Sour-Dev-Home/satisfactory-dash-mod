Reader: Storage
Code: mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Native/Storage/NativeStorage.h
Code: mod/SatisDashAgent/Source/SatisDashCore/Private/Native/Storage/NativeStorage.cpp
Code: mod/SatisDashAgent/Source/SatisDashCore/Private/Native/Factory/NativeFactoryShared.h
Code: mod/SatisDashAgent/Source/SatisDashGame/Private/GameReader.cpp
Headers: Source/FactoryGame/Public/FGInventoryComponent.h:326 (GetStackFromIndex), :443 (GetSizeLinear)
Headers: Source/FactoryGame/Public/Buildables/FGBuildableStorage.h:30 (GetStorageInventory), Source/FactoryGame/Public/Buildables/FGBuildablePipeReservoir.h:67 (GetFluidContent), :71 (GetFluidContentMax), :87 (GetFluidDescriptor), FGInventoryComponent.h:493 (GetSlotSize, the stackMax)
Survey: mod-native-readers-s4b-headers-2026-10-27.md
Captures: frm-getStorageInv-2026-10-27-test3-trimmed.json (FRM HTTP output only; shape and scale reference, the test fixtures are synthetic)
Facts: the vendored contract (StorageSchema, StorageContainerSchema, StorageItemSchema in harness/contract/src/storage.ts; STORAGE_MAX_CONTAINERS and STORAGE_MAX_ITEMS_PER_CONTAINER) is the rule for the key order, the caps, the merge per class and fluidCapacityM3 only on a fluid buffer; the building bounds are the factory part's (NativeFactoryShared.h); no field name is taken from FRM source
Author: Task 14 implementer subagent (Sprint S4-B, 2026-10-27)
Date: 2026-10-27

FRM source was not opened
