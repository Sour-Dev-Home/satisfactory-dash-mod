#pragma once

#include "CoreMinimal.h"
#include "SatisDash/FrmRecords.h"

namespace SatisDash
{
	/** map.ts MAP_WORLD_MAX_ITEMS, and the bound on purity and nodeType. */
	inline constexpr int32 MaxResourceNodes = 5000;
	inline constexpr int32 MaxNodeFieldChars = 40;

	/** nodeType.ts mapNodeType: "Node", "Fracking Satellite" and "Geyser" map explicitly; anything else is camel-cased on JavaScript's \s. */
	SATISDASHCORE_API FString MapNodeType(const FString& Raw);

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
