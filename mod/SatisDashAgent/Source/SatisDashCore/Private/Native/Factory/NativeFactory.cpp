#include "SatisDash/Native/Factory/NativeFactory.h"
#include "NativeFactoryShared.h"

namespace
{
	using SatisDash::NativeShared::NonNegative;

	double NativeFactoryNonNegative(double Value)
	{
		return NonNegative(Value);
	}

	void NativeFactoryCleanRates(TArray<SatisDash::FRateReading>& Rates)
	{
		for (SatisDash::FRateReading& Rate : Rates)
		{
			Rate.Current = NativeFactoryNonNegative(Rate.Current);
			Rate.Max = NativeFactoryNonNegative(Rate.Max);
			Rate.Percent = NativeFactoryNonNegative(Rate.Percent);
		}
	}

	void NativeFactoryCleanSlots(TArray<SatisDash::FSlotReading>& Slots)
	{
		for (SatisDash::FSlotReading& Slot : Slots)
		{
			Slot.Amount = NativeFactoryNonNegative(Slot.Amount);
			Slot.MaxAmount = NativeFactoryNonNegative(Slot.MaxAmount);
		}
	}

	bool NativeFactoryLess(const SatisDash::FFactoryReading& A, const SatisDash::FFactoryReading& B)
	{
		int32 Order = A.Id.Compare(B.Id, ESearchCase::CaseSensitive);
		if (Order == 0)
		{
			Order = A.Name.Compare(B.Name, ESearchCase::CaseSensitive);
		}
		if (Order == 0)
		{
			Order = A.ClassName.Compare(B.ClassName, ESearchCase::CaseSensitive);
		}
		return Order < 0;
	}

	void NativeFactoryWrite(SatisDash::FFactoryJsonWriter& Writer, SatisDash::FFactoryReading& R)
	{
		using namespace SatisDash;
		NativeFactoryCleanRates(R.Production);
		NativeFactoryCleanRates(R.Ingredients);
		NativeFactoryCleanSlots(R.Output);
		if (R.Input.IsSet())
		{
			NativeFactoryCleanSlots(R.Input.GetValue());
		}

		// The bridge's rule: "Unassigned" or no recipe is no recipe, and then no rates and no flags.
		const bool bConfigured = R.Recipe.IsSet() && !R.Recipe->IsEmpty() && !R.Recipe->Equals(TEXT("Unassigned"), ESearchCase::CaseSensitive);
		static const TArray<FRateReading> NoRates;
		const TArray<FRateReading>& Production = bConfigured ? R.Production : NoRates;
		const TArray<FRateReading>& Ingredients = bConfigured ? R.Ingredients : NoRates;

		FFactoryBuildingModel Model;
		Model.Id = R.Id;
		Model.Name = R.Name;
		Model.ClassName = R.ClassName;
		if (bConfigured)
		{
			Model.Recipe = R.Recipe;
		}
		Model.bProducing = R.bProducing;
		Model.bPaused = R.bPaused;
		bool bInputsFull = false;
		DeriveFactoryFlags(R.bPaused, bConfigured, R.Output, Ingredients, R.Input, Model.bBackedUp, bInputsFull);
		if (R.Input.IsSet())
		{
			Model.InputsFull = bInputsFull;
		}
		Model.CircuitGroupId = R.CircuitGroupId;
		using namespace NativeShared;
		if (R.bHasLocation)
		{
			SetLocation(Model, R.XCm, R.YCm, R.ZCm, R.YawDeg);
		}
		Model.ClockSpeedPercent = ClockPercent(R.ClockPercent);
		Model.Production = &Production;
		Model.Ingredients = &Ingredients;
		AppendFactoryBuilding(Writer, Model);
	}
}

SatisDash::FSlotReading SatisDash::MakeSlotReading(const FString& ClassName, int32 NumItems, int32 Capacity, bool bFluid)
{
	// A true division by 1000 for a fluid, as the unit change is (litres to m3); a solid is counted in items.
	const double Scale = bFluid ? 1000.0 : 1.0;
	FSlotReading Slot;
	Slot.ClassName = ClassName;
	Slot.Amount = double(NumItems) / Scale;
	Slot.MaxAmount = double(Capacity) / Scale;
	return Slot;
}

FString SatisDash::NativeFactoryPart(TArray<FFactoryReading> Readings, int32& OutDropped)
{
	OutDropped = 0;
	Readings.StableSort(NativeFactoryLess);

	FString Out;
	const TSharedRef<FFactoryJsonWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	Writer->WriteObjectStart();
	Writer->WriteArrayStart(TEXT("buildings"));
	int32 Written = 0;
	const FString* LastId = nullptr;
	for (FFactoryReading& Reading : Readings)
	{
		if (LastId && LastId->Equals(Reading.Id, ESearchCase::CaseSensitive))
		{
			continue; // a later duplicate of an id already seen
		}
		LastId = &Reading.Id;
		if (Written >= MaxBuildings)
		{
			++OutDropped;
			continue;
		}
		NativeFactoryWrite(*Writer, Reading);
		++Written;
	}
	Writer->WriteArrayEnd();
	Writer->WriteObjectEnd();
	Writer->Close();
	return Out;
}
