#pragma once

#include "CoreMinimal.h"
#include "Configuration/ModConfiguration.h"
#include "SatisModS5Config.generated.h"

// Two string properties, "code" and "backendUrl". SML stores them as FactoryGame/Configs/SatisModS5.cfg (ConfigManager.cpp:309-318).
UCLASS()
class USatisModS5Config : public UModConfiguration
{
	GENERATED_BODY()
public:
	USatisModS5Config();
};
