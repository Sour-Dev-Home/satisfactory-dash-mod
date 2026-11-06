#pragma once

#include "CoreMinimal.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"

namespace SatisDash
{
	inline constexpr int32 MaxBuildings = 20000;
	inline constexpr int32 MaxRatesPerBuilding = 16;

	/** One production or ingredient rate of a machine, in the units the snapshot uses (per minute, percent). */
	struct FRateReading { FString Name; FString ClassName; double Current = 0; double Max = 0; double Percent = 0; };
	/** One inventory slot summary, already in the dash contract's scale (fluids in m3). */
	struct FSlotReading { FString ClassName; double Amount = 0; double MaxAmount = 0; };

	using FFactoryJsonWriter = TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

	/** Centimetres to metres, a true division as JavaScript does it (see FactoryRecords.cpp). */
	SATISDASHCORE_API double FactoryMetres(double Centimetres);
	/** conform.ts normalizeYaw: into [0, 360); float noise that lands on 360 is 0. */
	SATISDASHCORE_API double FactoryYaw(double Degrees);

	/**
	 * factoryMapper.ts isBackedUp and areInputsFull, on the whole lists (they run before conform cuts them). The one
	 * derivation both sources use. OutInputsFull is meaningful only when Input is set. Ingredients are the machine's
	 * own, whatever bConfigured says; an unconfigured machine is never backed up and never has its inputs full.
	 */
	SATISDASHCORE_API void DeriveFactoryFlags(bool bPaused, bool bConfigured, const TArray<FSlotReading>& Output, const TArray<FRateReading>& Ingredients, const TOptional<TArray<FSlotReading>>& Input, bool& OutBackedUp, bool& OutInputsFull);

	/** One building as the snapshot's factory part writes it: every derived value already settled. */
	struct FFactoryBuildingModel
	{
		FString Id, Name, ClassName;
		/** Set only when the machine has a recipe; unset writes "recipe":null. */
		TOptional<FString> Recipe;
		bool bProducing = false;
		bool bPaused = false;
		bool bBackedUp = false;
		/** Unset leaves "inputsFull" out. */
		TOptional<bool> InputsFull;
		int64 CircuitGroupId = -1;
		bool bHasLocation = false;
		/** Metres and a yaw in [0, 360), already converted. */
		double XM = 0, YM = 0, ZM = 0, YawDeg = 0;
		/** Unset leaves "clockSpeedPercent" out. */
		TOptional<double> ClockSpeedPercent;
		/** Not owned; null is none. Cut at MaxRatesPerBuilding when written. */
		const TArray<FRateReading>* Production = nullptr;
		const TArray<FRateReading>* Ingredients = nullptr;
	};

	/** The one record writer: every building record, whatever reads it, is written through this. */
	SATISDASHCORE_API void AppendFactoryBuilding(FFactoryJsonWriter& Writer, const FFactoryBuildingModel& Building);
}
