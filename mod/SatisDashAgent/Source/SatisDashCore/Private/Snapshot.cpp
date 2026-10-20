#include "SatisDash/Snapshot.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "SatisDash/JsonUtil.h"

FString SatisDash::ClampString(const FString& Value)
{
	if (Value.Len() <= MaxStringLength)
	{
		return Value;
	}
	int32 Keep = MaxStringLength;
	const TCHAR Last = Value[Keep - 1];
	if (Last >= 0xD800 && Last <= 0xDBFF)
	{
		--Keep; // a high surrogate whose pair would be cut off
	}
	return Value.Left(Keep);
}

double SatisDash::ClampFinite(double Value)
{
	return FMath::IsFinite(Value) ? Value : 0.0;
}

double SatisDash::ClampNonNegative(double Value)
{
	return FMath::Max(0.0, ClampFinite(Value));
}

SatisDash::FStatusReading SatisDash::ConformStatus(const FStatusReading& In)
{
	FStatusReading Out = In;
	Out.SessionName = ClampString(In.SessionName);
	Out.ConnectedPlayers = FMath::Max(0, In.ConnectedPlayers);
	Out.PlayerLimit = FMath::Max(0, In.PlayerLimit);
	Out.TickRate = ClampNonNegative(In.TickRate);
	Out.TotalGameDurationSeconds = ClampNonNegative(In.TotalGameDurationSeconds);
	return Out;
}

SatisDash::FCircuitReading SatisDash::ConformCircuit(const FCircuitReading& In)
{
	FCircuitReading Out = In;
	Out.ProductionMW = ClampNonNegative(In.ProductionMW);
	Out.ConsumptionMW = ClampNonNegative(In.ConsumptionMW);
	Out.CapacityMW = ClampNonNegative(In.CapacityMW);
	Out.MaxConsumptionMW = ClampNonNegative(In.MaxConsumptionMW);
	Out.BatteryCapacityMWh = ClampNonNegative(In.BatteryCapacityMWh);
	Out.BatteryPercent = FMath::Min(100.0, ClampNonNegative(In.BatteryPercent));
	Out.BatteryDifferentialMW = ClampFinite(In.BatteryDifferentialMW);
	return Out;
}

namespace
{
	TSharedRef<FJsonObject> StatusToJson(const SatisDash::FStatusReading& S)
	{
		const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("tickHealth"), S.bTickHealthy ? TEXT("healthy") : TEXT("slow"));
		O->SetBoolField(TEXT("isGameRunning"), S.bIsGameRunning);
		O->SetBoolField(TEXT("gamePaused"), S.bGamePaused);
		O->SetStringField(TEXT("sessionName"), S.SessionName);
		O->SetNumberField(TEXT("connectedPlayers"), S.ConnectedPlayers);
		O->SetNumberField(TEXT("playerLimit"), S.PlayerLimit);
		O->SetNumberField(TEXT("tickRate"), S.TickRate);
		O->SetNumberField(TEXT("totalGameDurationSeconds"), S.TotalGameDurationSeconds);
		return O;
	}

	TSharedRef<FJsonObject> CircuitToJson(const SatisDash::FCircuitReading& C)
	{
		const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("circuitGroupId"), C.CircuitGroupId);
		O->SetNumberField(TEXT("productionMW"), C.ProductionMW);
		O->SetNumberField(TEXT("consumptionMW"), C.ConsumptionMW);
		O->SetNumberField(TEXT("capacityMW"), C.CapacityMW);
		O->SetNumberField(TEXT("maxConsumptionMW"), C.MaxConsumptionMW);
		O->SetBoolField(TEXT("fuseTriggered"), C.bFuseTriggered);
		O->SetNumberField(TEXT("batteryCapacityMWh"), C.BatteryCapacityMWh);
		O->SetNumberField(TEXT("batteryPercent"), C.BatteryPercent);
		O->SetNumberField(TEXT("batteryDifferentialMW"), C.BatteryDifferentialMW);
		return O;
	}
}

FString SatisDash::SnapshotToJson(const FSnapshot& Snapshot)
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("agentVersion"), Snapshot.AgentVersion);
	Root->SetStringField(TEXT("observedAt"), Snapshot.ObservedAt.ToIso8601());
	Root->SetBoolField(TEXT("reachable"), true);
	if (Snapshot.Status.IsSet())
	{
		Root->SetBoolField(TEXT("paused"), Snapshot.Status->bGamePaused);
	}
	else
	{
		Root->SetField(TEXT("paused"), MakeShared<FJsonValueNull>());
	}
	if (Snapshot.AutoPause.IsSet())
	{
		const TSharedRef<FJsonObject> Settings = MakeShared<FJsonObject>();
		Settings->SetBoolField(TEXT("autoPause"), Snapshot.AutoPause.GetValue());
		Root->SetObjectField(TEXT("settings"), Settings);
	}
	if (Snapshot.Status.IsSet())
	{
		Root->SetObjectField(TEXT("status"), StatusToJson(ConformStatus(Snapshot.Status.GetValue())));
	}
	if (Snapshot.Power.IsSet())
	{
		TArray<TSharedPtr<FJsonValue>> Circuits;
		const int32 Count = FMath::Min(Snapshot.Power->Num(), MaxCircuits);
		for (int32 I = 0; I < Count; ++I)
		{
			Circuits.Add(MakeShared<FJsonValueObject>(CircuitToJson(ConformCircuit((*Snapshot.Power)[I]))));
		}
		const TSharedRef<FJsonObject> Power = MakeShared<FJsonObject>();
		Power->SetArrayField(TEXT("circuits"), Circuits);
		Root->SetObjectField(TEXT("power"), Power);
	}
	return SatisDash::Json::Write(Root);
}
