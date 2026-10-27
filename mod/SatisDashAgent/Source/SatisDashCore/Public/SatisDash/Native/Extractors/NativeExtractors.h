#pragma once

#include "CoreMinimal.h"

namespace SatisDash
{
	/** AGENT_INPUT_MAX_EXTRACTORS in the vendored contract (agent.ts). */
	inline constexpr int32 ExtractorsMaxItems = 5000;

	/**
	 * One extractor (miner, oil or water extractor, resource-well extractor) as the game reports it, before mapping (S4-B,
	 * ADR-0058). Positions are in game units (centimetres), the yaw in degrees, the clock in percent.
	 */
	struct FExtractorReading
	{
		/** GetName(), mDisplayName and GetClass()->GetName(). */
		FString Id, Name, ClassName;
		bool bProducing = false, bPaused = false;
		/** True only when the game reader said so; Core never infers it. */
		bool bBackedUp = false;
		/** 100 * the clock potential; 0 is not reported. */
		double ClockPercent = 100.0;
		double XCm = 0, YCm = 0, ZCm = 0, YawDeg = 0;
		int32 CircuitGroupId = -1;
		/** Empty = unoccupied or unknown: the extractor is written with no production. */
		FString ResourceClass;
		/** The resource's display name; empty falls back to ResourceClass. */
		FString ResourceName;
		/** m3 per minute for a fluid, items per minute for a solid. */
		double CurrentPerMinute = 0, MaxPerMinute = 0;
		bool bFluid = false;
	};

	/**
	 * The extractors list: JSON array text, spliced into the factory part as "extractors". Each record is written by the
	 * same AppendFactoryBuilding the buildings use (recipe null, no ingredients, production the resource). Sorted by Id
	 * (ordinal, ties by the rest of the content), a later duplicate id dropped, the first ExtractorsMaxItems kept (the
	 * rest counted in OutOver). A reading with a non-finite rate is dropped and counted in OutDropped; a negative rate is
	 * made zero, and a non-finite coordinate or clock is left out, as the factory part does.
	 * Sorts and builds text: the worker runs it, never the game thread.
	 */
	SATISDASHCORE_API FString NativeExtractorsFragment(TArray<FExtractorReading> Readings, int32& OutDropped, int32& OutOver);
}
