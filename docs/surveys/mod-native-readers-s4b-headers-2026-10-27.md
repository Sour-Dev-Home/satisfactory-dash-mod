# Sprint S4-B header survey: native readers for extractors, generators, station rates, storage (ADR-0058)

Paths relative to Source/FactoryGame/Public unless noted. Pinned toolchain: harness/toolchain.json (engine 5.6.1, SML 3.12.0).
Headers: the game's own, shipped with the SML starter project. FRM source was not opened. FRM output was read only from our own
captures (frm-s4-captures-2026-10-27: getExtractor, getGenerators, getStorageInv, getTrainStation), as a black box, and only to
check scales; no name below comes from FRM. Names only, no code. Status: found = declaration read; derived = computed from found
declarations; [NEEDS VERIFICATION] = not settled by a header I read, do not design on it.
Symbols that already have an `api-sources.md` row are marked (row). All others need a new row before use (CLAUDE.md rule 1).

## Enumeration (all four readers)

- `AFGBuildableSubsystem::GetAllBuildablesRef()` FGBuildableSubsystem.h:360 (row; "can be invalidated", so re-fetch per element, as StepFactoryRead does,
  GameReader.cpp:412-). `GetBuildableCount(TSubclassOf<AFGBuildable>)` :363 (row; advisory). `GetTypedBuildable(class, TArray&)` :367 and
  `GetTypedBuildable<T>(TArray<T*>&)` :371 exist (new row; they fill a whole array in one call, so they are not sliceable).
- Recommendation: reuse the cursor-and-Cast pattern (one Cast per buildable per reader). A single shared pass that tests all four casts per
  element avoids walking the list four times; the factory read already walks it once.

## 1. Extractors (`factory.extractors[]`)

- `AFGBuildableResourceExtractorBase : AFGBuildableFactory` FGBuildableResourceExtractorBase.h:65 (row; abstract, "miners and pumps")
  - `GetOutputInventory()` :117 (virtual, returns nullptr in the base); `GetExtractableResource()` :103 (TScriptInterface<IFGExtractableResourceInterface>); `GetExtractorTypeName()` :120
- `AFGBuildableResourceExtractor : AFGBuildableResourceExtractorBase` FGBuildableResourceExtractor.h:13 (new row; the Cast target that covers miners, oil pump, water pump)
  - `GetOutputInventory()` override :35 (mOutputInventory :116)
  - `GetNumExtractedItemsPerCycle()` :39 (int32, mItemsPerCycle :101); `GetNumExtractedItemsPerCycleConverted()` :43 (UI conversion)
  - `GetExtractionPerMinute()` :47 ("converted per resource form for the UI"; whether it follows the clock is not stated)
  - `GetProductionCycleTime()` override :29; `CalcProductionCycleTimeForPotential(float)` :31; `GetDefaultExtractCycleTime()` :65 (mExtractCycleTime :97)
  - `GetFlowRateSmoothed()` :59 ("m^3/s, only valid for Liquid or Gas extractors"); `GetMaxFlowRate()` :63 (m^3/s)
- `AFGBuildableWaterPump : AFGBuildableResourceExtractor` FGBuildableWaterPump.h:13; `AFGBuildableFrackingExtractor : AFGBuildableResourceExtractor`
  FGBuildableFrackingExtractor.h:14 (resource-well satellites). The oil pump is not a C++ class here (a Blueprint on AFGBuildableResourceExtractor: NEEDS VERIFICATION, though the capture lists Build_OilPump_C and the base cast covers it).
- `IFGExtractableResourceInterface` Resources/FGExtractableResourceInterface.h:18: `GetResourceClass()` :41 (the extracted descriptor, TSubclassOf<UFGResourceDescriptor>);
  `GetExtractionSpeedMultiplier()` :56 (purity; unit not stated). Implemented by AFGResourceNodeBase (FGResourceNodeBase.h:93, GetResourceClass :144) and AFGWaterVolume (FGWaterVolume.h:18, :83).
- From `AFGBuildableFactory` FGBuildableFactory.h:27 (rows exist for the first four): `IsProducing()` :151, `IsProductionPaused()` :184, `GetProductivity()` :213,
  `GetCurrentPotential()` :225 (clock as a fraction; the factory read multiplies by 100), `GetPowerInfo()` :108; `GetPendingPotential()` :229, `GetMaxPotential()` :241 (not needed).
