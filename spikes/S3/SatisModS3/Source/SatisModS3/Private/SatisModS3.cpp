#include "SatisModS3.h"
#include "Engine/World.h"
#include "FGCircuitSubsystem.h"
#include "FGPowerCircuit.h"

DEFINE_LOG_CATEGORY_STATIC(LogSatisMod, Display, All);

void FSatisModS3Module::StartupModule()
{
	UE_LOG(LogSatisMod, Display, TEXT("satis-mod loaded"));
	WorldInitHandle = FWorldDelegates::OnPostWorldInitialization.AddLambda([this](UWorld* InWorld, const UWorld::InitializationValues)
	{
		if (InWorld == nullptr || !InWorld->IsGameWorld()) { return; }
		// The server boots into its empty entry map; the harness then loads the save, which is a new world. Each new
		// game world starts the readings over, so the twelve readings come from the loaded save.
		World = InWorld;
		Logged = 0;
		FTSTicker::RemoveTicker(FirstHandle);
		FTSTicker::RemoveTicker(EveryHandle);
		// AddTicker's delay is also its interval: "return true to fire after another InDelay time" (Ticker.h:56). So one
		// ticker waits 60 s for the world to settle, logs once, and hands over to a second one that logs every 10 s.
		FirstHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("SatisModS3First"), 60.0f, [this](float)
		{
			LogCircuits();
			EveryHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("SatisModS3Every10s"), 10.0f, [this](float)
			{
				LogCircuits();
				return Logged < Total; // false after the 12th reading removes it
			});
			return false;
		});
	});
}

void FSatisModS3Module::ShutdownModule()
{
	FWorldDelegates::OnPostWorldInitialization.Remove(WorldInitHandle);
	FTSTicker::RemoveTicker(FirstHandle);
	FTSTicker::RemoveTicker(EveryHandle);
}

void FSatisModS3Module::LogCircuits()
{
	++Logged;
	AFGCircuitSubsystem* Circuits = World.IsValid() ? AFGCircuitSubsystem::GetCircuitSubsystem(World.Get()) : nullptr;
	if (Circuits == nullptr)
	{
		UE_LOG(LogSatisMod, Display, TEXT("satis-mod s3 error=no-circuit-subsystem"));
		return;
	}
	// mCircuits is private (FGCircuitSubsystem.h:248-251); FindCircuit<T> is public (:88). IDs are small ints (ruling 5).
	for (int32 Id = 0; Id < 10000; ++Id)
	{
		UFGPowerCircuit* Circuit = Circuits->FindCircuit<UFGPowerCircuit>(Id);
		if (Circuit == nullptr) { continue; }
		FPowerCircuitStats Stats;
		Circuit->GetStats(Stats);
		UE_LOG(LogSatisMod, Display, TEXT("satis-mod s3 circuit=%d production=%.2f consumption=%.2f capacity=%.2f"),
			Circuit->GetCircuitID(), Stats.PowerProduced, Stats.PowerConsumed, Stats.PowerProductionCapacity);
	}
}

IMPLEMENT_MODULE(FSatisModS3Module, SatisModS3)
