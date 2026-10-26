#pragma once

#include "CoreMinimal.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "SatisDash/JsonTokens.h"
#include "Serialization/JsonWriter.h"

namespace SatisDash
{
	inline constexpr int32 MaxBuildings = 20000;
	inline constexpr int32 MaxRatesPerBuilding = 16;
	/** FRM bridge spec "Threading": about 2,000 records per worker slice. */
	inline constexpr int32 FactorySliceBuildings = 2000;

	/** One production or ingredient rate of a machine, in the units the snapshot uses (per minute, percent). */
	struct FRateReading { FString Name; FString ClassName; double Current = 0; double Max = 0; double Percent = 0; };
	/** One inventory slot summary, already in FRM's scale (fluids in m3). */
	struct FSlotReading { FString ClassName; double Amount = 0; double MaxAmount = 0; };

	using FFactoryJsonWriter = TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

	/** Centimetres to metres, a true division as JavaScript does it (see FrmFactory.cpp). */
	SATISDASHCORE_API double FrmFactoryMetres(double Centimetres);
	/** conform.ts normalizeYaw: into [0, 360); float noise that lands on 360 is 0. */
	SATISDASHCORE_API double FrmFactoryYaw(double Degrees);

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

	/** The one record writer: the FRM parse and the native reader both write a building through this. */
	SATISDASHCORE_API void AppendFactoryBuilding(FFactoryJsonWriter& Writer, const FFactoryBuildingModel& Building);

	/**
	 * getFactory's body to the snapshot's factory part, {"buildings":[...]}, as the Node agent makes it:
	 *  - rawSchemas.ts:98-124 (what is valid; one invalid building refuses the whole body);
	 *  - satisfactoryServerAdapter.ts:173-209 (the mapping);
	 *  - factoryMapper.ts (isBackedUp, inputsFull);
	 *  - conform.ts:88-118 (caps, cuts, yaw).
	 * Pure, and touched by one thread at a time (FFrmBridge hands it to the worker a slice at a time). Not copyable or
	 * movable: the writer holds a pointer to Out, and the reader a view of Body.
	 */
	class SATISDASHCORE_API FFactoryParse
	{
	public:
		explicit FFactoryParse(TArray<uint8> InBody);
		FFactoryParse(const FFactoryParse&) = delete;
		FFactoryParse& operator=(const FFactoryParse&) = delete;

		enum class EStep : uint8 { More, Done, Invalid };
		/** Reads at most MaxPerSlice more buildings. After Done or Invalid it does nothing more. */
		EStep Step(int32 MaxPerSlice = FactorySliceBuildings);
		/** After Done: the part's JSON. Empties it. */
		FString TakeJson();
		int32 Buildings() const { return Count; }
		/** Valid buildings past MaxBuildings, read and checked but not sent. */
		int32 Dropped() const { return Over; }

	private:
		using FRate = FRateReading;
		using FSlot = FSlotReading;
		struct FBuilding
		{
			TOptional<FString> Id;
			TOptional<FString> Name;
			TOptional<FString> ClassName;
			TOptional<FString> Recipe;
			TOptional<bool> IsConfigured;
			TOptional<bool> IsProducing;
			TOptional<bool> IsPaused;
			TArray<FRate> Production;
			TArray<FRate> Ingredients;
			TArray<FSlot> Output;
			TOptional<TArray<FSlot>> Input;
			TOptional<int64> CircuitGroupId;
			bool bHasLocation = false;
			double X = 0, Y = 0, Z = 0, Rotation = 0;
			TOptional<double> ManuSpeed;
		};

		EStep Fail();
		bool ReadBuilding(FBuilding& Out);
		bool ReadRates(EJsonNotation Started, bool bIngredients, TArray<FRate>& Out);
		bool ReadSlots(EJsonNotation Started, TArray<FSlot>& Out);
		bool ReadPowerInfo(EJsonNotation Started, TOptional<int64>& OutGroup);
		bool ReadLocation(EJsonNotation Started, FBuilding& Out);
		bool ReadNonNegative(EJsonNotation N, double& Out) const;
		void WriteBuilding(const FBuilding& B);

		TArray<uint8> Body;
		FTokenReader Reader;
		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer;
		EStep State = EStep::More;
		bool bStarted = false;
		int32 Count = 0;
		int32 Over = 0;
	};
}
