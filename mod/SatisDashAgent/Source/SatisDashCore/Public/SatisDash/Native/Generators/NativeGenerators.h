#pragma once

#include "CoreMinimal.h"

namespace SatisDash
{
	/** AGENT_INPUT_MAX_GENERATORS in the vendored contract (agent.ts). */
	inline constexpr int32 GeneratorsMaxItems = 5000;

	/** An item in a generator's fuel or supplement slot. Amount is in the contract's unit: items for a solid, m3 for a fluid. */
	struct FFuelReading
	{
		FString ClassName, Name;
		double Amount = 0;
	};

	/**
	 * One generator as the game reports it, before mapping (S4-B, ADR-0058). Positions are in game units (centimetres), the
	 * yaw in degrees, the clock in percent, power in MW.
	 */
	struct FGeneratorReading
	{
		/** GetName(), mDisplayName and GetClass()->GetName(). */
		FString Id, Name, ClassName;
		bool bHasLocation = false;
		double XCm = 0, YCm = 0, ZCm = 0, YawDeg = 0;
		/** -1 = not connected. */
		int32 CircuitGroupId = -1;
		double CurrentMW = 0, CapacityMW = 0;
		/** 100 * the clock potential; 0 is not reported. */
		double ClockPercent = 100;
		bool bProducing = false;
		/** Unset is written as "fuel":null (always present). */
		TOptional<FFuelReading> Fuel;
		/** False = the supplement was not read: the key is absent. True with no value = read and empty: null. */
		bool bSupplementRead = false;
		TOptional<FFuelReading> Supplement;
	};

	/**
	 * The generators list: JSON array text, the power part's "generators". Keys in the vendored contract's order. Sorted by Id
	 * (ordinal, ties by the rest of the content), a later duplicate id dropped, the first GeneratorsMaxItems kept (the rest
	 * counted in OutOver). A reading with a negative or non-finite MW is dropped and counted in OutDropped; a negative or
	 * non-finite fuel amount is made zero. Ids, names and class names are cut at 200 characters.
	 * Sorts and builds text: the worker runs it, never the game thread.
	 */
	SATISDASHCORE_API FString NativeGeneratorsFragment(TArray<FGeneratorReading> Readings, int32& OutDropped, int32& OutOver);
}
