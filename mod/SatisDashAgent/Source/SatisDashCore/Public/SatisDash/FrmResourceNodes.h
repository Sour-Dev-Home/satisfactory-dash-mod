#pragma once

#include "CoreMinimal.h"
#include "SatisDash/FrmRecords.h"

namespace SatisDash
{
	/** MaxResourceNodes, MaxNodeFieldChars, MapNodeType and AppendResourceNodeRecord (the record writer the native reader shares) are in Records.h. */

	/**
	 * getResourceNode's body to the resourceNodes world body {"observedAt":...,"data":[{"type","purity","nodeType","x","y",
	 * "exploited"}]} (rawSchemas.ts RawFrmResourceNodeSchema, resourceNodesMapper.ts). Dropped() counts nodes with a bad coordinate.
	 */
	class SATISDASHCORE_API FResourceNodesParse final : public FFrmArrayParse
	{
	public:
		FResourceNodesParse(TArray<uint8> InBody, const FString& ObservedAt);

	protected:
		virtual ERecord ReadRecord(bool bRoom) override;
	};
}
