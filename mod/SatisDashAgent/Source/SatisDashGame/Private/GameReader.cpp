#include "GameReader.h"

#include "Buildables/FGBuildableAttachmentMerger.h"
#include "Buildables/FGBuildableAttachmentSplitter.h"
#include "Buildables/FGBuildableConveyorBase.h"
#include "Buildables/FGBuildableConveyorBelt.h"
#include "Buildables/FGBuildableConveyorLift.h"
#include "Buildables/FGBuildableDockingStation.h"
#include "Buildables/FGBuildableDroneStation.h"
#include "Buildables/FGBuildableGenerator.h"
#include "Buildables/FGBuildableManufacturer.h"
#include "Buildables/FGBuildableMergerPriority.h"
#include "Buildables/FGBuildablePipeline.h"
#include "Buildables/FGBuildablePipelineJunction.h"
#include "Buildables/FGBuildablePipelinePump.h"
#include "Buildables/FGBuildablePipeReservoir.h"
#include "Buildables/FGBuildableResourceExtractor.h"
#include "Buildables/FGBuildableResourceExtractorBase.h"
#include "Buildables/FGBuildableResourceSink.h"
#include "Buildables/FGBuildableSpaceElevator.h"
#include "Buildables/FGBuildableSplitterSmart.h"
#include "Buildables/FGBuildableStorage.h"
#include "Buildables/FGBuildableRailroadStation.h"
#include "Buildables/FGBuildableRailroadTrack.h"
#include "Components/SplineComponent.h"
#include "Buildables/FGBuildableTrainPlatform.h"
#include "Buildables/FGBuildableTrainPlatformCargo.h"
#include "Buildables/FGBuildableWire.h"
#include "FGCircuitConnectionComponent.h"
#include "Controller/FGServerManagementController.h"
#include "Controller/FGServerStateController.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "FGBuildableSubsystem.h"
#include "FGFactoryConnectionComponent.h"
#include "FGPipeConnectionComponent.h"
#include "FGCircuitSubsystem.h"
#include "FGGameState.h"
#include "FGMapManager.h"
#include "FGBuildingTagInterface.h"
#include "FGDroneStationInfo.h"
#include "FGDroneSubsystem.h"
#include "FGActorRepresentation.h"
#include "FGInventoryComponent.h"
#include "FGPowerCircuit.h"
#include "FGFreightWagon.h"
#include "FGPowerInfoComponent.h"
#include "FGRailroadSubsystem.h"
#include "FGRailroadTimeTable.h"
#include "FGRecipe.h"
#include "FGServerSubsystem.h"
#include "FGTrain.h"
#include "FGTrainPlatformConnection.h"
#include "FGTrainStationIdentifier.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Online/PlayerInfoCache.h"
#include "Resources/FGExtractableResourceInterface.h"
#include "Resources/FGItemDescriptor.h"
#include "Resources/FGResourceNode.h"
#include "Resources/FGResourceDescriptor.h"
#include "Resources/FGResourceNodeBase.h"
#include "SatisDash/FrmResourceNodes.h"
#include "SatisDash/GameValues.h"
#include "SatisDash/Log.h"
#include "SatisDash/Native/Extractors/NativeExtractors.h"
#include "SatisDash/Native/Rates/NativeRates.h"
#include "SatisDash/Native/TrainRoutes/NativeTrainRoutes.h"

UFGServerSubsystem* SatisDash::FGameReader::Server() const
{
	return World.IsValid() ? UFGServerSubsystem::Get(World.Get()) : nullptr;
}

TOptional<SatisDash::FStatusReading> SatisDash::FGameReader::ReadStatus()
{
	UFGServerSubsystem* Subsystem = Server();
	UFGServerAPIManager* Manager = Subsystem != nullptr ? Subsystem->GetServerAPIManager() : nullptr;
	if (Manager == nullptr)
	{
		return {};
	}
	const FServerGameState State = Subsystem->GetServerGameState();
	// HealthCheck's verdict has no getter; the game's own handler computes it, on a controller made under the API
	// manager, which finds the subsystem through its outers (S6).
	FString Health;
	FString CustomData;
	NewObject<UFGServerStateController>(Manager)->Handler_HealthCheck(FString(), Health, CustomData);
	FStatusReading Out;
	Out.bTickHealthy = IsTickHealthy(Health);
	Out.bIsGameRunning = State.IsGameRunning;
	Out.bGamePaused = State.IsGamePaused;
	Out.SessionName = State.ActiveSessionName;
	Out.ConnectedPlayers = State.NumConnectedPlayers;
	Out.PlayerLimit = State.PlayerLimit;
	Out.TickRate = State.AverageTickRate;
	Out.TotalGameDurationSeconds = State.TotalGameDuration;
	return Out;
}

TOptional<SatisDash::FAutoPauseReading> SatisDash::FGameReader::ReadAutoPause()
{
	UFGServerSubsystem* Subsystem = Server();
	UFGServerAPIManager* Manager = Subsystem != nullptr ? Subsystem->GetServerAPIManager() : nullptr;
	if (Manager == nullptr)
	{
		return {};
	}
	// The options also hold FRM's token: only FG.DSAutoPause is read, and nothing of them is logged (S7).
	TMap<FString, FString> Options;
	TMap<FString, FString> Pending;
	NewObject<UFGServerStateController>(Manager)->Handler_GetServerOptions(Options, Pending);
	const TOptional<bool> Applied = ParseAutoPauseOption(Options.Find(AutoPauseOption));
	if (!Applied.IsSet())
	{
		return {};
	}
	FAutoPauseReading Out;
	Out.bEnabled = Applied.GetValue();
	// A change the server defers sits in the pending options (satisfactory-dash #621). An unreadable pending value
	// counts as none: only a value the mod can name is ever reported as pending.
	Out.PendingValue = ParseAutoPauseOption(Pending.Find(AutoPauseOption));
	return Out;
}

TOptional<SatisDash::FAutoPauseReading> SatisDash::FGameReader::ApplyAutoPause(bool bEnabled)
{
	UFGServerSubsystem* Subsystem = Server();
	UFGServerAPIManager* Manager = Subsystem != nullptr ? Subsystem->GetServerAPIManager() : nullptr;
	if (Manager == nullptr)
	{
		return {};
	}
	TMap<FString, FString> Updated;
	Updated.Add(AutoPauseOption, AutoPauseOptionValue(bEnabled));
	NewObject<UFGServerManagementController>(Manager)->Handler_ApplyServerOptions(Updated);
	// Handler_ApplyServerOptions returns nothing (FGServerManagementController.h:60): reading back is the only check,
	// and the driver judges it (applied or pending).
	return ReadAutoPause();
}

TOptional<TArray<SatisDash::FCircuitReading>> SatisDash::FGameReader::ReadPower()
{
	AFGCircuitSubsystem* Circuits = World.IsValid() ? AFGCircuitSubsystem::GetCircuitSubsystem(World.Get()) : nullptr;
	if (Circuits == nullptr)
	{
		return {};
	}
	TArray<FPowerCircuitSample> Samples;
	Samples.Reserve(32);
	for (int32 Id = 0; Id < MaxCircuitIdScanned; ++Id)
	{
		UFGPowerCircuit* Circuit = Circuits->FindCircuit<UFGPowerCircuit>(Id);
		if (Circuit == nullptr)
		{
			continue;
		}
		FPowerCircuitStats Stats;
		Circuit->GetStats(Stats);
		FPowerCircuitSample S;
		S.CircuitId = Circuit->GetCircuitID();
		S.GroupId = Circuit->GetCircuitGroupID();
		S.ProducedMW = Stats.PowerProduced;
		S.ConsumedMW = Stats.PowerConsumed;
		S.CapacityMW = Stats.PowerProductionCapacity;
		S.MaxConsumptionMW = Stats.MaximumPowerConsumption;
		S.bFuseTriggered = Circuit->IsFuseTriggered();
		S.bHasBatteries = Circuit->HasBatteries();
		S.BatteryStoreMWh = Circuit->GetBatterySumPowerStore();
		S.BatteryCapacityMWh = Circuit->GetBatterySumPowerStoreCapacity();
		// The public getters split the signed input into charge and discharge (FGPowerCircuit.h:227, :231).
		S.BatteryInputMW = Circuit->GetBatterySumPowerInput() - Circuit->GetBatterySumPowerOutput();
		Samples.Add(S);
	}
	return GroupCircuits(Samples);
}

