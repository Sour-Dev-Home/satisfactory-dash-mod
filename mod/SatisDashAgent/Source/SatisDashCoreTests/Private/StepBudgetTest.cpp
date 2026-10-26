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
