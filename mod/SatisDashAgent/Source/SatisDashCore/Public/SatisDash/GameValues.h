#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Snapshot.h"

namespace SatisDash
{
	/** One UFGPowerCircuit as the game reports it (FGPowerCircuit.h), before grouping (ruling 7). */
	struct FPowerCircuitSample
	{
		int32 CircuitId = 0;
		/** UFGCircuit::GetCircuitGroupID(); negative while the circuit has no group. */
		int32 GroupId = 0;
		double ProducedMW = 0;
		double ConsumedMW = 0;
		double CapacityMW = 0;
		double MaxConsumptionMW = 0;
		bool bFuseTriggered = false;
		bool bHasBatteries = false;
		double BatteryStoreMWh = 0;
		double BatteryCapacityMWh = 0;
		/** Signed, positive while charging (the game's mBatterySumPowerInput). */
		double BatteryInputMW = 0;
	};

	/** Circuit IDs the reader tries, as S3 did (S3 ruling 5): the game keeps its circuit map private. */
	inline constexpr int32 MaxCircuitIdScanned = 10000;

	/** One reading per group, ordered by group id; the group's lowest circuit id speaks for it. At most MaxCircuits. */
	SATISDASHCORE_API TArray<FCircuitReading> GroupCircuits(const TArray<FPowerCircuitSample>& Samples);

	/** The option's name in GetServerOptions and ApplyServerOptions (S7). */
	inline const TCHAR* AutoPauseOption = TEXT("FG.DSAutoPause");
	/** Exactly "True" or "False"; anything else, or no value, is unread. */
	SATISDASHCORE_API TOptional<bool> ParseAutoPauseOption(const FString* Value);
	SATISDASHCORE_API FString AutoPauseOptionValue(bool bEnabled);

	/** HealthCheck's verdict: "healthy" or "slow" (S6). */
	SATISDASHCORE_API bool IsTickHealthy(const FString& Health);
}
