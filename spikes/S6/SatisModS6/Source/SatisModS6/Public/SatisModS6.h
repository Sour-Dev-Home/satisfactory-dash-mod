#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Modules/ModuleManager.h"

class UWorld;

class FSatisModS6Module : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void LogStatus();

	TWeakObjectPtr<UWorld> World;
	FDelegateHandle WorldInitHandle;
	FTSTicker::FDelegateHandle EveryHandle;
	int32 Logged = 0;
	static constexpr int32 Total = 15; // every 10 s for 2.5 min, per game world
};
