#pragma once

#include "CoreMinimal.h"

namespace SatisDash
{
	/**
	 * What the mod sends as agentVersion: "mod/<version>", at most 32 characters (spec "The contract it speaks"). The
	 * version is SatisDashAgent.uplugin's VersionName ("mod/unknown" if the plugin can't be found).
	 */
	SATISDASHCORE_API FString AgentVersion();
}
