#pragma once

#include "CoreMinimal.h"

namespace SatisDash
{
	/** What the mod sends as agentVersion: "mod/<version>", at most 32 characters (spec "The contract it speaks"). */
	SATISDASHCORE_API FString AgentVersion();
}
