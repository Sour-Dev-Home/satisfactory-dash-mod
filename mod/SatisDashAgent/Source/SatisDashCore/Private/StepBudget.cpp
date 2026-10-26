#include "SatisDash/StepBudget.h"

namespace SatisDash
{
	FStepBudget::FStepBudget(TFunction<double()> InClock, double InBudgetSeconds)
		: TimeSource{MoveTemp(InClock)}, BudgetSeconds{InBudgetSeconds}
	{
	}

	void FStepBudget::Begin()
	{
		StepStart = TimeSource.Read();
		LastSample = StepStart;
		Sampled = false;
	}

	bool FStepBudget::Spent() const
	{
		LastSample = TimeSource.Read();
		Sampled = true;
		return LastSample - StepStart >= BudgetSeconds;
	}

	void FStepBudget::End()
	{
		// Always read the clock: a loop that exits on its cursor short-circuits Spent() after the last element, and that
		// element's cost must still count (the 2 ms judge reads this, so it fails closed, never under-reports).
		const double Took = FMath::Max(0.0, TimeSource.Read() - StepStart);
		++StepCount;
		Total += Took;
		Longest = FMath::Max(Longest, Took);
		Sampled = false;
	}

	void FStepBudget::Reset()
	{
		StepStart = 0.0;
		LastSample = 0.0;
		Sampled = false;
		StepCount = 0;
		Longest = 0.0;
		Total = 0.0;
	}
}
