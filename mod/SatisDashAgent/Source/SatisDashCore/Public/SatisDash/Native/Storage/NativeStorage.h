#pragma once

#include "CoreMinimal.h"

namespace SatisDash
{
	/** STORAGE_MAX_CONTAINERS in the vendored contract (storage.ts). */
	inline constexpr int32 StorageMaxContainers = 2000;
	/** STORAGE_MAX_ITEMS_PER_CONTAINER in the vendored contract (storage.ts). */
	inline constexpr int32 StorageMaxItems = 64;

	/** One stack (or, in a fluid buffer, the fluid). Amount is items for a solid, m3 for a fluid. */
	struct FStorageItemReading
	{
		FString ClassName, Name;
		double Amount = 0;
		/** Items per full stack. Unset (a fluid, or not read) is written as null. */
		TOptional<int32> StackMax;
	};

	/**
	 * One container as the game reports it, before mapping (S4-B, ADR-0058). Positions are in game units (centimetres).
	 * No yaw is read for a container: the location is written with rotationDeg 0 (the contract requires the key).
	 */
	struct FContainerReading
	{
		/** GetName(), mDisplayName and GetClass()->GetName(). */
		FString Id, Name, ClassName;
		bool bHasLocation = false;
		double XCm = 0, YCm = 0, ZCm = 0;
		int32 Slots = 0, SlotsUsed = 0;
		/** Set ONLY on a fluid buffer: it is the contract's one test for one. */
		TOptional<double> FluidCapacityM3;
		/** Per stack; merged per class here. */
		TArray<FStorageItemReading> Items;
	};

	/**
	 * The storage part: JSON text {"containers":[...]}, keys in the vendored contract's order. Items are merged per class (amounts
	 * summed, name and stackMax of the first stack of that class), sorted by class and cut at StorageMaxItems (OutItemsCut counts
	 * the containers cut). Containers are sorted by the written id (ordinal, ties by the written content), a later duplicate of
	 * that id dropped, the first StorageMaxContainers kept (the rest counted in OutOver). slotsUsed is clamped to slots. A
	 * container with a negative or non-finite fluid capacity is dropped and counted in OutDropped; a negative or non-finite
	 * amount is made zero. Ids, names and class names are cut at 200 characters.
	 * Sorts and builds text: the worker runs it, never the game thread.
	 */
	SATISDASHCORE_API FString NativeStorageFragment(TArray<FContainerReading> Readings, int32& OutDropped, int32& OutOver, int32& OutItemsCut);
}
