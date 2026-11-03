#include "SatisDash/Records.h"

#include "SatisDash/JsonUtil.h"
#include "SatisDash/Snapshot.h"

namespace
{
	/** JavaScript's \s: the ECMAScript WhiteSpace and LineTerminator characters. */
	bool RecordsIsJsSpace(TCHAR C)
	{
		switch (C)
		{
		case 0x0009: case 0x000A: case 0x000B: case 0x000C: case 0x000D: case 0x0020: case 0x00A0: case 0x1680:
		case 0x2028: case 0x2029: case 0x202F: case 0x205F: case 0x3000: case 0xFEFF:
			return true;
		default:
			return C >= 0x2000 && C <= 0x200A;
		}
	}
}

FString SatisDash::WorldBodyPrefix(const FString& ObservedAt)
{
	FString Prefix = TEXT("{\"observedAt\":");
	SatisDash::Json::AppendString(Prefix, ObservedAt);
	Prefix += TEXT(",\"data\":");
	return Prefix;
}

double SatisDash::JsRound(double Value)
{
	if (!FMath::IsFinite(Value))
	{
		return Value;
	}
	// value - floor(value) is exact, so the half test is exact too.
	const double Floor = FMath::FloorToDouble(Value);
	return Value - Floor >= 0.5 ? Floor + 1.0 : Floor;
}

double SatisDash::ToWholeMetres(double GameUnits)
{
	// UE builds with /fp:fast, which turns a division by the constant 100.0 into a multiplication by 0.01 and lands one ulp
	// away for some values (see FactoryMetres in FactoryRecords.cpp). The volatile divisor keeps the true division.
	volatile double Hundred = 100.0;
	return JsRound(GameUnits / Hundred);
}

bool SatisDash::InWholeMetreBounds(double WholeMetres)
{
	return FMath::IsFinite(WholeMetres) && FMath::Abs(WholeMetres) <= 1000000.0;
}

void SatisDash::AppendTrainRecord(FString& Out, const FString& Id, const FString& Name, double Mx, double My, const FString& Status)
{
	Out += TEXT("{\"id\":");
	SatisDash::Json::AppendString(Out, ClampString(Id));
	Out += TEXT(",\"name\":");
	SatisDash::Json::AppendString(Out, ClampString(Name));
	Out += TEXT(",\"x\":");
	Out.AppendInt(int32(Mx));
	Out += TEXT(",\"y\":");
	Out.AppendInt(int32(My));
	Out += TEXT(",\"status\":");
	SatisDash::Json::AppendString(Out, ClampString(Status));
	Out.AppendChar(TEXT('}'));
}

void SatisDash::AppendStationRecord(FString& Out, const FString& Id, const FString& Name, double Mx, double My)
{
	Out += TEXT("{\"id\":");
	SatisDash::Json::AppendString(Out, ClampString(Id));
	Out += TEXT(",\"name\":");
	SatisDash::Json::AppendString(Out, ClampString(Name));
	Out += TEXT(",\"x\":");
	Out.AppendInt(int32(Mx));
	Out += TEXT(",\"y\":");
	Out.AppendInt(int32(My));
	Out.AppendChar(TEXT('}'));
}

TOptional<FString> SatisDash::JoinMapLive(const FString& TrainsArray, int64 TrainsBytes, const FString& StationsArray, int64 StationsBytes)
{
	if (MapLiveWrapperBytes + TrainsBytes + StationsBytes > MapLiveMaxBytes)
	{
		return {};
	}
	FString Out;
	Out.Reserve(TrainsArray.Len() + StationsArray.Len() + 24);
	Out += TEXT("{\"trains\":");
	Out += TrainsArray;
	Out += TEXT(",\"stations\":");
	Out += StationsArray;
	Out.AppendChar(TEXT('}'));
	return Out;
}

FString SatisDash::MapNodeType(const FString& Raw)
{
	if (Raw.Equals(TEXT("Node"), ESearchCase::CaseSensitive))
	{
		return TEXT("node");
	}
	if (Raw.Equals(TEXT("Fracking Satellite"), ESearchCase::CaseSensitive))
	{
		return TEXT("frackingSatellite");
	}
	if (Raw.Equals(TEXT("Geyser"), ESearchCase::CaseSensitive))
	{
		return TEXT("geyser");
	}
	TArray<FString> Words;
	FString Word;
	for (const TCHAR C : Raw)
	{
		if (RecordsIsJsSpace(C))
		{
			if (!Word.IsEmpty())
			{
				Words.Add(Word);
				Word.Reset();
			}
		}
		else
		{
			Word.AppendChar(C);
		}
	}
	if (!Word.IsEmpty())
	{
		Words.Add(Word);
	}
	FString Out;
	for (int32 I = 0; I < Words.Num(); ++I)
	{
		Out += I == 0 ? Words[I].ToLower() : Words[I].Left(1).ToUpper() + Words[I].Mid(1).ToLower();
	}
	return Out;
}

void SatisDash::AppendResourceNodeRecord(FString& Out, const FString& Type, const FString& Purity, const FString& NodeType, double Mx, double My, bool bExploited, const FString& ClassName)
{
	Out += TEXT("{\"type\":");
	SatisDash::Json::AppendString(Out, ClampString(Type));
	Out += TEXT(",\"purity\":");
	SatisDash::Json::AppendString(Out, ClampStringTo(Purity.ToLower(), MaxNodeFieldChars));
	Out += TEXT(",\"nodeType\":");
	SatisDash::Json::AppendString(Out, ClampStringTo(NodeType, MaxNodeFieldChars));
	Out += TEXT(",\"x\":");
	Out.AppendInt(int32(Mx));
	Out += TEXT(",\"y\":");
	Out.AppendInt(int32(My));
	Out += bExploited ? TEXT(",\"exploited\":true") : TEXT(",\"exploited\":false");
	if (!ClassName.IsEmpty())
	{
		Out += TEXT(",\"className\":");
		SatisDash::Json::AppendString(Out, ClampString(ClassName));
	}
	Out.AppendChar(TEXT('}'));
}
