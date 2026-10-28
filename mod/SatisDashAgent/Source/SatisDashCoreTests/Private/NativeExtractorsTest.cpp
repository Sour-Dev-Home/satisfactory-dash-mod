#include "SatisDash/Native/Extractors/NativeExtractors.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	/** A synthetic Mk1 miner on a synthetic resource, 60 of 120 per minute. */
	FExtractorReading NativeExtractorsMiner(const TCHAR* Id)
	{
		FExtractorReading R;
		R.Id = Id;
		R.Name = TEXT("Test Miner");
		R.ClassName = TEXT("Build_TestMinerMk1_C");
		R.bProducing = true;
		R.XCm = 10000;
		R.YCm = 25000;
		R.ZCm = -2000;
		R.YawDeg = 90;
		R.CircuitGroupId = 3;
		R.ResourceClass = TEXT("Desc_TestOre_C");
		R.ResourceName = TEXT("Test Ore");
		R.CurrentPerMinute = 60;
		R.MaxPerMinute = 120;
		return R;
	}

	FString NativeExtractorsOne(const FExtractorReading& Reading)
	{
		int32 Dropped = 0;
		int32 Over = 0;
		return NativeExtractorsFragment({ Reading }, Dropped, Over);
	}

	/** The three golden readings, out of id order. */
	TArray<FExtractorReading> NativeExtractorsGoldenReadings()
	{
		FExtractorReading Well;
		Well.Id = TEXT("Test_Well_C_3");
		Well.Name = TEXT("Test Well");
		Well.ClassName = TEXT("Build_TestWell_C");

		FExtractorReading Pump;
		Pump.Id = TEXT("Test_Pump_C_2");
		Pump.Name = TEXT("Test Pump");
		Pump.ClassName = TEXT("Build_TestPump_C");
		Pump.bPaused = true;
		Pump.bBackedUp = true;
		Pump.ClockPercent = 150;
		Pump.XCm = -500;
		Pump.YCm = 50;
		Pump.ZCm = 1200;
		Pump.YawDeg = 270;
		Pump.ResourceClass = TEXT("Desc_TestFluid_C");
		Pump.ResourceName = TEXT("Test Fluid");
		Pump.CurrentPerMinute = 45;
		Pump.MaxPerMinute = 60;
		Pump.bFluid = true;

		return { Well, NativeExtractorsMiner(TEXT("Test_Miner_C_1")), Pump };
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeExtractorsMapTest, "SatisDash.Core.NativeExtractors.Map", SatisDash::Test::Flags)

