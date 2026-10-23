#include "SatisDash/Version.h"

#include "Interfaces/IPluginManager.h"

FString SatisDash::AgentVersion()
{
	// The version is written once, as the .uplugin's VersionName; read on first use, it can't change while loaded.
	static const FString Version = []
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SatisDashAgent"));
		return FString(TEXT("mod/")) + (Plugin.IsValid() ? Plugin->GetDescriptor().VersionName : FString(TEXT("unknown")));
	}();
	return Version;
}
