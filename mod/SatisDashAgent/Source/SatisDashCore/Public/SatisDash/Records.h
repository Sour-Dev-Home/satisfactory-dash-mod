#pragma once

#include "CoreMinimal.h"

namespace SatisDash
{
	/** {"observedAt":"<ObservedAt>","data": : what a world body (worldReader.ts: { observedAt, data }) is written after. */
	SATISDASHCORE_API FString WorldBodyPrefix(const FString& ObservedAt);

	/** JavaScript's Math.round: a half goes toward +infinity (-1.5 is -1, 2.5 is 3, -0.5 is 0). Not FMath::RoundToDouble, which is floor(x + 0.5) and gets 0.49999999999999994 wrong. */
	SATISDASHCORE_API double JsRound(double Value);
	/** toWholeMetres: Math.round(gameUnits / 100), the division a true division (see Records.cpp). */
	SATISDASHCORE_API double ToWholeMetres(double GameUnits);
	/** inWholeMetreBounds: finite and within +/-1,000,000 m. */
	SATISDASHCORE_API bool InWholeMetreBounds(double WholeMetres);

	/** sampler.ts MAP_LIVE_AGENT_MAX_ITEMS and MAP_LIVE_AGENT_MAX_BYTES (tighter than the schema's MAP_LIVE_MAX_*, 2,000). */
	inline constexpr int32 MapLiveMaxItems = 500;
	inline constexpr int64 MapLiveMaxBytes = 256000;
	/** The bytes of {"trains":  ,"stations":  } around the two arrays: 10 + 12 + 1. */
	inline constexpr int64 MapLiveWrapperBytes = 23;

	/**
	 * One mapLive train record {"id","name","x","y","status"}, written by the one shared writer (the native reader from the
	 * game). Id, Name and Status are cut at 200 characters (ClampString); Mx and My are whole
	 * metres, already in bounds.
	 */
	SATISDASHCORE_API void AppendTrainRecord(FString& Out, const FString& Id, const FString& Name, double Mx, double My, const FString& Status);
	/** One mapLive station record {"id","name","x","y"}, shared the same way. */
	SATISDASHCORE_API void AppendStationRecord(FString& Out, const FString& Id, const FString& Name, double Mx, double My);

	/**
	 * {"trains":<TrainsArray>,"stations":<StationsArray>}, or unset when the whole object would pass MapLiveMaxBytes UTF-8
	 * bytes (sampler.ts leaves the whole part out then, "never cut short by bytes"). The byte counts are the arrays' own,
	 * measured on the worker.
	 */
	SATISDASHCORE_API TOptional<FString> JoinMapLive(const FString& TrainsArray, int64 TrainsBytes, const FString& StationsArray, int64 StationsBytes);

	/** map.ts MAP_WORLD_MAX_ITEMS and MAP_WORLD_MAX_POLYLINE_POINTS. */
	inline constexpr int32 MaxRailSegments = 5000;
	inline constexpr int32 MaxRailPoints = 1000;

	/** map.ts MAP_WORLD_MAX_ITEMS, and the bound on purity and nodeType. */
	inline constexpr int32 MaxResourceNodes = 5000;
	inline constexpr int32 MaxNodeFieldChars = 40;

	/** nodeType.ts mapNodeType: "Node", "Fracking Satellite" and "Geyser" map explicitly; anything else is camel-cased on JavaScript's \s. */
	SATISDASHCORE_API FString MapNodeType(const FString& Raw);

	/**
	 * Appends one resourceNodes record {"type","purity","nodeType","x","y","exploited"[,"className"]} with the contract's
	 * bounds: type cut to 200, purity lowercased and cut to MaxNodeFieldChars, nodeType (already mapped) cut to
	 * MaxNodeFieldChars, x and y whole metres, className (the game's descriptor class name) cut to 200 and
	 * written after exploited only when it is not empty. Used by BuildNativeResourceNodes (from the game).
	 */
	SATISDASHCORE_API void AppendResourceNodeRecord(FString& Out, const FString& Type, const FString& Purity, const FString& NodeType, double Mx, double My, bool bExploited, const FString& ClassName);
}
