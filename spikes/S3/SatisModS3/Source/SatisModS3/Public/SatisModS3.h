#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Modules/ModuleManager.h"

class UWorld;

class FSatisModS3Module : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void LogCircuits();

	TWeakObjectPtr<UWorld> World;
	FDelegateHandle WorldInitHandle;
	FTSTicker::FDelegateHandle FirstHandle;
	FTSTicker::FDelegateHandle EveryHandle;
	int32 Logged = 0;
	static constexpr int32 Total = 12; // every 10 s for 2 min
};
