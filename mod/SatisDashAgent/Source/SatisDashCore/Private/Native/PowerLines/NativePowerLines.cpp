#include "SatisDash/Native/PowerLines/NativePowerLines.h"

#include "SatisDash/FrmMap.h"
#include "SatisDash/FrmRecords.h"
#include "SatisDash/JsonUtil.h"
#include "SatisDash/Snapshot.h"

namespace
{
	/** A wire after the checks: the id, and the points text that breaks a tie between equal ids. */
	struct FNativePowerLine
	{
		FString Id;
		FString Points; // [[x0,y0],[x1,y1]]
	};

	bool NativePowerLineLess(const FNativePowerLine& A, const FNativePowerLine& B)
	{
		const int32 ById = A.Id.Compare(B.Id, ESearchCase::CaseSensitive);
		if (ById != 0)
		{
			return ById < 0;
		}
		return A.Points.Compare(B.Points, ESearchCase::CaseSensitive) < 0;
	}

	void NativePowerLineAppendEnd(FString& Out, double Mx, double My)
	{
		Out.AppendChar(TEXT('['));
		Out.AppendInt(int32(Mx));
		Out.AppendChar(TEXT(','));
		Out.AppendInt(int32(My));
		Out.AppendChar(TEXT(']'));
	}
}

SatisDash::FNativeWorldBody SatisDash::BuildNativePowerLines(const FString& ObservedAt, const TArray<FPowerLineReading>& Readings)
{
	FNativeWorldBody Out;

	TArray<FNativePowerLine> Lines;
	Lines.Reserve(Readings.Num());
	for (const FPowerLineReading& Reading : Readings)
	{
		const double Mx0 = ToWholeMetres(Reading.X0Cm);
		const double My0 = ToWholeMetres(Reading.Y0Cm);
		const double Mx1 = ToWholeMetres(Reading.X1Cm);
		const double My1 = ToWholeMetres(Reading.Y1Cm);
		if (Reading.Id.IsEmpty() || !InWholeMetreBounds(Mx0) || !InWholeMetreBounds(My0) || !InWholeMetreBounds(Mx1) || !InWholeMetreBounds(My1))
		{
			++Out.Dropped;
			continue;
		}
		FNativePowerLine Line;
		Line.Id = ClampString(Reading.Id);
		Line.Points = TEXT("[");
		NativePowerLineAppendEnd(Line.Points, Mx0, My0);
		Line.Points.AppendChar(TEXT(','));
		NativePowerLineAppendEnd(Line.Points, Mx1, My1);
		Line.Points.AppendChar(TEXT(']'));
		Lines.Add(MoveTemp(Line));
	}
	Lines.Sort(NativePowerLineLess);

	Out.Json = WorldBodyPrefix(ObservedAt);
	Out.DataFrom = Out.Json.Len();
	Out.Json.AppendChar(TEXT('['));
	const FNativePowerLine* Previous = nullptr;
	for (const FNativePowerLine& Line : Lines)
	{
		if (Previous && Previous->Id.Equals(Line.Id, ESearchCase::CaseSensitive))
		{
			++Out.Dropped; // a repeated id: the one that sorted first stays
			continue;
		}
		Previous = &Line;
		if (Out.Items == PowerLinesMaxItems)
		{
			++Out.Over;
			continue;
		}
		if (Out.Items > 0)
		{
			Out.Json.AppendChar(TEXT(','));
		}
		Out.Json += TEXT("{\"id\":");
		SatisDash::Json::AppendString(Out.Json, Line.Id);
		Out.Json += TEXT(",\"points\":");
		Out.Json += Line.Points;
		Out.Json.AppendChar(TEXT('}'));
		++Out.Items;
	}
	Out.Json.AppendChar(TEXT(']'));
	Out.DataEnd = Out.Json.Len();
	Out.Json += TEXT("}");
	Out.Bytes = FTCHARToUTF8_Convert::ConvertedLength(*Out.Json, Out.Json.Len());
	return Out;
}
