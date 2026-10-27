#include "SatisDash/Native/TrainRoutes/NativeTrainRoutes.h"

#include "SatisDash/FrmRecords.h"
#include "SatisDash/JsonUtil.h"
#include "SatisDash/Snapshot.h"

namespace
{
	/** The contract's strings, after the 200-character cut and the sort every list goes through, so a record is the same whatever order the game listed things in. */
	struct FNativeRoutesPlatform
	{
		FString Id;
		bool bLoad = false;
		TArray<FString> Items;
	};

	struct FNativeRoutesStation
	{
		FString Id;
		FString Name;
		TArray<FNativeRoutesPlatform> Platforms;
	};

	struct FNativeRoutesTrain
	{
		FString Id;
		FString Name;
		TArray<FString> Stops;
		TArray<FString> Items;
	};

	int32 NativeRoutesCompareText(const FString& A, const FString& B)
	{
		return A.Compare(B, ESearchCase::CaseSensitive);
	}

	/** Element by element, then the shorter first. */
	int32 NativeRoutesCompareLists(const TArray<FString>& A, const TArray<FString>& B)
	{
		const int32 Common = FMath::Min(A.Num(), B.Num());
		for (int32 I = 0; I < Common; ++I)
		{
			const int32 Cmp = NativeRoutesCompareText(A[I], B[I]);
			if (Cmp != 0)
			{
				return Cmp;
			}
		}
		return A.Num() == B.Num() ? 0 : (A.Num() < B.Num() ? -1 : 1);
	}

	/** Clamped to 200, empty ones left out (the contract's min(1)), sorted, distinct, at most TrainRoutesMaxItemClasses. */
	TArray<FString> NativeRoutesItemClasses(const TArray<FString>& Items)
	{
		TArray<FString> Out;
		Out.Reserve(Items.Num());
		for (const FString& Item : Items)
		{
			FString Clamped = SatisDash::ClampString(Item);
			if (!Clamped.IsEmpty())
			{
				Out.Add(MoveTemp(Clamped));
			}
		}
		Out.Sort([](const FString& A, const FString& B) { return NativeRoutesCompareText(A, B) < 0; });
		for (int32 I = Out.Num() - 1; I > 0; --I)
		{
			if (Out[I].Equals(Out[I - 1], ESearchCase::CaseSensitive))
			{
				Out.RemoveAt(I);
			}
		}
		if (Out.Num() > SatisDash::TrainRoutesMaxItemClasses)
		{
			Out.SetNum(SatisDash::TrainRoutesMaxItemClasses);
		}
		return Out;
	}

	bool NativeRoutesPlatformLess(const FNativeRoutesPlatform& A, const FNativeRoutesPlatform& B)
	{
		const int32 ById = NativeRoutesCompareText(A.Id, B.Id);
		if (ById != 0)
		{
			return ById < 0;
		}
		if (A.bLoad != B.bLoad)
		{
			return !A.bLoad;
		}
		return NativeRoutesCompareLists(A.Items, B.Items) < 0;
	}

	bool NativeRoutesStationLess(const FNativeRoutesStation& A, const FNativeRoutesStation& B)
	{
		const int32 ById = NativeRoutesCompareText(A.Id, B.Id);
		if (ById != 0)
		{
			return ById < 0;
		}
		const int32 ByName = NativeRoutesCompareText(A.Name, B.Name);
		if (ByName != 0)
		{
			return ByName < 0;
		}
		const int32 Common = FMath::Min(A.Platforms.Num(), B.Platforms.Num());
		for (int32 I = 0; I < Common; ++I)
		{
			if (NativeRoutesPlatformLess(A.Platforms[I], B.Platforms[I]))
			{
				return true;
			}
			if (NativeRoutesPlatformLess(B.Platforms[I], A.Platforms[I]))
			{
				return false;
			}
		}
		return A.Platforms.Num() < B.Platforms.Num();
	}

	bool NativeRoutesTrainLess(const FNativeRoutesTrain& A, const FNativeRoutesTrain& B)
	{
		const int32 ById = NativeRoutesCompareText(A.Id, B.Id);
		if (ById != 0)
		{
			return ById < 0;
		}
		const int32 ByName = NativeRoutesCompareText(A.Name, B.Name);
		if (ByName != 0)
		{
			return ByName < 0;
		}
		const int32 ByStops = NativeRoutesCompareLists(A.Stops, B.Stops);
		if (ByStops != 0)
		{
			return ByStops < 0;
		}
		return NativeRoutesCompareLists(A.Items, B.Items) < 0;
	}

	void NativeRoutesAppendStrings(FString& Out, const TArray<FString>& Strings)
	{
		Out.AppendChar(TEXT('['));
		for (int32 I = 0; I < Strings.Num(); ++I)
		{
			if (I > 0)
			{
				Out.AppendChar(TEXT(','));
			}
			SatisDash::Json::AppendString(Out, Strings[I]);
		}
		Out.AppendChar(TEXT(']'));
	}

