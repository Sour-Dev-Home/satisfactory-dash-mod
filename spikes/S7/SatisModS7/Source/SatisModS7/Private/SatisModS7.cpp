#include "SatisModS7.h"
#include "Engine/World.h"
#include "FGServerSubsystem.h"
#include "Controller/FGServerStateController.h"
#include "Controller/FGServerManagementController.h"

DEFINE_LOG_CATEGORY_STATIC(LogSatisMod, Display, All);

namespace
{
	// The option's name in GetServerOptions and ApplyServerOptions (harness/src/frm.ts:73, :80).
	const TCHAR* const AutoPauseOption = TEXT("FG.DSAutoPause");
}

void FSatisModS7Module::StartupModule()
{
	UE_LOG(LogSatisMod, Display, TEXT("satis-mod loaded"));
	WorldInitHandle = FWorldDelegates::OnPostWorldInitialization.AddLambda([this](UWorld* InWorld, const UWorld::InitializationValues)
	{
		if (InWorld == nullptr || !InWorld->IsGameWorld()) { return; }
		// Only the loaded save matters here; the entry map's world is replaced before the first step runs.
		World = InWorld;
		Step = 0;
		Original.Reset();
		FTSTicker::RemoveTicker(FirstHandle);
		FTSTicker::RemoveTicker(EveryHandle);
		// 60 s for the harness to record the value first, then one step every 15 s so it can read the API between them.
		FirstHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("SatisModS7First"), 60.0f, [this](float)
		{
			if (RunStep())
			{
				EveryHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("SatisModS7Every15s"), 15.0f, [this](float) { return RunStep(); });
			}
			return false;
		});
	});
}

void FSatisModS7Module::ShutdownModule()
{
	FWorldDelegates::OnPostWorldInitialization.Remove(WorldInitHandle);
	FTSTicker::RemoveTicker(FirstHandle);
	FTSTicker::RemoveTicker(EveryHandle);
}

bool FSatisModS7Module::RunStep()
{
	UFGServerSubsystem* Server = World.IsValid() ? UFGServerSubsystem::Get(World.Get()) : nullptr;
	UFGServerAPIManager* Manager = Server != nullptr ? Server->GetServerAPIManager() : nullptr;
	if (Manager == nullptr)
	{
		UE_LOG(LogSatisMod, Display, TEXT("satis-mod s7 error=no-api-manager"));
		UE_LOG(LogSatisMod, Display, TEXT("satis-mod s7 done"));
		return false;
	}
	switch (Step++)
	{
	case 0:
		Original = Read(Manager, TEXT("read"));
		if (Original.IsEmpty())
		{
			UE_LOG(LogSatisMod, Display, TEXT("satis-mod s7 done"));
			return false;
		}
		return true;
	case 1:
		Apply(Manager, Original == TEXT("True") ? TEXT("False") : TEXT("True"), TEXT("set"));
		return true;
	case 2:
		Read(Manager, TEXT("check"));
		return true;
	case 3:
		Apply(Manager, Original, TEXT("restore"));
		return true;
	default:
		Read(Manager, TEXT("final"));
		UE_LOG(LogSatisMod, Display, TEXT("satis-mod s7 done"));
		return false;
	}
}

FString FSatisModS7Module::Read(UFGServerAPIManager* Manager, const TCHAR* StepName) const
{
	// The game's own GetServerOptions handler, in-process. The options also hold FRM's token: only ours is touched.
	TMap<FString, FString> Options;
	TMap<FString, FString> Pending;
	NewObject<UFGServerStateController>(Manager)->Handler_GetServerOptions(Options, Pending);
	const FString* Value = Options.Find(AutoPauseOption);
	if (Value == nullptr || (*Value != TEXT("True") && *Value != TEXT("False")))
	{
		UE_LOG(LogSatisMod, Display, TEXT("satis-mod s7 error=no-option step=%s"), StepName);
		return FString();
	}
	UE_LOG(LogSatisMod, Display, TEXT("satis-mod s7 step=%s value=%s pending=%d"), StepName, **Value, Pending.Contains(AutoPauseOption) ? 1 : 0);
	return *Value;
}

void FSatisModS7Module::Apply(UFGServerAPIManager* Manager, const FString& Value, const TCHAR* StepName) const
{
	TMap<FString, FString> Updated;
	Updated.Add(AutoPauseOption, Value);
	NewObject<UFGServerManagementController>(Manager)->Handler_ApplyServerOptions(Updated);
	UE_LOG(LogSatisMod, Display, TEXT("satis-mod s7 step=%s value=%s"), StepName, *Value);
}

IMPLEMENT_MODULE(FSatisModS7Module, SatisModS7)
