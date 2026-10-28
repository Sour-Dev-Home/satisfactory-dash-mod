#include "SatisDash/Native/Generators/NativeGenerators.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	FFuelReading NativeGeneratorsItem(const TCHAR* ClassName, const TCHAR* Name, double Amount)
	{
		FFuelReading F;
		F.ClassName = ClassName;
		F.Name = Name;
		F.Amount = Amount;
		return F;
	}

	/** A synthetic coal-style generator: fuel and a water supplement, 40 of 75 MW. */
	FGeneratorReading NativeGeneratorsCoal(const TCHAR* Id)
	{
		FGeneratorReading R;
		R.Id = Id;
		R.Name = TEXT("Test Coal Generator");
		R.ClassName = TEXT("Build_TestCoalGen_C");
		R.bHasLocation = true;
		R.XCm = 10000;
		R.YCm = 25000;
		R.ZCm = -2000;
		R.YawDeg = 90;
		R.CircuitGroupId = 3;
		R.CurrentMW = 40;
		R.CapacityMW = 75;
		R.bProducing = true;
		R.Fuel = NativeGeneratorsItem(TEXT("Desc_TestCoal_C"), TEXT("Test Coal"), 25);
		R.bSupplementRead = true;
		R.Supplement = NativeGeneratorsItem(TEXT("Desc_TestWater_C"), TEXT("Test Water"), 1.5);
		return R;
	}

	FString NativeGeneratorsOne(const FGeneratorReading& Reading)
	{
		int32 Dropped = 0;
		int32 Over = 0;
		return NativeGeneratorsFragment({ Reading }, Dropped, Over);
	}

	/** The three golden readings, out of id order. */
	TArray<FGeneratorReading> NativeGeneratorsGoldenReadings()
	{
		FGeneratorReading Geo;
		Geo.Id = TEXT("Test_Geo_C_3");
		Geo.Name = TEXT("Test Geothermal");
		Geo.ClassName = TEXT("Build_TestGeo_C");
		Geo.CurrentMW = 150;
		Geo.CapacityMW = 200;
		Geo.bProducing = true;
		Geo.bSupplementRead = true;

		FGeneratorReading Fuel;
		Fuel.Id = TEXT("Test_Fuel_C_2");
		Fuel.Name = TEXT("Test Fuel Generator");
		Fuel.ClassName = TEXT("Build_TestFuelGen_C");
		Fuel.bHasLocation = true;
		Fuel.XCm = -500;
		Fuel.YCm = 50;
		Fuel.ZCm = 1200;
		Fuel.YawDeg = 270;
		Fuel.CurrentMW = 0;
		Fuel.CapacityMW = 187.5;
		Fuel.ClockPercent = 150;
		Fuel.Fuel = NativeGeneratorsItem(TEXT("Desc_TestFluid_C"), TEXT("Test Fluid"), 12.25);

		return { Geo, NativeGeneratorsCoal(TEXT("Test_Coal_C_1")), Fuel };
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeGeneratorsMapTest, "SatisDash.Core.NativeGenerators.Map", SatisDash::Test::Flags)

bool FNativeGeneratorsMapTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::SameText(*this, TEXT("coal with a water supplement"), NativeGeneratorsOne(NativeGeneratorsCoal(TEXT("Test_Coal_C_1"))),
		TEXT("[{\"id\":\"Test_Coal_C_1\",\"name\":\"Test Coal Generator\",\"className\":\"Build_TestCoalGen_C\",")
		TEXT("\"location\":{\"xM\":100,\"yM\":250,\"zM\":-20,\"rotationDeg\":90},\"circuitGroupId\":3,\"currentMW\":40,\"capacityMW\":75,")
		TEXT("\"clockSpeedPercent\":100,\"isProducing\":true,\"fuel\":{\"className\":\"Desc_TestCoal_C\",\"name\":\"Test Coal\",\"amount\":25},")
		TEXT("\"supplement\":{\"className\":\"Desc_TestWater_C\",\"name\":\"Test Water\",\"amount\":1.5}}]"));

	// A fuel generator (a fluid fuel, no supplement read), idle, overclocked.
	FGeneratorReading Fuel = NativeGeneratorsCoal(TEXT("F"));
	Fuel.bSupplementRead = false;
	Fuel.Supplement.Reset();
	Fuel.bProducing = false;
	Fuel.CurrentMW = 0;
	Fuel.ClockPercent = 250;
	Fuel.Fuel = NativeGeneratorsItem(TEXT("Desc_TestFluid_C"), TEXT("Test Fluid"), 12.25);
	const FString FuelOut = NativeGeneratorsOne(Fuel);
	TestTrue(TEXT("idle fuel generator"), FuelOut.Contains(TEXT("\"currentMW\":0,\"capacityMW\":75,\"clockSpeedPercent\":250,\"isProducing\":false,\"fuel\":{")));
	TestTrue(TEXT("a fluid amount in m3 stays fractional"), FuelOut.Contains(TEXT("\"amount\":12.25}")));

	// Biomass: a solid fuel, no location (not read), unconnected.
	FGeneratorReading Biomass = NativeGeneratorsCoal(TEXT("B"));
	Biomass.bHasLocation = false;
	Biomass.CircuitGroupId = -1;
	Biomass.bSupplementRead = false;
	Biomass.Fuel = NativeGeneratorsItem(TEXT("Desc_TestLeaves_C"), TEXT("Test Leaves"), 100);
	const FString BiomassOut = NativeGeneratorsOne(Biomass);
	TestFalse(TEXT("no location read: the key is absent"), BiomassOut.Contains(TEXT("location")));
	TestTrue(TEXT("unconnected is -1"), BiomassOut.Contains(TEXT("\"circuitGroupId\":-1,")));

	// Nuclear: uranium rods and water, no waste field in the contract.
	FGeneratorReading Nuclear = NativeGeneratorsCoal(TEXT("N"));
	Nuclear.Fuel = NativeGeneratorsItem(TEXT("Desc_TestRod_C"), TEXT("Test Rod"), 3);
	const FString NuclearOut = NativeGeneratorsOne(Nuclear);
	TestTrue(TEXT("nuclear: rods in fuel, water in supplement"), NuclearOut.Contains(TEXT("\"fuel\":{\"className\":\"Desc_TestRod_C\",\"name\":\"Test Rod\",\"amount\":3},\"supplement\":{")));
	TestFalse(TEXT("nothing about waste is written"), NuclearOut.Contains(TEXT("aste")));

	// A clock that is not reported is left out; long text is cut at 200.
	FGeneratorReading Odd = NativeGeneratorsCoal(TEXT("A"));
	Odd.ClockPercent = 0;
	Odd.ClassName = FString::ChrN(250, TEXT('x'));
	const FString OddOut = NativeGeneratorsOne(Odd);
	TestFalse(TEXT("a clock of 0 is not reported"), OddOut.Contains(TEXT("clockSpeedPercent")));
	TestTrue(TEXT("the class name is cut at 200"), OddOut.Contains(TEXT("\"className\":\"") + FString::ChrN(200, TEXT('x')) + TEXT("\",")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeGeneratorsFuelTest, "SatisDash.Core.NativeGenerators.Fuel", SatisDash::Test::Flags)

bool FNativeGeneratorsFuelTest::RunTest(const FString& Parameters)
{
	// Geothermal takes no fuel: "fuel":null is written, not omitted.
	FGeneratorReading Geo = NativeGeneratorsCoal(TEXT("G"));
	Geo.Fuel.Reset();
	Geo.bSupplementRead = true;
	Geo.Supplement.Reset();
	const FString GeoOut = NativeGeneratorsOne(Geo);
	TestTrue(TEXT("no fuel: null, present"), GeoOut.Contains(TEXT("\"isProducing\":true,\"fuel\":null,\"supplement\":null}")));

	// An empty fuel slot (read, nothing in it) is the same null.
	FGeneratorReading Empty = NativeGeneratorsCoal(TEXT("E"));
	Empty.Fuel.Reset();
	TestTrue(TEXT("empty fuel slot: null"), NativeGeneratorsOne(Empty).Contains(TEXT("\"fuel\":null,\"supplement\":{")));

	// Supplement: not read is absent, read and empty is null.
	FGeneratorReading NotRead = NativeGeneratorsCoal(TEXT("S"));
	NotRead.bSupplementRead = false;
	NotRead.Supplement.Reset();
	const FString NotReadOut = NativeGeneratorsOne(NotRead);
	TestFalse(TEXT("supplement not read: the key is absent"), NotReadOut.Contains(TEXT("supplement")));
	TestTrue(TEXT("and the record still closes after fuel"), NotReadOut.EndsWith(TEXT("\"amount\":25}}]")));

	FGeneratorReading ReadEmpty = NativeGeneratorsCoal(TEXT("S"));
	ReadEmpty.Supplement.Reset();
	TestTrue(TEXT("supplement read and empty: null"), NativeGeneratorsOne(ReadEmpty).EndsWith(TEXT("\"supplement\":null}]")));

	// A fuel amount the schema would refuse is zero.
	FGeneratorReading Neg = NativeGeneratorsCoal(TEXT("N"));
	Neg.Fuel = NativeGeneratorsItem(TEXT("Desc_TestCoal_C"), TEXT("Test Coal"), -4);
	TestTrue(TEXT("a negative amount is 0"), NativeGeneratorsOne(Neg).Contains(TEXT("\"name\":\"Test Coal\",\"amount\":0}")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeGeneratorsRulesTest, "SatisDash.Core.NativeGenerators.Rules", SatisDash::Test::Flags)

bool FNativeGeneratorsRulesTest::RunTest(const FString& Parameters)
{
	int32 Dropped = 0;
	int32 Over = 0;
	SatisDash::Test::SameText(*this, TEXT("empty"), NativeGeneratorsFragment({}, Dropped, Over), TEXT("[]"));
	TestEqual(TEXT("empty: nothing dropped or over"), Dropped + Over, 0);

	// More than the cap keeps the cap and counts the rest.
	TArray<FGeneratorReading> Many;
	for (int32 I = 0; I < GeneratorsMaxItems + 1; ++I)
	{
		Many.Add(NativeGeneratorsCoal(*FString::Printf(TEXT("Test_Coal_C_%05d"), I)));
	}
	const FString Capped = NativeGeneratorsFragment(Many, Dropped, Over);
	TestEqual(TEXT("5001 readings: one over"), Over, 1);
	TestEqual(TEXT("5001 readings: none dropped"), Dropped, 0);
	TestTrue(TEXT("the first 5000 by id are kept"), Capped.Contains(TEXT("Test_Coal_C_04999")) && !Capped.Contains(TEXT("Test_Coal_C_05000")));

	// The same bytes whatever order the scan found them in.
	TArray<FGeneratorReading> Forward = NativeGeneratorsGoldenReadings();
	TArray<FGeneratorReading> Backward;
	for (int32 I = Forward.Num() - 1; I >= 0; --I)
	{
		Backward.Add(Forward[I]);
	}
	SatisDash::Test::SameText(*this, TEXT("shuffled input"), NativeGeneratorsFragment(Backward, Dropped, Over), NativeGeneratorsFragment(Forward, Dropped, Over));

	// A repeated id keeps the first (by content order), once.
	FGeneratorReading Twin = NativeGeneratorsCoal(TEXT("Same"));
	Twin.CurrentMW = 60;
	const FString Pair = NativeGeneratorsFragment({ NativeGeneratorsCoal(TEXT("Same")), Twin }, Dropped, Over);
	SatisDash::Test::SameText(*this, TEXT("a duplicate id is written once, the first one stays"), Pair, NativeGeneratorsOne(NativeGeneratorsCoal(TEXT("Same"))));

	// A power that is not a number, or is negative, drops the reading and counts it.
	const uint64 NanBits = 0x7FF8000000000000ull;
	double Nan = 0;
	FMemory::Memcpy(&Nan, &NanBits, sizeof(Nan));
	FGeneratorReading Bad = NativeGeneratorsCoal(TEXT("Bad"));
	Bad.CurrentMW = Nan;
	FGeneratorReading Negative = NativeGeneratorsCoal(TEXT("Negative"));
	Negative.CapacityMW = -1;
	const FString Kept = NativeGeneratorsFragment({ Bad, Negative, NativeGeneratorsCoal(TEXT("Good")) }, Dropped, Over);
	TestEqual(TEXT("two dropped"), Dropped, 2);
	TestTrue(TEXT("the good one stays, the bad ones are gone"),
		Kept.Contains(TEXT("\"id\":\"Good\"")) && !Kept.Contains(TEXT("\"id\":\"Bad\"")) && !Kept.Contains(TEXT("\"id\":\"Negative\"")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeGeneratorsGoldenTest, "SatisDash.Core.NativeGenerators.Golden", SatisDash::Test::Flags)

bool FNativeGeneratorsGoldenTest::RunTest(const FString& Parameters)
{
	int32 Dropped = 0;
	int32 Over = 0;
	const FString Actual = NativeGeneratorsFragment(NativeGeneratorsGoldenReadings(), Dropped, Over);
	FString Expected = SatisDash::Test::Fixture(TEXT("native-generators.expected.json"));
	while (Expected.Len() > 0 && (Expected[Expected.Len() - 1] == TEXT('\n') || Expected[Expected.Len() - 1] == TEXT('\r')))
	{
		Expected.LeftChopInline(1);
	}
	TestTrue(TEXT("the fixture was found"), !Expected.IsEmpty());
	SatisDash::Test::SameText(*this, TEXT("the hand-written text, byte for byte"), Actual, Expected);
	TestEqual(TEXT("nothing dropped or over"), Dropped + Over, 0);
	return true;
}
