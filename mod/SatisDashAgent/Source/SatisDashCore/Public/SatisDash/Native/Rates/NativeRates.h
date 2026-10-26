#pragma once

#include "CoreMinimal.h"
#include "SatisDash/FrmFactory.h"

namespace SatisDash
{
	/** How the "current" side of a rate is worked out. Averaged follows the game's productivity measure; Instantaneous follows isProducing. */
	enum class ERatesMode : uint8 { Averaged, Instantaneous };

	/** The switch PR for R2b flips nothing; the owner's fallback ruling (R2b ships max only) flips this one constant. */
	inline constexpr ERatesMode DefaultRatesMode = ERatesMode::Averaged;

	/**
	 * GetProductivity() full scale. 1.0 = a 0..1 fraction (the header does not say; the parity run checks it, Task 10: the
	 * native-parity judge compares current rates with FRM's, and a wrong scale shows there).
	 */
	inline constexpr double ProductivityFullScale = 1.0;

	/** One recipe line as the game reports it: the amount per cycle is in game units (litres for a fluid). */
	struct FRecipeItemReading { FString Name; FString ClassName; double AmountPerCycle = 0; bool bFluid = false; };

	struct FRateInput
	{
		TArray<FRecipeItemReading> Products, Ingredients;
		/** UFGRecipe::GetManufacturingDuration, no modifiers. */
		double CycleSeconds = 0;
		/** GetCurrentPotential. */
		double Potential = 1.0;
		/** GetCurrentProductionBoost: the Somersloop multiplier, applied to products only (FRM's output, run 40378567334). */
		double ProductionBoost = 1.0;
		/** GetProductivity(). */
		double RawProductivity = 0;
		bool bProducing = false;
	};

	/**
	 * R2b (ADR-0058): per-minute rates for one machine. Max is the recipe amount over the base duration, times the clock
	 * (fluids in m3), and products also times the production boost; current and percent follow the mode. Never produces NaN
	 * or infinity: an unusable cycle, clock, boost or productivity gives 0.
	 */
	SATISDASHCORE_API void NativeRates(const FRateInput& In, ERatesMode Mode, TArray<FRateReading>& OutProduction, TArray<FRateReading>& OutIngredients);
}