- Inventory: `UFGInventoryComponent` FGInventoryComponent.h:158 (row). `GetSizeLinear` :443, `GetStackFromIndex(idx, out)` :326, `GetSlotSize(idx, desc)` :493 (all rows), `HasEnoughSpaceForItem(FInventoryItem)` :433 (new row; returns false if "not allowed" too, :432 comment).

| Contract field | Native source | Status |
|---|---|---|
| id, name, className, location, yaw | GetName(), mDisplayName (FGBuildable.h:824, row), GetClass()->GetName(), actor location/rotation: same as the factory read | found (row) |
| production item | `GetExtractableResource()` :103 then `GetResourceClass()` Resources/FGExtractableResourceInterface.h:41; fallback the first stack in the output inventory | found; null resource (unoccupied) must be handled |
| currentPerMinute / maxPerMinute | `GetExtractionPerMinute` :47 and/or `GetNumExtractedItemsPerCycle` :39 over `GetProductionCycleTime` :29 (x60) | [NEEDS VERIFICATION] clock dependence, purity, fluid scaling (see Q1) |
| percent | `GetProductivity()` :213 (scale judged by parity run, as for factories) or current/max | found; see Q1 |
| isProducing / isPaused | `IsProducing()` :151 / `IsProductionPaused()` :184 | found (row) |
| isBackedUp | output inventory has no room: the output slot at/over `GetSlotSize` :493 (or `HasEnoughSpaceForItem` :433 false) | derived; see Q2 |
| clockSpeedPercent | 100 * `GetCurrentPotential()` :225 | found (row) |
| circuitGroupId | `GetPowerInfo()` :108 -> `GetPowerCircuit()` FGPowerInfoComponent.h:80 -> `GetCircuitGroupID()` FGCircuit.h:71, else -1 (as GameReader.cpp:474-476) | found (row) |
| recipe null, ingredients [] | constants | n/a |

Oracle (getExtractor, Test3): Mk1 showed max 120/min, current 18.8, percent 15.7 while IsProducing was false; Mk2 240 max / 200.5 current / 83.5 percent;
oil pump 150/150; water 120/120; output slot "Amount" 94 of MaxAmount 100 and oil 25.7 of 50. So percent equals current/max, max includes purity and the clock, and
the slot amounts are in m^3 for fluids. This is evidence of scale only; the semantics must come from the header and the parity run.

## 2. Generators (`power.generators[]`)

- `AFGBuildableGenerator : AFGBuildableFactory` FGBuildableGenerator.h:60 (row; Cast target)
  - `GetPowerProductionCapacity()` :87 ("The power this generator can produce"; MW per mPowerProduction :126 comment "in MW"); `GetDefaultPowerProductionCapacity()` :91; `CalcPowerProductionCapacityForPotential(float)` :95
  - `GetLoadPercentage()` :83 ("in the range [0,1]"; mLoadPercentage :131); `CanStartPowerProduction()` :99
- `UFGPowerInfoComponent` FGPowerInfoComponent.h (new rows): `GetBaseProduction()` :137 ("power we always provide to the circuit"), `GetDynamicProductionCapacity()` :148 ("maximum dynamic power production"),
  `GetRegulatedDynamicProduction()` :160 ("dynamic power production we provide to the circuit"), `GetDynamicProductionDemandFactor()` :156, `GetProductionBoostFactor()` :168. Units are not stated in these comments.
- `AFGBuildableGeneratorFuel : AFGBuildableGenerator` FGBuildableGeneratorFuel.h:14 (coal, fuel, biomass, and the base of nuclear)
  - `GetFuelInventory()` :54 (mFuelInventory :207); `GetCurrentFuelClass()` :103 (mCurrentFuelClass :241); `GetSupplementalResourceClass()` :111; `GetRequiresSupplementalResource()` :74
  - `GetFuelInventoryIndex()` :116 and `GetSupplementalResourceInventoryIndex()` :117 (indexes into the same fuel inventory)
  - `GetFuelAmount()` :92 ("how much of the fuel have we burned? [0,1]"; the comment reads as a burn fraction of the current piece; mCurrentFuelAmount :225 is MWs left)
  - `GetSupplementalAmount()` :96 ([0,1] remaining); mCurrentSupplementalAmount :229 ("Liters"); `GetSupplementalConsumptionRateCurrent/Maximum()` :80/:86 (m^3/s)
  - `HasFuel()` :61, `HasSupplementalResource()` :68; mUsesFluidsAsFuel :170
