#pragma once

#include "CoreMinimal.h"
#include "SatisDash/FrmRails.h"
#include "SatisDash/Native/ResourceNodes/NativeResourceNodes.h"

namespace SatisDash
{
	/** Survey ruling S2: one sample a metre of track. */
	inline constexpr double RailSampleStepCm = 100.0;

	/** Which game call gives a track's points. */
	enum class ERailSampler : uint8
	{
		SplineComponent,
		GameTrackSampler
	};

	/** Ruling M2: the parity run judges the choice; the fallback is this one constant. */
	inline constexpr ERailSampler NativeRailSampler = ERailSampler::SplineComponent;

	/** One sampled point, world space, centimetres. */
	struct FRailPointCm
	{
		double XCm = 0;
		double YCm = 0;
	};

	/** One track as the game read it: its name and its sampled points, in order along the track. */
	struct FRailReading
	{
		FString Id;
		TArray<FRailPointCm> Points;
	};

	/** Intervals to sample a track of LengthCm: ceil(Length / RailSampleStepCm), at least 1, at most MaxRailPoints - 1 (999); a non-finite or non-positive length gives 1. */
	SATISDASHCORE_API int32 RailSampleIntervals(double LengthCm);

	/** Distance of sample Index (0..Intervals): Length * Index / Intervals; Index == Intervals returns Length exactly. */
	SATISDASHCORE_API double RailSampleDistanceCm(double LengthCm, int32 Intervals, int32 Index);

	/**
	 * R5 (ADR-0058): the rails world body {"observedAt":...,"data":[{"id","points":[[x,y],...]}]} from the game's own tracks,
	 * in the layout FRailsParse writes from FRM. Points go to whole metres (Math.round semantics); a point outside +/-1,000,000 m
	 * is skipped, not the track; consecutive equal whole-metre points collapse. A track with fewer than 2 points left, more than
	 * MaxRailPoints, or an empty id is left out and counted in Dropped. Tracks are sorted by (id, points) so the same readings
	 * in any order give the same bytes; a repeated id keeps the first; at most MaxRailSegments are kept (Over counts the rest).
	 * Sorts and builds text, so it runs on the worker, never the game thread.
	 */
	SATISDASHCORE_API FNativeWorldBody BuildNativeRails(const FString& ObservedAt, const TArray<FRailReading>& Readings);
}
