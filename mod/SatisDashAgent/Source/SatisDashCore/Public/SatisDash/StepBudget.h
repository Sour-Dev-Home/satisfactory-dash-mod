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
		FStepBudget(TFunction<double()> InNow, double InBudgetSeconds = NativeStepBudgetSeconds);
		/** Starts a step. */
		void Begin();
		/** True once the budget is used up. Samples the clock. */
		bool Spent() const;
		/** Records this step's duration: Begin() to now. Always reads the clock, so an element after the last Spent() still counts. */
		void End();
		int32 Steps() const { return StepCount; }
		double LongestMs() const { return Longest * 1000.0; }
		double TotalMs() const { return Total * 1000.0; }
		void Reset();

	private:
		FStepClock TimeSource;
		double BudgetSeconds;
		double StepStart = 0.0;
		mutable double LastSample = 0.0;
		mutable bool Sampled = false;
		int32 StepCount = 0;
		double Longest = 0.0;
		double Total = 0.0;
	};
}