- `AFGBuildableGeneratorNuclear : AFGBuildableGeneratorFuel` FGBuildableGeneratorNuclear.h:24; `GetWasteInventory()` :36 (waste is an output, not fuel); `IsWasteFull()` :51
- `AFGBuildableGeneratorGeoThermal : AFGBuildableGenerator` FGBuildableGeneratorGeoThermal.h:14: `GetMinPowerProduction()` :35, `GetMaxPowerProduction()` :39 (MW); `GetExtractableResource()` :60. It has no fuel (not a Fuel subclass).

| Contract field | Native source | Status |
|---|---|---|
| id, name, className, location | as above | found (row) |
| circuitGroupId | power info -> circuit -> group id, else -1 | found (row) |
| currentMW | `GetPowerInfo()->GetBaseProduction()` :137 + `GetRegulatedDynamicProduction()` :160 (MW assumed) | [NEEDS VERIFICATION] Q3 |
| capacityMW | `GetPowerProductionCapacity()` :87 (comment "can produce"; potential-aware not stated, alternative `CalcPowerProductionCapacityForPotential(GetCurrentPotential())` :95) | [NEEDS VERIFICATION] Q3; geothermal Q4 |
| clockSpeedPercent | 100 * `GetCurrentPotential()` FGBuildableFactory.h:225 | found (row) |
| isProducing | `IsProducing()` FGBuildableFactory.h:151 | found (row) |
| fuel {className,name,amount} | Cast to `AFGBuildableGeneratorFuel`; class `GetCurrentFuelClass()` :103; amount = count of that class in `GetFuelInventory()` via `GetStackFromIndex(GetFuelInventoryIndex())` or `GetNumItems` FGInventoryComponent.h:415 (new row); null when none or not a Fuel subclass (geothermal) | derived; Q5 |
| supplement | `GetSupplementalResourceClass()` :111 + stack at `GetSupplementalResourceInventoryIndex()` :117, only if `GetRequiresSupplementalResource()` :74; water inventory item is a fluid (litres in-game, m^3 in the snapshot, as the factory read scales) | derived; Q5 |

Oracle (getGenerators, Test3): coal 75/75 with Supplement Water; fuel generator 250; biomass 20 and 30; geothermal capacity 0 with a separate min/max pair and Supplement empty; nuclear warning field present.
Biomass burners with load 0 showed capacity 20/30 (capacity = potential-based nameplate, not current output). Not used to name anything.

## 3. Station rates (`mapLive.stations[]`)

- Walk (unchanged): `TrainsReadStation` GameReader.cpp:586-: `AFGBuildableRailroadStation` FGBuildableRailroadStation.h:16 (row) -> `GetStationOutputConnection()` :56 (row) -> `GetConnectedTo()` / `GetPlatformOwner()` FGTrainPlatformConnection.h:42/:53 (rows) ->
  `AFGBuildableTrainPlatform::GetConnectionInOppositeDirection` FGBuildableTrainPlatform.h:66 (row). Bounded 256 hops with a visited set. Station id is `Building.GetName()`, the same string the trains read uses, which is the trainRoutes id requirement.
- `AFGBuildableTrainPlatformCargo : AFGBuildableTrainPlatform` FGBuildableTrainPlatformCargo.h:18 (row): `GetInventory()` :37 (row), `GetIsInLoadMode()` :57 (row), `IsLoadUnloading()` :61
  - Public (lines 22-102): `GetOutflowRate()` :83 and `GetInflowRate()` :87, "smoothed ... in m^3/s, only valid for Liquid Freight Platforms" (mReplicatedOutflowRate/mReplicatedInflowRate :394/:398). These are fluid only: not for item platforms.
  - Item side: `GetCurrentItemTransferRate()` :180 is declared in the `private:` block (:121-189), non-const, "the transfer rate for the current load/unload mode"; the unit is not stated. Backing state mSmoothedLoadRate :363 and mSmoothedUnloadRate :366, also private (UPROPERTY, replicated). `UpdateItemTransferRate(int32)` :176 private.
  - Public: delegate `mOnTransferRateUpdated` :198 (`FOnItemTransferRateUpdated`, one int32 `itemTransferRate`, :12; "Delegate for UI to hookup to when the transfer rate is updated", BlueprintAssignable).
  - Fluid vs item: no public getter. Private: FilterResourceForms :124, mPipeInputConnections :277, mPipeOutputConnections :281, mStorageInputConnections :273. The pipe component class is `UFGPipeConnectionComponent` FGPipeConnectionComponent.h:177.

