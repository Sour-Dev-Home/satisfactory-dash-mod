#pragma once

#include "CoreMinimal.h"

namespace SatisDash
{
	/** The purity the game reports (EResourcePurity), by the contract's words. */
	enum class ENodePurity : uint8 { Impure, Normal, Pure };
	/** The node types the map draws (EResourceNodeType): fracking cores and deposits are not part of the layer. */
	enum class ENodeKind : uint8 { Node, FrackingSatellite, Geyser };

	/** One resource node as the game reports it, before mapping. Positions are in game units (centimetres). */
	struct FResourceNodeReading
	{
		FString Resource;
		ENodePurity Purity = ENodePurity::Normal;
		ENodeKind Kind = ENodeKind::Node;
		double XCm = 0;
		double YCm = 0;
		bool bExploited = false;
	};

	/** Actors looked at per driver tick during a node scan (about 0.1 ms of class checks). */
	inline constexpr int32 NodeScanActorsPerStep = 2000;
	/** After a scan could not read the world (no world, no actors, no nodes): when to try again. */
	inline constexpr double NodeScanRetrySeconds = 30.0;

	/** A world body built whole: the text, where the data array sits in it, and the counts FWorldRead reports. */
	struct FNativeWorldBody
	{
		FString Json;
		/** Where the array's "[" is in Json, and one past its "]" (the hash covers exactly that). */
		int32 DataFrom = 0;
		int32 DataEnd = 0;
		int32 Items = 0;
		/** Readings with a coordinate outside +/-1,000,000 m. */
		int32 Dropped = 0;
		/** Readings past the 5,000-item cap. */
		int32 Over = 0;
		/** The whole body's UTF-8 bytes. */
		int64 Bytes = 0;
	};

	/**
	 * R4 (ADR-0058): the resourceNodes world body {"observedAt":...,"data":[...]} from the game's own nodes, each record in
	 * the form FResourceNodesParse writes from FRM (one shared record writer). Positions go to whole metres as the map does
	 * (Math.round of cm / 100), nodes outside +/-1,000,000 m are dropped, and the nodes are sorted by (x, y, resource,
	 * purity, type, exploited) before the 5,000-item cap, so the same world always gives the same bytes whatever order the
	 * scan found it in. Sorts and builds text, so it runs on the worker, never the game thread.
	 */
	SATISDASHCORE_API FNativeWorldBody BuildNativeResourceNodes(const FString& ObservedAt, const TArray<FResourceNodeReading>& Readings);
}
