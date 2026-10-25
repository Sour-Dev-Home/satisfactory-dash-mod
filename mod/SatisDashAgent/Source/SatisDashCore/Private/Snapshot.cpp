#include "SatisDash/Snapshot.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "SatisDash/JsonUtil.h"

FString SatisDash::ClampStringTo(const FString& Value, int32 MaxChars)
{
	if (MaxChars <= 0)
	{
		return FString();
	}
	if (Value.Len() <= MaxChars)
	{
		return Value;
	}
	int32 Keep = MaxChars;
	const TCHAR Last = Value[Keep - 1];
	if (Last >= 0xD800 && Last <= 0xDBFF)
	{
		--Keep; // a high surrogate whose pair would be cut off
	}
	return Value.Left(Keep);
}

FString SatisDash::ClampString(const FString& Value)
{
	return ClampStringTo(Value, MaxStringLength);
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
		// satisfactory-dash #621: when a change waits, autoPause already shows the new value and pending is true.
		Settings->SetBoolField(TEXT("autoPause"), EffectiveAutoPause(Snapshot.AutoPause.GetValue()));
		Settings->SetBoolField(TEXT("pending"), Snapshot.AutoPause->PendingValue.IsSet());
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
	FString Json = SatisDash::Json::Write(Root);
	// Parts that arrive as finished JSON, spliced in this order before the root's closing brace.
	struct FPart { const TCHAR* Key; const TOptional<FString>* Value; };
	const FPart Parts[] = {
		{ TEXT(",\"players\":"), &Snapshot.PlayersJson },
		{ TEXT(",\"factory\":"), &Snapshot.FactoryJson },
		{ TEXT(",\"mapLive\":"), &Snapshot.MapLiveJson },
	};
	constexpr int32 KeySlack = 16; // each key (",\"players\":") is shorter than this
	int32 Extra = 0;
	for (const FPart& Part : Parts)
	{
		Extra += Part.Value->IsSet() ? KeySlack + Part.Value->GetValue().Len() : 0;
	}
	if (Extra == 0)
	{
		return Json;
	}
	// The root always has fields, and the condensed writer ends it with '}' (pinned by Snapshot.Splice). Anything else
	// would be corrupted by the splice: send the snapshot without the parts instead.
	if (!Json.EndsWith(TEXT("}"), ESearchCase::CaseSensitive))
	{
		return Json;
	}
	Json.LeftChopInline(1);
	Json.Reserve(Json.Len() + 1 + Extra); // one allocation for the whole result, each piece appended on its own
	for (const FPart& Part : Parts)
	{
		if (Part.Value->IsSet())
		{
			Json += Part.Key;
			Json += Part.Value->GetValue();
		}
	}
	Json.AppendChar(TEXT('}'));
	return Json;
}