	void NativeRoutesAppendStation(FString& Out, const FNativeRoutesStation& Station)
	{
		Out += TEXT("{\"recordType\":\"station\",\"id\":");
		SatisDash::Json::AppendString(Out, Station.Id);
		Out += TEXT(",\"name\":");
		SatisDash::Json::AppendString(Out, Station.Name);
		Out += TEXT(",\"platforms\":[");
		for (int32 I = 0; I < Station.Platforms.Num(); ++I)
		{
			const FNativeRoutesPlatform& Platform = Station.Platforms[I];
			if (I > 0)
			{
				Out.AppendChar(TEXT(','));
			}
			Out += TEXT("{\"id\":");
			SatisDash::Json::AppendString(Out, Platform.Id);
			Out += Platform.bLoad ? TEXT(",\"mode\":\"load\",\"items\":") : TEXT(",\"mode\":\"unload\",\"items\":");
			NativeRoutesAppendStrings(Out, Platform.Items);
			Out.AppendChar(TEXT('}'));
		}
		Out += TEXT("]}");
	}

	void NativeRoutesAppendTrain(FString& Out, const FNativeRoutesTrain& Train)
	{
		Out += TEXT("{\"recordType\":\"train\",\"id\":");
		SatisDash::Json::AppendString(Out, Train.Id);
		Out += TEXT(",\"name\":");
		SatisDash::Json::AppendString(Out, Train.Name);
		Out += TEXT(",\"stops\":");
		NativeRoutesAppendStrings(Out, Train.Stops);
		Out += TEXT(",\"items\":");
		NativeRoutesAppendStrings(Out, Train.Items);
		Out.AppendChar(TEXT('}'));
	}
}

SatisDash::FNativeWorldBody SatisDash::BuildNativeTrainRoutes(const FString& ObservedAt, const TArray<FStationReading>& Stations, const TArray<FTrainReading>& Trains)
{
	FNativeWorldBody Out;

	TArray<FNativeRoutesStation> StationModels;
	StationModels.Reserve(Stations.Num());
	for (const FStationReading& Reading : Stations)
	{
		FNativeRoutesStation Model;
		Model.Id = ClampString(Reading.Id);
		if (Model.Id.IsEmpty())
		{
			++Out.Dropped;
			continue;
		}
		Model.Name = ClampString(Reading.Name);
		for (const FPlatformReading& PlatformReading : Reading.Platforms)
		{
			FNativeRoutesPlatform Platform;
			Platform.Id = ClampString(PlatformReading.Id);
			if (Platform.Id.IsEmpty())
			{
				continue;
			}
			Platform.bLoad = PlatformReading.bLoad;
			Platform.Items = NativeRoutesItemClasses(PlatformReading.Items);
			Model.Platforms.Add(MoveTemp(Platform));
		}
		Model.Platforms.Sort(NativeRoutesPlatformLess);
		if (Model.Platforms.Num() > TrainRoutesMaxPlatforms)
		{
			Model.Platforms.SetNum(TrainRoutesMaxPlatforms);
		}
		StationModels.Add(MoveTemp(Model));
	}
	StationModels.Sort(NativeRoutesStationLess);

	TArray<FNativeRoutesTrain> TrainModels;
	TrainModels.Reserve(Trains.Num());
	for (const FTrainReading& Reading : Trains)
	{
		FNativeRoutesTrain Model;
		Model.Id = ClampString(Reading.Id);
		if (Model.Id.IsEmpty())
		{
			++Out.Dropped;
			continue;
		}
		Model.Name = ClampString(Reading.Name);
		// Controller ruling, mirroring FRM (run 40439238142, train BP_Train_C_2147144902, Parked on both sides): FRM lists no
		// timetable stops for a parked train, so a Parked train sends none; its cargo is unchanged. Other states keep their stops.
		const int32 StopCount = Reading.State == ETrainState::Parked ? 0 : FMath::Min(Reading.Stops.Num(), TrainRoutesMaxStops);
		Model.Stops.Reserve(StopCount);
		for (int32 I = 0; I < StopCount; ++I)
		{
			Model.Stops.Add(ClampString(Reading.Stops[I]));
		}
		Model.Items = NativeRoutesItemClasses(Reading.Items);
		TrainModels.Add(MoveTemp(Model));
	}
	TrainModels.Sort(NativeRoutesTrainLess);

	Out.Json = WorldBodyPrefix(ObservedAt);
	Out.DataFrom = Out.Json.Len();
	Out.Json.AppendChar(TEXT('['));
	// Stations first, then trains; the layer is cut at TrainRoutesMaxItems records.
	for (const FNativeRoutesStation& Station : StationModels)
	{
		if (Out.Items == TrainRoutesMaxItems)
		{
			++Out.Over;
			continue;
		}
		if (Out.Items > 0)
		{
			Out.Json.AppendChar(TEXT(','));
		}
		NativeRoutesAppendStation(Out.Json, Station);
		++Out.Items;
	}
	for (const FNativeRoutesTrain& Train : TrainModels)
	{
		if (Out.Items == TrainRoutesMaxItems)
		{
			++Out.Over;
			continue;
		}
		if (Out.Items > 0)
		{
			Out.Json.AppendChar(TEXT(','));
		}
		NativeRoutesAppendTrain(Out.Json, Train);
		++Out.Items;
	}
	Out.Json.AppendChar(TEXT(']'));
	Out.DataEnd = Out.Json.Len();
	Out.Json += TEXT("}");
	Out.Bytes = FTCHARToUTF8_Convert::ConvertedLength(*Out.Json, Out.Json.Len());
	return Out;
}