TOptional<TArray<SatisDash::FPlayerReading>> SatisDash::FGameReader::ReadPlayers()
{
	UWorld* Current = World.Get();
	const AFGGameState* State = Current != nullptr ? Current->GetGameState<AFGGameState>() : nullptr;
	if (State == nullptr)
	{
		return {};
	}
	TArray<FPlayerReading> Out;
	// Connected: the replicated player list (GameStateBase.h:55). Offline: the players the save remembers (the inline
	// getter, FGGameState.h:515) none of whose identities is a connected player's (PlayerState.h:408), named by the
	// platform they were last seen on, else their first identity. GetOfflineCachedPlayerInfosInSession (:519) is not
	// used: it crashed the dedicated server on its first call (access violation inside it, core run 40241942772).
	// Names are personal data: they are copied into the reading and never logged.
	TArray<const FUniqueNetIdRepl*> OnlineIds;
	for (const APlayerState* Player : State->PlayerArray)
	{
		if (Player != nullptr)
		{
			Out.Add({ Player->GetPlayerName(), true });
			OnlineIds.Add(&Player->GetUniqueId());
		}
	}
	for (const FCachedPlayerInfo& Info : State->GetAllPlayerInfoData())
	{
		// Invalid ids are skipped: two invalid ids compare equal (CoreOnline.h:671).
		const bool bOnline = Info.PerPlatformInfo.ContainsByPredicate([&OnlineIds](const FCachedPlayerPlatformInfo& Id) {
			return Id.UniqueNetId.IsValid() && OnlineIds.ContainsByPredicate([&Id](const FUniqueNetIdRepl* Online) {
				return *Online == Id.UniqueNetId;
			});
		});
		if (bOnline)
		{
			continue;
		}
		const FCachedPlayerPlatformInfo* Platform = Info.GetForPlatform(Info.GetLastSeenServicesType());
		if (Platform == nullptr && !Info.PerPlatformInfo.IsEmpty())
		{
			Platform = &Info.PerPlatformInfo[0];
		}
		if (Platform != nullptr)
		{
			Out.Add({ Platform->PlayerName, false });
		}
	}
	return Out;
}

bool SatisDash::FGameReader::BeginResourceNodeScan()
{
	UWorld* Current = World.Get();
	if (Current == nullptr)
	{
		return false;
	}
	ScanLevels.Reset();
	ScanWorld = Current;
	for (ULevel* Level : Current->GetLevels())
	{
		ScanLevels.Add(Level);
	}
	ScanLevel = 0;
	ScanIndex = 0;
	ScanActors = 0;
	ScanSteps = 0;
	ScanLongestStep = 0;
	ScanLongestItems = 0;
	ScanLongestStartLevel = 0;
	ScanLongestEndLevel = 0;
	ScanTotal = 0;
	ScanFound.Reset();
	return true;
}

TOptional<TArray<SatisDash::FResourceNodeReading>> SatisDash::FGameReader::StepResourceNodeScan(int32 MaxActors)
{
	const double StepStart = FPlatformTime::Seconds();
	// The node list is kept bounded whatever the world holds (twice the map's own cap).
	const int32 MaxFound = 2 * MaxResourceNodes;
	int32 Budget = MaxActors;
	const int32 StartLevel = ScanLevel;
	const int32 ActorsBefore = ScanActors;
	while (Budget > 0 && ScanLevel < ScanLevels.Num())
	{
		const ULevel* Level = ScanLevels[ScanLevel].Get();
		if (Level == nullptr || ScanIndex >= Level->Actors.Num())
		{
			++ScanLevel;
			ScanIndex = 0;
			continue;
		}
		AActor* Actor = Level->Actors[ScanIndex++];
		--Budget;
		++ScanActors;
		const AFGResourceNode* Node = Cast<AFGResourceNode>(Actor);
		if (Node == nullptr || ScanFound.Num() >= MaxFound)
		{
			continue;
		}
		FResourceNodeReading Reading;
		// Fracking cores are not AFGResourceNode (FGResourceNodeFrackingCore.h:14), deposits are but report their own type
		// (EResourceNodeType::Deposit): the type decides what the map draws (FGResourceNodeBase.h:24).
		switch (Node->GetResourceNodeType())
		{
		case EResourceNodeType::Node: Reading.Kind = ENodeKind::Node; break;
		case EResourceNodeType::FrackingSatellite: Reading.Kind = ENodeKind::FrackingSatellite; break;
		case EResourceNodeType::Geyser: Reading.Kind = ENodeKind::Geyser; break;
		default: continue;
		}
		switch (Node->GetResourcePurity())
		{
		case RP_Inpure: Reading.Purity = ENodePurity::Impure; break;
		case RP_Normal: Reading.Purity = ENodePurity::Normal; break;
		case RP_Pure: Reading.Purity = ENodePurity::Pure; break;
		default: continue;
		}
		Reading.Resource = Node->GetResourceName().ToString();
		// The descriptor class's short name (Desc_OreIron_C), as FRM's ClassName: GetName, not the path. No class gives empty.
		const TSubclassOf<UFGResourceDescriptor> ResourceClass = Node->GetResourceClass();
		Reading.ClassName = ResourceClass ? ResourceClass->GetName() : FString();
		const FVector Location = Node->GetActorLocation();
		Reading.XCm = Location.X;
		Reading.YCm = Location.Y;
		Reading.bExploited = Node->IsOccupied();
		ScanFound.Add(MoveTemp(Reading));
	}
	const double Spent = FPlatformTime::Seconds() - StepStart;
	++ScanSteps;
	ScanTotal += Spent;
	if (Spent > ScanLongestStep)
	{
		ScanLongestStep = Spent;
		ScanLongestItems = ScanActors - ActorsBefore;
		ScanLongestStartLevel = StartLevel;
		ScanLongestEndLevel = ScanLevel;
	}
	if (ScanLevel < ScanLevels.Num())
	{
		return {};
	}
	ScanLevels.Reset();
	// A different save was loaded while the slices ran (the newest world is the one SetWorld holds): what was found is the old
	// world's or a mix, so it is "cannot read" (empty: nothing posted, retried), never a partial post.
	if (World.Get() == nullptr || World.Get() != ScanWorld.Get())
	{
		ScanFound.Reset();
		return TArray<FResourceNodeReading>();
	}
	// Counts and times only, and only when something was found (an empty scan is the sender's rate-limited warning, and is
	// retried every 30 s). The times are the game thread's scan alone: the sort and the JSON text are built on the worker.
	if (ScanFound.Num() > 0)
	{
		UE_LOG(LogSatisDash, Display, TEXT("Resource node scan (game thread only): %d actors in %d steps, %d nodes, longest step %.2f ms, total %.2f ms"),
			ScanActors, ScanSteps, ScanFound.Num(), ScanLongestStep * 1000.0, ScanTotal * 1000.0);
		// The scan's phase is the level index; its elements are actors.
		LogLongestStep(TEXT("Resource node"), ScanLongestStep * 1000.0, ScanLongestItems, ScanLongestStartLevel, ScanLongestEndLevel);
	}
	// A world with no levels, no actors or no nodes comes back empty, which the caller treats as "cannot read".
	return MoveTemp(ScanFound);
}

namespace
{
	/** R2a: a form the snapshot reports in m3 (the pure side scales it). */
	bool FactoryReadIsFluid(const TSubclassOf<UFGItemDescriptor>& ItemClass)
	{
		const EResourceForm Form = UFGItemDescriptor::GetForm(ItemClass);
		return Form == EResourceForm::RF_LIQUID || Form == EResourceForm::RF_GAS;
	}

	void FactoryReadRecipeItems(const TArray<FItemAmount>& Items, TArray<SatisDash::FRecipeItemReading>& Out)
	{
		for (const FItemAmount& Item : Items)
		{
			if (!Item.ItemClass)
			{
				continue;
			}
			SatisDash::FRecipeItemReading Line;
			Line.Name = UFGItemDescriptor::GetItemName(Item.ItemClass).ToString();
			Line.ClassName = Item.ItemClass->GetName();
			Line.AmountPerCycle = Item.Amount; // game units (litres for a fluid): NativeRates converts
			Line.bFluid = FactoryReadIsFluid(Item.ItemClass);
			Out.Add(MoveTemp(Line));
		}
	}