| Contract field | Native source | Status |
|---|---|---|
| transferPerMinute | sum over a station's item cargo platforms of the platform item transfer rate; no public getter | [NEEDS VERIFICATION] access path and unit (Q6) |
| inflowPerMinute / outflowPerMinute | public `GetInflowRate()` :87 / `GetOutflowRate()` :83 are fluid only; for items no header-named equivalent | not found for items (Q7) |
| fluid platform excluded | no public flag; candidates: any `UFGPipeConnectionComponent` on the platform actor (UE component query), or the item form of the held stack | [NEEDS VERIFICATION] (Q8) |

Oracle (getTrainStation, Test3): the station-level TransferRate (2.059) equals the sum of its two freight platforms' rates (1.498 + 0.561); empty platforms read 0; item platforms read Inflow/Outflow as 0.
The numbers are small non-integers, so the scale is not obviously items per minute (the int32 delegate payload suggests whole items); the unit must be settled by the parity run.

## 4. Storage (`storage.containers[]`)

- `AFGBuildableStorage : AFGBuildableFactory` FGBuildableStorage.h:13 (row; Cast target). `GetStorageInventory()` :30 is non-const (FORCEINLINE, returns mStorageInventory :56), so the Cast must keep a non-const pointer. mInventorySizeX/Y :47/:51 (access not checked).
- `AFGCentralStorageContainer : AFGBuildableStorage` FGCentralStorageContainer.h:15 (the dimensional depot uploader; new row). It is a subclass, so a Cast to AFGBuildableStorage includes it. The capture shows its own inventory is 1 slot. The depot's pooled content lives in `AFGCentralStorageSubsystem` FGCentralStorageSubsystem.h:25 (not read; Q9).
  Other users of a `GetStorageInventory()` name (vehicles, drone, FGBuildableBlueprintDesigner.h:148) are not AFGBuildableStorage and are not buildables of this kind.
- `AFGBuildablePipeReservoir : AFGBuildableFactory, IFGFluidIntegrantInterface` FGBuildablePipeReservoir.h:43 (row; fluid buffer): `GetFluidContent()` :67 ("[m^3], may exceed maximum when over-pressurized"), `GetFluidContentMax()` :71 ([m^3]),
  `GetFluidDescriptor()` :87 (the fluid; may be null when empty, NEEDS VERIFICATION), mStorageCapacity :101 ("cubic meters"). No inventory: its content is an `FFluidBox` (mFluidBox :96). Whether the Industrial Fluid Buffer is this class or a subclass is not shown by a header (Blueprint): NEEDS VERIFICATION.
- Inventory per item: `GetSizeLinear` :443, `GetStackFromIndex` :326, `GetSlotSize` :493 (rows); `FInventoryStack` :95 (Item :112, NumItems :116, row); `FInventoryItem::GetItemClass` :45 (row).
  `UFGItemDescriptor::GetStackSize(TSubclassOf)` Resources/FGItemDescriptor.h:251 (int32); `GetStackSizeConverted` :258; `GetForm` :158 (row), `GetItemName` :191 (row); `EStackSize` :42 (SS_ONE .. SS_HUGE, SS_FLUID).

| Contract field | Native source | Status |
|---|---|---|
| id, name, className, location | as the factory read | found (row) |
| slots | `GetSizeLinear()` FGInventoryComponent.h:443 on `GetStorageInventory()` FGBuildableStorage.h:30 | found |
| slotsUsed | count of stacks with NumItems > 0 (the same loop as FactoryReadSlots) | derived |
| items[].className, name, amount | stack item class `GetName()`, `GetItemName` :191, summed NumItems per class (merge) | derived (rows for the loop exist) |
| items[].stackMax | `GetSlotSize` :493 of a stack of that class (honours an arbitrary slot size) or `GetStackSize` Resources/FGItemDescriptor.h:251; null for fluids | found; Q10 |
| fluidCapacityM3 | `GetFluidContentMax()` FGBuildablePipeReservoir.h:71 (m^3) | found |
| fluid item amount | `GetFluidContent()` :67 (m^3), item className from `GetFluidDescriptor()` :87 | found |

