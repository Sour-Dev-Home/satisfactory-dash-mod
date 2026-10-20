#pragma once

#include "CoreMinimal.h"
#include "Configuration/ModConfiguration.h"
#include "SatisModS4Config.generated.h"

// One string property, "credential". SML stores it as FactoryGame/Configs/SatisModS4.cfg (ConfigManager.cpp:309-318).
UCLASS()
class USatisModS4Config : public UModConfiguration
{
	GENERATED_BODY()
public:
	USatisModS4Config();
};
