#include "SatisDash/FactoryRecords.h"

#include "SatisDash/Snapshot.h"

namespace
{
	void FactoryWriteRates(SatisDash::FFactoryJsonWriter& Writer, const TCHAR* Field, const TArray<SatisDash::FRateReading>* Rates)
	{
		Writer.WriteArrayStart(Field);
		const int32 Keep = Rates ? FMath::Min(Rates->Num(), SatisDash::MaxRatesPerBuilding) : 0;
		for (int32 I = 0; I < Keep; ++I)
		{
			const SatisDash::FRateReading& Rate = (*Rates)[I];
			Writer.WriteObjectStart();
			Writer.WriteValue(TEXT("name"), SatisDash::ClampString(Rate.Name));
			Writer.WriteValue(TEXT("className"), SatisDash::ClampString(Rate.ClassName));
			Writer.WriteValue(TEXT("currentPerMinute"), Rate.Current);
			Writer.WriteValue(TEXT("maxPerMinute"), Rate.Max);
			Writer.WriteValue(TEXT("percent"), Rate.Percent);
			Writer.WriteObjectEnd();
		}
		Writer.WriteArrayEnd();
	}
}

/**
 * Centimetres to metres, a true division as JavaScript does it. UE builds with /fp:fast, which turns a division by the
 * constant 100.0 into a multiplication by 0.01 and lands one ulp away for some values (4800.0126953125 / 100); the
 * volatile divisor keeps the compiler from seeing a constant, so the output equals the Node agent's to the bit.
 */
double SatisDash::FactoryMetres(double Centimetres)
{
	volatile double Hundred = 100.0;
	return Centimetres / Hundred;
}

/** conform.ts normalizeYaw: into [0, 360); float noise that lands on 360 is 0. JavaScript's % is fmod. */
double SatisDash::FactoryYaw(double Degrees)
{
	const double Yaw = FMath::Fmod(FMath::Fmod(Degrees, 360.0) + 360.0, 360.0);
	return Yaw >= 360.0 ? 0.0 : Yaw;
}

void SatisDash::DeriveFactoryFlags(bool bPaused, bool bConfigured, const TArray<FSlotReading>& Output, const TArray<FRateReading>& Ingredients, const TOptional<TArray<FSlotReading>>& Input, bool& OutBackedUp, bool& OutInputsFull)
{
	// factoryMapper.ts isBackedUp and areInputsFull, on the whole lists (they run before conform cuts them).
	OutBackedUp = false;
	if (!bPaused && bConfigured)
	{
		for (const FSlotReading& Slot : Output)
		{
			OutBackedUp = OutBackedUp || (Slot.MaxAmount > 0 && Slot.Amount >= Slot.MaxAmount);
		}
	}
	OutInputsFull = !bPaused && bConfigured && Ingredients.Num() > 0 && Input.IsSet();
	for (int32 I = 0; OutInputsFull && I < Ingredients.Num(); ++I)
	{
		bool bFull = false;
		for (const FSlotReading& Slot : Input.GetValue())
		{
			bFull = bFull || (Slot.ClassName.Equals(Ingredients[I].ClassName, ESearchCase::CaseSensitive) && Slot.MaxAmount > 0 && Slot.Amount >= Slot.MaxAmount);
		}
		OutInputsFull = bFull;
	}
}

void SatisDash::AppendFactoryBuilding(FFactoryJsonWriter& Writer, const FFactoryBuildingModel& B)
{
	Writer.WriteObjectStart();
	Writer.WriteValue(TEXT("id"), ClampString(B.Id));
	Writer.WriteValue(TEXT("name"), ClampString(B.Name));
	Writer.WriteValue(TEXT("className"), ClampString(B.ClassName));
	if (B.Recipe.IsSet())
	{
		Writer.WriteValue(TEXT("recipe"), ClampString(B.Recipe.GetValue()));
	}
	else
	{
		Writer.WriteNull(TEXT("recipe"));
	}
	Writer.WriteValue(TEXT("isProducing"), B.bProducing);
	Writer.WriteValue(TEXT("isPaused"), B.bPaused);
	Writer.WriteValue(TEXT("isBackedUp"), B.bBackedUp);
	if (B.InputsFull.IsSet())
	{
		Writer.WriteValue(TEXT("inputsFull"), B.InputsFull.GetValue());
	}
	Writer.WriteValue(TEXT("circuitGroupId"), B.CircuitGroupId);
	if (B.bHasLocation)
	{
		Writer.WriteObjectStart(TEXT("location"));
		Writer.WriteValue(TEXT("xM"), B.XM);
		Writer.WriteValue(TEXT("yM"), B.YM);
		Writer.WriteValue(TEXT("zM"), B.ZM);
		Writer.WriteValue(TEXT("rotationDeg"), B.YawDeg);
		Writer.WriteObjectEnd();
	}
	if (B.ClockSpeedPercent.IsSet())
	{
		Writer.WriteValue(TEXT("clockSpeedPercent"), B.ClockSpeedPercent.GetValue());
	}
	FactoryWriteRates(Writer, TEXT("production"), B.Production);
	FactoryWriteRates(Writer, TEXT("ingredients"), B.Ingredients);
	Writer.WriteObjectEnd();
}
