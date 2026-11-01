#include "SatisDash/StepBudget.h"

#include "SatisDash/Log.h"

namespace SatisDash
{
	FStepBudget::FStepBudget(TFunction<double()> InClock, double InBudgetSeconds, TFunction<TOptional<uint64>()> InThreadCycles)
		: TimeSource{MoveTemp(InClock)}, ThreadCycles{MoveTemp(InThreadCycles)}, BudgetSeconds{InBudgetSeconds}
	{
	}

	void FStepBudget::Begin()
	{
		// One cycle read per step here and one in End(), never per element.
		StepStartCycles = ThreadCycles ? ThreadCycles() : TOptional<uint64>();
		StepStart = TimeSource.Read();
		StepItems = 0;
		StepStartTag = CurrentTag;
		LastRead = StepStart;
		StepElementSeconds = 0.0;
		StepElementIndex = 0;
		StepElementTag = CurrentTag;
	}

	bool FStepBudget::Spent() const
	{
		++StepItems;
		const double Now = TimeSource.Read();
		if (Now - LastRead > StepElementSeconds)
		{
			StepElementSeconds = Now - LastRead;
			StepElementIndex = StepItems;
			StepElementTag = CurrentTag;
		}
		LastRead = Now;
		return Now - StepStart >= BudgetSeconds;
	}

	void FStepBudget::End()
	{
		// Always read the clock: a loop that exits on its cursor short-circuits Spent() after the last element, and that
		// element's cost must still count (the 2 ms judge reads this, so it fails closed, never under-reports).
		const double EndRead = TimeSource.Read();
		const TOptional<uint64> EndCycles = ThreadCycles ? ThreadCycles() : TOptional<uint64>();
		TOptional<uint64> StepCycles;
		if (StepStartCycles.IsSet() && EndCycles.IsSet() && EndCycles.GetValue() >= StepStartCycles.GetValue())
		{
			StepCycles = EndCycles.GetValue() - StepStartCycles.GetValue();
		}
		const double Took = FMath::Max(0.0, EndRead - StepStart);
		// The gap after the last Spent() is the element End() closes: element StepItems + 1, the tag now.
		if (EndRead - LastRead > StepElementSeconds)
		{
			StepElementSeconds = EndRead - LastRead;
			StepElementIndex = StepItems + 1;
			StepElementTag = CurrentTag;
		}
		LastRead = EndRead;
		++StepCount;
		Total += Took;
		if (StepCycles.IsSet())
		{
			CyclesTotal += StepCycles.GetValue();
			CyclesWallSeconds += Took;
			++CyclesStepCount;
		}
		if (Took > Longest)
		{
			Longest = Took;
			LongestCycles = StepCycles;
			LongestItemCount = StepItems;
			LongestStartTagValue = StepStartTag;
			LongestEndTagValue = CurrentTag;
			LongestElementSeconds = StepElementSeconds;
			LongestElementIndexValue = StepElementIndex;
			LongestElementTagValue = StepElementTag;
		}
	}

	TOptional<double> FStepBudget::LongestStepCyclesPerMs() const
	{
		if (!LongestCycles.IsSet() || Longest <= 0.0)
		{
			return {};
		}
		return double(LongestCycles.GetValue()) / (Longest * 1000.0);
	}

	int32 FStepBudget::OtherStepsWithCycles() const
	{
		return LongestCycles.IsSet() ? CyclesStepCount - 1 : 0;
	}

	TOptional<double> FStepBudget::OtherStepsCyclesPerMs() const
	{
		const double OtherSeconds = CyclesWallSeconds - Longest;
		if (!LongestCycles.IsSet() || CyclesStepCount < 2 || OtherSeconds <= 0.0)
		{
			return {};
		}
		return double(CyclesTotal - LongestCycles.GetValue()) / (OtherSeconds * 1000.0);
	}

	void FStepBudget::Reset()
	{
		StepStartCycles.Reset();
		LongestCycles.Reset();
		CyclesTotal = 0;
		CyclesWallSeconds = 0.0;
		CyclesStepCount = 0;
		StepStart = 0.0;
		StepCount = 0;
		Longest = 0.0;
		Total = 0.0;
		StepItems = 0;
		StepStartTag = 0;
		LongestItemCount = 0;
		LongestStartTagValue = 0;
		LongestEndTagValue = 0;
		LastRead = 0.0;
		StepElementSeconds = 0.0;
		StepElementIndex = 0;
		StepElementTag = 0;
		LongestElementSeconds = 0.0;
		LongestElementIndexValue = 0;
		LongestElementTagValue = 0;
	}

	void LogLongestStep(const TCHAR* ReaderName, double LongestMs, int32 Items, int32 StartTag, int32 EndTag)
	{
		if (LongestMs > MaxNativeStepMs)
		{
			UE_LOG(LogSatisDash, Display, TEXT("%s read longest step: %.2f ms over %d elements, phase %d to %d"), ReaderName, LongestMs, Items, StartTag, EndTag);
		}
	}

	FString LongestStepCyclesTail(const FStepBudget& Budget)
	{
		const TOptional<double> InStep = Budget.LongestStepCyclesPerMs();
		const TOptional<double> InOthers = Budget.OtherStepsCyclesPerMs();
		if (!InStep.IsSet() || !InOthers.IsSet())
		{
			return FString();
		}
		return FString::Printf(TEXT(", thread cycles per ms %.0f in this step, %.0f in %d other steps"), InStep.GetValue(), InOthers.GetValue(), Budget.OtherStepsWithCycles());
	}

	void LogLongestStep(const TCHAR* ReaderName, const FStepBudget& Budget)
	{
		if (Budget.LongestMs() > MaxNativeStepMs)
		{
			UE_LOG(LogSatisDash, Display, TEXT("%s read longest step: %.2f ms over %d elements, phase %d to %d, longest element %.2f ms at %d (phase %d)%s"),
				ReaderName, Budget.LongestMs(), Budget.LongestItems(), Budget.LongestStartTag(), Budget.LongestEndTag(),
				Budget.LongestElementMs(), Budget.LongestElementIndex(), Budget.LongestElementTag(), *LongestStepCyclesTail(Budget));
		}
	}
}
