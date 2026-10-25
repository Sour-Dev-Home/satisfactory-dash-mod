#pragma once

#include "CoreMinimal.h"
#include "SatisDash/FrmRecords.h"

namespace SatisDash
{
	/** sampler.ts MAP_LIVE_AGENT_MAX_ITEMS and MAP_LIVE_AGENT_MAX_BYTES (tighter than the schema's MAP_LIVE_MAX_*, 2,000). */
	inline constexpr int32 MapLiveMaxItems = 500;
	inline constexpr int64 MapLiveMaxBytes = 256000;
	/** The bytes of {"trains":  ,"stations":  } around the two arrays: 10 + 12 + 1. */
	inline constexpr int64 MapLiveWrapperBytes = 23;

	/**
	 * getTrains' body to the trains array [{id,name,x,y,status}], as the Node agent makes it (rawSchemas.ts RawFrmTrainSchema,
	 * trainsMapper.ts, coordinates.ts, then conformMapLive's 500 and 200). Dropped() counts trains with a coordinate the
	 * contract can't hold (they do not count towards the 500).
	 */
	class SATISDASHCORE_API FTrainsParse final : public FFrmArrayParse
	{
	public:
		explicit FTrainsParse(TArray<uint8> InBody);

	protected:
		virtual ERecord ReadRecord(bool bRoom) override;
	};

	/** getTrainStation's body to the stations array [{id,name,x,y}] (RawFrmTrainStationSchema, stationsMapper.ts). */
	class SATISDASHCORE_API FStationsParse final : public FFrmArrayParse
	{
	public:
		explicit FStationsParse(TArray<uint8> InBody);

	protected:
		virtual ERecord ReadRecord(bool bRoom) override;
	};

	/**
	 * {"trains":<TrainsArray>,"stations":<StationsArray>}, or unset when the whole object would pass MapLiveMaxBytes UTF-8
	 * bytes (sampler.ts leaves the whole part out then, "never cut short by bytes"). The byte counts are the arrays' own
	 * (FFrmArrayParse::Utf8Bytes), measured on the worker.
	 */
	SATISDASHCORE_API TOptional<FString> JoinMapLive(const FString& TrainsArray, int64 TrainsBytes, const FString& StationsArray, int64 StationsBytes);
}