	/**
	 * Only non-empty slots are kept. Raw counts go to MakeSlotReading, which scales a fluid (litres to m3) in Core: the capacity is
	 * GetSlotSize (FGInventoryComponent.h:493, "the arbitrary slot size if one is set, otherwise the stack size").
	 */
	void FactoryReadSlots(const UFGInventoryComponent& Inventory, TArray<SatisDash::FSlotReading>& Out)
	{
		const int32 Size = Inventory.GetSizeLinear();
		for (int32 Index = 0; Index < Size; ++Index)
		{
			FInventoryStack Stack;
			if (!Inventory.GetStackFromIndex(Index, Stack) || Stack.NumItems <= 0)
			{
				continue;
			}
			const TSubclassOf<UFGItemDescriptor> ItemClass = Stack.Item.GetItemClass();
			if (!ItemClass)
			{
				continue;
			}
			Out.Add(SatisDash::MakeSlotReading(ItemClass->GetName(), Stack.NumItems, Inventory.GetSlotSize(Index, ItemClass), FactoryReadIsFluid(ItemClass)));
		}
	}
}

void SatisDash::FGameReader::ResetFactoryRead()
{
	FactoryWorld.Reset();
	FactoryCursor = 0;
	FactoryFound.Reset();
}

bool SatisDash::FGameReader::BeginFactoryRead()
{
	UWorld* Current = World.Get();
	const AFGBuildableSubsystem* Subsystem = Current != nullptr ? AFGBuildableSubsystem::Get(Current) : nullptr;
	if (Subsystem == nullptr)
	{
		return false;
	}
	ResetFactoryRead();
	bFactoryAbandoned = false;
	FactoryWorld = Current;
	// Advisory only (a count of the manufacturers the subsystem knows, logged next to what was read).
	FactoryExpected = Subsystem->GetBuildableCount(AFGBuildableManufacturer::StaticClass());
	FactoryBoosted = 0;
	FactoryProductivityMin = 1e300;
	FactoryProductivityMax = -1e300;
	return true;
}

TOptional<TArray<SatisDash::FFactoryReading>> SatisDash::FGameReader::StepFactoryRead(FStepBudget& Budget)
{
	UWorld* Current = World.Get();
	// A different save was loaded while the slices ran (or the world went away): what was read is the old world's or a mix, so
	// the read is reported as abandoned (nothing submitted), never a partial part. A read that finishes with no machines is a
	// different answer: it submits an empty part.
	const AFGBuildableSubsystem* Subsystem = Current != nullptr && Current == FactoryWorld.Get() ? AFGBuildableSubsystem::Get(Current) : nullptr;
	if (Subsystem == nullptr)
	{
		ResetFactoryRead();
		bFactoryAbandoned = true;
		return TArray<FFactoryReading>();
	}
	// The list is kept bounded whatever the world holds (twice the snapshot's own cap).
	const int32 MaxFound = 2 * MaxBuildings;
	bool bFinished = false;
	Budget.Begin();
	do
	{
		// Re-fetched for every element and never kept: buildables come and go between ticks.
		const TArray<AFGBuildable*>& All = Subsystem->GetAllBuildablesRef();
		if (FactoryCursor >= All.Num())
		{
			bFinished = true;
			break;
		}
		const AFGBuildableManufacturer* Machine = Cast<AFGBuildableManufacturer>(All[FactoryCursor++]);
		if (Machine == nullptr || !IsValid(Machine) || FactoryFound.Num() >= MaxFound)
		{
			continue;
		}
		FFactoryReading Reading;
		Reading.Id = Machine->GetName();
		Reading.Name = Machine->mDisplayName.ToString();
		Reading.ClassName = Machine->GetClass()->GetName();
		Reading.bProducing = Machine->IsProducing();
		Reading.bPaused = Machine->IsProductionPaused();
		const TSubclassOf<UFGRecipe> Recipe = Machine->GetCurrentRecipe();
		FRateInput Rates;
		if (Recipe)
		{
			Reading.Recipe = UFGRecipe::GetRecipeName(Recipe).ToString();
			FactoryReadRecipeItems(UFGRecipe::GetProducts(Recipe), Rates.Products);
			FactoryReadRecipeItems(UFGRecipe::GetIngredients(Machine, Recipe), Rates.Ingredients);
			Rates.CycleSeconds = UFGRecipe::GetManufacturingDuration(Recipe);
		}
		Rates.Potential = Machine->GetCurrentPotential();
		Rates.ProductionBoost = Machine->GetCurrentProductionBoost();
		Rates.RawProductivity = Machine->GetProductivity();
		Rates.bProducing = Reading.bProducing;
		NativeRates(Rates, DefaultRatesMode, Reading.Production, Reading.Ingredients);
		Reading.ClockPercent = 100.0 * Rates.Potential;
		FactoryProductivityMin = FMath::Min(FactoryProductivityMin, Rates.RawProductivity);
		FactoryProductivityMax = FMath::Max(FactoryProductivityMax, Rates.RawProductivity);
		FactoryBoosted += Rates.ProductionBoost > 1.0 ? 1 : 0;
		const FVector Location = Machine->GetActorLocation();
		Reading.bHasLocation = true;
		Reading.XCm = Location.X;
		Reading.YCm = Location.Y;
		Reading.ZCm = Location.Z;
		Reading.YawDeg = Machine->GetActorRotation().Yaw;
		// Any link in the chain can be missing (no power use, not connected): -1 is "no group".
		const UFGPowerInfoComponent* PowerInfo = Machine->GetPowerInfo();
		const UFGPowerCircuit* Circuit = PowerInfo != nullptr ? PowerInfo->GetPowerCircuit() : nullptr;
		Reading.CircuitGroupId = Circuit != nullptr ? Circuit->GetCircuitGroupID() : -1;
		if (const UFGInventoryComponent* Output = Machine->GetOutputInventory())
		{
			FactoryReadSlots(*Output, Reading.Output);
		}
		if (const UFGInventoryComponent* Input = Machine->GetInputInventory())
		{
			TArray<FSlotReading> Slots;
			FactoryReadSlots(*Input, Slots);
			Reading.Input = MoveTemp(Slots);
		}
		FactoryFound.Add(MoveTemp(Reading));
	} while (!Budget.Spent());
	Budget.End(); // straight after the loop: nothing else is done on the game thread this step
	if (!bFinished)
	{
		return {};
	}
	// Counts and times only; never an id, a name or a recipe. Productivity is the game's raw measure (its scale is judged by the parity run).
	const bool bAny = FactoryFound.Num() > 0;
	UE_LOG(LogSatisDash, Display, TEXT("Factory read (game thread only): %d buildings in %d steps, %d machines, longest step %.2f ms, total %.2f ms, expected %d, boosted %d, productivity %.3f..%.3f"),
		FactoryCursor, Budget.Steps(), FactoryFound.Num(), Budget.LongestMs(), Budget.TotalMs(), FactoryExpected, FactoryBoosted,
		bAny ? FactoryProductivityMin : 0.0, bAny ? FactoryProductivityMax : 0.0);
	LogLongestStep(TEXT("Factory"), Budget);
	TArray<FFactoryReading> Done = MoveTemp(FactoryFound);
	ResetFactoryRead();
	return MoveTemp(Done);
}

namespace
{
	/** R3: a platform chain ends after this many hops, whatever the connections say (a cyclic or broken chain). */
	constexpr int32 TrainsMaxPlatformHops = 256;

	/** The distinct item class names in an inventory's non-empty stacks (Desc_IronPlate_C), added to Out. */
	void TrainsReadItems(const UFGInventoryComponent* Inventory, TArray<FString>& Out)
	{
		if (!IsValid(Inventory))
		{
			return;
		}
		TArray<FInventoryStack> Stacks;
		Inventory->GetInventoryStacks(Stacks);
		for (const FInventoryStack& Stack : Stacks)
		{
			const TSubclassOf<UFGItemDescriptor> ItemClass = Stack.Item.GetItemClass();
			if (Stack.NumItems > 0 && ItemClass)
			{
				Out.AddUnique(ItemClass->GetName());
			}
		}
	}

	SatisDash::ETrainState TrainsReadState(ETrainStatus Status)
	{
		switch (Status)
		{
		case ETrainStatus::TS_ManualDriving: return SatisDash::ETrainState::Manual;
		case ETrainStatus::TS_SelfDriving: return SatisDash::ETrainState::SelfDriving;
		case ETrainStatus::TS_Derailed: return SatisDash::ETrainState::Derailed;
		default: return SatisDash::ETrainState::Parked;
		}
	}

