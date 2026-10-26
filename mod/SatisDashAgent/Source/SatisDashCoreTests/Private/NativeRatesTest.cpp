#include "SatisDash/Native/Rates/NativeRates.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	/** Infinity and NaN by their bits: the numeric_limits helpers are not available, and HUGE_VAL is finite under /fp:fast. */
	double NativeRatesFromBits(uint64 Bits)
	{
		double Value = 0;
		FMemory::Memcpy(&Value, &Bits, sizeof(Value));
		return Value;
	}
	double NativeRatesInfinity() { return NativeRatesFromBits(0x7FF0000000000000ull); }
	double NativeRatesNaN() { return NativeRatesFromBits(0x7FF8000000000000ull); }

	// Every expected number below is read from our recorded FRM output (black-box HTTP), never from the code under test:
	// Fixtures/frm-getFactory-running.json (the same records are in captured-responses/frm-getFactory-2026-09-22-01-running-trimmed.json).
	// The recipe amounts and durations are chosen so that the 100% clock reproduces the recorded MaxProd; the capture
	// does not carry the recipe itself.
	FRateInput Stator(double Potential, double RawProductivity, bool bProducing)
	{
		FRateInput In;
		// One stator per 12 s makes 5 per minute at 100%: the capture's Build_AssemblerMk1_C_2146597364 "Stator" MaxProd 5.
		In.Products.Add({ TEXT("Stator"), TEXT("Desc_Stator_C"), 1.0, false });
		In.Ingredients.Add({ TEXT("Steel Pipe"), TEXT("Desc_SteelPlate_C"), 3.0, false });
		In.CycleSeconds = 12.0;
		In.Potential = Potential;
		In.RawProductivity = RawProductivity;
		In.bProducing = bProducing;
		return In;
	}

	FRateInput FuelRefinery(double RawProductivity, bool bProducing)
	{
		FRateInput In;
		// The capture's Build_OilRefinery_C_2146613066 at 100%: 60 Crude Oil in (MaxConsumed 60) and 40 Fuel out (MaxProd 40),
		// in m3 per minute. Litres per cycle 60000 and 40000 over a 60 s cycle reproduce them.
		In.Products.Add({ TEXT("Fuel"), TEXT("Desc_LiquidFuel_C"), 40000.0, true });
		In.Ingredients.Add({ TEXT("Crude Oil"), TEXT("Desc_LiquidOil_C"), 60000.0, true });
		In.CycleSeconds = 60.0;
		In.Potential = 1.0;
		In.RawProductivity = RawProductivity;
		In.bProducing = bProducing;
		return In;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeRatesMaxTest, "SatisDash.Core.NativeRates.Max", SatisDash::Test::Flags)

