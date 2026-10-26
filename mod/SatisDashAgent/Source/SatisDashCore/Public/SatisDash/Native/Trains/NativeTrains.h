#pragma once

#include "CoreMinimal.h"

namespace SatisDash
{
	/** The train states the game reports (ETrainStatus), by our words. */
	enum class ETrainState : uint8 { Parked, Manual, SelfDriving, Derailed };

	/**
	 * The status text of a train. The wording is ours (owner, 2026-10-26): "Self-Driving" is byte-equal to what the
	 * recorded captures show, and the other three are our own words ("Parked", "Manual", "Derailed"), which parity reports
	 * on and does not fail.
	 */
	inline constexpr const TCHAR* NativeTrainStatusText(ETrainState State)
	{
		return State == ETrainState::SelfDriving ? TEXT("Self-Driving")
			: State == ETrainState::Manual ? TEXT("Manual")
			: State == ETrainState::Derailed ? TEXT("Derailed")
			: TEXT("Parked");
	}

	/** Which vehicle gives a train's x,y. FirstVehicle until the parity run says otherwise; one constant to change. */
	enum class ETrainPositionSource : uint8 { FirstVehicle, LastVehicle, Actor };
	inline constexpr ETrainPositionSource NativeTrainPositionSource = ETrainPositionSource::FirstVehicle;

	/** One train as the game reports it, before mapping. Positions are in game units (centimetres). */
	struct FTrainReading
	{
		FString Id;
		FString Name;
		ETrainState State = ETrainState::Parked;
		double XCm = 0;
		double YCm = 0;
		/** The timetable's stops as station names, in timetable order (trainRoutes only). */
		TArray<FString> Stops;
		/** Item class names in the consist's freight wagons, any order, repeats allowed (trainRoutes only). */
		TArray<FString> Items;
	};

	/** One cargo platform of a station (trainRoutes only). */
	struct FPlatformReading
	{
		FString Id;
		/** The platform's transfer direction: true loads the train ("load"), false unloads it ("unload"). */
		bool bLoad = false;
		/** Item class names the platform holds, any order, repeats allowed. */
		TArray<FString> Items;
	};

	/** One train station as the game reports it, before mapping. Positions are in game units (centimetres). */
	struct FStationReading
	{
		FString Id;
		FString Name;
		double XCm = 0;
		double YCm = 0;
		TArray<FPlatformReading> Platforms;
	};

	/** What a mapLive build kept and left out, for the read's log line. */
	struct FNativeMapLiveCounts
	{
		int32 Trains = 0;
		int32 Stations = 0;
		/** Readings with a coordinate outside +/-1,000,000 m (they do not count towards the 500). */
		int32 TrainsDropped = 0;
		int32 StationsDropped = 0;
		/** Readings past the 500 cap. */
		int32 TrainsOver = 0;
		int32 StationsOver = 0;
	};

	/**
	 * R3 (ADR-0058): the mapLive part {"trains":[...],"stations":[...]} from the game's own trains and stations, each record
	 * written by the shared AppendTrainRecord / AppendStationRecord the FRM parse uses. Positions go to whole metres as the
	 * map does, readings outside +/-1,000,000 m are dropped, and each list is sorted by id (ties by name, then the rest) and
	 * cut at MapLiveMaxItems, so the same world gives the same bytes in any scan order. Returns false (OutJson untouched)
	 * when the text would pass MapLiveMaxBytes, which leaves the part out, as JoinMapLive does. Sorts and builds text, so it
	 * runs on the worker, never the game thread.
	 */
	SATISDASHCORE_API bool NativeMapLiveJson(const TArray<FTrainReading>& Trains, const TArray<FStationReading>& Stations, FString& OutJson, FNativeMapLiveCounts* OutCounts = nullptr);
}
