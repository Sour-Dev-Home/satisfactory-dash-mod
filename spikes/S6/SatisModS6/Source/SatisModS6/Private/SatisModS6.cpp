#include "SatisModS6.h"
#include "Engine/World.h"
#include "FGServerSubsystem.h"
#include "Controller/FGServerStateController.h"
#include "Misc/SecureHash.h"

DEFINE_LOG_CATEGORY_STATIC(LogSatisMod, Display, All);

void FSatisModS6Module::StartupModule()
{
	UE_LOG(LogSatisMod, Display, TEXT("satis-mod loaded"));
	WorldInitHandle = FWorldDelegates::OnPostWorldInitialization.AddLambda([this](UWorld* InWorld, const UWorld::InitializationValues)
	{
		if (InWorld == nullptr || !InWorld->IsGameWorld()) { return; }
		// The server boots into its empty entry map, then the harness loads the save: a new world. Each game world gets
		// its own readings, so the harness pairs some without a world and some with one.
		World = InWorld;
		Logged = 0;
		FTSTicker::RemoveTicker(EveryHandle);
		EveryHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("SatisModS6Every10s"), 10.0f, [this](float)
		{
			LogStatus();
			if (Logged < Total) { return true; }
			UE_LOG(LogSatisMod, Display, TEXT("satis-mod s6 world-done"));
			return false;
		});
	});
}

void FSatisModS6Module::ShutdownModule()
{
	FWorldDelegates::OnPostWorldInitialization.Remove(WorldInitHandle);
	FTSTicker::RemoveTicker(EveryHandle);
}

void FSatisModS6Module::LogStatus()
{
	++Logged;
	UFGServerSubsystem* Server = World.IsValid() ? UFGServerSubsystem::Get(World.Get()) : nullptr;
	UFGServerAPIManager* Manager = Server != nullptr ? Server->GetServerAPIManager() : nullptr;
	if (Manager == nullptr)
	{
		UE_LOG(LogSatisMod, Display, TEXT("satis-mod s6 error=no-api-manager"));
		return;
	}
	const FServerGameState State = Server->GetServerGameState();
	// HealthCheck's verdict has no getter, so the game's own handler computes it. A controller created under the API
	// manager finds the subsystem through its outers (FGServerControllerBase.h:17, FGServerAPIManager.h:183).
	FString Health;
	FString CustomData;
	NewObject<UFGServerStateController>(Manager)->Handler_HealthCheck(FString(), Health, CustomData);
	// Bundles are public: only a hash of the session name is logged, over UTF-8 as the harness hashes it.
	const FTCHARToUTF8 Utf8{*State.ActiveSessionName};
	FSHAHash Hash;
	FSHA1::HashBuffer(Utf8.Get(), Utf8.Length(), Hash.Hash);
	UE_LOG(LogSatisMod, Display, TEXT("satis-mod s6 running=%d paused=%d players=%d limit=%d duration=%d tick=%.2f health=%s session=%s"),
		State.IsGameRunning ? 1 : 0, State.IsGamePaused ? 1 : 0, State.NumConnectedPlayers, State.PlayerLimit,
		State.TotalGameDuration, State.AverageTickRate, *Health, *Hash.ToString().Left(12).ToLower());
}

IMPLEMENT_MODULE(FSatisModS6Module, SatisModS6)