bool FNativeRatesMaxTest::RunTest(const FString& Parameters)
{
	TArray<FRateReading> Prod, Ing;

	NativeRates(Stator(1.0, 1.0, true), ERatesMode::Averaged, Prod, Ing);
	TestEqual(TEXT("one product"), Prod.Num(), 1);
	TestEqual(TEXT("one ingredient"), Ing.Num(), 1);
	TestEqual(TEXT("Stator at 100%: MaxProd 5"), Prod[0].Max, 5.0);
	TestEqual(TEXT("the name and class are carried"), Prod[0].ClassName, FString(TEXT("Desc_Stator_C")));
	TestEqual(TEXT("the ingredient name is carried"), Ing[0].Name, FString(TEXT("Steel Pipe")));
	TestEqual(TEXT("ingredient: 3 per 12 s is 15 a minute"), Ing[0].Max, 15.0);

	// A float clock of 1.6 is 1.60000002384...; the result must be exactly 8, not 8.0000001 (capture: Stator at 160%, MaxProd 8).
	NativeRates(Stator(static_cast<double>(1.6f), 1.0, true), ERatesMode::Averaged, Prod, Ing);
	TestEqual(TEXT("Stator at 160%: MaxProd 8, no float noise"), Prod[0].Max, 8.0);

	NativeRates(Stator(2.5, 1.0, true), ERatesMode::Averaged, Prod, Ing);
	TestEqual(TEXT("250% clock"), Prod[0].Max, 12.5);

	NativeRates(FuelRefinery(1.0, true), ERatesMode::Averaged, Prod, Ing);
	TestEqual(TEXT("refinery: Fuel out is 40 m3 a minute"), Prod[0].Max, 40.0);
	TestEqual(TEXT("refinery: Crude Oil in is 60 m3 a minute"), Ing[0].Max, 60.0);

	// Somersloops (FRM's output, run 40378567334): products times the boost, ingredients unchanged; current follows Max.
	FRateInput Boosted = Stator(1.0, 0.5, true);
	Boosted.ProductionBoost = 2.0;
	NativeRates(Boosted, ERatesMode::Averaged, Prod, Ing);
	TestEqual(TEXT("boost 2: MaxProd 10"), Prod[0].Max, 10.0);
	TestEqual(TEXT("boost 2: current is the boosted Max times the fraction"), Prod[0].Current, 5.0);
	TestEqual(TEXT("boost 2: the ingredient is not boosted"), Ing[0].Max, 15.0);
	TestEqual(TEXT("boost 2: percent is the fraction"), Prod[0].Percent, 50.0);
	Boosted.ProductionBoost = 1.5;
	NativeRates(Boosted, ERatesMode::Averaged, Prod, Ing);
	TestEqual(TEXT("boost 1.5: MaxProd 7.5"), Prod[0].Max, 7.5);
	Boosted.ProductionBoost = NativeRatesNaN();
	NativeRates(Boosted, ERatesMode::Averaged, Prod, Ing);
	TestEqual(TEXT("a NaN boost is 0"), Prod[0].Max, 0.0);
	TestEqual(TEXT("a NaN boost leaves the ingredient alone"), Ing[0].Max, 15.0);

	FRateInput Zero = Stator(1.0, 1.0, true);
	Zero.CycleSeconds = 0.0;
	NativeRates(Zero, ERatesMode::Averaged, Prod, Ing);
	TestEqual(TEXT("a zero cycle is 0, not infinity"), Prod[0].Max, 0.0);
	TestEqual(TEXT("a zero cycle: current 0"), Prod[0].Current, 0.0);
	Zero.CycleSeconds = -3.0;
	NativeRates(Zero, ERatesMode::Averaged, Prod, Ing);
	TestEqual(TEXT("a negative cycle is 0"), Prod[0].Max, 0.0);
	Zero.CycleSeconds = NativeRatesInfinity();
	NativeRates(Zero, ERatesMode::Averaged, Prod, Ing);
	TestEqual(TEXT("a non-finite cycle is 0"), Prod[0].Max, 0.0);
	Zero.CycleSeconds = 12.0;
	Zero.Potential = NativeRatesNaN();
	NativeRates(Zero, ERatesMode::Averaged, Prod, Ing);
	TestEqual(TEXT("a NaN clock is 0"), Prod[0].Max, 0.0);

	NativeRates(FRateInput(), ERatesMode::Averaged, Prod, Ing);
	TestEqual(TEXT("no recipe: no products"), Prod.Num(), 0);
	TestEqual(TEXT("no recipe: no ingredients"), Ing.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeRatesCurrentTest, "SatisDash.Core.NativeRates.Current", SatisDash::Test::Flags)

bool FNativeRatesCurrentTest::RunTest(const FString& Parameters)
{
	TArray<FRateReading> Prod, Ing;

	// The capture's grid value 24/255 (ProdPercent 9.4117657...) on a Max of 60 gives 5.647 (record: the capture's
	// ProdPercent 9.411765, an Assembler line in the 2026-09-22 file). Percent is 100 times the fraction.
	FRateInput Sixty = Stator(1.0, 24.0 / 255.0, true);
	Sixty.CycleSeconds = 1.0;
	NativeRates(Sixty, ERatesMode::Averaged, Prod, Ing);
	TestEqual(TEXT("Max is 60"), Prod[0].Max, 60.0);
	TestEqual(TEXT("current is Max times the fraction"), Prod[0].Current, 5.647, 1e-3);
	TestEqual(TEXT("percent"), Prod[0].Percent, 9.4118, 1e-3);
	TestEqual(TEXT("the ingredient uses the same fraction"), Ing[0].Percent, 9.4118, 1e-3);

	// The capture's Stator at 160%: CurrentProd 1.9764708..., MaxProd 8, ProdPercent 24.705885...
	NativeRates(Stator(static_cast<double>(1.6f), 0.24705885350704193, false), ERatesMode::Averaged, Prod, Ing);
	TestEqual(TEXT("Stator at 160%: current"), Prod[0].Current, 1.9764708, 1e-3);
	TestEqual(TEXT("Stator at 160%: percent"), Prod[0].Percent, 24.705885, 1e-3);

	NativeRates(Stator(1.0, 1.0, true), ERatesMode::Averaged, Prod, Ing);
	TestEqual(TEXT("full productivity is the maximum"), Prod[0].Current, 5.0);
	TestEqual(TEXT("and 100 percent"), Prod[0].Percent, 100.0);

	NativeRates(Stator(1.0, 7.0, true), ERatesMode::Averaged, Prod, Ing);
	TestEqual(TEXT("above full scale clamps to 100 percent"), Prod[0].Percent, 100.0);
	TestEqual(TEXT("above full scale clamps the current to Max"), Prod[0].Current, 5.0);

	NativeRates(Stator(1.0, -0.5, true), ERatesMode::Averaged, Prod, Ing);
	TestEqual(TEXT("negative productivity is 0"), Prod[0].Current, 0.0);
	TestEqual(TEXT("negative productivity: percent 0"), Prod[0].Percent, 0.0);
	NativeRates(Stator(1.0, NativeRatesNaN(), true), ERatesMode::Averaged, Prod, Ing);
	TestEqual(TEXT("NaN productivity is 0"), Prod[0].Current, 0.0);
	TestEqual(TEXT("NaN productivity: percent 0"), Prod[0].Percent, 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeRatesFallbackTest, "SatisDash.Core.NativeRates.Fallback", SatisDash::Test::Flags)

bool FNativeRatesFallbackTest::RunTest(const FString& Parameters)
{
	// The owner's fallback is one constant; pinning the default makes a flip a visible diff in this test.
	TestTrue(TEXT("the default mode is Averaged"), DefaultRatesMode == ERatesMode::Averaged);

	TArray<FRateReading> Prod, Ing;
	// Instantaneous ignores the averaged productivity (here 0.2) and follows isProducing.
	NativeRates(FuelRefinery(0.2, true), ERatesMode::Instantaneous, Prod, Ing);
	TestEqual(TEXT("producing: products at Max"), Prod[0].Current, 40.0);
	TestEqual(TEXT("producing: products 100 percent"), Prod[0].Percent, 100.0);
	TestEqual(TEXT("producing: ingredients at Max"), Ing[0].Current, 60.0);
	TestEqual(TEXT("producing: ingredients 100 percent"), Ing[0].Percent, 100.0);
	TestEqual(TEXT("Max is unchanged"), Prod[0].Max, 40.0);

	NativeRates(FuelRefinery(0.9, false), ERatesMode::Instantaneous, Prod, Ing);
	TestEqual(TEXT("idle: products 0"), Prod[0].Current, 0.0);
	TestEqual(TEXT("idle: products 0 percent"), Prod[0].Percent, 0.0);
	TestEqual(TEXT("idle: ingredients 0"), Ing[0].Current, 0.0);
	TestEqual(TEXT("idle: Max is still reported"), Ing[0].Max, 60.0);
	return true;
}