	/** One train, copied into a plain reading. Everything it touches is used inside this call and re-checked. */
	void TrainsReadTrain(const AFGTrain& Train, SatisDash::FTrainReading& Reading)
	{
		Reading.Id = Train.GetName();
		Reading.Name = Train.GetTrainName().ToString();
		Reading.State = TrainsReadState(Train.GetTrainStatus());
		// Which vehicle gives x,y is one constant (NativeTrainPositionSource); a train with no such vehicle is placed at its actor.
		const AFGRailroadVehicle* Vehicle = nullptr;
		switch (SatisDash::NativeTrainPositionSource)
		{
		case SatisDash::ETrainPositionSource::LastVehicle: Vehicle = Train.GetLastVehicle(); break;
		case SatisDash::ETrainPositionSource::FirstVehicle: Vehicle = Train.GetFirstVehicle(); break;
		default: break;
		}
		const FVector Location = IsValid(Vehicle) ? Vehicle->GetVehicleLocation() : Train.GetActorLocation();
		Reading.XCm = Location.X;
		Reading.YCm = Location.Y;
		// The timetable's stops, by station name exactly as the game gives it; a stop with no station is skipped.
		if (Train.HasTimeTable())
		{
			const AFGRailroadTimeTable* Table = Train.GetTimeTable();
			if (IsValid(Table))
			{
				TArray<FTimeTableStop> Stops;
				Table->GetStops(Stops);
				for (const FTimeTableStop& Stop : Stops)
				{
					if (IsValid(Stop.Station))
					{
						Reading.Stops.Add(Stop.Station->GetStationName().ToString());
					}
				}
			}
		}
		// The cargo: distinct item classes over the consist's freight wagons.
		for (const AFGRailroadVehicle* Each : Train.mConsistData.Vehicles)
		{
			const AFGFreightWagon* Wagon = Cast<AFGFreightWagon>(Each);
			if (IsValid(Wagon))
			{
				TrainsReadItems(Wagon->GetFreightInventory(), Reading.Items);
			}
		}
	}

	/** One station and its cargo and empty platforms (a bounded walk along the chain from the station's output). */
	void TrainsReadStation(AFGTrainStationIdentifier& Identifier, AFGBuildableRailroadStation& Building, SatisDash::FStationReading& Reading)
	{
		Reading.Id = Building.GetName();
		Reading.Name = Identifier.GetStationName().ToString();
		const FVector Location = Building.GetActorLocation();
		Reading.XCm = Location.X;
		Reading.YCm = Location.Y;
		TSet<const AFGBuildableTrainPlatform*> Visited;
		Visited.Add(&Building);
		UFGTrainPlatformConnection* Output = Building.GetStationOutputConnection();
		for (int32 Hop = 0; Hop < TrainsMaxPlatformHops && IsValid(Output); ++Hop)
		{
			UFGTrainPlatformConnection* Next = Output->GetConnectedTo();
			AFGBuildableTrainPlatform* Platform = IsValid(Next) ? Next->GetPlatformOwner() : nullptr;
			if (!IsValid(Platform) || Visited.Contains(Platform))
			{
				break; // the end of the chain, or a cycle
			}
			Visited.Add(Platform);
			if (const AFGBuildableTrainPlatformCargo* Cargo = Cast<AFGBuildableTrainPlatformCargo>(Platform))
			{
				SatisDash::FPlatformReading Entry;
				Entry.Id = Cargo->GetName();
				Entry.bLoad = Cargo->GetIsInLoadMode();
				TrainsReadItems(Cargo->GetInventory(), Entry.Items);
				Reading.Platforms.Add(MoveTemp(Entry));
			}
			else if (!Platform->IsA<AFGBuildableRailroadStation>())
			{
				// An empty platform: listed as unload with no items, as FRM's output lists Build_TrainPlatformEmpty_02_C
				// (run 40378567334). A second station in the chain stays out: no capture shows how FRM lists one.
				SatisDash::FPlatformReading Entry;
				Entry.Id = Platform->GetName();
				Entry.bLoad = false;
				Reading.Platforms.Add(MoveTemp(Entry));
			}
			Output = Platform->GetConnectionInOppositeDirection(Next);
		}
	}
}

void SatisDash::FGameReader::ResetTrainsRead()
{
	TrainsWorld.Reset();
	TrainsPending.Reset();
	StationsPending.Reset();
	TrainsCursor = 0;
	StationsCursor = 0;
	TrainsFound = FTrainsSnapshot();
	bTrainsOpen = false;
}

bool SatisDash::FGameReader::BeginTrainsRead()
{
	if (bTrainsOpen)
	{
		return false; // one cursor: the other consumer's read is still open
	}
	UWorld* Current = World.Get();
	const AFGRailroadSubsystem* Subsystem = Current != nullptr ? AFGRailroadSubsystem::Get(Current) : nullptr;
	if (Subsystem == nullptr)
	{
		return false;
	}
	ResetTrainsRead();
	bTrainsAbandoned = false;
	TrainsWorld = Current;
	// Dozens of pointers, copied once; the arrays themselves are never kept.
	TArray<AFGTrain*> Trains;
	Subsystem->GetAllTrains(Trains);
	for (AFGTrain* Train : Trains)
	{
		TrainsPending.Add(Train);
	}
	TArray<AFGTrainStationIdentifier*> Stations;
	Subsystem->GetAllTrainStations(Stations);
	for (AFGTrainStationIdentifier* Station : Stations)
	{
		StationsPending.Add(Station);
	}
	bTrainsOpen = true;
	return true;
}

TOptional<SatisDash::FTrainsSnapshot> SatisDash::FGameReader::StepTrainsRead(FStepBudget& Budget)
{
	UWorld* Current = World.Get();
	// A different save was loaded while the slices ran (or the world or its subsystem went away): what was read is the old world's
	// or a mix, so it is "cannot read" (empty and flagged), never a partial snapshot.
	const AFGRailroadSubsystem* Subsystem = Current != nullptr && Current == TrainsWorld.Get() ? AFGRailroadSubsystem::Get(Current) : nullptr;
	if (Subsystem == nullptr)
	{
		ResetTrainsRead();
		bTrainsAbandoned = true;
		return FTrainsSnapshot();
	}
	bool bFinished = false;
	Budget.Begin();
	do
	{
		if (TrainsCursor < TrainsPending.Num())
		{
			// Weak: a train that was dismantled since the read began is gone, and is skipped.
			const AFGTrain* Train = TrainsPending[TrainsCursor++].Get();
			if (IsValid(Train) && TrainsFound.Trains.Num() < TrainRoutesMaxItems)
			{
				FTrainReading Reading;
				TrainsReadTrain(*Train, Reading);
				TrainsFound.Trains.Add(MoveTemp(Reading));
			}
		}
		else if (StationsCursor < StationsPending.Num())
		{
			AFGTrainStationIdentifier* Identifier = StationsPending[StationsCursor++].Get();
			AFGBuildableRailroadStation* Building = IsValid(Identifier) ? Identifier->GetStation() : nullptr;
			if (IsValid(Building) && TrainsFound.Stations.Num() < TrainRoutesMaxItems)
			{
				FStationReading Reading;
				TrainsReadStation(*Identifier, *Building, Reading);
				TrainsFound.Stations.Add(MoveTemp(Reading));
			}
		}
		else
		{
			bFinished = true;
			break;
		}
	} while (!Budget.Spent());
	Budget.End(); // straight after the loop: nothing else is done on the game thread this step
	if (!bFinished)
	{
		return {};
	}
	FTrainsSnapshot Done = MoveTemp(TrainsFound);
	ResetTrainsRead();
	return MoveTemp(Done);
}

void SatisDash::FGameReader::ResetMapMarkersRead()
{
	MarkersWorld.Reset();
	MarkersCopy.Empty(); // frees the strings, not just the count
	MarkersCursor = 0;
	bMarkersCopied = false;
	MarkersFound = FMapMarkersSnapshot();
	bMarkersOpen = false;
}

bool SatisDash::FGameReader::BeginMapMarkersRead()
{
	if (bMarkersOpen)
	{
		return false;
	}
	UWorld* Current = World.Get();
	AFGMapManager* Manager = Current != nullptr ? AFGMapManager::Get(Current) : nullptr;
	if (Manager == nullptr)
	{
		return false;
	}
	ResetMapMarkersRead();
	bMarkersAbandoned = false;
	MarkersWorld = Current;
	bMarkersOpen = true;
	return true;
}

