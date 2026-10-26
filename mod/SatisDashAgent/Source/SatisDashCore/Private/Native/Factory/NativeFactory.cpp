#include "SatisDash/Native/Factory/NativeFactory.h"

namespace
{
	/** By the exponent bits: UE builds with /fp:fast, where the compiler may assume no infinity and fold isfinite away. */
	bool NativeFactoryFinite(double Value)
	{
		uint64 Bits = 0;
		FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
		return (Bits & 0x7FF0000000000000ull) != 0x7FF0000000000000ull;
	}

	/** Zero for anything the bridge's schema would refuse: negative, -0 or not finite. */
	double NativeFactoryNonNegative(double Value)
	{
		return NativeFactoryFinite(Value) && Value > 0 ? Value : 0.0;
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
		if (R.bHasLocation && NativeFactoryFinite(R.XCm) && NativeFactoryFinite(R.YCm) && NativeFactoryFinite(R.ZCm) && NativeFactoryFinite(R.YawDeg))
		{
			Model.bHasLocation = true;
			Model.XM = FrmFactoryMetres(R.XCm);
			Model.YM = FrmFactoryMetres(R.YCm);
			Model.ZM = FrmFactoryMetres(R.ZCm);
			Model.YawDeg = FrmFactoryYaw(R.YawDeg);
		}
		if (NativeFactoryFinite(R.ClockPercent))
		{
			// To 0.01 by a true division, as the metres are, so 159.99999 is 160; nothing positive left is not reported.
			const double Hundredths = FrmFactoryMetres(FMath::RoundToDouble(R.ClockPercent * 100.0));
			if (Hundredths > 0)
			{
				Model.ClockSpeedPercent = Hundredths;
			}
		}
		Model.Production = &Production;
		Model.Ingredients = &Ingredients;
		AppendFactoryBuilding(Writer, Model);
	}
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
