#include "SatisDash/Native/Generators/NativeGenerators.h"
#include "../Factory/NativeFactoryShared.h"
#include "SatisDash/Snapshot.h"

namespace
{
	using namespace SatisDash;
	using namespace SatisDash::NativeShared;

	int32 NativeGeneratorsCompareFuel(const TOptional<FFuelReading>& A, const TOptional<FFuelReading>& B)
	{
		if (A.IsSet() != B.IsSet())
		{
			return A.IsSet() ? 1 : -1;
		}
		if (!A.IsSet())
		{
			return 0;
		}
		int32 Order = CompareText(A->ClassName, B->ClassName);
		if (Order == 0)
		{
			Order = CompareText(A->Name, B->Name);
		}
		if (Order == 0 && A->Amount != B->Amount)
		{
			return A->Amount < B->Amount ? -1 : 1;
		}
		return Order;
	}

	bool NativeGeneratorsLess(const FGeneratorReading& A, const FGeneratorReading& B)
	{
		int32 Order = CompareText(A.Id, B.Id);
		if (Order == 0)
		{
			Order = CompareText(A.Name, B.Name);
		}
		if (Order == 0)
		{
			Order = CompareText(A.ClassName, B.ClassName);
		}
		if (Order == 0 && A.CurrentMW != B.CurrentMW)
		{
			return A.CurrentMW < B.CurrentMW;
		}
		if (Order == 0 && A.CapacityMW != B.CapacityMW)
		{
			return A.CapacityMW < B.CapacityMW;
		}
		if (Order == 0)
		{
			Order = NativeGeneratorsCompareFuel(A.Fuel, B.Fuel);
		}
		if (Order == 0)
		{
			Order = NativeGeneratorsCompareFuel(A.Supplement, B.Supplement);
		}
		return Order < 0;
	}

	void NativeGeneratorsWriteItem(FFactoryJsonWriter& Writer, const TCHAR* Key, const TOptional<FFuelReading>& Item)
	{
		if (!Item.IsSet())
		{
			Writer.WriteNull(Key);
			return;
		}
		Writer.WriteObjectStart(Key);
		Writer.WriteValue(TEXT("className"), ClampString(Item->ClassName));
		Writer.WriteValue(TEXT("name"), ClampString(Item->Name));
		Writer.WriteValue(TEXT("amount"), NonNegative(Item->Amount));
		Writer.WriteObjectEnd();
	}

	void NativeGeneratorsWrite(FFactoryJsonWriter& Writer, const FGeneratorReading& R)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("id"), ClampString(R.Id));
		Writer.WriteValue(TEXT("name"), ClampString(R.Name));
		Writer.WriteValue(TEXT("className"), ClampString(R.ClassName));
		FFactoryBuildingModel Place;
		if (R.bHasLocation && SetLocation(Place, R.XCm, R.YCm, R.ZCm, R.YawDeg))
		{
			Writer.WriteObjectStart(TEXT("location"));
			Writer.WriteValue(TEXT("xM"), Place.XM);
			Writer.WriteValue(TEXT("yM"), Place.YM);
			Writer.WriteValue(TEXT("zM"), Place.ZM);
			Writer.WriteValue(TEXT("rotationDeg"), Place.YawDeg);
			Writer.WriteObjectEnd();
		}
		Writer.WriteValue(TEXT("circuitGroupId"), R.CircuitGroupId);
		Writer.WriteValue(TEXT("currentMW"), NonNegative(R.CurrentMW)); // NonNegative also turns -0 into 0
		Writer.WriteValue(TEXT("capacityMW"), NonNegative(R.CapacityMW));
		const TOptional<double> Clock = ClockPercent(R.ClockPercent);
		if (Clock.IsSet())
		{
			Writer.WriteValue(TEXT("clockSpeedPercent"), Clock.GetValue());
		}
		Writer.WriteValue(TEXT("isProducing"), R.bProducing);
		NativeGeneratorsWriteItem(Writer, TEXT("fuel"), R.Fuel);
		if (R.bSupplementRead)
		{
			NativeGeneratorsWriteItem(Writer, TEXT("supplement"), R.Supplement);
		}
		Writer.WriteObjectEnd();
	}
}

FString SatisDash::NativeGeneratorsFragment(TArray<FGeneratorReading> Readings, int32& OutDropped, int32& OutOver)
{
	OutDropped = 0;
	OutOver = 0;
	// A power the bridge's schema cannot take is a reading that cannot be trusted: drop it before sorting.
	Readings.RemoveAll([&OutDropped](const FGeneratorReading& R)
	{
		if (!Finite(R.CurrentMW) || !Finite(R.CapacityMW) || R.CurrentMW < 0 || R.CapacityMW < 0)
		{
			++OutDropped;
			return true;
		}
		return false;
	});
	Readings.StableSort(NativeGeneratorsLess);

	FString Out;
	const TSharedRef<FFactoryJsonWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	Writer->WriteArrayStart();
	int32 Written = 0;
	const FString* LastId = nullptr;
	for (const FGeneratorReading& Reading : Readings)
	{
		if (LastId && LastId->Equals(Reading.Id, ESearchCase::CaseSensitive))
		{
			continue; // a later duplicate of an id already seen
		}
		LastId = &Reading.Id;
		if (Written >= GeneratorsMaxItems)
		{
			++OutOver;
			continue;
		}
		NativeGeneratorsWrite(*Writer, Reading);
		++Written;
	}
	Writer->WriteArrayEnd();
	Writer->Close();
	return Out;
}
