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
	TestTrue(TEXT("longest about 1.2 ms"), FMath::Abs(Slicer.LongestMs() - 1.2) < 0.01);

	Slicer.Begin();
	TestFalse(TEXT("a one-element step is not spent"), Slicer.Spent());
	Slicer.End();
	TestEqual(TEXT("two steps"), Slicer.Steps(), 2);
	TestTrue(TEXT("longest stays the maximum"), FMath::Abs(Slicer.LongestMs() - 1.2) < 0.01);
	TestTrue(TEXT("total adds both"), FMath::Abs(Slicer.TotalMs() - 1.6) < 0.01);

	Slicer.Reset();
	TestEqual(TEXT("reset steps"), Slicer.Steps(), 0);
	TestEqual(TEXT("reset longest"), Slicer.LongestMs(), 0.0);
	TestEqual(TEXT("reset total"), Slicer.TotalMs(), 0.0);
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
