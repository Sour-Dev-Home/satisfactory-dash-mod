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

TOptional<bool> SatisDash::FGameReader::ReadAutoPause()
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
	return ParseAutoPauseOption(Options.Find(AutoPauseOption));
}

bool SatisDash::FGameReader::ApplyAutoPause(bool bEnabled)
{
	UFGServerSubsystem* Subsystem = Server();
	UFGServerAPIManager* Manager = Subsystem != nullptr ? Subsystem->GetServerAPIManager() : nullptr;
	if (Manager == nullptr)
	{
		return false;
	}
	TMap<FString, FString> Updated;
	Updated.Add(AutoPauseOption, AutoPauseOptionValue(bEnabled));
	NewObject<UFGServerManagementController>(Manager)->Handler_ApplyServerOptions(Updated);
	// Handler_ApplyServerOptions returns nothing (FGServerManagementController.h:60): reading back is the only check.
	const TOptional<bool> Now = ReadAutoPause();
	return Now.IsSet() && Now.GetValue() == bEnabled;
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
