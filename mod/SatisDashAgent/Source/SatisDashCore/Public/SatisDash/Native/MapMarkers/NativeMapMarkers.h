#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Native/ResourceNodes/NativeResourceNodes.h"

namespace SatisDash
{
	inline constexpr int32 MapMarkersMaxItems = 1000;   // map.ts MAP_MARKERS_MAX_ITEMS (Q10)
	inline constexpr int32 MapMarkerNameMaxChars = 60;  // map.ts MAP_MARKER_NAME_MAX_CHARS
	inline constexpr int32 MapMarkerMaxIcon = 1000000;  // map.ts MapMarkerSchema icon, max

	/** Q1: which conversion turns the game's FLinearColor into the contract's #rrggbb. Parity decides; one constant flips it. */
	inline constexpr bool NativeMarkerColorSRGB = true;

	enum class EMapMarkerKind : uint8
	{
		Marker,
		Stamp
	};

	/** One marker as the game gave it. */
	struct FMapMarkerReading
	{
		/** Guid digits, upper case (Q2); empty = no valid Guid (M4). */
		FString Id;
		/** Raw, as the game gives it. */
		FString Name;
		EMapMarkerKind Kind = EMapMarkerKind::Marker;
		int32 Icon = 0;
		FLinearColor Color = FLinearColor::White;
		double XCm = 0;
		double YCm = 0;
	};

	/** The dash markerName: lone surrogate -> U+FFFD, JavaScript trim, cut to 60 UTF-16 units without splitting a pair (M3). */
	SATISDASHCORE_API FString MapMarkerName(const FString& Raw);

	/** "#rrggbb", lower case: Color.ToFColor(bSRGB) then the first six of ToHex (alpha dropped); a NaN or non-finite channel gives "#ffffff". */
	SATISDASHCORE_API FString MapMarkerColorHex(const FLinearColor& Color, bool bSRGB);

	/**
	 * R8 (ADR-0058): the mapMarkers world body {"observedAt":...,"data":[{"id","name","kind","icon","color","x","y"}]}, the dash
	 * mapMarkersMapper ported. x and y go to whole metres (Math.round semantics); a marker outside +/-1,000,000 m, with an icon
	 * outside 0..MapMarkerMaxIcon, or an empty id is left out and counted in Dropped. Markers are sorted by (id, item text) so the
	 * same readings in any order give the same bytes; a repeated id keeps the first; at most MapMarkersMaxItems are kept (Over
	 * counts the rest). Sorts and builds text, so it runs on the worker, never the game thread.
	 */
	SATISDASHCORE_API FNativeWorldBody BuildNativeMapMarkers(const FString& ObservedAt, const TArray<FMapMarkerReading>& Readings);
}
