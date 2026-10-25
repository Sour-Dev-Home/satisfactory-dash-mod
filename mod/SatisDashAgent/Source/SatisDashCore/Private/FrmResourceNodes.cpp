#include "SatisDash/FrmResourceNodes.h"

#include "SatisDash/FrmMap.h"
#include "SatisDash/JsonUtil.h"
#include "SatisDash/Snapshot.h"

namespace
{
	/** JavaScript's \s: the ECMAScript WhiteSpace and LineTerminator characters. */
	bool FrmNodesIsJsSpace(TCHAR C)
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
		if (FrmNodesIsJsSpace(C))
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

SatisDash::FResourceNodesParse::FResourceNodesParse(TArray<uint8> InBody, const FString& ObservedAt)
	: FFrmArrayParse(MoveTemp(InBody), MaxResourceNodes, WorldBodyPrefix(ObservedAt), FString(TEXT("}")))
{
}

auto SatisDash::FResourceNodesParse::ReadRecord(bool bRoom) -> ERecord
{
	TOptional<FString> Name;
	TOptional<FString> Purity;
	TOptional<FString> NodeType;
	TOptional<bool> Exploited;
	bool bLocation = false;
	double X = 0;
	double Y = 0;
	EJsonNotation N;
	while (Reader.Next(N))
	{
		if (N == EJsonNotation::ObjectEnd)
		{
			if (!Name.IsSet() || !Purity.IsSet() || !NodeType.IsSet() || !Exploited.IsSet() || !bLocation)
			{
				return ERecord::Invalid;
			}
			const double Mx = ToWholeMetres(X);
			const double My = ToWholeMetres(Y);
			if (!InWholeMetreBounds(Mx) || !InWholeMetreBounds(My))
			{
				return ERecord::Dropped;
			}
			if (!bRoom)
			{
				return ERecord::Over;
			}
			BeginRecord();
			Out += TEXT("{\"type\":");
			SatisDash::Json::AppendString(Out, ClampString(Name.GetValue()));
			Out += TEXT(",\"purity\":");
			SatisDash::Json::AppendString(Out, ClampStringTo(Purity->ToLower(), MaxNodeFieldChars));
			Out += TEXT(",\"nodeType\":");
			SatisDash::Json::AppendString(Out, ClampStringTo(MapNodeType(NodeType.GetValue()), MaxNodeFieldChars));
			Out += TEXT(",\"x\":");
			Out.AppendInt(int32(Mx));
			Out += TEXT(",\"y\":");
			Out.AppendInt(int32(My));
			Out += Exploited.GetValue() ? TEXT(",\"exploited\":true}") : TEXT(",\"exploited\":false}");
			return ERecord::Kept;
		}
		const FString Key = Reader.Name();
		const bool bName = Is(Key, TEXT("Name"));
		const bool bPurity = Is(Key, TEXT("Purity"));
		const bool bNodeType = Is(Key, TEXT("NodeType"));
		if (bName || bPurity || bNodeType)
		{
			if (N != EJsonNotation::String)
			{
				return ERecord::Invalid;
			}
			(bName ? Name : bPurity ? Purity : NodeType) = Reader.String();
		}
		else if (Is(Key, TEXT("Exploited")))
		{
			if (N != EJsonNotation::Boolean)
			{
				return ERecord::Invalid;
			}
			Exploited = Reader.Boolean();
		}
		else if (Is(Key, TEXT("location")))
		{
			if (!ReadFrmLocation(Reader, N, X, Y))
			{
				return ERecord::Invalid;
			}
			bLocation = true;
		}
		else if (!Reader.SkipValue(N))
		{
			return ERecord::Invalid;
		}
	}
	return ERecord::Invalid;
}
