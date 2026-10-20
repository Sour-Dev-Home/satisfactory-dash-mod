#include "SatisDash/GameValues.h"

TArray<SatisDash::FCircuitReading> SatisDash::GroupCircuits(const TArray<FPowerCircuitSample>& Samples)
{
	TMap<int32, const FPowerCircuitSample*> Speaker;
	for (const FPowerCircuitSample& S : Samples)
	{
		if (S.GroupId < 0)
		{
			continue;
		}
		const FPowerCircuitSample** Found = Speaker.Find(S.GroupId);
		if (Found == nullptr || S.CircuitId < (*Found)->CircuitId)
		{
			Speaker.Add(S.GroupId, &S);
		}
	}
	TArray<int32> Groups;
	Speaker.GetKeys(Groups);
	Groups.Sort();
	TArray<FCircuitReading> Out;
	for (const int32 Group : Groups)
	{
		if (Out.Num() == MaxCircuits)
		{
			break;
		}
		const FPowerCircuitSample& S = *Speaker[Group];
		FCircuitReading R;
		R.CircuitGroupId = Group;
		R.ProductionMW = S.ProducedMW;
		R.ConsumptionMW = S.ConsumedMW;
		R.CapacityMW = S.CapacityMW;
		R.MaxConsumptionMW = S.MaxConsumptionMW;
		R.bFuseTriggered = S.bFuseTriggered;
		if (S.bHasBatteries)
		{
			R.BatteryCapacityMWh = S.BatteryCapacityMWh;
			R.BatteryPercent = S.BatteryCapacityMWh > 0 ? 100.0 * S.BatteryStoreMWh / S.BatteryCapacityMWh : 0.0;
			R.BatteryDifferentialMW = S.BatteryInputMW;
		}
		Out.Add(R);
	}
	return Out;
}

TOptional<bool> SatisDash::ParseAutoPauseOption(const FString* Value)
{
	if (Value == nullptr)
	{
		return {};
	}
	if (Value->Equals(TEXT("True"), ESearchCase::CaseSensitive))
	{
		return true;
	}
	if (Value->Equals(TEXT("False"), ESearchCase::CaseSensitive))
	{
		return false;
	}
	return {};
}

FString SatisDash::AutoPauseOptionValue(bool bEnabled)
{
	return bEnabled ? TEXT("True") : TEXT("False");
}

bool SatisDash::IsTickHealthy(const FString& Health)
{
	return Health.Equals(TEXT("healthy"), ESearchCase::CaseSensitive);
}
