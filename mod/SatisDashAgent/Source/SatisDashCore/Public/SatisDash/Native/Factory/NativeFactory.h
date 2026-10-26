#pragma once

#include "CoreMinimal.h"
#include "SatisDash/FrmFactory.h"

namespace SatisDash
{
	/**
	 * One machine as the game reports it, before mapping (R2a, ADR-0058). Rates and slots are already in the units the
	 * snapshot uses (FRateReading and FSlotReading are shared with the FRM parse); positions are in game units (centimetres)
	 * and the yaw in degrees.
	 */
	struct FFactoryReading
	{
		/** GetName(), mDisplayName and GetClass()->GetName(). */
		FString Id, Name, ClassName;
		/** Unset: no recipe. */
		TOptional<FString> Recipe;
		bool bProducing = false;
		bool bPaused = false;
		TArray<FRateReading> Production, Ingredients;
		TArray<FSlotReading> Output;
		/** Unset: the machine has no input inventory (then "inputsFull" is left out). */
		TOptional<TArray<FSlotReading>> Input;
		int64 CircuitGroupId = -1;
		bool bHasLocation = false;
		double XCm = 0, YCm = 0, ZCm = 0, YawDeg = 0;
		/** 100 * the clock potential; 0 is not reported. */
		double ClockPercent = 0;
	};

	/**
	 * The snapshot's factory part, {"buildings":[...]}, from the game's own machines, each record written by the same
	 * AppendFactoryBuilding the FRM parse uses. Sorted by Id (ordinal, ties by name and class name), a later duplicate id
	 * dropped, the first MaxBuildings kept (the rest counted in OutDropped), so the same world always gives the same bytes
	 * whatever order the scan found it in. Values the bridge would refuse (a negative or non-finite rate, a non-finite
	 * coordinate) are made zero, or the location left out, never written. Sorts and builds text: the worker runs it, never
	 * the game thread.
	 */
	SATISDASHCORE_API FString NativeFactoryPart(TArray<FFactoryReading> Readings, int32& OutDropped);
}