bool FNativeExtractorsMapTest::RunTest(const FString& Parameters)
{
	const FString Miner = NativeExtractorsOne(NativeExtractorsMiner(TEXT("Test_Miner_C_1")));
	SatisDash::Test::SameText(*this, TEXT("a Mk1 miner"), Miner,
		TEXT("[{\"id\":\"Test_Miner_C_1\",\"name\":\"Test Miner\",\"className\":\"Build_TestMinerMk1_C\",\"recipe\":null,\"isProducing\":true,")
		TEXT("\"isPaused\":false,\"isBackedUp\":false,\"circuitGroupId\":3,\"location\":{\"xM\":100,\"yM\":250,\"zM\":-20,\"rotationDeg\":90},")
		TEXT("\"clockSpeedPercent\":100,\"production\":[{\"name\":\"Test Ore\",\"className\":\"Desc_TestOre_C\",\"currentPerMinute\":60,")
		TEXT("\"maxPerMinute\":120,\"percent\":50}],\"ingredients\":[]}]"));

	// No maximum: the percent is 0, never a division by zero.
	FExtractorReading NoMax = NativeExtractorsMiner(TEXT("A"));
	NoMax.MaxPerMinute = 0;
	TestTrue(TEXT("percent is 0 when the maximum is 0"), NativeExtractorsOne(NoMax).Contains(TEXT("\"maxPerMinute\":0,\"percent\":0}")));

	// The name falls back to the class.
	FExtractorReading Nameless = NativeExtractorsMiner(TEXT("A"));
	Nameless.ResourceName.Empty();
	TestTrue(TEXT("no resource name: the class stands in"), NativeExtractorsOne(Nameless).Contains(TEXT("\"name\":\"Desc_TestOre_C\",\"className\":\"Desc_TestOre_C\"")));

	// Unoccupied or unknown: no production.
	FExtractorReading Unoccupied = NativeExtractorsMiner(TEXT("A"));
	Unoccupied.ResourceClass.Empty();
	TestTrue(TEXT("unoccupied: no production"), NativeExtractorsOne(Unoccupied).Contains(TEXT("\"production\":[]")));

	// A clock that is not finite is left out; a negative rate is zero.
	FExtractorReading Odd = NativeExtractorsMiner(TEXT("A"));
	Odd.ClockPercent = 0;
	Odd.CurrentPerMinute = -3;
	const FString OddOut = NativeExtractorsOne(Odd);
	TestFalse(TEXT("a clock of 0 is not reported"), OddOut.Contains(TEXT("clockSpeedPercent")));
	TestTrue(TEXT("a negative rate is 0"), OddOut.Contains(TEXT("\"currentPerMinute\":0,")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeExtractorsFlagsTest, "SatisDash.Core.NativeExtractors.Flags", SatisDash::Test::Flags)

bool FNativeExtractorsFlagsTest::RunTest(const FString& Parameters)
{
	FExtractorReading Quiet = NativeExtractorsMiner(TEXT("A"));
	Quiet.bProducing = false;
	const FString QuietOut = NativeExtractorsOne(Quiet);
	TestTrue(TEXT("nothing set: not producing, not paused, not backed up"),
		QuietOut.Contains(TEXT("\"isProducing\":false,\"isPaused\":false,\"isBackedUp\":false")));

	// Core never infers backed-up: a full-looking rate, or a pause, does not set it.
	FExtractorReading Full = NativeExtractorsMiner(TEXT("A"));
	Full.CurrentPerMinute = Full.MaxPerMinute;
	Full.bPaused = true;
	TestTrue(TEXT("paused at the maximum is still not backed up"), NativeExtractorsOne(Full).Contains(TEXT("\"isBackedUp\":false")));

	FExtractorReading Backed = NativeExtractorsMiner(TEXT("A"));
	Backed.bProducing = false;
	Backed.bBackedUp = true;
	TestTrue(TEXT("backed up passes through"), NativeExtractorsOne(Backed).Contains(TEXT("\"isProducing\":false,\"isPaused\":false,\"isBackedUp\":true")));

	FExtractorReading Paused = NativeExtractorsMiner(TEXT("A"));
	Paused.bPaused = true;
	TestTrue(TEXT("paused is independent of producing and backed up"),
		NativeExtractorsOne(Paused).Contains(TEXT("\"isProducing\":true,\"isPaused\":true,\"isBackedUp\":false")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeExtractorsRulesTest, "SatisDash.Core.NativeExtractors.Rules", SatisDash::Test::Flags)

bool FNativeExtractorsRulesTest::RunTest(const FString& Parameters)
{
	int32 Dropped = 0;
	int32 Over = 0;
	SatisDash::Test::SameText(*this, TEXT("empty"), NativeExtractorsFragment({}, Dropped, Over), TEXT("[]"));
	TestEqual(TEXT("empty: nothing dropped or over"), Dropped + Over, 0);

	// More than the cap keeps the cap and counts the rest.
	TArray<FExtractorReading> Many;
	for (int32 I = 0; I < ExtractorsMaxItems + 1; ++I)
	{
		Many.Add(NativeExtractorsMiner(*FString::Printf(TEXT("Test_Miner_C_%05d"), I)));
	}
	const FString Capped = NativeExtractorsFragment(Many, Dropped, Over);
	TestEqual(TEXT("5001 readings: one over"), Over, 1);
	TestEqual(TEXT("5001 readings: none dropped"), Dropped, 0);
	TestTrue(TEXT("the first 5000 by id are kept"), Capped.Contains(TEXT("Test_Miner_C_04999")) && !Capped.Contains(TEXT("Test_Miner_C_05000")));

	// The same bytes whatever order the scan found them in.
	TArray<FExtractorReading> Forward = NativeExtractorsGoldenReadings();
	TArray<FExtractorReading> Backward;
	for (int32 I = Forward.Num() - 1; I >= 0; --I)
	{
		Backward.Add(Forward[I]);
	}
	SatisDash::Test::SameText(*this, TEXT("shuffled input"), NativeExtractorsFragment(Backward, Dropped, Over), NativeExtractorsFragment(Forward, Dropped, Over));

	// A repeated id keeps the first (by content order), once.
	FExtractorReading Twin = NativeExtractorsMiner(TEXT("Same"));
	Twin.CurrentPerMinute = 90;
	const FString Pair = NativeExtractorsFragment({ NativeExtractorsMiner(TEXT("Same")), Twin }, Dropped, Over);
	SatisDash::Test::SameText(*this, TEXT("a duplicate id is written once, the first one stays"), Pair, NativeExtractorsOne(NativeExtractorsMiner(TEXT("Same"))));

	// A rate that is not a number drops the reading and counts it.
	const uint64 NanBits = 0x7FF8000000000000ull;
	double Nan = 0;
	FMemory::Memcpy(&Nan, &NanBits, sizeof(Nan));
	FExtractorReading Bad = NativeExtractorsMiner(TEXT("Bad"));
	Bad.CurrentPerMinute = Nan;
	const FString Kept = NativeExtractorsFragment({ Bad, NativeExtractorsMiner(TEXT("Good")) }, Dropped, Over);
	TestEqual(TEXT("one dropped"), Dropped, 1);
	TestTrue(TEXT("the good one stays, the bad one is gone"), Kept.Contains(TEXT("\"id\":\"Good\"")) && !Kept.Contains(TEXT("\"id\":\"Bad\"")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeExtractorsGoldenTest, "SatisDash.Core.NativeExtractors.Golden", SatisDash::Test::Flags)

bool FNativeExtractorsGoldenTest::RunTest(const FString& Parameters)
{
	int32 Dropped = 0;
	int32 Over = 0;
	const FString Actual = NativeExtractorsFragment(NativeExtractorsGoldenReadings(), Dropped, Over);
	FString Expected = SatisDash::Test::Fixture(TEXT("native-extractors.expected.json"));
	while (Expected.Len() > 0 && (Expected[Expected.Len() - 1] == TEXT('\n') || Expected[Expected.Len() - 1] == TEXT('\r')))
	{
		Expected.LeftChopInline(1);
	}
	TestTrue(TEXT("the fixture was found"), !Expected.IsEmpty());
	SatisDash::Test::SameText(*this, TEXT("the hand-written text, byte for byte"), Actual, Expected);
	TestEqual(TEXT("nothing dropped or over"), Dropped + Over, 0);
	return true;
}
