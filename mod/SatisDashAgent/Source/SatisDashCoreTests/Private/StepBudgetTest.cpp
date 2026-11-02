#include "SatisDash/StepBudget.h"
#include "TestUtil.h"

using namespace SatisDash;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStepBudgetSlicesTest, "SatisDash.Core.StepBudget.Slices", SatisDash::Test::Flags)

bool FStepBudgetSlicesTest::RunTest(const FString& Parameters)
{
	// A fake clock that advances 0.4 ms per call.
	TSharedRef<double> Time = MakeShared<double>(-0.0004);
	FStepBudget Slicer{[Time]() { *Time += 0.0004; return *Time; }};

	Slicer.Begin();
	TestFalse(TEXT("not spent after the first element"), Slicer.Spent());
	TestFalse(TEXT("nor after the second"), Slicer.Spent());
	TestTrue(TEXT("spent after the third"), Slicer.Spent());
	Slicer.End();
	TestEqual(TEXT("one step"), Slicer.Steps(), 1);
	TestTrue(TEXT("longest about 1.6 ms (End reads the clock)"), FMath::Abs(Slicer.LongestMs() - 1.6) < 0.01);

	Slicer.Begin();
	TestFalse(TEXT("a one-element step is not spent"), Slicer.Spent());
	Slicer.End();
	TestEqual(TEXT("two steps"), Slicer.Steps(), 2);
	TestTrue(TEXT("longest stays the maximum"), FMath::Abs(Slicer.LongestMs() - 1.6) < 0.01);
	// End() reads the clock itself: step one is Begin + 3 samples + End = 4 ticks of 0.4 ms = 1.6 ms; step two is 2 ticks = 0.8 ms.
	TestTrue(TEXT("total adds both, End reading the clock"), FMath::Abs(Slicer.TotalMs() - 2.4) < 0.01);

	Slicer.Reset();
	TestEqual(TEXT("reset steps"), Slicer.Steps(), 0);
	TestEqual(TEXT("reset longest"), Slicer.LongestMs(), 0.0);
	TestEqual(TEXT("reset total"), Slicer.TotalMs(), 0.0);
	TestEqual(TEXT("reset items"), Slicer.LongestItems(), 0);
	TestEqual(TEXT("reset start tag"), Slicer.LongestStartTag(), 0);
	TestEqual(TEXT("reset end tag"), Slicer.LongestEndTag(), 0);

	// A step whose Spent() was never called records the elapsed time (Begin to End = one 0.4 ms tick).
	TSharedRef<double> Time2 = MakeShared<double>(0.0);
	FStepBudget Unsampled{[Time2]() { const double Now = *Time2; *Time2 += 0.0004; return Now; }};
	Unsampled.Begin();
	Unsampled.End();
	TestTrue(TEXT("a step without Spent() records the elapsed time"), FMath::Abs(Unsampled.LongestMs() - 0.4) < 0.01);

	// The S2-B loop shape exits on the cursor with Spent() short-circuited after the last element: that element's cost still counts.
	// Clock reads: Begin 0 ms, the one Spent() sample 1 ms (under the 5 ms budget), End 4 ms.
	TSharedRef<int32> Next = MakeShared<int32>(0);
	FStepBudget Looped{[Next]() { static const double Reads[] = { 0.0, 0.001, 0.004 }; return Reads[FMath::Min((*Next)++, 2)]; }, 0.005};
	int32 Cursor = 0;
	const int32 N = 2;
	Looped.Begin();
	do { ++Cursor; } while (Cursor < N && !Looped.Spent());
	Looped.End();
	TestEqual(TEXT("the loop ran both elements"), Cursor, 2);
	TestTrue(TEXT("the last element's cost is counted (4 ms: Spent() was sampled at 1 ms only)"), FMath::Abs(Looped.LongestMs() - 4.0) < 0.01);

	// The diagnostic: Spent() calls are counted per step, and the longest step keeps its count and the tags at its Begin() and End().
	// Step 1 (phase 1 to 2): 3 Spent() calls, 1 ms. Step 2 (phase 2 to 3): 2 calls, 5 ms, the new longest. Step 3 (phase 3 to 3): 4 calls, 2 ms, not.
	TSharedRef<double> Now = MakeShared<double>(0.0);
	FStepBudget Tagged{[Now]() { return *Now; }, 1.0};
	Tagged.SetTag(1);
	Tagged.Begin();
	Tagged.Spent(); Tagged.Spent(); Tagged.Spent();
	Tagged.SetTag(2);
	*Now += 0.001;
	Tagged.End();
	TestEqual(TEXT("the first step is the longest so far: 3 items"), Tagged.LongestItems(), 3);
	TestEqual(TEXT("it began in phase 1"), Tagged.LongestStartTag(), 1);
	TestEqual(TEXT("and ended in phase 2"), Tagged.LongestEndTag(), 2);
	Tagged.Begin();
	Tagged.Spent(); Tagged.Spent();
	Tagged.SetTag(3);
	*Now += 0.005;
	Tagged.End();
	TestEqual(TEXT("the longer step replaces it: 2 items (the count restarts at Begin)"), Tagged.LongestItems(), 2);
	TestEqual(TEXT("began in phase 2"), Tagged.LongestStartTag(), 2);
	TestEqual(TEXT("ended in phase 3"), Tagged.LongestEndTag(), 3);
	Tagged.Begin();
	Tagged.Spent(); Tagged.Spent(); Tagged.Spent(); Tagged.Spent();
	*Now += 0.002;
	Tagged.End();
	TestEqual(TEXT("a shorter step changes nothing: items"), Tagged.LongestItems(), 2);
	TestEqual(TEXT("start tag"), Tagged.LongestStartTag(), 2);
	TestEqual(TEXT("end tag"), Tagged.LongestEndTag(), 3);
	TestEqual(TEXT("steps are still counted"), Tagged.Steps(), 3);
	TestTrue(TEXT("longest and total are unchanged in meaning"), FMath::Abs(Tagged.LongestMs() - 5.0) < 0.01 && FMath::Abs(Tagged.TotalMs() - 8.0) < 0.01);
	Tagged.Reset();
	TestEqual(TEXT("Reset clears the items"), Tagged.LongestItems(), 0);
	TestEqual(TEXT("and both tags"), Tagged.LongestStartTag() + Tagged.LongestEndTag(), 0);
	TestEqual(TEXT("Reset clears the longest element"), Tagged.LongestElementMs() + Tagged.LongestElementIndex() + Tagged.LongestElementTag(), 0.0);

	// The longest element (tick-cost run 40626602265): 0.003 ms per element, then a 9 ms jump inside element 319 after SetTag(2).
	TSharedRef<double> Tick = MakeShared<double>(0.0);
	FStepBudget Elements{[Tick]() { return *Tick; }, 1.0};
	Elements.SetTag(1);
	Elements.Begin();
	for (int32 Element = 1; Element <= 318; ++Element)
	{
		*Tick += 0.000003;
		Elements.Spent();
	}
	Elements.SetTag(2);
	*Tick += 0.009;
	Elements.Spent();
	Elements.End();
	TestTrue(TEXT("the step is about 9.95 ms (318 x 0.003 ms plus the jump)"), FMath::Abs(Elements.LongestMs() - 9.954) < 0.01);
	TestEqual(TEXT("the element count is 319"), Elements.LongestItems(), 319);
	TestTrue(TEXT("the longest element is the 9 ms jump"), FMath::Abs(Elements.LongestElementMs() - 9.0) < 0.001);
	TestEqual(TEXT("at element 319"), Elements.LongestElementIndex(), 319);
	TestEqual(TEXT("in phase 2"), Elements.LongestElementTag(), 2);

	// A shorter step later leaves the longest step's element alone, even if its own element is longer than the others in it.
	Elements.Begin();
	Elements.Spent();
	*Tick += 0.002;
	Elements.End();
	TestEqual(TEXT("a shorter step does not replace the longest element"), Elements.LongestElementIndex(), 319);

	// A jump between the last Spent() and End() is the element End() closes: StepItems + 1, with the tag at End().
	TSharedRef<double> Tick2 = MakeShared<double>(0.0);
	FStepBudget Closing{[Tick2]() { return *Tick2; }, 1.0};
	Closing.SetTag(1);
	Closing.Begin();
	Closing.Spent(); Closing.Spent(); Closing.Spent();
	Closing.SetTag(3);
	*Tick2 += 0.004;
	Closing.End();
	TestTrue(TEXT("the closing element is 4 ms"), FMath::Abs(Closing.LongestElementMs() - 4.0) < 0.001);
	TestEqual(TEXT("charged to element 4 (three Spent() calls plus the one End() closes)"), Closing.LongestElementIndex(), 4);
	TestEqual(TEXT("with the tag at End()"), Closing.LongestElementTag(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStepBudgetThreadCyclesTest, "SatisDash.Core.StepBudget.ThreadCycles", SatisDash::Test::Flags)

bool FStepBudgetThreadCyclesTest::RunTest(const FString& Parameters)
{
	// Tick-cost run 41334503941: nine normal steps (1 ms wall, 3,000,000 cycles each) and one stalled step (9 ms wall, 41,000 cycles).
	// Cycle reads come from a queue: one per Begin() and one per End(), so the test also proves there are exactly two per step.
	TSharedRef<double> Now = MakeShared<double>(0.0);
	TSharedRef<TArray<uint64>> Reads = MakeShared<TArray<uint64>>();
	TSharedRef<int32> ReadCount = MakeShared<int32>(0);
	FStepBudget Budget{[Now]() { return *Now; }, 1.0, [Reads, ReadCount]() -> TOptional<uint64>
	{
		const int32 Index = (*ReadCount)++;
		return Reads->IsValidIndex(Index) ? TOptional<uint64>((*Reads)[Index]) : TOptional<uint64>();
	}};
	auto runStep = [&](double WallSeconds, uint64 Cycles)
	{
		// Begin's reading, then End's.
		Reads->Add(10000000);
		Reads->Add(10000000 + Cycles);
		Budget.Begin();
		Budget.Spent();
		*Now += WallSeconds;
		Budget.End();
	};
	for (int32 Step = 0; Step < 4; ++Step)
	{
		runStep(0.001, 3000000);
	}
	runStep(0.009, 41000);
	for (int32 Step = 0; Step < 5; ++Step)
	{
		runStep(0.001, 3000000);
	}
	TestEqual(TEXT("two cycle reads per step, never per element"), *ReadCount, 20);
	TestEqual(TEXT("ten steps"), Budget.Steps(), 10);
	TestTrue(TEXT("the longest step is the stalled one"), FMath::Abs(Budget.LongestMs() - 9.0) < 0.001);
	TestTrue(TEXT("its rate is 41000 / 9 ms"), Budget.LongestStepCyclesPerMs().IsSet() && FMath::Abs(Budget.LongestStepCyclesPerMs().GetValue() - 41000.0 / 9.0) < 0.5);
	TestTrue(TEXT("the other nine steps run at 3,000,000 per ms"), Budget.OtherStepsCyclesPerMs().IsSet() && FMath::Abs(Budget.OtherStepsCyclesPerMs().GetValue() - 3000000.0) < 0.5);
	TestEqual(TEXT("over 9 other steps"), Budget.OtherStepsWithCycles(), 9);
	TestEqual(TEXT("the log tail"), LongestStepCyclesTail(Budget), FString(TEXT(", thread cycles per ms 4556 in this step, 3000000 in 9 other steps")));
	Budget.Reset();
	TestFalse(TEXT("Reset clears the longest rate"), Budget.LongestStepCyclesPerMs().IsSet());
	TestFalse(TEXT("Reset clears the other rate"), Budget.OtherStepsCyclesPerMs().IsSet());
	TestEqual(TEXT("Reset clears the other steps"), Budget.OtherStepsWithCycles(), 0);
	TestEqual(TEXT("and the tail"), LongestStepCyclesTail(Budget), FString());

	// No source: no cycles, and the tail is empty so the log line is byte-identical to the one without this diagnostic.
	TSharedRef<double> Now2 = MakeShared<double>(0.0);
	FStepBudget NoSource{[Now2]() { return *Now2; }, 1.0};
	NoSource.Begin();
	*Now2 += 0.009;
	NoSource.End();
	TestFalse(TEXT("no source: no longest rate"), NoSource.LongestStepCyclesPerMs().IsSet());
	TestEqual(TEXT("no source: empty tail"), LongestStepCyclesTail(NoSource), FString());

	// A source that returns unset, or End's reading smaller than Begin's: the step has no cycles.
	TSharedRef<int32> Calls = MakeShared<int32>(0);
	TSharedRef<double> Now3 = MakeShared<double>(0.0);
	FStepBudget Backwards{[Now3]() { return *Now3; }, 1.0, [Calls]() -> TOptional<uint64> { return (*Calls)++ == 0 ? TOptional<uint64>(500) : TOptional<uint64>(100); }};
	Backwards.Begin();
	*Now3 += 0.003;
	Backwards.End();
	TestFalse(TEXT("End reading smaller than Begin's: no cycles"), Backwards.LongestStepCyclesPerMs().IsSet());
	FStepBudget Unset{[Now3]() { return *Now3; }, 1.0, []() -> TOptional<uint64> { return {}; }};
	Unset.Begin();
	*Now3 += 0.003;
	Unset.End();
	TestFalse(TEXT("an unset source: no cycles"), Unset.LongestStepCyclesPerMs().IsSet());

	// A single step has no other steps to compare with: no other-steps rate, so no tail.
	TSharedRef<int32> Pair = MakeShared<int32>(0);
	TSharedRef<double> Now4 = MakeShared<double>(0.0);
	FStepBudget Single{[Now4]() { return *Now4; }, 1.0, [Pair]() -> TOptional<uint64> { return TOptional<uint64>(uint64(1000 * (*Pair)++)); }};
	Single.Begin();
	*Now4 += 0.003;
	Single.End();
	TestTrue(TEXT("one step: its own rate is known"), Single.LongestStepCyclesPerMs().IsSet());
	TestFalse(TEXT("one step: no other steps"), Single.OtherStepsCyclesPerMs().IsSet());
	TestEqual(TEXT("one step: empty tail"), LongestStepCyclesTail(Single), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStepBudgetAlwaysOneElementTest, "SatisDash.Core.StepBudget.AlwaysOneElement", SatisDash::Test::Flags)

bool FStepBudgetAlwaysOneElementTest::RunTest(const FString& Parameters)
{
	// The clock jumps 5 ms after its first call: the step still does one element, and reports 5 ms.
	TSharedRef<int32> Calls = MakeShared<int32>(0);
	FStepBudget Slicer{[Calls]() { return (*Calls)++ == 0 ? 0.0 : 0.005; }};

	int32 Elements = 0;
	Slicer.Begin();
	do { ++Elements; } while (!Slicer.Spent());
	Slicer.End();

	TestEqual(TEXT("exactly one element"), Elements, 1);
	TestEqual(TEXT("one step"), Slicer.Steps(), 1);
	TestTrue(TEXT("reported as 5 ms"), FMath::Abs(Slicer.LongestMs() - 5.0) < 0.001);
	return true;
}
