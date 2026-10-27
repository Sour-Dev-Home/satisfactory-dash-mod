#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Native/ResourceNodes/NativeResourceNodes.h"

namespace SatisDash
{
	inline constexpr int32 PowerLinesMaxItems = 20000;       // map.ts POWER_LINES_MAX_ITEMS

	/** One power wire as the game gave it. */
	struct FPowerLineReading
	{
		/** The wire's GetName(). */
		FString Id;
		/** Both ends, world cm, in the order the game gives them. */
		double X0Cm = 0;
		double Y0Cm = 0;
		double X1Cm = 0;
		double Y1Cm = 0;
	};

	/**
	 * R10 (ADR-0058): the powerLines world body {"observedAt":...,"data":[{"id":"...","points":[[x0,y0],[x1,y1]]}]}, the dash powerLinesMapper ported.
	 * Each end is whole metres (ToWholeMetres); the end order is kept as given. A wire with an empty id, or any of the four
	 * coordinates outside +/-1,000,000 m, is left out and counted in Dropped. Two ends that round to the same metre are kept.
	 * The id is cut at 200 characters (ClampString). Lines are sorted by (id, then the points text); a repeated id keeps the first (the
	 * rest count in Dropped); at most PowerLinesMaxItems are kept (Over counts the rest). Sorts and builds text, so it runs on the
	 * worker, never the game thread; the body cap (WorldLayerMaxBytes) is applied by the world read that carries it.
	 */
	SATISDASHCORE_API FNativeWorldBody BuildNativePowerLines(const FString& ObservedAt, const TArray<FPowerLineReading>& Readings);
}
