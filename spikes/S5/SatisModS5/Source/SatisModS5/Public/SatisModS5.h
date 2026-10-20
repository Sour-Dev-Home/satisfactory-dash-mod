#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class UWorld;

class FSatisModS5Module : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void Enrol(UWorld* World);

	FDelegateHandle WorldInitHandle;
	bool bStarted = false;
};
