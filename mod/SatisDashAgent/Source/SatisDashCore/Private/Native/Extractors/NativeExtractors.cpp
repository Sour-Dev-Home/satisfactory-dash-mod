#include "SatisDash/Native/Extractors/NativeExtractors.h"
#include "../Factory/NativeFactoryShared.h"

namespace
{
	using namespace SatisDash;
	using namespace SatisDash::NativeShared;

	bool NativeExtractorsLess(const FExtractorReading& A, const FExtractorReading& B)
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
		if (Order == 0)
		{
			Order = CompareText(A.ResourceClass, B.ResourceClass);
		}
		if (Order == 0 && A.CurrentPerMinute != B.CurrentPerMinute)
		{
			return A.CurrentPerMinute < B.CurrentPerMinute;
		}
		if (Order == 0 && A.MaxPerMinute != B.MaxPerMinute)
		{
			return A.MaxPerMinute < B.MaxPerMinute;
		}
		return Order < 0;
	}

	void NativeExtractorsWrite(FFactoryJsonWriter& Writer, const FExtractorReading& R)
	{
		TArray<FRateReading> Production;
		if (!R.ResourceClass.IsEmpty())
		{
			FRateReading Rate;
			Rate.Name = R.ResourceName.IsEmpty() ? R.ResourceClass : R.ResourceName;
			Rate.ClassName = R.ResourceClass;
			Rate.Current = NonNegative(R.CurrentPerMinute);
			Rate.Max = NonNegative(R.MaxPerMinute);
			// current / max as a percentage; a machine with no maximum is at 0.
			Rate.Percent = Rate.Max > 0 ? NonNegative(Rate.Current / Rate.Max * 100.0) : 0.0;
			Production.Add(Rate);
		}
		const TArray<FRateReading> NoIngredients;

		FFactoryBuildingModel Model;
		Model.Id = R.Id;
		Model.Name = R.Name;
		Model.ClassName = R.ClassName;
		Model.bProducing = R.bProducing;
		Model.bPaused = R.bPaused;
		Model.bBackedUp = R.bBackedUp;
		Model.CircuitGroupId = R.CircuitGroupId;
		SetLocation(Model, R.XCm, R.YCm, R.ZCm, R.YawDeg);
		Model.ClockSpeedPercent = ClockPercent(R.ClockPercent);
		Model.Production = &Production;
		Model.Ingredients = &NoIngredients;
		AppendFactoryBuilding(Writer, Model);
	}
}

FString SatisDash::NativeExtractorsFragment(TArray<FExtractorReading> Readings, int32& OutDropped, int32& OutOver)
{
	OutDropped = 0;
	OutOver = 0;
	// A rate the bridge's schema cannot take is a reading that cannot be trusted: drop it before sorting.
	Readings.RemoveAll([&OutDropped](const FExtractorReading& R)
	{
		if (!Finite(R.CurrentPerMinute) || !Finite(R.MaxPerMinute))
		{
			++OutDropped;
			return true;
		}
		return false;
	});
	Readings.StableSort(NativeExtractorsLess);

	FString Out;
	const TSharedRef<FFactoryJsonWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	Writer->WriteArrayStart();
	int32 Written = 0;
	const FString* LastId = nullptr;
	for (const FExtractorReading& Reading : Readings)
	{
		if (LastId && LastId->Equals(Reading.Id, ESearchCase::CaseSensitive))
		{
			continue; // a later duplicate of an id already seen
		}
		LastId = &Reading.Id;
		if (Written >= ExtractorsMaxItems)
		{
			++OutOver;
			continue;
		}
		NativeExtractorsWrite(*Writer, Reading);
		++Written;
	}
	Writer->WriteArrayEnd();
	Writer->Close();
	return Out;
}
