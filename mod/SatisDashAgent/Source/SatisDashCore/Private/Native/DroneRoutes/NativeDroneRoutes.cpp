#include "SatisDash/Native/DroneRoutes/NativeDroneRoutes.h"

#include "SatisDash/FrmMap.h"
#include "SatisDash/FrmRecords.h"
#include "SatisDash/JsonUtil.h"
#include "SatisDash/Snapshot.h"

namespace
{
	/** A port after the checks. The item text is split around pairedId, which is only known once the kept ids are. */
	struct FNativeDronePort
	{
		FString Id;
		FString Head;      // {"id":..,"name":..,"paired":..
		FString RawPaired; // the reading's PairedId, unresolved
		FString Tail;      // ,"input":..,"y":..}
	};

	bool NativeDronePortLess(const FNativeDronePort& A, const FNativeDronePort& B)
	{
		const int32 ById = A.Id.Compare(B.Id, ESearchCase::CaseSensitive);
		if (ById != 0)
		{
			return ById < 0;
		}
		const int32 ByHead = A.Head.Compare(B.Head, ESearchCase::CaseSensitive);
		if (ByHead != 0)
		{
			return ByHead < 0;
		}
		const int32 ByPaired = A.RawPaired.Compare(B.RawPaired, ESearchCase::CaseSensitive);
		if (ByPaired != 0)
		{
			return ByPaired < 0;
		}
		return A.Tail.Compare(B.Tail, ESearchCase::CaseSensitive) < 0;
	}

	/** Clamped to 200, empty ones left out (the contract's min(1)), sorted, distinct, at most DroneRoutesMaxItemClasses. */
	TArray<FString> NativeDroneItemClasses(const TArray<FString>& Items)
	{
		TArray<FString> Out;
		Out.Reserve(Items.Num());
		for (const FString& Item : Items)
		{
			if (!Item.IsEmpty() && Item.Len() <= SatisDash::DroneRoutesMaxStringChars)
			{
				Out.Add(Item);
			}
		}
		Out.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
		for (int32 I = Out.Num() - 1; I > 0; --I)
		{
			if (Out[I].Equals(Out[I - 1], ESearchCase::CaseSensitive))
			{
				Out.RemoveAt(I);
			}
		}
		if (Out.Num() > SatisDash::DroneRoutesMaxItemClasses)
		{
			Out.SetNum(SatisDash::DroneRoutesMaxItemClasses);
		}
		return Out;
	}

	void NativeDroneAppendList(FString& Out, const TCHAR* Key, const TArray<FString>& Items)
	{
		Out += Key;
		Out.AppendChar(TEXT('['));
		for (int32 I = 0; I < Items.Num(); ++I)
		{
			if (I > 0)
			{
				Out.AppendChar(TEXT(','));
			}
			SatisDash::Json::AppendString(Out, Items[I]);
		}
		Out.AppendChar(TEXT(']'));
	}
}

SatisDash::FNativeWorldBody SatisDash::BuildNativeDroneRoutes(const FString& ObservedAt, const TArray<FDronePortReading>& Readings)
{
	FNativeWorldBody Out;

	TArray<FNativeDronePort> Ports;
	Ports.Reserve(Readings.Num());
	for (const FDronePortReading& Reading : Readings)
	{
		const double Mx = ToWholeMetres(Reading.XCm);
		const double My = ToWholeMetres(Reading.YCm);
		if (Reading.Id.Len() > DroneRoutesMaxStringChars || Reading.Name.Len() > DroneRoutesMaxStringChars || !InWholeMetreBounds(Mx) || !InWholeMetreBounds(My))
		{
			++Out.Dropped;
			continue;
		}
		FNativeDronePort Port;
		Port.Id = Reading.Id;
		Port.Head = TEXT("{\"id\":");
		SatisDash::Json::AppendString(Port.Head, Reading.Id);
		Port.Head += TEXT(",\"name\":");
		SatisDash::Json::AppendString(Port.Head, Reading.Name);
		Port.Head += TEXT(",\"paired\":");
		if (Reading.bPaired && !Reading.PairedName.IsEmpty() && Reading.PairedName.Len() <= DroneRoutesMaxStringChars)
		{
			SatisDash::Json::AppendString(Port.Head, Reading.PairedName);
		}
		else
		{
			Port.Head += TEXT("null");
		}
		Port.RawPaired = Reading.PairedId;
		NativeDroneAppendList(Port.Tail, TEXT(",\"input\":"), NativeDroneItemClasses(Reading.Input));
		NativeDroneAppendList(Port.Tail, TEXT(",\"output\":"), NativeDroneItemClasses(Reading.Output));
		NativeDroneAppendList(Port.Tail, TEXT(",\"fuel\":"), NativeDroneItemClasses(Reading.Fuel));
		Port.Tail += TEXT(",\"x\":");
		Port.Tail.AppendInt(int32(Mx));
		Port.Tail += TEXT(",\"y\":");
		Port.Tail.AppendInt(int32(My));
		Port.Tail += TEXT("}");
		Ports.Add(MoveTemp(Port));
	}
	Ports.Sort(NativeDronePortLess);

	// The kept layer first (a repeated id keeps the first, the cap keeps the first MaxItems), so pairedId can be checked against it.
	TArray<const FNativeDronePort*> Kept;
	TSet<FString> KeptIds;
	const FNativeDronePort* Previous = nullptr;
	for (const FNativeDronePort& Port : Ports)
	{
		if (Previous && Previous->Id.Equals(Port.Id, ESearchCase::CaseSensitive))
		{
			++Out.Dropped; // a repeated id: the one that sorted first stays
			continue;
		}
		Previous = &Port;
		if (Kept.Num() == DroneRoutesMaxItems)
		{
			++Out.Over;
			continue;
		}
		Kept.Add(&Port);
		KeptIds.Add(Port.Id);
	}

	Out.Json = WorldBodyPrefix(ObservedAt);
	Out.DataFrom = Out.Json.Len();
	Out.Json.AppendChar(TEXT('['));
	for (const FNativeDronePort* Port : Kept)
	{
		if (Out.Items > 0)
		{
			Out.Json.AppendChar(TEXT(','));
		}
		Out.Json += Port->Head;
		Out.Json += TEXT(",\"pairedId\":");
		if (!Port->RawPaired.IsEmpty() && KeptIds.Contains(Port->RawPaired))
		{
			SatisDash::Json::AppendString(Out.Json, Port->RawPaired);
		}
		else
		{
			Out.Json += TEXT("null");
		}
		Out.Json += Port->Tail;
		++Out.Items;
	}
	Out.Json.AppendChar(TEXT(']'));
	Out.DataEnd = Out.Json.Len();
	Out.Json += TEXT("}");
	Out.Bytes = FTCHARToUTF8_Convert::ConvertedLength(*Out.Json, Out.Json.Len());
	return Out;
}
