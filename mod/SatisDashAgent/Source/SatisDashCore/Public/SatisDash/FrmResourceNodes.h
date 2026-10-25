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
	 * Appends one resourceNodes record {"type","purity","nodeType","x","y","exploited"} with the contract's bounds: type cut to
	 * 200, purity lowercased and cut to MaxNodeFieldChars, nodeType (already mapped) cut to MaxNodeFieldChars, x and y whole
	 * metres. Shared by FResourceNodesParse (from FRM) and BuildNativeResourceNodes (from the game).
	 */
	SATISDASHCORE_API void AppendResourceNodeRecord(FString& Out, const FString& Type, const FString& Purity, const FString& NodeType, double Mx, double My, bool bExploited);

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
