#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Native/ResourceNodes/NativeResourceNodes.h"

namespace SatisDash
{
	inline constexpr int32 ConveyorsMaxEdges = 50000;      // map.ts CONVEYORS_MAX_EDGES
	inline constexpr int32 ConveyorMaxIdLength = 128;
	inline constexpr int32 ConveyorMaxTierLength = 64;

	enum class EConveyorKind : uint8 { Belt, Lift, Pipe };
	enum class EConveyorNodeKind : uint8 { Machine, Splitter, Merger, Storage, Station, Extractor, Consumer, Junction, Conveyor, Other };

	/** Survey ruling S5: how a belt's or lift's items per minute is derived from GetSpeed(). The census run settles it; one constant flips it. */
	enum class ECapacityMode : uint8 { SpeedHalf, ClassTable };
	inline constexpr ECapacityMode NativeConveyorCapacityMode = ECapacityMode::SpeedHalf;

	/** One belt, lift or pipe as the game read it. */
	struct FConveyorReading
	{
		FString Id;                 // GetName()
		EConveyorKind Kind = EConveyorKind::Belt;
		FString FromId, ToId;       // end 0 / end 1 owner GetName(); empty = open end
		FString Tier;               // mDisplayName.ToString()
		double RawRate = 0;         // belts/lifts: GetSpeed(); pipes: GetFlowLimit() (m3/s)
		double XCm = 0, YCm = 0;    // end 0 connector location, world space
	};

	/** One belt, lift or pipe tier as the census saw it (R6): game data (a display name), numbers; for the one-off census log lines. */
	struct FTierCount
	{
		EConveyorKind Kind = EConveyorKind::Belt;
		FString Name;
		double RawRate = 0;   // the first reading's raw rate for this tier
		int32 Count = 0;
	};

	inline constexpr int32 ConveyorMaxTiers = 16;

	/** Census: counts one edge into Tiers by exact (Kind, Name) pair, keeping the first RawRate; a new pair past ConveyorMaxTiers entries is ignored. */
	SATISDASHCORE_API void NoteConveyorTier(TArray<FTierCount>& Tiers, EConveyorKind Kind, const FString& Name, double RawRate);

	/** The dashboard adapter's classifyConveyorEnd, row for row and in order: the class part of an end id (one trailing _<digits> removed) typed by prefix. */
	SATISDASHCORE_API EConveyorNodeKind ClassifyConveyorEnd(const FString& Id);

	/** "machine", "splitter", "merger", "storage", "station", "extractor", "consumer", "junction", "conveyor", "other". */
	SATISDASHCORE_API const TCHAR* ConveyorNodeKindText(EConveyorNodeKind Kind);

	/**
	 * Belts and lifts: SpeedHalf gives RawRate / 2 (ITEM_SPACING 120: speed x 60 / 120); ClassTable reads the number after the last "Mk."
	 * in Tier (1 -> 60, 2 -> 120, 3 -> 270, 4 -> 480, else unset). Pipes: RawRate * 60 in both modes. Rounded to 4 decimals;
	 * non-finite or negative is unset.
	 */
	SATISDASHCORE_API TOptional<double> ConveyorCapacityPerMinute(EConveyorKind Kind, double RawRate, const FString& Tier, ECapacityMode Mode);

	/**
	 * R6 (ADR-0058): the conveyors world body {"observedAt":...,"data":[edge,...]} from the game's own belts, lifts and pipes, each
	 * edge as the adapter writes it (key order id, kind, from, fromKind, to, toKind, capacityPerMinute, tier, at). A reading with an
	 * empty or over-128 id, an over-128 end id, an empty tier or an end-0 position outside +/-1,000,000 m is dropped (Dropped); an
	 * empty end id is an open end (null, null). Edges sort by id (code units), a repeated id keeps the first by (id, from, to, tier);
	 * at most ConveyorsMaxEdges are kept (Over counts the rest). Sorts and builds text, so it runs on the worker.
	 */
	SATISDASHCORE_API FNativeWorldBody BuildNativeConveyors(const FString& ObservedAt, const TArray<FConveyorReading>& Readings);
}
