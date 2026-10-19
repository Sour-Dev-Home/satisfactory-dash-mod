#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Modules/ModuleManager.h"

class FSatisModS2Module : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	bool PostNext();

	static constexpr int32 Total = 40;
	int32 Sent = 0;
	FTSTicker::FDelegateHandle TickerHandle;
};
