#include "SatisDash/Native/Trains/NativeTrains.h"

#include "SatisDash/FrmMap.h"
#include "SatisDash/FrmMapLive.h"

namespace
{
	struct FNativeTrainsKeptTrain
	{
		double Mx;
		double My;
		const SatisDash::FTrainReading* Reading;
	};

	struct FNativeTrainsKeptStation
	{
		double Mx;
		double My;
		const SatisDash::FStationReading* Reading;
	};

	/** A rate to 3 decimals by a true division (the volatile keeps /fp:fast from multiplying by the reciprocal); unset when absent, not finite or negative. */
	TOptional<double> NativeTrainsRate3(const TOptional<double>& Value)
	{
		if (!Value.IsSet() || !FMath::IsFinite(Value.GetValue()) || Value.GetValue() < 0.0)
		{
			return TOptional<double>();
		}
		volatile double Scale = 1000.0;
		const double Scaled = FMath::RoundToDouble(Value.GetValue() * Scale);
		if (!FMath::IsFinite(Scaled))
		{
			return TOptional<double>();
		}
		const double Rounded = Scaled / Scale;
		return Rounded == 0.0 ? 0.0 : Rounded; // never -0
	}

	/** A total order: id, then name, then the rest, so the bytes do not depend on the order the readings came in. */
	bool NativeTrainsTrainLess(const FNativeTrainsKeptTrain& A, const FNativeTrainsKeptTrain& B)
	{
		const int32 ById = A.Reading->Id.Compare(B.Reading->Id, ESearchCase::CaseSensitive);
		if (ById != 0)
		{
			return ById < 0;
		}
		const int32 ByName = A.Reading->Name.Compare(B.Reading->Name, ESearchCase::CaseSensitive);
		if (ByName != 0)
		{
			return ByName < 0;
		}
		if (A.Reading->State != B.Reading->State)
		{
			return uint8(A.Reading->State) < uint8(B.Reading->State);
		}
		if (A.Mx != B.Mx)
		{
			return A.Mx < B.Mx;
		}
		return A.My < B.My;
	}

	bool NativeTrainsStationLess(const FNativeTrainsKeptStation& A, const FNativeTrainsKeptStation& B)
	{
		const int32 ById = A.Reading->Id.Compare(B.Reading->Id, ESearchCase::CaseSensitive);
		if (ById != 0)
		{
			return ById < 0;
		}
		const int32 ByName = A.Reading->Name.Compare(B.Reading->Name, ESearchCase::CaseSensitive);
		if (ByName != 0)
		{
			return ByName < 0;
		}
		if (A.Mx != B.Mx)
		{
			return A.Mx < B.Mx;
		}
		return A.My < B.My;
	}
}

bool SatisDash::NativeMapLiveJson(const TArray<FTrainReading>& Trains, const TArray<FStationReading>& Stations, FString& OutJson, FNativeMapLiveCounts* OutCounts)
{
	FNativeMapLiveCounts Counts;

	// A reading with a coordinate the contract can't hold is dropped, and does not count towards the 500.
	TArray<FNativeTrainsKeptTrain> KeptTrains;
	KeptTrains.Reserve(Trains.Num());
	for (const FTrainReading& Train : Trains)
	{
		const double Mx = ToWholeMetres(Train.XCm);
		const double My = ToWholeMetres(Train.YCm);
		if (!InWholeMetreBounds(Mx) || !InWholeMetreBounds(My))
		{
			++Counts.TrainsDropped;
			continue;
		}
		KeptTrains.Add({ Mx, My, &Train });
	}
	TArray<FNativeTrainsKeptStation> KeptStations;
	KeptStations.Reserve(Stations.Num());
	for (const FStationReading& Station : Stations)
	{
		const double Mx = ToWholeMetres(Station.XCm);
		const double My = ToWholeMetres(Station.YCm);
		if (!InWholeMetreBounds(Mx) || !InWholeMetreBounds(My))
		{
			++Counts.StationsDropped;
			continue;
		}
		KeptStations.Add({ Mx, My, &Station });
	}
	KeptTrains.Sort(NativeTrainsTrainLess);
	KeptStations.Sort(NativeTrainsStationLess);

	FString TrainsArray;
	TrainsArray.AppendChar(TEXT('['));
	for (const FNativeTrainsKeptTrain& Train : KeptTrains)
	{
		if (Counts.Trains == MapLiveMaxItems)
		{
			++Counts.TrainsOver;
			continue;
		}
		if (Counts.Trains > 0)
		{
			TrainsArray.AppendChar(TEXT(','));
		}
		AppendTrainRecord(TrainsArray, Train.Reading->Id, Train.Reading->Name, Train.Mx, Train.My, NativeTrainStatusText(Train.Reading->State));
		++Counts.Trains;
	}
	TrainsArray.AppendChar(TEXT(']'));

	FString StationsArray;
	StationsArray.AppendChar(TEXT('['));
	for (const FNativeTrainsKeptStation& Station : KeptStations)
	{
		if (Counts.Stations == MapLiveMaxItems)
		{
			++Counts.StationsOver;
			continue;
		}
		if (Counts.Stations > 0)
		{
			StationsArray.AppendChar(TEXT(','));
		}
		AppendStationRecord(StationsArray, Station.Reading->Id, Station.Reading->Name, Station.Mx, Station.My);
		const TOptional<double> Rate = NativeTrainsRate3(Station.Reading->TransferPerMinute);
		if (Rate.IsSet())
		{
			// The shared record ends in '}'; the rate key goes after the existing keys, so their order never moves.
			StationsArray.LeftChopInline(1);
			StationsArray += TEXT(",\"transferPerMinute\":");
			StationsArray += FString::SanitizeFloat(Rate.GetValue(), 0);
			StationsArray.AppendChar(TEXT('}'));
		}
		++Counts.Stations;
	}
	StationsArray.AppendChar(TEXT(']'));

	if (OutCounts)
	{
		*OutCounts = Counts;
	}
	// The same join, and the same byte rule, as the FRM path: over MapLiveMaxBytes the whole part is left out.
	const int64 TrainsBytes = FTCHARToUTF8_Convert::ConvertedLength(*TrainsArray, TrainsArray.Len());
	const int64 StationsBytes = FTCHARToUTF8_Convert::ConvertedLength(*StationsArray, StationsArray.Len());
	TOptional<FString> Joined = JoinMapLive(TrainsArray, TrainsBytes, StationsArray, StationsBytes);
	if (!Joined.IsSet())
	{
		return false;
	}
	OutJson = MoveTemp(Joined.GetValue());
	return true;
}
