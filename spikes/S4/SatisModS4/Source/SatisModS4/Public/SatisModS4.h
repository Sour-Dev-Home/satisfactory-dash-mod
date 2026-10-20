#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class UWorld;

class FSatisModS4Module : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void ReadCredential(UWorld* World);

	FDelegateHandle WorldInitHandle;
	bool bRead = false;
};
