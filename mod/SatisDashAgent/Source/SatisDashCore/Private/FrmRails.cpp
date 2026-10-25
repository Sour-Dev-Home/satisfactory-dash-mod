#include "SatisDash/FrmRails.h"

#include "SatisDash/FrmMap.h"
#include "SatisDash/JsonUtil.h"
#include "SatisDash/Snapshot.h"

SatisDash::FRailsParse::FRailsParse(TArray<uint8> InBody, const FString& ObservedAt)
	: FFrmArrayParse(MoveTemp(InBody), MaxRailSegments, WorldBodyPrefix(ObservedAt), FString(TEXT("}")))
{
}

bool SatisDash::FRailsParse::ReadSpline(EJsonNotation Started, TArray<int32>& Points, bool& bTooLong)
{
	if (Started != EJsonNotation::ArrayStart)
	{
		return false;
	}
	Points.Reset(); // a repeated SplineData key: the last one wins
	bTooLong = false;
	bool bHasLast = false;
	int32 LastX = 0;
	int32 LastY = 0;
	EJsonNotation N;
	while (Reader.Next(N))
	{
		if (N == EJsonNotation::ArrayEnd)
		{
			return true;
		}
		if (N != EJsonNotation::ObjectStart)
		{
			return false;
		}
		double X = 0;
		double Y = 0;
		uint8 Seen = 0;
		bool bClosed = false;
		while (Reader.Next(N))
		{
			if (N == EJsonNotation::ObjectEnd)
			{
				bClosed = true;
				break;
			}
			const FString Key = Reader.Name();
			const bool bX = Is(Key, TEXT("x"));
			const bool bY = Is(Key, TEXT("y"));
			if (bX || bY)
			{
				if (N != EJsonNotation::Number || !FMath::IsFinite(Reader.Number()))
				{
					return false;
				}
				(bX ? X : Y) = Reader.Number();
				Seen |= uint8(bX ? 1 : 2);
			}
			else if (!Reader.SkipValue(N))
			{
				return false;
			}
		}
		if (!bClosed || Seen != 3)
		{
			return false;
		}
		const double Mx = ToWholeMetres(X);
		const double My = ToWholeMetres(Y);
		if (!InWholeMetreBounds(Mx) || !InWholeMetreBounds(My))
		{
			continue; // railsMapper.ts: a bad point is skipped, not the segment
		}
		const int32 Ix = int32(Mx);
		const int32 Iy = int32(My);
		if (bHasLast && LastX == Ix && LastY == Iy)
		{
			continue; // consecutive duplicates (the spline is far finer than a metre) are collapsed
		}
		bHasLast = true;
		LastX = Ix;
		LastY = Iy;
		if (Points.Num() / 2 >= MaxRailPoints)
		{
			bTooLong = true; // still read and validated, no longer stored
			continue;
		}
		Points.Add(Ix);
		Points.Add(Iy);
	}
	return false;
}

auto SatisDash::FRailsParse::ReadRecord(bool bRoom) -> ERecord
{
	TOptional<FString> Id;
	bool bSpline = false;
	bool bTooLong = false;
	TArray<int32> Points; // x, y pairs in whole metres
	EJsonNotation N;
	while (Reader.Next(N))
	{
		if (N == EJsonNotation::ObjectEnd)
		{
			if (!Id.IsSet() || Id->IsEmpty() || !bSpline)
			{
				return ERecord::Invalid;
			}
			if (bTooLong || Points.Num() < 4)
			{
				return ERecord::Dropped;
			}
			if (!bRoom)
			{
				return ERecord::Over;
			}
			BeginRecord();
			Out += TEXT("{\"id\":");
			SatisDash::Json::AppendString(Out, ClampString(Id.GetValue()));
			Out += TEXT(",\"points\":[");
			for (int32 I = 0; I < Points.Num(); I += 2)
			{
				if (I > 0)
				{
					Out.AppendChar(TEXT(','));
				}
				Out.AppendChar(TEXT('['));
				Out.AppendInt(Points[I]);
				Out.AppendChar(TEXT(','));
				Out.AppendInt(Points[I + 1]);
				Out.AppendChar(TEXT(']'));
			}
			Out += TEXT("]}");
			return ERecord::Kept;
		}
		const FString Key = Reader.Name();
		if (Is(Key, TEXT("ID")))
		{
			if (N != EJsonNotation::String)
			{
				return ERecord::Invalid;
			}
			Id = Reader.String();
		}
		else if (Is(Key, TEXT("SplineData")))
		{
			if (!ReadSpline(N, Points, bTooLong))
			{
				return ERecord::Invalid;
			}
			bSpline = true;
		}
		else if (!Reader.SkipValue(N))
		{
			return ERecord::Invalid;
		}
	}
	return ERecord::Invalid;
}