TOptional<SatisDash::FMapMarkersSnapshot> SatisDash::FGameReader::StepMapMarkersRead(FStepBudget& Budget)
{
	UWorld* Current = World.Get();
	// A different save was loaded while the slices ran (or the manager went away): "cannot read", empty and flagged.
	AFGMapManager* Manager = Current != nullptr && Current == MarkersWorld.Get() ? AFGMapManager::Get(Current) : nullptr;
	if (Manager == nullptr)
	{
		ResetMapMarkersRead();
		bMarkersAbandoned = true;
		return FMapMarkersSnapshot();
	}
	bool bFinished = false;
	Budget.Begin();
	if (!bMarkersCopied)
	{
		// The game's own cap is 250 markers; copied once, inside the first step so the budget times it, then sliced.
		Manager->GetMapMarkers(MarkersCopy);
		bMarkersCopied = true;
	}
	do
	{
		if (MarkersCursor < MarkersCopy.Num())
		{
			// Only these fields are read; the author fields (LastEditedBy, CreatedByPlayerID) never are.
			const FMapMarker& Marker = MarkersCopy[MarkersCursor++];
			FMapMarkerReading Reading;
			if (Marker.HasValidID())
			{
				Reading.Id = Marker.MarkerGUID.ToString(EGuidFormats::Digits).ToUpper();
			}
			else
			{
				++MarkersFound.Counts.Dropped; // the builder also leaves out an empty id, so the reading still goes on
			}
			Reading.Name = Marker.Name;
			Reading.Kind = Marker.MapMarkerType == ERepresentationType::RT_Stamp ? EMapMarkerKind::Stamp : EMapMarkerKind::Marker;
			Reading.Icon = Marker.IconID;
			Reading.Color = Marker.Color;
			Reading.XCm = Marker.Location.X;
			Reading.YCm = Marker.Location.Y;
			MarkersFound.Markers.Add(MoveTemp(Reading));
		}
		else
		{
			bFinished = true;
			break;
		}
	} while (!Budget.Spent());
	Budget.End(); // straight after the loop: nothing else is done on the game thread this step
	if (!bFinished)
	{
		return {};
	}
	FMapMarkersSnapshot Done = MoveTemp(MarkersFound);
	Done.Counts.Read = Done.Markers.Num();
	ResetMapMarkersRead();
	return MoveTemp(Done);
}

void SatisDash::FGameReader::ResetDroneRoutesRead()
{
	DroneWorld.Reset();
	DroneCursor = 0;
	DroneFound = FDroneRoutesSnapshot();
	bDroneOpen = false;
}

bool SatisDash::FGameReader::BeginDroneRoutesRead()
{
	if (bDroneOpen)
	{
		return false;
	}
	UWorld* Current = World.Get();
	AFGDroneSubsystem* Subsystem = Current != nullptr ? AFGDroneSubsystem::Get(Current) : nullptr;
	if (Subsystem == nullptr)
	{
		return false;
	}
	ResetDroneRoutesRead();
	bDroneAbandoned = false;
	DroneWorld = Current;
	bDroneOpen = true;
	return true;
}

TOptional<SatisDash::FDroneRoutesSnapshot> SatisDash::FGameReader::StepDroneRoutesRead(FStepBudget& Budget)
{
	UWorld* Current = World.Get();
	// A different save was loaded while the slices ran (or the subsystem went away): "cannot read", empty and flagged.
	AFGDroneSubsystem* Subsystem = Current != nullptr && Current == DroneWorld.Get() ? AFGDroneSubsystem::Get(Current) : nullptr;
	if (Subsystem == nullptr)
	{
		ResetDroneRoutesRead();
		bDroneAbandoned = true;
		return FDroneRoutesSnapshot();
	}
	bool bFinished = false;
	Budget.Begin();
	do
	{
		// The list is re-fetched for every station: nothing from it is kept between iterations or ticks.
		const TArray<AFGDroneStationInfo*>& Stations = Subsystem->GetAllStations();
		if (DroneCursor >= Stations.Num())
		{
			bFinished = true;
			break;
		}
		const AFGDroneStationInfo* Info = Stations[DroneCursor++];
		if (!IsValid(Info))
		{
			++DroneFound.Counts.Dropped; // an invalid info counts like a station that is gone
			continue;
		}
		const AFGBuildableDroneStation* Station = Info->GetStation(); // server-only; null on a client or when the building is gone
		if (!IsValid(Station))
		{
			++DroneFound.Counts.Dropped; // V6
			continue;
		}
		FDronePortReading Reading;
		Reading.Id = Station->GetName();
		Reading.Name = IFGBuildingTagInterface::Execute_GetBuildingTag(Info); // BlueprintNativeEvent: the generated wrapper, not _Implementation
		const AFGDroneStationInfo* Partner = Info->GetPairedStation();
		if (IsValid(Partner))
		{
			Reading.bPaired = true;
			Reading.PairedName = IFGBuildingTagInterface::Execute_GetBuildingTag(Partner);
			const AFGBuildableDroneStation* PartnerStation = Partner->GetStation();
			if (IsValid(PartnerStation))
			{
				Reading.PairedId = PartnerStation->GetName();
			}
		}
		// V9: the field comments (mInputInventory loads a drone, mOutputInventory unloads one) agree with the capture; the getter comments say the opposite.
		// If the parity judge reports input/output swapped, swap these two getters.
		TrainsReadItems(Station->GetInputInventory(), Reading.Input);
		TrainsReadItems(Station->GetOutputInventory(), Reading.Output);
		TrainsReadItems(Station->GetFuelInventory(), Reading.Fuel);
		// V10: the actor's location (the info's GetStationLocation() is the one-line flip). Nothing here is logged.
		const FVector Location = Station->GetActorLocation();
		Reading.XCm = Location.X;
		Reading.YCm = Location.Y;
		DroneFound.Ports.Add(MoveTemp(Reading));
	} while (!Budget.Spent());
	Budget.End(); // straight after the loop: nothing else is done on the game thread this step
	if (!bFinished)
	{
		return {};
	}
	FDroneRoutesSnapshot Done = MoveTemp(DroneFound);
	Done.Counts.Read = Done.Ports.Num();
	ResetDroneRoutesRead();
	return MoveTemp(Done);
}

namespace
{
	/** Tracks one logistics pass keeps: above the contract's cap (MaxRailSegments, 5,000) so the builder, not the reader, counts the overflow it drops. */
	constexpr int32 MaxLogisticsTracks = 2 * SatisDash::MaxRailSegments;
	/**
	 * Belts, lifts and pipelines one pass keeps: one past the contract's cap (ConveyorsMaxEdges). A layer over the cap is not posted
	 * anyway, so one more than the cap is enough to know it is over; the rest only go into Counts.Over (the owner's footprint goal).
	 */
	constexpr int32 MaxLogisticsConveyors = SatisDash::ConveyorsMaxEdges + 1;
	/** One past the cap, so the builder's own Over count sees the overflow; the extra wires are only counted. */
	constexpr int32 MaxLogisticsWires = SatisDash::PowerLinesMaxItems + 1;
	/** Differing class names the kind check keeps. */
	constexpr int32 ConvMaxDifferClasses = 8;

	/** Ruling S4: the end kind from the game's own class tree, in the table's order. Only ever compared with the name-based kind; never used for output. */
	SatisDash::EConveyorNodeKind ConvKindByCast(UObject* Owner)
	{
		using SatisDash::EConveyorNodeKind;
		if (Cast<AFGBuildableAttachmentSplitter>(Owner) != nullptr || Cast<AFGBuildableSplitterSmart>(Owner) != nullptr)
		{
			return EConveyorNodeKind::Splitter;
		}
		if (Cast<AFGBuildableAttachmentMerger>(Owner) != nullptr || Cast<AFGBuildableMergerPriority>(Owner) != nullptr)
		{
			return EConveyorNodeKind::Merger;
		}
		if (Cast<AFGBuildableConveyorBase>(Owner) != nullptr || Cast<AFGBuildablePipeline>(Owner) != nullptr)
		{
			return EConveyorNodeKind::Conveyor;
		}
		if (Cast<AFGBuildablePipelineJunction>(Owner) != nullptr || Cast<AFGBuildablePipelinePump>(Owner) != nullptr)
		{
			return EConveyorNodeKind::Junction;
		}
		if (Cast<AFGBuildableResourceExtractorBase>(Owner) != nullptr)
		{
			return EConveyorNodeKind::Extractor;
		}
		if (Cast<AFGBuildableGenerator>(Owner) != nullptr || Cast<AFGBuildableResourceSink>(Owner) != nullptr || Cast<AFGBuildableSpaceElevator>(Owner) != nullptr)
		{
			return EConveyorNodeKind::Consumer;
		}
		if (Cast<AFGBuildableStorage>(Owner) != nullptr || Cast<AFGBuildablePipeReservoir>(Owner) != nullptr)
		{
			return EConveyorNodeKind::Storage;
		}
		if (Cast<AFGBuildableDroneStation>(Owner) != nullptr || Cast<AFGBuildableTrainPlatformCargo>(Owner) != nullptr || Cast<AFGBuildableDockingStation>(Owner) != nullptr)
		{
			return EConveyorNodeKind::Station;
		}
		if (Cast<AFGBuildableManufacturer>(Owner) != nullptr)
		{
			return EConveyorNodeKind::Machine;
		}
		return EConveyorNodeKind::Other;
	}

