#include "SatisModS0.h"

// Our own category: the game's config filters LogTemp to Warning ([Core.Log] LogTemp=all Warning).
DEFINE_LOG_CATEGORY_STATIC(LogSatisMod, Display, All);

void FSatisModS0Module::StartupModule()
{
	// The harness's S0 pass condition looks for this exact text in this run's FactoryGame.log.
	UE_LOG(LogSatisMod, Display, TEXT("satis-mod loaded"));
}

IMPLEMENT_MODULE(FSatisModS0Module, SatisModS0)
