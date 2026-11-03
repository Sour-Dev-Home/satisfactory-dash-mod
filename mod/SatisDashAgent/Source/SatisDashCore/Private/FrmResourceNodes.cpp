#include "SatisDash/FrmResourceNodes.h"

#include "SatisDash/FrmMap.h"
#include "SatisDash/JsonUtil.h"
#include "SatisDash/Snapshot.h"

SatisDash::FResourceNodesParse::FResourceNodesParse(TArray<uint8> InBody, const FString& ObservedAt)
	: FFrmArrayParse(MoveTemp(InBody), MaxResourceNodes, WorldBodyPrefix(ObservedAt), FString(TEXT("}")))
{
}

auto SatisDash::FResourceNodesParse::ReadRecord(bool bRoom) -> ERecord
{
	TOptional<FString> Name;
	TOptional<FString> Purity;
	TOptional<FString> NodeType;
	FString ClassName;
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
			AppendResourceNodeRecord(Out, Name.GetValue(), Purity.GetValue(), MapNodeType(NodeType.GetValue()), Mx, My, Exploited.GetValue(), ClassName);
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
		else if (Is(Key, TEXT("ClassName")))
		{
			if (N != EJsonNotation::String)
			{
				return ERecord::Invalid;
			}
			ClassName = Reader.String();
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
