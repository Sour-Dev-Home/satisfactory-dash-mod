#include "GameReader.h"

#include "Controller/FGServerManagementController.h"
#include "Controller/FGServerStateController.h"
#include "Engine/World.h"
#include "FGCircuitSubsystem.h"
#include "FGPowerCircuit.h"
#include "FGServerSubsystem.h"
#include "SatisDash/GameValues.h"

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
