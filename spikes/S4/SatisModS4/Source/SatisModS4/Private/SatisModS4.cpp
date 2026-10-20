#include "SatisModS4.h"
#include "SatisModS4Config.h"
#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/ConfigPropertyString.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/SecureHash.h"

// Our own category: the game's config filters LogTemp to Warning ([Core.Log] LogTemp=all Warning).
DEFINE_LOG_CATEGORY_STATIC(LogSatisMod, Display, All);

void FSatisModS4Module::StartupModule()
{
	UE_LOG(LogSatisMod, Display, TEXT("satis-mod loaded"));
	// UConfigManager is a game-instance subsystem (ConfigManager.h:44), so read once the first world has one.
	WorldInitHandle = FWorldDelegates::OnPostWorldInitialization.AddLambda([this](UWorld* World, const UWorld::InitializationValues)
	{
		ReadCredential(World);
	});
}

void FSatisModS4Module::ShutdownModule()
{
	FWorldDelegates::OnPostWorldInitialization.Remove(WorldInitHandle);
}

void FSatisModS4Module::ReadCredential(UWorld* World)
{
	if (bRead || World == nullptr || World->GetGameInstance() == nullptr) { return; }
	UConfigManager* Configs = World->GetGameInstance()->GetSubsystem<UConfigManager>();
	if (Configs == nullptr) { return; }
	bRead = true;
	Configs->RegisterModConfiguration(USatisModS4Config::StaticClass()); // loads the file from disk (ConfigManager.cpp:287)
	UConfigPropertySection* Root = Configs->GetConfigurationRootSection(GetDefault<USatisModS4Config>()->ConfigId);
	UConfigPropertyString* Property = Root ? Cast<UConfigPropertyString>(Root->SectionProperties.FindRef(TEXT("credential"))) : nullptr;
	if (Property == nullptr)
	{
		UE_LOG(LogSatisMod, Display, TEXT("satis-mod s4 error=no-credential-property"));
		return;
	}
	// The value itself is never logged: only its length and a SHA-1 prefix the harness can check.
	const FTCHARToUTF8 Utf8{*Property->Value};
	const FString Prefix = FSHA1::HashBuffer(Utf8.Get(), Utf8.Length()).ToString().Left(12).ToLower();
	UE_LOG(LogSatisMod, Display, TEXT("satis-mod s4 read length=%d sha1=%s"), Property->Value.Len(), *Prefix);
}

IMPLEMENT_MODULE(FSatisModS4Module, SatisModS4)
