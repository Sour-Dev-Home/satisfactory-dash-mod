#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Modules/ModuleManager.h"

class UWorld;
class UFGServerAPIManager;

class FSatisModS7Module : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** Runs the next step; false when there are no more. */
	bool RunStep();
	FString Read(UFGServerAPIManager* Manager, const TCHAR* StepName) const;
	void Apply(UFGServerAPIManager* Manager, const FString& Value, const TCHAR* StepName) const;

	TWeakObjectPtr<UWorld> World;
	FDelegateHandle WorldInitHandle;
	FTSTicker::FDelegateHandle FirstHandle;
	FTSTicker::FDelegateHandle EveryHandle;
	int32 Step = 0;
	FString Original;
};
