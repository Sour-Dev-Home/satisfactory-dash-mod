#pragma once

#include "CoreMinimal.h"
#include "SatisDash/FrmRecords.h"

namespace SatisDash
{
	/** The mapLive constants, the record writers and JoinMapLive are shared with the native reader: see Records.h. */

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
}