	/**
	 * One end of a belt, lift or pipe. OutId stays empty for an open end (not connected, or the connected owner is gone: the latter is
	 * also counted ownerless). A connected end with an owner gets the owner's GetName(), the kind cross-check and the drone-port count.
	 */
	void ConvReadEnd(UObject* Owner, bool bConnected, int32 EndIndex, SatisDash::FLogisticsCounts& Counts, FString& OutId)
	{
		OutId.Reset();
		if (!bConnected)
		{
			++Counts.OpenEnds;
			return;
		}
		if (!IsValid(Owner))
		{
			++Counts.OwnerlessEnds;
			++Counts.OpenEnds;
			return;
		}
		OutId = Owner->GetName();
		const FString ClassName = Owner->GetClass()->GetName();
		++Counts.KindChecked;
		if (SatisDash::ClassifyConveyorEnd(OutId) != ConvKindByCast(Owner))
		{
			++Counts.KindDiffer;
			if (Counts.DifferClasses.Num() < ConvMaxDifferClasses && !Counts.DifferClasses.Contains(ClassName))
			{
				Counts.DifferClasses.Add(ClassName);
			}
		}
		if (ClassName.StartsWith(TEXT("Build_DroneStation")))
		{
			if (EndIndex == 1)
			{
				++Counts.EndsIntoPorts;
			}
			else
			{
				++Counts.EndsOutOfPorts;
			}
		}
	}

}

void SatisDash::FGameReader::ResetLogisticsRead()
{
	LogisticsWorld.Reset();
	LogisticsTracks.Empty();
	LogisticsBelts.Empty();
	LogisticsLifts.Empty();
	LogisticsPipes.Empty();
	LogisticsWires.Empty();
	LogisticsFound = FLogisticsSnapshot();
	LogisticsPhase = ELogisticsPhase::Collect;
	LogisticsCursor = 0;
	bLogisticsRails = false;
	bLogisticsConveyors = false;
	bLogisticsPowerLines = false;
	bLogisticsOpen = false;
}

bool SatisDash::FGameReader::BeginLogisticsRead(bool bRails, bool bConveyors, bool bPowerLines)
{
	if (bLogisticsOpen)
	{
		return false; // one cursor: a pass is still open
	}
	UWorld* Current = World.Get();
	const AFGBuildableSubsystem* Subsystem = Current != nullptr ? AFGBuildableSubsystem::Get(Current) : nullptr;
	if (Subsystem == nullptr)
	{
		return false;
	}
	ResetLogisticsRead();
	bLogisticsAbandoned = false;
	LogisticsWorld = Current;
	bLogisticsRails = bRails;
	bLogisticsConveyors = bConveyors;
	bLogisticsPowerLines = bPowerLines;
	if (bRails)
	{
		// Advisory only (the subsystem's own count of tracks, logged next to what was read).
		LogisticsFound.Counts.ExpectedTracks = Subsystem->GetBuildableCount(AFGBuildableRailroadTrack::StaticClass());
	}
	if (bPowerLines)
	{
		// Advisory too: the class is Abstract, so subclasses are not assumed to be counted. The sender compares it with what was read.
		LogisticsFound.Counts.ExpectedWires = Subsystem->GetBuildableCount(AFGBuildableWire::StaticClass());
	}
	bLogisticsOpen = true;
	return true;
}