Oracle (getStorageInv, Test3): storage classes seen are Build_StorageIntegrated_C, Build_StoragePlayer_C, Build_StorageContainerMk1/Mk2_C, Build_CentralStorage_C; slots 24 (Mk1), 48 (Mk2), 25 (player box), 1 (central); per-item max matches stack size (Wire 500, Plastic 200).
Test3 has 110 containers; the contract caps 2000 containers and 64 items each (the cap check is a mod-side concern).

## Game-thread constraints and cost

- All of this is game-thread only (actor reads), sliced by `FStepBudget` as the factory read is. Buildable iteration must re-fetch `GetAllBuildablesRef()` per element.
- Cheap per element: Cast, getters, power-info pointer chasing. Costlier: storage (a container of 48 slots, 110 containers, each stack walked; bounded by merge into a small map, 64 items cap), generators' inventory reads (a few slots), the station walk (up to 256 hops per station, existing).
- `GetStorageInventory()` and `GetCurrentItemTransferRate()` are non-const; get the non-const object pointer (or reflection) deliberately.
- Reading private state (transfer rates) by reflection is a per-platform name lookup; cache the FProperty once per class, not per call.

## Open questions, with recommendations

1. Extractor current/max per minute. Q: use `GetExtractionPerMinute` :47 or compute from per-cycle count and cycle time? Rec: compute max = `GetNumExtractedItemsPerCycle` :39 x 60 / `CalcProductionCycleTimeForPotential(GetCurrentPotential())` :31 (clock-aware), purity via `GetExtractionSpeedMultiplier` :56 if the cycle time does not already include it, and current = max x `GetProductivity` :213; let the parity run against the capture judge it, and fall back to GetExtractionPerMinute for fluids if m^3 scaling differs.
2. isBackedUp. Rec: true only when the output stack is at its `GetSlotSize` :493 and `IsProducing` is false; read it from the output inventory; fluid pumps have no meaningful full inventory, so judge in the parity run.
3. Generator currentMW and capacityMW. Rec: capacity = `CalcPowerProductionCapacityForPotential(GetCurrentPotential())` :95 (explicitly at current clock), current = power-info base plus regulated dynamic production; verify units (MW assumed) and that biomass/coal read right in the parity run.
4. Geothermal. Header gives `GetMinPowerProduction`/`GetMaxPowerProduction` :35/:39; the capture shows capacity 0 in the FRM scheme. Rec: capacity from the same power-info path as other generators; if that gives 0, report 0 and no fuel; do not invent a value from min/max.
5. Fuel/supplement amount units. Rec: amount = item count in the fuel (or supplemental) inventory slot; fluid fuel (fuel generator, water) scaled to m^3 by the same Core scaling the factory read uses (litres/1000); null for geothermal and when the slot is empty.
6. Station transfer rate access. The item rate getter is private. Rec: read the private replicated floats mSmoothedLoadRate :363 / mSmoothedUnloadRate :366 by reflection (FProperty lookup by name), choosing by `GetIsInLoadMode()` :57; alternative is binding `mOnTransferRateUpdated` :198 on every platform (more state, lifecycle risk). Owner call, needs a new api-sources row either way.
7. Station inflow/outflow for items. Rec: omit them (absent = not read) until a header names an item inflow/outflow; send only transferPerMinute. The capture reads 0 for both on item platforms.
8. Fluid platform detection. Rec: a platform with any `UFGPipeConnectionComponent` FGPipeConnectionComponent.h:177 is fluid and contributes nothing; verify on Test3 that the item platforms have none.
9. Dimensional depot. Rec: report the uploader like any container (1 slot), do not read the depot subsystem.
10. Storage stackMax. Rec: `GetSlotSize` of the stack's slot (arbitrary slot sizes honoured), null for fluid forms.

## Counts

Found 32, derived 9, [NEEDS VERIFICATION] 9 (extractor rate semantics, oil pump class, generator current/capacity units, geothermal, station item-rate access and unit, item inflow/outflow, fluid platform detection, industrial buffer class, GetFluidDescriptor when empty).
