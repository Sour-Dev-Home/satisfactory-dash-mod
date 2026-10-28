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
	FSnapshotSizeReport Report;
	return SnapshotToJson(Snapshot, Report, SnapshotMaxBytes);
}

FString SatisDash::SnapshotToJson(const FSnapshot& Snapshot, FSnapshotSizeReport& Report, int64 MaxBytes)
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("agentVersion"), Snapshot.AgentVersion);
	Root->SetStringField(TEXT("observedAt"), Snapshot.ObservedAt.ToIso8601());
	Root->SetBoolField(TEXT("reachable"), true);
	// S4-A (R7): top level, beside reachable, never inside status (absent whenever the game can't be read).
	if (Snapshot.bGameReachable.IsSet())
	{
		Root->SetBoolField(TEXT("gameReachable"), Snapshot.bGameReachable.GetValue());
	}
	if (Snapshot.LastGameReadAt.IsSet())
	{
		Root->SetStringField(TEXT("lastGameReadAt"), Snapshot.LastGameReadAt->ToIso8601());
	}
	if (!Snapshot.ModVersion.IsEmpty())
	{
		Root->SetStringField(TEXT("modVersion"), ClampStringTo(Snapshot.ModVersion, 64)); // SENDER_VERSION_MAX
	}
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
	TOptional<FString> PowerJson; // the power object with generators, spliced as a part (only when there are generators)
	int32 PowerBaseLen = 0; // the chars of PowerJson before ,"generators": (the power object without its closing brace)
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
		if (Snapshot.GeneratorsJson.IsSet())
		{
			// power is the root's last field, so it can be written apart and spliced first among the parts with the same bytes
			// around it; the generators array goes in before its closing brace.
			PowerJson = SatisDash::Json::Write(Power);
			if (PowerJson->EndsWith(TEXT("}"), ESearchCase::CaseSensitive))
			{
				PowerJson->LeftChopInline(1);
				PowerBaseLen = PowerJson->Len();
				PowerJson->Append(TEXT(",\"generators\":"));
				PowerJson->Append(Snapshot.GeneratorsJson.GetValue());
				PowerJson->AppendChar(TEXT('}'));
			}
			else
			{
				PowerJson.Reset(); // not the condensed shape: the generators are left out
			}
		}
		if (!PowerJson.IsSet())
		{
			Root->SetObjectField(TEXT("power"), Power);
		}
	}
	FString Json = SatisDash::Json::Write(Root);
	// Parts that arrive as finished JSON, spliced in this order before the root's closing brace.
	// Text is null when the part is absent; Len is how much of it is written, and bClose adds the part object's closing brace
	// (a part cut by the size rule: power without generators, factory without extractors).
	struct FPart { const TCHAR* Key; const FString* Text; int32 Len; bool bClose; };
	const auto makePart =[](const TCHAR* Key, const TOptional<FString>& Value) { return FPart{ Key, Value.IsSet() ? &Value.GetValue() : nullptr, Value.IsSet() ? Value.GetValue().Len() : 0, false }; };
	// Storage is a data part: never written when the status read failed (the dash refuses it with reachable false).
	const TOptional<FString> NoPart;
	FPart Parts[] = {
		makePart(TEXT(",\"power\":"), PowerJson),
		makePart(TEXT(",\"players\":"), Snapshot.PlayersJson),
		makePart(TEXT(",\"factory\":"), Snapshot.FactoryJson),
		makePart(TEXT(",\"mapLive\":"), Snapshot.MapLiveJson),
		makePart(TEXT(",\"storage\":"), Snapshot.Status.IsSet() ? Snapshot.StorageJson : NoPart),
	};
	constexpr int32 PowerAt = 0, FactoryAt = 2, StorageAt = 4;
	constexpr int32 KeySlack = 16; // each key (",\"players\":") is shorter than this
	const auto extraUnits = [&Parts]
	{
		int32 Sum = 0;
		for (const FPart& Part : Parts)
		{
			Sum += Part.Text ? KeySlack + Part.Len + (Part.bClose ? 1 : 0) : 0;
		}
		return Sum;
	};
	Report = FSnapshotSizeReport();
	if (extraUnits() == 0)
	{
		Report.BytesBefore = Report.BytesAfter = Json.Len();
		return Json;
	}
	// The root always has fields, and the condensed writer ends it with '}' (pinned by Snapshot.Splice). Anything else
	// would be corrupted by the splice: send the snapshot without the parts instead.
	if (!Json.EndsWith(TEXT("}"), ESearchCase::CaseSensitive))
	{
		Report.BytesBefore = Report.BytesAfter = Json.Len();
		return Json;
	}
	// The assembled size in UTF-8 bytes. A UTF-16 unit is at most 3 bytes, so a body of at most MaxBytes / 3 units fits without
	// counting; a larger one is counted exactly (a pass over the text, on the worker for the big parts).
	const auto utf8Bytes = [](const TCHAR* Text, int32 Len) -> int64 { return Len > 0 ? FTCHARToUTF8_Convert::ConvertedLength(Text, Len) : 0; };
	const auto measure = [&]() -> int64
	{
		const int64 Units = int64(Json.Len()) + extraUnits();
		if (Units * 3 <= MaxBytes)
		{
			return Units; // only an upper bound, which is all the rule needs
		}
		int64 Sum = utf8Bytes(*Json, Json.Len());
		for (const FPart& Part : Parts)
		{
			Sum += Part.Text ? FCString::Strlen(Part.Key) + utf8Bytes(**Part.Text, Part.Len) + (Part.bClose ? 1 : 0) : 0;
		}
		return Sum;
	};
	int64 Total = measure();
	Report.BytesBefore = Total;
	// Ruling (Open question 7): over the cap, storage goes first, then power.generators, then factory.extractors; each is a whole part.
	if (Total > MaxBytes && Parts[StorageAt].Text)
	{
		Parts[StorageAt].Text = nullptr;
		Report.bDroppedStorage = true;
		Total = measure();
	}
	if (Total > MaxBytes && Parts[PowerAt].Text && PowerBaseLen > 0)
	{
		Parts[PowerAt].Len = PowerBaseLen;
		Parts[PowerAt].bClose = true;
		Report.bDroppedGenerators = true;
		Total = measure();
	}
	if (Total > MaxBytes && Parts[FactoryAt].Text && Parts[FactoryAt].Text->EndsWith(TEXT("}"), ESearchCase::CaseSensitive))
	{
		// The extractors were appended last, before the part's closing brace; a raw quote cannot occur inside a JSON string, so the
		// last ,"extractors": is the key. A factory part without one (a bridge part) is left alone.
		const int32 At = Parts[FactoryAt].Text->Find(TEXT(",\"extractors\":"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (At != INDEX_NONE)
		{
			Parts[FactoryAt].Len = At;
			Parts[FactoryAt].bClose = true;
			Report.bDroppedExtractors = true;
			Total = measure();
		}
	}
	Report.BytesAfter = Total;
	Json.LeftChopInline(1);
	Json.Reserve(Json.Len() + 1 + extraUnits()); // one allocation for the whole result, each piece appended on its own
	for (const FPart& Part : Parts)
	{
		if (Part.Text)
		{
			Json += Part.Key;
			Json.AppendChars(**Part.Text, Part.Len);
			if (Part.bClose)
			{
				Json.AppendChar(TEXT('}'));
			}
		}
	}
	Json.AppendChar(TEXT('}'));
	return Json;
}
