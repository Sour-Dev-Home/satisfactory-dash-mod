#include "SatisDash/Native/Rails/NativeRails.h"

#include "SatisDash/FrmMap.h"
#include "SatisDash/FrmRecords.h"
#include "SatisDash/JsonUtil.h"
#include "SatisDash/Snapshot.h"

namespace
{
	/** A track after the cut, the metre rounding and the collapse: x, y pairs in whole metres. */
	struct FNativeRailsTrack
	{
		FString Id;
		TArray<int32> Metres;
	};

	/** Id first, then the point list pairwise (x, then y), the shorter list first: a total order, so any input order gives the same output. */
	bool NativeRailsTrackLess(const FNativeRailsTrack& A, const FNativeRailsTrack& B)
	{
		const int32 ById = A.Id.Compare(B.Id, ESearchCase::CaseSensitive);
		if (ById != 0)
		{
			return ById < 0;
		}
		const int32 Common = FMath::Min(A.Metres.Num(), B.Metres.Num());
		for (int32 I = 0; I < Common; ++I)
		{
			if (A.Metres[I] != B.Metres[I])
			{
				return A.Metres[I] < B.Metres[I];
			}
		}
		return A.Metres.Num() < B.Metres.Num();
	}
}

int32 SatisDash::RailSampleIntervals(double LengthCm)
{
	if (!FMath::IsFinite(LengthCm) || LengthCm <= 0.0)
	{
		return 1;
	}
	// The volatile step keeps a true division (UE's /fp:fast would multiply by 0.01 and can land an ulp over a whole number).
	volatile double Step = RailSampleStepCm;
	const double Highest = double(MaxRailPoints - 1);
	const double Quotient = FMath::Min(LengthCm / Step, Highest); // capped first, so the int32 below cannot overflow
	int32 Wanted = int32(Quotient);
	if (double(Wanted) < Quotient)
	{
		++Wanted; // ceil
	}
	return FMath::Clamp(Wanted, 1, MaxRailPoints - 1);
}

double SatisDash::RailSampleDistanceCm(double LengthCm, int32 Intervals, int32 Index)
{
	if (Intervals < 1 || Index >= Intervals)
	{
		return LengthCm;
	}
	if (Index <= 0)
	{
		return 0.0;
	}
	return LengthCm * double(Index) / double(Intervals);
}

SatisDash::FNativeWorldBody SatisDash::BuildNativeRails(const FString& ObservedAt, const TArray<FRailReading>& Readings)
{
	FNativeWorldBody Out;

	TArray<FNativeRailsTrack> Tracks;
	Tracks.Reserve(Readings.Num());
	for (const FRailReading& Reading : Readings)
	{
		FNativeRailsTrack Track;
		Track.Id = ClampString(Reading.Id);
		if (Track.Id.IsEmpty())
		{
			++Out.Dropped;
			continue;
		}
		bool bHasLast = false;
		int32 LastX = 0;
		int32 LastY = 0;
		int32 Count = 0;
		bool bTooLong = false;
		for (const FRailPointCm& Point : Reading.Points)
		{
			const double Mx = ToWholeMetres(Point.XCm);
			const double My = ToWholeMetres(Point.YCm);
			if (!InWholeMetreBounds(Mx) || !InWholeMetreBounds(My))
			{
				continue; // a bad point is skipped, not the track
			}
			const int32 Ix = int32(Mx);
			const int32 Iy = int32(My);
			if (bHasLast && LastX == Ix && LastY == Iy)
			{
				continue;
			}
			bHasLast = true;
			LastX = Ix;
			LastY = Iy;
			if (++Count > MaxRailPoints)
			{
				bTooLong = true;
				break;
			}
			Track.Metres.Add(Ix);
			Track.Metres.Add(Iy);
		}
		if (bTooLong || Track.Metres.Num() < 4)
		{
			++Out.Dropped;
			continue;
		}
		Tracks.Add(MoveTemp(Track));
	}
	Tracks.Sort(NativeRailsTrackLess);

	Out.Json = WorldBodyPrefix(ObservedAt);
	Out.DataFrom = Out.Json.Len();
	Out.Json.AppendChar(TEXT('['));
	const FNativeRailsTrack* Previous = nullptr;
	for (const FNativeRailsTrack& Track : Tracks)
	{
		if (Previous && Previous->Id.Equals(Track.Id, ESearchCase::CaseSensitive))
		{
			continue; // a repeated id: the one that sorted first stays
		}
		Previous = &Track;
		if (Out.Items == MaxRailSegments)
		{
			++Out.Over;
			continue;
		}
		if (Out.Items > 0)
		{
			Out.Json.AppendChar(TEXT(','));
		}
		Out.Json += TEXT("{\"id\":");
		SatisDash::Json::AppendString(Out.Json, Track.Id);
		Out.Json += TEXT(",\"points\":[");
		for (int32 I = 0; I < Track.Metres.Num(); I += 2)
		{
			if (I > 0)
			{
				Out.Json.AppendChar(TEXT(','));
			}
			Out.Json.AppendChar(TEXT('['));
			Out.Json.AppendInt(Track.Metres[I]);
			Out.Json.AppendChar(TEXT(','));
			Out.Json.AppendInt(Track.Metres[I + 1]);
			Out.Json.AppendChar(TEXT(']'));
		}
		Out.Json += TEXT("]}");
		++Out.Items;
	}
	Out.Json.AppendChar(TEXT(']'));
	Out.DataEnd = Out.Json.Len();
	Out.Json += TEXT("}");
	Out.Bytes = FTCHARToUTF8_Convert::ConvertedLength(*Out.Json, Out.Json.Len());
	return Out;
}