TOptional<SatisDash::FLogisticsSnapshot> SatisDash::FGameReader::StepLogisticsRead(FStepBudget& Budget)
{
	if (!bLogisticsOpen)
	{
		return {};
	}
	UWorld* Current = World.Get();
	// A different save was loaded while the slices ran (or the world or its subsystem went away): what was read is the old world's
	// or a mix, so the pass is abandoned (empty and flagged), never a partial snapshot.
	const AFGBuildableSubsystem* Subsystem = Current != nullptr && Current == LogisticsWorld.Get() ? AFGBuildableSubsystem::Get(Current) : nullptr;
	if (Subsystem == nullptr)
	{
		ResetLogisticsRead();
		bLogisticsAbandoned = true;
		return FLogisticsSnapshot();
	}
	bool bFinished = false;
	// The next asked-for read phase after the given one (Collect, ReadRails, ReadConveyors, ReadPowerLines in that order), or the end.
	const auto nextPhase = [this, &bFinished, &Budget](ELogisticsPhase After)
	{
		LogisticsCursor = 0;
		if (After < ELogisticsPhase::ReadRails && bLogisticsRails)
		{
			LogisticsPhase = ELogisticsPhase::ReadRails;
		}
		else if (After < ELogisticsPhase::ReadConveyors && bLogisticsConveyors)
		{
			LogisticsPhase = ELogisticsPhase::ReadConveyors;
		}
		else if (After < ELogisticsPhase::ReadPowerLines && bLogisticsPowerLines)
		{
			LogisticsPhase = ELogisticsPhase::ReadPowerLines;
		}
		else
		{
			bFinished = true;
		}
		// The diagnostic's phase number (0 Collect, 1 ReadRails, 2 ReadConveyors, 3 ReadPowerLines): which phase a slow step was in.
		Budget.SetTag(int32(LogisticsPhase));
	};
	// Set before Begin(), which records it as the step's starting phase.
	Budget.SetTag(int32(LogisticsPhase));
	Budget.Begin();
	do
	{
		if (LogisticsPhase == ELogisticsPhase::Collect)
		{
			// Re-fetched for every element and never kept: buildables come and go between ticks.
			const TArray<AFGBuildable*>& All = Subsystem->GetAllBuildablesRef();
			if (LogisticsCursor >= All.Num())
			{
				nextPhase(ELogisticsPhase::Collect);
				if (bFinished)
				{
					break;
				}
				continue;
			}
			AFGBuildable* Building = All[LogisticsCursor++];
			++LogisticsFound.Counts.Buildables;
			if (!IsValid(Building))
			{
				continue;
			}
			if (bLogisticsRails)
			{
				if (AFGBuildableRailroadTrack* Track = Cast<AFGBuildableRailroadTrack>(Building))
				{
					if (LogisticsTracks.Num() < MaxLogisticsTracks)
					{
						LogisticsTracks.Add(Track);
					}
					else
					{
						++LogisticsFound.Counts.Over;
					}
				}
			}
			if (bLogisticsConveyors)
			{
				// Belts and lifts through the conveyor base; pipes through the pipeline class, never the pipe base (hypertubes, ruling S10).
				if (AFGBuildableConveyorBase* Conveyor = Cast<AFGBuildableConveyorBase>(Building))
				{
					const bool bLift = Cast<AFGBuildableConveyorLift>(Conveyor) != nullptr;
					if (bLift || Cast<AFGBuildableConveyorBelt>(Conveyor) != nullptr)
					{
						if (LogisticsBelts.Num() + LogisticsLifts.Num() + LogisticsPipes.Num() < MaxLogisticsConveyors)
						{
							(bLift ? LogisticsLifts : LogisticsBelts).Add(Conveyor);
						}
						else
						{
							++LogisticsFound.Counts.Over;
						}
					}
				}
				else if (AFGBuildablePipeline* Pipe = Cast<AFGBuildablePipeline>(Building))
				{
					if (LogisticsBelts.Num() + LogisticsLifts.Num() + LogisticsPipes.Num() < MaxLogisticsConveyors)
					{
						LogisticsPipes.Add(Pipe);
					}
					else
					{
						++LogisticsFound.Counts.Over;
					}
				}
			}
			if (bLogisticsPowerLines)
			{
				// The wire class is Abstract: Cast reaches the concrete wires. Past the cap the extra counts in Over, and in WiresDropped too so the cast-count check still sees it.
				if (AFGBuildableWire* Wire = Cast<AFGBuildableWire>(Building))
				{
					if (LogisticsWires.Num() < MaxLogisticsWires)
					{
						LogisticsWires.Add(Wire);
					}
					else
					{
						++LogisticsFound.Counts.Over;
						++LogisticsFound.Counts.WiresDropped;
					}
				}
			}
			continue;
		}
		if (LogisticsPhase == ELogisticsPhase::ReadConveyors)
		{
			// One weak pointer per iteration (belts, then lifts, then pipes), checked at every use.
			const int32 BeltCount = LogisticsBelts.Num();
			const int32 LiftCount = LogisticsLifts.Num();
			if (LogisticsCursor >= BeltCount + LiftCount + LogisticsPipes.Num())
			{
				nextPhase(ELogisticsPhase::ReadConveyors);
				if (bFinished)
				{
					break;
				}
				continue;
			}
			const int32 Slot = LogisticsCursor++;
			FLogisticsCounts& Counts = LogisticsFound.Counts;
			FConveyorReading Reading;
			if (Slot < BeltCount + LiftCount)
			{
				const bool bLift = Slot >= BeltCount;
				const AFGBuildableConveyorBase* Edge = (bLift ? LogisticsLifts[Slot - BeltCount] : LogisticsBelts[Slot]).Get();
				if (!IsValid(Edge))
				{
					continue;
				}
				const UFGFactoryConnectionComponent* C0 = Edge->GetConnection0();
				const UFGFactoryConnectionComponent* C1 = Edge->GetConnection1();
				if (!IsValid(C0))
				{
					++Counts.Dropped; // no end 0, so no position (ruling S8)
					continue;
				}
				Reading.Id = Edge->GetName();
				Reading.Kind = bLift ? EConveyorKind::Lift : EConveyorKind::Belt;
				Reading.Tier = Edge->mDisplayName.ToString();
				Reading.RawRate = Edge->GetSpeed();
				const FVector At = C0->GetConnectorLocation();
				Reading.XCm = At.X;
				Reading.YCm = At.Y;
				for (int32 End = 0; End < 2; ++End)
				{
					const UFGFactoryConnectionComponent* Conn = End == 0 ? C0 : C1;
					const bool bConnected = IsValid(Conn) && Conn->IsConnected();
					const UFGFactoryConnectionComponent* Other = bConnected ? Conn->GetConnection() : nullptr;
					// A connected end whose far side or owner is gone is ownerless, and open; a missing component is just open.
					ConvReadEnd(IsValid(Other) ? Other->GetOuterBuildable() : nullptr, bConnected, End, Counts, End == 0 ? Reading.FromId : Reading.ToId);
				}
				if (bLift)
				{
					++Counts.Lifts;
				}
				else
				{
					++Counts.Belts;
				}
			}
			else
			{
				const AFGBuildablePipeline* Edge = LogisticsPipes[Slot - BeltCount - LiftCount].Get();
				if (!IsValid(Edge))
				{
					continue;
				}
				const UFGPipeConnectionComponentBase* P0 = Edge->GetConnection0();
				const UFGPipeConnectionComponentBase* P1 = Edge->GetConnection1();
				if (!IsValid(P0))
				{
					++Counts.Dropped;
					continue;
				}
				Reading.Id = Edge->GetName();
				Reading.Kind = EConveyorKind::Pipe;
				Reading.Tier = Edge->mDisplayName.ToString();
				Reading.RawRate = Edge->GetFlowLimit();
				const FVector At = P0->GetComponentLocation();
				Reading.XCm = At.X;
				Reading.YCm = At.Y;
				for (int32 End = 0; End < 2; ++End)
				{
					const UFGPipeConnectionComponentBase* Conn = End == 0 ? P0 : P1;
					const bool bConnected = IsValid(Conn) && Conn->IsConnected();
					const UFGPipeConnectionComponentBase* Other = bConnected ? Conn->GetConnection() : nullptr;
					ConvReadEnd(IsValid(Other) ? Other->GetOwner() : nullptr, bConnected, End, Counts, End == 0 ? Reading.FromId : Reading.ToId);
				}
				++Counts.Pipes;
			}
			NoteConveyorTier(Counts.Tiers, Reading.Kind, Reading.Tier, Reading.RawRate);
			LogisticsFound.Conveyors.Add(MoveTemp(Reading));
			continue;
		}
		if (LogisticsPhase == ELogisticsPhase::ReadPowerLines)
		{
			// One wire per iteration, weak, checked at every use.
			if (LogisticsCursor >= LogisticsWires.Num())
			{
				bFinished = true;
				break;
			}
			const AFGBuildableWire* Wire = LogisticsWires[LogisticsCursor++].Get();
			if (!IsValid(Wire))
			{
				++LogisticsFound.Counts.Dropped; // gone between Collect and here: counted in both, as a null-end wire is
				++LogisticsFound.Counts.WiresDropped;
				continue;
			}
			// Both ends must be there (a dangling wire has no line to draw); counted, not an error (rulings R-a, Q8).
			if (!IsValid(Wire->GetConnection(0)) || !IsValid(Wire->GetConnection(1)))
			{
				++LogisticsFound.Counts.Dropped;
				++LogisticsFound.Counts.WiresDropped;
				continue;
			}
			const FVector L0 = Wire->GetConnectionLocation(0);
			const FVector L1 = Wire->GetConnectionLocation(1);
			FPowerLineReading Reading;
			Reading.Id = Wire->GetName();
			Reading.X0Cm = L0.X;
			Reading.Y0Cm = L0.Y;
			Reading.X1Cm = L1.X;
			Reading.Y1Cm = L1.Y;
			LogisticsFound.PowerLines.Add(MoveTemp(Reading));
			++LogisticsFound.Counts.Wires;
			continue;
		}
		// ReadRails: one track per iteration, weak, checked at every use.
		if (LogisticsCursor >= LogisticsTracks.Num())
		{
			nextPhase(ELogisticsPhase::ReadRails);
			if (bFinished)
			{
				break;
			}
			continue;
		}
		const AFGBuildableRailroadTrack* Track = LogisticsTracks[LogisticsCursor++].Get();
		if (!IsValid(Track))
		{
			continue;
		}
		double LengthCm = Track->GetLength();
		const USplineComponent* Spline = nullptr;
		if (NativeRailSampler == ERailSampler::SplineComponent)
		{
			Spline = Track->GetSplineComponent();
			if (!IsValid(Spline))
			{
				++LogisticsFound.Counts.Dropped;
				continue;
			}
			LengthCm = Spline->GetSplineLength();
		}
		// The engine never gets a NaN, infinite or negative distance.
		if (!FMath::IsFinite(LengthCm) || LengthCm <= 0.0)
		{
			++LogisticsFound.Counts.Dropped;
			continue;
		}
		const int32 Intervals = RailSampleIntervals(LengthCm);
		FRailReading Reading;
		Reading.Id = Track->GetName();
		Reading.Points.Reserve(Intervals + 1);
		for (int32 Index = 0; Index <= Intervals; ++Index)
		{
			const float DistanceCm = static_cast<float>(RailSampleDistanceCm(LengthCm, Intervals, Index));
			FVector Location = FVector::ZeroVector;
			if (Spline != nullptr)
			{
				Location = Spline->GetLocationAtDistanceAlongSpline(DistanceCm, ESplineCoordinateSpace::World);
			}
			else
			{
				FVector Direction = FVector::ZeroVector;
				Track->GetWorldLocationAndDirectionAtDistance(DistanceCm, Location, Direction);
			}
			Reading.Points.Add(FRailPointCm{Location.X, Location.Y});
		}
		LogisticsFound.Rails.Add(MoveTemp(Reading));
		++LogisticsFound.Counts.Tracks;
	} while (!Budget.Spent());
	Budget.End(); // straight after the loop: nothing else is done on the game thread this step
	if (!bFinished)
	{
		return {};
	}
	if (LogisticsFound.Counts.Buildables == 0)
	{
		// Ruling M5: a world with no buildables at all is "cannot read", not an empty railway.
		ResetLogisticsRead();
		bLogisticsAbandoned = true;
		return FLogisticsSnapshot();
	}
	FLogisticsSnapshot Done = MoveTemp(LogisticsFound);
	ResetLogisticsRead(); // closes the pass: the sender does not call EndLogisticsRead after a finished one
	return MoveTemp(Done);
}

