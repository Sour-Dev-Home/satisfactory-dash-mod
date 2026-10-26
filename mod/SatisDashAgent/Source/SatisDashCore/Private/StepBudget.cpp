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
	}

	bool FStepBudget::Spent() const
	{
		return TimeSource.Read() - StepStart >= BudgetSeconds;
	}

	void FStepBudget::End()
	{
		// Always read the clock: a loop that exits on its cursor short-circuits Spent() after the last element, and that
		// element's cost must still count (the 2 ms judge reads this, so it fails closed, never under-reports).
		const double Took = FMath::Max(0.0, TimeSource.Read() - StepStart);
		++StepCount;
		Total += Took;
		Longest = FMath::Max(Longest, Took);
	}

	void FStepBudget::Reset()
	{
		StepStart = 0.0;
		StepCount = 0;
		Longest = 0.0;
		Total = 0.0;
	}
}
