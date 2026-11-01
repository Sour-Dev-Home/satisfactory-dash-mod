#pragma once

#include "CoreMinimal.h"

namespace SatisDash
{
	/** A sliced native step stops after the element that crosses this (seconds). */
	inline constexpr double NativeStepBudgetSeconds = 0.001;
	/** Owner, 2026-10-26: the ceiling tick-cost-native judges on any single native game-thread step (milliseconds). */
	inline constexpr double MaxNativeStepMs = 2.0;

	/** The injected clock, in seconds. */
	struct FStepClock
	{
		TFunction<double()> Read;
	};

	/**
	 * Times the steps of a sliced read. The clock is injected (the game passes FPlatformTime::Seconds, tests a fake), so Core
	 * has no UE time dependency. Use as: Begin(); do { work on one element } while (!Spent()); End();
	 * Spent() is checked after each element, never before the first, so every step makes progress.
	 */
	class SATISDASHCORE_API FStepBudget
	{
	public:
		/**
		 * InThreadCycles is optional (empty or unset = not available): the calling thread's CPU cycle count, read once in Begin() and
		 * once in End(), never per element. Diagnostic only; it never changes what the 2 ms ceiling judges (wall time).
		 */
		FStepBudget(TFunction<double()> InNow, double InBudgetSeconds = NativeStepBudgetSeconds, TFunction<TOptional<uint64>()> InThreadCycles = {});
		/** Starts a step. */
		void Begin();
		/** True once the budget is used up. Samples the clock. */
		bool Spent() const;
		/** Records this step's duration: Begin() to now. Always reads the clock, so an element after the last Spent() still counts. */
		void End();
		int32 Steps() const { return StepCount; }
		double LongestMs() const { return Longest * 1000.0; }
		double TotalMs() const { return Total * 1000.0; }
		/** Says which phase of its read the reader is in (a small number, 0 unless the reader sets it). Recorded at Begin() and at End(). */
		void SetTag(int32 Tag) { CurrentTag = Tag; }
		/** Spent() calls in the longest step so far: one per element, so the count is the longest step's element count (a loop that leaves on its cursor skips the last call). */
		int32 LongestItems() const { return LongestItemCount; }
		/** The tag current at Begin() and at End() of the longest step. */
		int32 LongestStartTag() const { return LongestStartTagValue; }
		int32 LongestEndTag() const { return LongestEndTagValue; }
		/**
		 * The longest single element of the longest step: the gap between consecutive clock reads (Begin to the first Spent(), then
		 * Spent() to Spent()). A gap between the last Spent() and End() is charged to element StepItems + 1 (End's own read), with the tag at End().
		 * The index is 1-based, the same count LongestItems() uses. Diagnostic only (tick-cost run 40626602265).
		 */
		double LongestElementMs() const { return LongestElementSeconds * 1000.0; }
		int32 LongestElementIndex() const { return LongestElementIndexValue; }
		int32 LongestElementTag() const { return LongestElementTagValue; }
		/**
		 * Thread CPU cycles (tick-cost run 41334503941): a step has cycles only when the source gave a reading at Begin() and at End()
		 * and End's was not smaller. Windows says a cycle count must not be converted to elapsed time, so the longest step is judged
		 * against the same read's other steps: cycles per wall millisecond in each. A stalled (descheduled) step shows few cycles per ms.
		 * Each is unset when it cannot be computed (no cycles, no other steps, or zero wall time).
		 */
		TOptional<double> LongestStepCyclesPerMs() const;
		TOptional<double> OtherStepsCyclesPerMs() const;
		/** Steps that had cycles, minus the longest one: the "other steps" the rate above is over. 0 when the longest has no cycles. */
		int32 OtherStepsWithCycles() const;
		void Reset();

	private:
		FStepClock TimeSource;
		TFunction<TOptional<uint64>()> ThreadCycles;
		TOptional<uint64> StepStartCycles;
		/** The longest step's cycles (unset when that step had none), and totals over every step that had cycles. */
		TOptional<uint64> LongestCycles;
		uint64 CyclesTotal = 0;
		double CyclesWallSeconds = 0.0;
		int32 CyclesStepCount = 0;
		double BudgetSeconds;
		double StepStart = 0.0;
		int32 StepCount = 0;
		double Longest = 0.0;
		double Total = 0.0;
		/** Spent() is const, so the per-step counter is mutable. */
		mutable int32 StepItems = 0;
		int32 CurrentTag = 0;
		int32 StepStartTag = 0;
		int32 LongestItemCount = 0;
		int32 LongestStartTagValue = 0;
		int32 LongestEndTagValue = 0;
		/** The previous clock read of this step, and the longest gap so far in it (Spent() is const, so mutable). */
		mutable double LastRead = 0.0;
		mutable double StepElementSeconds = 0.0;
		mutable int32 StepElementIndex = 0;
		mutable int32 StepElementTag = 0;
		double LongestElementSeconds = 0.0;
		int32 LongestElementIndexValue = 0;
		int32 LongestElementTagValue = 0;
	};

	/**
	 * Owner, 2026-10-27: when a budget's longest step passes MaxNativeStepMs, one extra Display line says what that step was doing:
	 * "<Name> read longest step: <x.xx> ms over <n> elements, phase <a> to <b>[, longest element <y.yy> ms at <i> (phase <t>)][, thread cycles per ms <a> in this step, <b> in <n> other steps]". Counts and times only, never a name or an id. Logs nothing at or below the ceiling.
	 * The bracketed parts are written only by the budget overload (the resource node scan has no per-element data); the cycles tail
	 * (owner 2026-11-01) only when the budget has a cycle source and both rates are available, else the line is unchanged.
	 */
	SATISDASHCORE_API void LogLongestStep(const TCHAR* ReaderName, double LongestMs, int32 Items, int32 StartTag, int32 EndTag);
	/** ExtraTail (owner 2026-11-01, Logistics only) is appended after the cycles tail; empty leaves the line byte-identical. */
	SATISDASHCORE_API void LogLongestStep(const TCHAR* ReaderName, const FStepBudget& Budget, const FString& ExtraTail = FString());
	/** The cycles tail of that line (", thread cycles per ms ..."), or an empty string when either rate is unavailable. */
	SATISDASHCORE_API FString LongestStepCyclesTail(const FStepBudget& Budget);
}
