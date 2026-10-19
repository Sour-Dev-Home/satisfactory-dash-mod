#include "SatisModS0.h"

void FSatisModS0Module::StartupModule()
{
	// The harness's S0 pass condition looks for this exact text in this run's FactoryGame.log.
	UE_LOG(LogTemp, Display, TEXT("satis-mod loaded"));
}

IMPLEMENT_MODULE(FSatisModS0Module, SatisModS0)
