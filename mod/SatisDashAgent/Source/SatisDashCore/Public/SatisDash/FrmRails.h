#pragma once

#include "CoreMinimal.h"
#include "SatisDash/FrmRecords.h"

namespace SatisDash
{
	/** map.ts MAP_WORLD_MAX_ITEMS and MAP_WORLD_MAX_POLYLINE_POINTS. */
	inline constexpr int32 MaxRailSegments = 5000;
	inline constexpr int32 MaxRailPoints = 1000;

	/**
	 * getTrainRails' body to the rails world body {"observedAt":...,"data":[{"id","points":[[x,y],...]}]}, as the Node agent
	 * makes it (rawSchemas.ts RawFrmTrainRailSchema, railsMapper.ts, coordinates.ts). Dropped() counts segments left with
	 * fewer than 2 points or with more than MaxRailPoints; Over() those past MaxRailSegments.
	 */
	class SATISDASHCORE_API FRailsParse final : public FFrmArrayParse
	{
	public:
		FRailsParse(TArray<uint8> InBody, const FString& ObservedAt);

	protected:
		virtual ERecord ReadRecord(bool bRoom) override;

	private:
		bool ReadSpline(EJsonNotation Started, TArray<int32>& Points, bool& bTooLong);
	};
}
