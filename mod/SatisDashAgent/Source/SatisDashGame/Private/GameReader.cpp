#include "GameReader.h"

#include "Buildables/FGBuildableManufacturer.h"
#include "Controller/FGServerManagementController.h"
#include "Controller/FGServerStateController.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "FGBuildableSubsystem.h"
#include "FGCircuitSubsystem.h"
#include "FGGameState.h"
#include "FGInventoryComponent.h"
#include "FGPowerCircuit.h"
#include "FGPowerInfoComponent.h"
#include "FGRecipe.h"
#include "FGServerSubsystem.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Online/PlayerInfoCache.h"
#include "Resources/FGItemDescriptor.h"
#include "Resources/FGResourceNode.h"
#include "Resources/FGResourceDescriptor.h"
#include "Resources/FGResourceNodeBase.h"
#include "SatisDash/FrmResourceNodes.h"
#include "SatisDash/GameValues.h"
#include "SatisDash/Log.h"
#include "SatisDash/Native/Rates/NativeRates.h"

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
	ScanLongestStep = FMath::Max(ScanLongestStep, Spent);
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
	// it is "cannot read" (empty: nothing submitted), never a partial part.
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
		Rates.RawProductivity = Machine->GetProductivity();
		Rates.bProducing = Reading.bProducing;
		NativeRates(Rates, DefaultRatesMode, Reading.Production, Reading.Ingredients);
		Reading.ClockPercent = 100.0 * Rates.Potential;
		FactoryProductivityMin = FMath::Min(FactoryProductivityMin, Rates.RawProductivity);
		FactoryProductivityMax = FMath::Max(FactoryProductivityMax, Rates.RawProductivity);
		FactoryBoosted += Machine->GetCurrentProductionBoost() > 1.0f ? 1 : 0;
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
	TArray<FFactoryReading> Done = MoveTemp(FactoryFound);
	ResetFactoryRead();
	return MoveTemp(Done);
}
