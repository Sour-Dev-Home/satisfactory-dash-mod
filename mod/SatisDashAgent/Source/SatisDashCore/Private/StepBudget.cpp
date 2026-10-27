#include "SatisDash/StepBudget.h"

#include "SatisDash/Log.h"

namespace SatisDash
{
	FStepBudget::FStepBudget(TFunction<double()> InClock, double InBudgetSeconds)
		: TimeSource{MoveTemp(InClock)}, BudgetSeconds{InBudgetSeconds}
	{
	}

	void FStepBudget::Begin()
	{
		StepStart = TimeSource.Read();
		StepItems = 0;
		StepStartTag = CurrentTag;
	}

	bool FStepBudget::Spent() const
	{
		++StepItems;
		return TimeSource.Read() - StepStart >= BudgetSeconds;
	}

	void FStepBudget::End()
	{
		// Always read the clock: a loop that exits on its cursor short-circuits Spent() after the last element, and that
		// element's cost must still count (the 2 ms judge reads this, so it fails closed, never under-reports).
		const double Took = FMath::Max(0.0, TimeSource.Read() - StepStart);
		++StepCount;
		Total += Took;
		if (Took > Longest)
		{
			Longest = Took;
			LongestItemCount = StepItems;
			LongestStartTagValue = StepStartTag;
			LongestEndTagValue = CurrentTag;
		}
	}

	void FStepBudget::Reset()
	{
		StepStart = 0.0;
		StepCount = 0;
		Longest = 0.0;
		Total = 0.0;
		StepItems = 0;
		StepStartTag = 0;
		LongestItemCount = 0;
		LongestStartTagValue = 0;
		LongestEndTagValue = 0;
	}

	void LogLongestStep(const TCHAR* ReaderName, double LongestMs, int32 Items, int32 StartTag, int32 EndTag)
	{
		if (LongestMs > MaxNativeStepMs)
		{
			UE_LOG(LogSatisDash, Display, TEXT("%s read longest step: %.2f ms over %d elements, phase %d to %d"), ReaderName, LongestMs, Items, StartTag, EndTag);
		}
	}

	void LogLongestStep(const TCHAR* ReaderName, const FStepBudget& Budget)
	{
		LogLongestStep(ReaderName, Budget.LongestMs(), Budget.LongestItems(), Budget.LongestStartTag(), Budget.LongestEndTag());
	}
}
