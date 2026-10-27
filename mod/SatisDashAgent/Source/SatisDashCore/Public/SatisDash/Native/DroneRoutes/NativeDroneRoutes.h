#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Native/ResourceNodes/NativeResourceNodes.h"

namespace SatisDash
{
	inline constexpr int32 DroneRoutesMaxItems = 2000;       // map.ts DRONE_ROUTES_MAX_ITEMS
	inline constexpr int32 DroneRoutesMaxItemClasses = 64;   // map.ts TRAIN_ROUTES_MAX_ITEM_CLASSES (the item-class cap the port schema reuses)

	inline constexpr int32 DroneRoutesMaxStringChars = 200;  // map.ts boundedString

	/** One drone port as the game gave it. */
	struct FDronePortReading
	{
		/** The buildable's GetName(). */
		FString Id;
		/** The building tag, as the game gives it. */
		FString Name;
		bool bPaired = false;
		/** The partner's tag; meaningful when bPaired. */
		FString PairedName;
		/** The partner buildable's GetName(); empty = unresolved (unpaired, or the partner's buildable is gone). */
		FString PairedId;
		/** Item class names, any order, duplicates allowed. */
		TArray<FString> Input, Output, Fuel;
		double XCm = 0;
		double YCm = 0;
	};

	/**
	 * R9 (ADR-0058): the droneRoutes world body {"observedAt":...,"data":[{"id","name","paired","pairedId","input","output","fuel","x","y"}]},
	 * the dash droneRoutesMapper ported plus pairedId. paired is the partner's name, null when unpaired, empty or over 200 characters.
	 * pairedId is the partner's id only when the port is paired and that id belongs to another (or the same) port kept in this layer, else null, so it never
	 * names a dropped, capped or missing port. A port with an empty id (the dash schema's min(1)), or an id or name over 200 characters, or a position outside +/-1,000,000 m, is
	 * left out and counted in Dropped. Item classes are distinct, sorted in code-unit order, empty or over-200 ones skipped, at most
	 * DroneRoutesMaxItemClasses. Ports are sorted by (id, content); a repeated id keeps the first (the rest count in Dropped); at most
	 * DroneRoutesMaxItems are kept (Over counts the rest). Sorts and builds text, so it runs on the worker, never the game thread.
	 */
	SATISDASHCORE_API FNativeWorldBody BuildNativeDroneRoutes(const FString& ObservedAt, const TArray<FDronePortReading>& Readings);
}