namespace
{
	/**
	 * S4-B extractor rates, per minute (B4 / V1). The headers settle the inputs, not every unit:
	 *   Max     = items per cycle * 60 / cycle time, where the items per cycle is GetNumExtractedItemsPerCycle
	 *             (FGBuildableResourceExtractor.h:39) and the cycle time is GetProductionCycleTime (FGBuildableResourceExtractor.h:29,
	 *             FGBuildableFactory.h:197: "with potential multiplier applied", so it is the time at the current clock).
	 *   Current = Max * GetProductivity (FGBuildableFactory.h:213) / ProductivityFullScale (the factory read's constant), clamped to [0,1].
	 * A fluid's count is divided by 1000 (litres to m3), as every other fluid amount in this reader; the raw count is not in
	 * display units (GetNumExtractedItemsPerCycleConverted exists "for displaying in the UI", FGBuildableResourceExtractor.h:43).
	 * NEEDS VERIFICATION (the parity run judges, a failure goes to a ruling PR, never a weakened judge):
	 *   - purity: the headers do not say whether the cycle time already holds it. GetExtractionSpeedMultiplier
	 *     (FGExtractableResourceInterface.h:56) is a separate getter, but the extractor overrides OnExtractableResourceSet
	 *     (FGBuildableResourceExtractor.h:78), read as "the cycle time depends on the resource", so purity is NOT applied again here;
	 *   - fluids: whether mItemsPerCycle counts litres (assumed, as the slot amounts do).
	 */
	void BuildablesReadExtractorRates(const AFGBuildableResourceExtractor& Extractor, bool bFluid, double& OutCurrent, double& OutMax)
	{
		OutCurrent = 0;
		OutMax = 0;
		const double CycleSeconds = Extractor.GetProductionCycleTime();
		if (!(CycleSeconds > 0.0))
		{
			return;
		}
		const double Items = static_cast<double>(Extractor.GetNumExtractedItemsPerCycle()) / (bFluid ? 1000.0 : 1.0);
		OutMax = Items * 60.0 / CycleSeconds;
		OutCurrent = OutMax * FMath::Clamp(static_cast<double>(Extractor.GetProductivity()) / SatisDash::ProductivityFullScale, 0.0, 1.0);
	}

	/** V3: backed up is true only when the output has a slot that is full (a held stack at its GetSlotSize); a null inventory is not backed up. */
	bool BuildablesReadOutputFull(const UFGInventoryComponent* Output)
	{
		if (Output == nullptr)
		{
			return false;
		}
		const int32 Size = Output->GetSizeLinear();
		for (int32 Index = 0; Index < Size; ++Index)
		{
			FInventoryStack Stack;
			if (!Output->GetStackFromIndex(Index, Stack) || Stack.NumItems <= 0)
			{
				continue;
			}
			const TSubclassOf<UFGItemDescriptor> ItemClass = Stack.Item.GetItemClass();
			if (ItemClass && Stack.NumItems >= Output->GetSlotSize(Index, ItemClass))
			{
				return true;
			}
		}
		return false;
	}
}

void SatisDash::FGameReader::ResetBuildablesRead()
{
	BuildablesWorld.Reset();
	BuildablesAsked = FBuildablesFlags();
	BuildablesCursor = 0;
	bBuildablesOpen = false;
	BuildablesFound = FBuildablesSnapshot();
}

bool SatisDash::FGameReader::BeginBuildablesRead(const FBuildablesFlags& Asked)
{
	if (bBuildablesOpen)
	{
		return false; // one cursor: a pass is still open
	}
	UWorld* Current = World.Get();
	const AFGBuildableSubsystem* Subsystem = Current != nullptr ? AFGBuildableSubsystem::Get(Current) : nullptr;
	if (Subsystem == nullptr)
	{
		return false;
	}
	ResetBuildablesRead();
	bBuildablesAbandoned = false;
	BuildablesWorld = Current;
	BuildablesAsked = Asked;
	bBuildablesOpen = true;
	return true;
}

TOptional<SatisDash::FBuildablesSnapshot> SatisDash::FGameReader::StepBuildablesRead(FStepBudget& Budget)
{
	if (!bBuildablesOpen)
	{
		return {};
	}
	UWorld* Current = World.Get();
	// A different save was loaded while the slices ran (or the world or its subsystem went away): what was read is the old world's
	// or a mix, so the pass is abandoned (empty and flagged), never a partial snapshot.
	const AFGBuildableSubsystem* Subsystem = Current != nullptr && Current == BuildablesWorld.Get() ? AFGBuildableSubsystem::Get(Current) : nullptr;
	if (Subsystem == nullptr)
	{
		ResetBuildablesRead();
		bBuildablesAbandoned = true;
		return FBuildablesSnapshot();
	}
	// The list is kept bounded whatever the world holds (twice the fragment's own cap).
	const int32 MaxExtractors = 2 * ExtractorsMaxItems;
	bool bFinished = false;
	Budget.Begin();
	do
	{
		// Re-fetched for every element and never kept: buildables come and go between ticks.
		const TArray<AFGBuildable*>& All = Subsystem->GetAllBuildablesRef();
		if (BuildablesCursor >= All.Num())
		{
			bFinished = true;
			break;
		}
		const AFGBuildable* Buildable = All[BuildablesCursor++];
		if (Buildable == nullptr || !IsValid(Buildable))
		{
			continue;
		}
		++BuildablesFound.Counts.Elements;
		if (BuildablesAsked.bExtractors)
		{
			const AFGBuildableResourceExtractor* Extractor = Cast<AFGBuildableResourceExtractor>(Buildable);
			if (Extractor != nullptr && BuildablesFound.Extractors.Num() >= MaxExtractors)
			{
				++BuildablesFound.Counts.ExtractorsOverCap;
			}
			else if (Extractor != nullptr)
			{
				FExtractorReading Reading;
				Reading.Id = Extractor->GetName();
				Reading.Name = Extractor->mDisplayName.ToString();
				Reading.ClassName = Extractor->GetClass()->GetName();
				Reading.bProducing = Extractor->IsProducing();
				Reading.bPaused = Extractor->IsProductionPaused();
				Reading.ClockPercent = 100.0 * Extractor->GetCurrentPotential();
				const FVector Location = Extractor->GetActorLocation();
				Reading.XCm = Location.X;
				Reading.YCm = Location.Y;
				Reading.ZCm = Location.Z;
				Reading.YawDeg = Extractor->GetActorRotation().Yaw;
				// Any link in the chain can be missing (no power use, not connected): -1 is "no group".
				const UFGPowerInfoComponent* PowerInfo = Extractor->GetPowerInfo();
				const UFGPowerCircuit* Circuit = PowerInfo != nullptr ? PowerInfo->GetPowerCircuit() : nullptr;
				Reading.CircuitGroupId = Circuit != nullptr ? Circuit->GetCircuitGroupID() : -1;
				// An unoccupied extractor (or one whose resource is not a native interface) has no resource: empty class, Core omits production.
				// The object behind the interface is checked first: one pending kill is not yet collected, and its interface pointer is stale.
				const TScriptInterface<IFGExtractableResourceInterface> ResourceRef = Extractor->GetExtractableResource();
				const IFGExtractableResourceInterface* Resource = IsValid(ResourceRef.GetObject()) ? ResourceRef.GetInterface() : nullptr;
				const TSubclassOf<UFGResourceDescriptor> ResourceClass = Resource != nullptr ? Resource->GetResourceClass() : nullptr;
				if (ResourceClass)
				{
					Reading.ResourceClass = ResourceClass->GetName();
					Reading.ResourceName = UFGItemDescriptor::GetItemName(ResourceClass).ToString();
					Reading.bFluid = FactoryReadIsFluid(ResourceClass);
				}
				BuildablesReadExtractorRates(*Extractor, Reading.bFluid, Reading.CurrentPerMinute, Reading.MaxPerMinute);
				Reading.bBackedUp = BuildablesReadOutputFull(Extractor->GetOutputInventory());
				BuildablesFound.Extractors.Add(MoveTemp(Reading));
				++BuildablesFound.Counts.Extractors;
			}
		}
	} while (!Budget.Spent());
	Budget.End(); // straight after the loop: nothing else is done on the game thread this step
	if (!bFinished)
	{
		return {};
	}
	if (BuildablesFound.Counts.Elements == 0)
	{
		// Ruling M5: a world with no buildables at all is "cannot read" (a save still loading), not an empty factory.
		ResetBuildablesRead();
		bBuildablesAbandoned = true;
		return FBuildablesSnapshot();
	}
	// The driver owns the one log line of a finished pass (counts and times), as for trains.
	FBuildablesSnapshot Done = MoveTemp(BuildablesFound);
	ResetBuildablesRead(); // closes the pass: the driver does not call EndBuildablesRead after a finished one
	return MoveTemp(Done);
}
