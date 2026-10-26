#include "SatisDash/Native/Rates/NativeRates.h"

namespace SatisDash
{
	namespace
	{
		/** Rounded to 4 decimals, so float noise in the clock (1.6f is 1.6000000238) does not show: 8, not 8.0000001. */
		double NativeRatesRound4(double Value)
		{
			return FMath::FloorToDouble(Value * 10000.0 + 0.5) / 10000.0;
		}

		double NativeRatesMax(const FRecipeItemReading& Item, const FRateInput& In, bool bProduct)
		{
			const double Boost = bProduct ? In.ProductionBoost : 1.0;
			if (!FMath::IsFinite(In.CycleSeconds) || In.CycleSeconds <= 0.0
				|| !FMath::IsFinite(In.Potential) || In.Potential < 0.0
				|| !FMath::IsFinite(Boost) || Boost < 0.0
				|| !FMath::IsFinite(Item.AmountPerCycle) || Item.AmountPerCycle < 0.0)
			{
				return 0.0;
			}
			const double Max = (Item.AmountPerCycle / (Item.bFluid ? 1000.0 : 1.0)) * 60.0 / In.CycleSeconds * In.Potential * Boost;
			return FMath::IsFinite(Max) ? NativeRatesRound4(Max) : 0.0;
		}

		double NativeRatesFraction(const FRateInput& In, ERatesMode Mode)
		{
			if (Mode == ERatesMode::Instantaneous)
			{
				return In.bProducing ? 1.0 : 0.0;
			}
			const double Fraction = In.RawProductivity / ProductivityFullScale;
			return FMath::IsFinite(Fraction) ? FMath::Clamp(Fraction, 0.0, 1.0) : 0.0;
		}

		void NativeRatesFill(const TArray<FRecipeItemReading>& Items, const FRateInput& In, bool bProduct, double Fraction, TArray<FRateReading>& Out)
		{
			Out.Reset();
			for (const FRecipeItemReading& Item : Items)
			{
				FRateReading Reading;
				Reading.Name = Item.Name;
				Reading.ClassName = Item.ClassName;
				Reading.Max = NativeRatesMax(Item, In, bProduct);
				Reading.Current = Reading.Max * Fraction;
				Reading.Percent = 100.0 * Fraction;
				Out.Add(MoveTemp(Reading));
			}
		}
	}

	void NativeRates(const FRateInput& In, ERatesMode Mode, TArray<FRateReading>& OutProduction, TArray<FRateReading>& OutIngredients)
	{
		const double Fraction = NativeRatesFraction(In, Mode);
		NativeRatesFill(In.Products, In, true, Fraction, OutProduction);
		NativeRatesFill(In.Ingredients, In, false, Fraction, OutIngredients);
	}
}
