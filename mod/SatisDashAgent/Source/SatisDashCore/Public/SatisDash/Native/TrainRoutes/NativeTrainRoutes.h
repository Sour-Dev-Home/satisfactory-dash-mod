#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Native/ResourceNodes/NativeResourceNodes.h"
#include "SatisDash/Native/Trains/NativeTrains.h"

namespace SatisDash
{
	/** map.ts TRAIN_ROUTES_MAX_*: records in the layer, platforms per station, item classes per record, stops per train. */
	inline constexpr int32 TrainRoutesMaxItems = 4000;
	inline constexpr int32 TrainRoutesMaxPlatforms = 64;
	inline constexpr int32 TrainRoutesMaxItemClasses = 64;
	inline constexpr int32 TrainRoutesMaxStops = 200;

	/**
	 * R3 (ADR-0058): the trainRoutes world body {"observedAt":...,"data":[...]} from the game's own stations and trains. The
	 * bridge never sent this layer, so the record layout is the contract's TrainRoutesLayerDataSchema:
	 *  - {"recordType":"station","id","name","platforms":[{"id","mode":"load"|"unload","items":[...]}]}, stations first,
	 *    sorted by id; platforms sorted by id and cut at 64; items distinct, sorted, cut at 64.
	 *  - {"recordType":"train","id","name","stops":[...],"items":[...]}, trains sorted by id after the stations; stops in
	 *    timetable order, cut at 200.
	 * Names are kept as the game gives them (a trailing space stays). A reading with an empty id is left out and counted in
	 * Dropped (an empty item class is left out of its list). The whole layer is cut at 4,000 records (Over counts the rest).
	 * Every ordering is total, so the same readings in any order give the same bytes. Sorts and builds text, so it runs on
	 * the worker, never the game thread; the body cap (WorldLayerMaxBytes) is applied by the world read that carries it.
	 */
	SATISDASHCORE_API FNativeWorldBody BuildNativeTrainRoutes(const FString& ObservedAt, const TArray<FStationReading>& Stations, const TArray<FTrainReading>& Trains);
}
