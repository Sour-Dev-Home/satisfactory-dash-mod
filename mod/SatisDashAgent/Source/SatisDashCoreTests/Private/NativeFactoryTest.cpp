#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "SatisDash/Native/Factory/NativeFactory.h"
#include "SatisDash/Snapshot.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	FRateReading NativeFactoryRate(const TCHAR* Name, const TCHAR* ClassName, double Current, double Max, double Percent)
	{
		FRateReading R;
		R.Name = Name;
		R.ClassName = ClassName;
		R.Current = Current;
		R.Max = Max;
		R.Percent = Percent;
		return R;
	}

	FSlotReading NativeFactorySlot(const TCHAR* ClassName, double Amount, double MaxAmount)
	{
		FSlotReading S;
		S.ClassName = ClassName;
		S.Amount = Amount;
		S.MaxAmount = MaxAmount;
		return S;
	}

	/** A bare machine: a recipe (null for none), no rates, an empty input list, no location. */
	FFactoryReading NativeFactoryMachine(const TCHAR* Id, const TCHAR* Recipe)
	{
		FFactoryReading R;
		R.Id = Id;
		R.Name = TEXT("Machine");
		R.ClassName = TEXT("Build_Machine_C");
		if (Recipe)
		{
			R.Recipe = FString(Recipe);
		}
		R.Input = TArray<FSlotReading>();
		return R;
	}

	void NativeFactoryPlace(FFactoryReading& R, double XCm, double YCm, double ZCm, double YawDeg)
	{
		R.bHasLocation = true;
		R.XCm = XCm;
		R.YCm = YCm;
		R.ZCm = ZCm;
		R.YawDeg = YawDeg;
	}

	/** Infinity by its bits: HUGE_VAL is a large finite number under /fp:fast. */
	double NativeFactoryInfinity()
	{
		const uint64 Bits = 0x7FF0000000000000ull;
		double Value = 0;
		FMemory::Memcpy(&Value, &Bits, sizeof(Value));
		return Value;
	}

	/** The part's text for one reading. */
	FString NativeFactoryOne(const FFactoryReading& Reading)
	{
		int32 Dropped = 0;
		return NativeFactoryPart({ Reading }, Dropped);
	}

	using FNativeFactoryItems = TArray<TSharedPtr<FJsonValue>>;

	/** The buildings array of a part. */
	FNativeFactoryItems NativeFactoryBuildings(const FString& Json)
	{
		const TSharedPtr<FJsonObject> Root = SatisDash::Json::ParseObject(Json);
		return Root.IsValid() ? Root->GetArrayField(TEXT("buildings")) : TArray<TSharedPtr<FJsonValue>>();
	}

	/** The ten machines (seven of the running fixture, three of the norecipe one) of the two bridge fixtures, one reading per record, written out by hand. */
	TArray<FFactoryReading> NativeFactoryFixtureReadings()
	{
		TArray<FFactoryReading> Out;
		auto add = [&Out](const TCHAR* Id, const TCHAR* Name, const TCHAR* Class, const TCHAR* Recipe, bool bProducing, int64 Group,
			double X, double Y, double Z, double Yaw) -> FFactoryReading&
		{
			Out.Add(FFactoryReading());
			FFactoryReading& R = Out[Out.Num() - 1];
			R.Id = Id;
			R.Name = Name;
			R.ClassName = Class;
			if (Recipe)
			{
				R.Recipe = FString(Recipe);
			}
			R.bProducing = bProducing;
			R.CircuitGroupId = Group;
			NativeFactoryPlace(R, X, Y, Z, Yaw);
			R.ClockPercent = 100;
			R.Input = TArray<FSlotReading>();
			return R;
		};

		// frm-getFactory-running.json
		FFactoryReading* R = &add(TEXT("Build_AssemblerMk1_C_2147069774"), TEXT("Assembler"), TEXT("Build_AssemblerMk1_C"), TEXT("Reinforced Iron Plate"), false, 0, -43600, -143900, 11800.0087890625, 180);
		R->Production = { NativeFactoryRate(TEXT("Reinforced Iron Plate"), TEXT("Desc_IronPlateReinforced_C"), 0, 5, 0) };
		R->Ingredients = { NativeFactoryRate(TEXT("Iron Plate"), TEXT("Desc_IronPlate_C"), 0, 30, 0), NativeFactoryRate(TEXT("Screws"), TEXT("Desc_IronScrew_C"), 0, 60, 0) };
		R->Output = { NativeFactorySlot(TEXT("Desc_IronPlateReinforced_C"), 100, 100) };
		R->Input = TArray<FSlotReading>{ NativeFactorySlot(TEXT("Desc_IronPlate_C"), 200, 200), NativeFactorySlot(TEXT("Desc_IronScrew_C"), 500, 500) };

		R = &add(TEXT("Build_AssemblerMk1_C_2147397136"), TEXT("Assembler"), TEXT("Build_AssemblerMk1_C"), TEXT("Alternate: Coated Iron Canister"), false, 0, -40000, -148800, 8200.01171875, 180);
		R->Production = { NativeFactoryRate(TEXT("Empty Canister"), TEXT("Desc_FluidCanister_C"), 5.647059440612793, 60, 9.411765734354654) };
		R->Ingredients = { NativeFactoryRate(TEXT("Iron Plate"), TEXT("Desc_IronPlate_C"), 2.8235297203063965, 30, 9.411765734354654), NativeFactoryRate(TEXT("Copper Sheet"), TEXT("Desc_CopperSheet_C"), 1.4117648601531982, 15, 9.411765734354654) };
		R->Output = { NativeFactorySlot(TEXT("Desc_FluidCanister_C"), 98, 100) };
		R->Input = TArray<FSlotReading>{ NativeFactorySlot(TEXT("Desc_IronPlate_C"), 200, 200), NativeFactorySlot(TEXT("Desc_CopperSheet_C"), 200, 200) };

		R = &add(TEXT("Build_AssemblerMk1_C_2146597364"), TEXT("Assembler"), TEXT("Build_AssemblerMk1_C"), TEXT("Stator"), true, 0, 57600, -67900, 14000, 270);
		R->Production = { NativeFactoryRate(TEXT("Stator"), TEXT("Desc_Stator_C"), 5, 5, 100) };
		R->Ingredients = { NativeFactoryRate(TEXT("Steel Pipe"), TEXT("Desc_SteelPipe_C"), 15, 15, 100), NativeFactoryRate(TEXT("Wire"), TEXT("Desc_Wire_C"), 40, 40, 100) };
		R->Input = TArray<FSlotReading>{ NativeFactorySlot(TEXT("Desc_SteelPipe_C"), 118, 200), NativeFactorySlot(TEXT("Desc_Wire_C"), 49, 500) };

		R = &add(TEXT("Build_AssemblerMk1_C_2147356083"), TEXT("Assembler"), TEXT("Build_AssemblerMk1_C"), TEXT("Stator"), false, 0, 58200, -61900, 14000, 90);
		R->ClockPercent = 160;
		R->Production = { NativeFactoryRate(TEXT("Stator"), TEXT("Desc_Stator_C"), 1.9764708280563354, 8, 24.705885350704193) };
		R->Ingredients = { NativeFactoryRate(TEXT("Steel Pipe"), TEXT("Desc_SteelPipe_C"), 5.929412364959717, 24, 24.705884853998818), NativeFactoryRate(TEXT("Wire"), TEXT("Desc_Wire_C"), 15.811766624450684, 64, 24.705885350704193) };
		R->Output = { NativeFactorySlot(TEXT("Desc_Stator_C"), 100, 100) };
		R->Input = TArray<FSlotReading>{ NativeFactorySlot(TEXT("Desc_SteelPipe_C"), 200, 200), NativeFactorySlot(TEXT("Desc_Wire_C"), 500, 500) };

		// The unconfigured assembler: no recipe, no rates and no slots.
		R = &add(TEXT("Build_AssemblerMk1_C_2145959128"), TEXT("Assembler"), TEXT("Build_AssemblerMk1_C"), nullptr, false, 0, -195700, -105600, 8600, 90);

		R = &add(TEXT("Build_ConstructorMk1_C_2147160359"), TEXT("Constructor"), TEXT("Build_ConstructorMk1_C"), TEXT("Quickwire"), false, -1, -14200, -112800, 11200.01171875, 0);
		R->Production = { NativeFactoryRate(TEXT("Quickwire"), TEXT("Desc_HighSpeedWire_C"), 0, 60, 0) };
		R->Ingredients = { NativeFactoryRate(TEXT("Caterium Ingot"), TEXT("Desc_GoldIngot_C"), 0, 12, 0) };

		R = &add(TEXT("Build_OilRefinery_C_2146613066"), TEXT("Refinery"), TEXT("Build_OilRefinery_C"), TEXT("Fuel"), true, 0, -32800, -190900, 4800.00439453125, 180);
		R->Production = { NativeFactoryRate(TEXT("Fuel"), TEXT("Desc_LiquidFuel_C"), 40, 40, 100), NativeFactoryRate(TEXT("Polymer Resin"), TEXT("Desc_PolymerResin_C"), 30, 30, 100) };
		R->Ingredients = { NativeFactoryRate(TEXT("Crude Oil"), TEXT("Desc_LiquidOil_C"), 60, 60, 100) };
		R->Output = { NativeFactorySlot(TEXT("Desc_LiquidFuel_C"), 3.1999998092651367, 50) };
		R->Input = TArray<FSlotReading>{ NativeFactorySlot(TEXT("Desc_LiquidOil_C"), 50, 50) };

		// frm-getFactory-norecipe.json
		add(TEXT("Build_OilRefinery_C_2147458064"), TEXT("Refinery"), TEXT("Build_OilRefinery_C"), nullptr, false, 1, -184900, -113300, 4800.008437196724, 0);
		add(TEXT("Build_ConstructorMk1_C_2147456649"), TEXT("Constructor"), TEXT("Build_ConstructorMk1_C"), nullptr, false, -1, -183400, -114300, 4800.007071845233, 180);
		R = &add(TEXT("Build_OilRefinery_C_2146846128"), TEXT("Refinery"), TEXT("Build_OilRefinery_C"), TEXT("Plastic"), true, 0, -38900, -189400, 4800.0126953125, 90);
		R->Production = { NativeFactoryRate(TEXT("Plastic"), TEXT("Desc_Plastic_C"), 20, 20, 100), NativeFactoryRate(TEXT("Heavy Oil Residue"), TEXT("Desc_HeavyOilResidue_C"), 10, 10, 100) };
		R->Ingredients = { NativeFactoryRate(TEXT("Crude Oil"), TEXT("Desc_LiquidOil_C"), 30, 30, 100) };
		R->Output = { NativeFactorySlot(TEXT("Desc_Plastic_C"), 1, 200) };
		R->Input = TArray<FSlotReading>{ NativeFactorySlot(TEXT("Desc_LiquidOil_C"), 50, 50) };
		return Out;
	}

	/** The expected text of a fixture's buildings, in the order the native part sorts them (by id). */
	FNativeFactoryItems NativeFactoryExpected(const TArray<const TCHAR*>& Files)
	{
		TArray<TSharedPtr<FJsonValue>> All;
		for (const TCHAR* File : Files)
		{
			All.Append(NativeFactoryBuildings(SatisDash::Test::Fixture(File)));
		}
		All.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
		{
			return A->AsObject()->GetStringField(TEXT("id")).Compare(B->AsObject()->GetStringField(TEXT("id")), ESearchCase::CaseSensitive) < 0;
		});
		return All;
	}

	/**
	 * The part text the bridge's expected records make: each record written by the same condensed writer, joined in the
	 * order given. Only the expected side is re-serialised; the native side's text is compared as it is.
	 */
	FString NativeFactoryExpectedText(const FNativeFactoryItems& Records)
	{
		FString Out = TEXT("{\"buildings\":[");
		for (int32 I = 0; I < Records.Num(); ++I)
		{
			FString One;
			const TSharedRef<FFactoryJsonWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&One);
			FJsonSerializer::Serialize(Records[I]->AsObject().ToSharedRef(), Writer);
			Out += (I > 0 ? TEXT(",") : TEXT("")) + One;
		}
		return Out + TEXT("]}");
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeFactoryGoldenTest, "SatisDash.Core.NativeFactory.Golden", SatisDash::Test::Flags)

bool FNativeFactoryGoldenTest::RunTest(const FString& Parameters)
{
	int32 Dropped = 0;
	const FString Actual = NativeFactoryPart(NativeFactoryFixtureReadings(), Dropped);
	TestEqual(TEXT("nothing dropped"), Dropped, 0);
	// The bridge keeps the game's order and the native part sorts by id, so the expected records are put in id order.
	// The comparison is of text, byte for byte: key order and number formatting count.
	const FNativeFactoryItems Want = NativeFactoryExpected({ TEXT("frm-getFactory-running.expected.json"), TEXT("frm-getFactory-norecipe.expected.json") });
	TestEqual(TEXT("one record per machine"), NativeFactoryBuildings(Actual).Num(), Want.Num());
	const FString Expected = NativeFactoryExpectedText(Want);
	TestTrue(FString::Printf(TEXT("the bridge's bytes; got %s"), *Actual), Actual == Expected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeFactoryMapTest, "SatisDash.Core.NativeFactory.Map", SatisDash::Test::Flags)

bool FNativeFactoryMapTest::RunTest(const FString& Parameters)
{
	// Unconfigured: no recipe, no rates, never backed up, inputs never full.
	FFactoryReading Idle = NativeFactoryMachine(TEXT("A"), nullptr);
	Idle.Production = { NativeFactoryRate(TEXT("x"), TEXT("Desc_X_C"), 1, 1, 100) };
	Idle.Ingredients = { NativeFactoryRate(TEXT("y"), TEXT("Desc_Y_C"), 1, 1, 100) };
	Idle.Output = { NativeFactorySlot(TEXT("Desc_X_C"), 10, 10) };
	Idle.Input = TArray<FSlotReading>{ NativeFactorySlot(TEXT("Desc_Y_C"), 10, 10) };
	const FString IdleOut = NativeFactoryOne(Idle);
	TestTrue(TEXT("unconfigured: recipe null"), IdleOut.Contains(TEXT("\"recipe\":null")));
	TestTrue(TEXT("unconfigured: no production"), IdleOut.Contains(TEXT("\"production\":[]")));
	TestTrue(TEXT("unconfigured: no ingredients"), IdleOut.Contains(TEXT("\"ingredients\":[]")));
	TestTrue(TEXT("unconfigured: not backed up"), IdleOut.Contains(TEXT("\"isBackedUp\":false")));
	TestTrue(TEXT("unconfigured: inputs not full"), IdleOut.Contains(TEXT("\"inputsFull\":false")));

	// A full output slot backs a machine up, unless it is paused.
	FFactoryReading Full = NativeFactoryMachine(TEXT("A"), TEXT("Plate"));
	Full.Output = { NativeFactorySlot(TEXT("Desc_P_C"), 100, 100) };
	TestTrue(TEXT("full output: backed up"), NativeFactoryOne(Full).Contains(TEXT("\"isBackedUp\":true")));
	Full.bPaused = true;
	TestTrue(TEXT("paused is never backed up"), NativeFactoryOne(Full).Contains(TEXT("\"isBackedUp\":false")));
	FFactoryReading Roomy = NativeFactoryMachine(TEXT("A"), TEXT("Plate"));
	Roomy.Output = { NativeFactorySlot(TEXT("Desc_P_C"), 99, 100), NativeFactorySlot(TEXT("Desc_Q_C"), 0, 0) };
	TestTrue(TEXT("a slot with room, and one with no capacity: not backed up"), NativeFactoryOne(Roomy).Contains(TEXT("\"isBackedUp\":false")));

	// inputsFull: every ingredient's slot full, and an input list set.
	FFactoryReading In = NativeFactoryMachine(TEXT("A"), TEXT("Plate"));
	In.Ingredients = { NativeFactoryRate(TEXT("a"), TEXT("Desc_A_C"), 0, 1, 0), NativeFactoryRate(TEXT("b"), TEXT("Desc_B_C"), 0, 1, 0) };
	In.Input = TArray<FSlotReading>{ NativeFactorySlot(TEXT("Desc_A_C"), 5, 5), NativeFactorySlot(TEXT("Desc_B_C"), 5, 5) };
	TestTrue(TEXT("every slot full"), NativeFactoryOne(In).Contains(TEXT("\"inputsFull\":true")));
	In.Input = TArray<FSlotReading>{ NativeFactorySlot(TEXT("Desc_A_C"), 5, 5), NativeFactorySlot(TEXT("Desc_B_C"), 4, 5) };
	TestTrue(TEXT("one slot short"), NativeFactoryOne(In).Contains(TEXT("\"inputsFull\":false")));
	In.Input = TArray<FSlotReading>{ NativeFactorySlot(TEXT("Desc_A_C"), 5, 5) };
	TestTrue(TEXT("one ingredient has no slot"), NativeFactoryOne(In).Contains(TEXT("\"inputsFull\":false")));
	In.Input.Reset();
	TestFalse(TEXT("no input list: no field"), NativeFactoryOne(In).Contains(TEXT("inputsFull")));

	// Circuit, location, clock.
	FFactoryReading Plain = NativeFactoryMachine(TEXT("A"), TEXT("Plate"));
	const FString PlainOut = NativeFactoryOne(Plain);
	TestTrue(TEXT("circuit -1 is written -1"), PlainOut.Contains(TEXT("\"circuitGroupId\":-1")));
	TestFalse(TEXT("no location: omitted"), PlainOut.Contains(TEXT("location")));
	TestFalse(TEXT("clock 0 is not reported"), PlainOut.Contains(TEXT("clockSpeedPercent")));
	FFactoryReading Placed = Plain;
	NativeFactoryPlace(Placed, 12300, -45600, 700, -90);
	Placed.ClockPercent = 159.99999;
	const FString PlacedOut = NativeFactoryOne(Placed);
	TestTrue(TEXT("yaw -90 is 270"), PlacedOut.Contains(TEXT("\"rotationDeg\":270")));
	TestTrue(TEXT("metres"), PlacedOut.Contains(TEXT("\"xM\":123")) && PlacedOut.Contains(TEXT("\"yM\":-456")) && PlacedOut.Contains(TEXT("\"zM\":7")));
	TestTrue(TEXT("159.99999 is written 160"), PlacedOut.Contains(TEXT("\"clockSpeedPercent\":160")));
	Placed.XCm = NativeFactoryInfinity();
	TestFalse(TEXT("a non-finite coordinate leaves the location out"), NativeFactoryOne(Placed).Contains(TEXT("location")));
	Placed.ClockPercent = NativeFactoryInfinity();
	TestFalse(TEXT("a non-finite clock is not reported"), NativeFactoryOne(Placed).Contains(TEXT("clockSpeedPercent")));
	Placed.ClockPercent = 1e307; // finite, but times 100 it overflows
	TestFalse(TEXT("a clock that overflows when scaled is not reported"), NativeFactoryOne(Placed).Contains(TEXT("clockSpeedPercent")));

	// Values the bridge would refuse are written as 0: negative, -0 and infinite rates; and slots the same way.
	FFactoryReading Odd = NativeFactoryMachine(TEXT("A"), TEXT("Plate"));
	Odd.Production = { NativeFactoryRate(TEXT("p"), TEXT("Desc_P_C"), -5, NativeFactoryInfinity(), -0.0) };
	Odd.Ingredients = { NativeFactoryRate(TEXT("i"), TEXT("Desc_I_C"), 1, 1, 1) };
	Odd.Output = { NativeFactorySlot(TEXT("Desc_P_C"), NativeFactoryInfinity(), 100), NativeFactorySlot(TEXT("Desc_Q_C"), 5, -1) };
	Odd.Input = TArray<FSlotReading>{ NativeFactorySlot(TEXT("Desc_I_C"), 5, NativeFactoryInfinity()) };
	const FString OddOut = NativeFactoryOne(Odd);
	TestTrue(FString::Printf(TEXT("negative, -0 and infinite rates are 0; got %s"), *OddOut), OddOut.Contains(TEXT("\"currentPerMinute\":0,\"maxPerMinute\":0,\"percent\":0")));
	TestTrue(TEXT("a slot amount that is infinite or a capacity that is negative or infinite is 0: not backed up, inputs not full"), OddOut.Contains(TEXT("\"isBackedUp\":false,\"inputsFull\":false")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeFactoryRulesTest, "SatisDash.Core.NativeFactory.Rules", SatisDash::Test::Flags)

bool FNativeFactoryRulesTest::RunTest(const FString& Parameters)
{
	// The cap: 20,001 readings keep 20,000, and the order they came in does not matter.
	TArray<FFactoryReading> Many;
	for (int32 I = 0; I < MaxBuildings + 1; ++I)
	{
		Many.Add(NativeFactoryMachine(*FString::Printf(TEXT("Id_%06d"), I), TEXT("Plate")));
	}
	TArray<FFactoryReading> Reversed;
	for (int32 I = Many.Num() - 1; I >= 0; --I)
	{
		Reversed.Add(Many[I]);
	}
	int32 Dropped = 0;
	const FString Forward = NativeFactoryPart(Many, Dropped);
	TestEqual(TEXT("one over the cap is dropped"), Dropped, 1);
	TestEqual(TEXT("20,000 are kept"), NativeFactoryBuildings(Forward).Num(), MaxBuildings);
	int32 DroppedBack = 0;
	TestTrue(TEXT("the same readings in another order give the same text"), NativeFactoryPart(Reversed, DroppedBack) == Forward && DroppedBack == 1);

	// A duplicate id keeps the first in sorted order, whichever order they arrive in.
	FFactoryReading First = NativeFactoryMachine(TEXT("Dup"), TEXT("Plate"));
	First.Name = TEXT("first");
	FFactoryReading Second = NativeFactoryMachine(TEXT("Dup"), TEXT("Plate"));
	Second.Name = TEXT("second");
	int32 Ignored = 0;
	for (const TArray<FFactoryReading>& Order : { TArray<FFactoryReading>{ First, Second }, TArray<FFactoryReading>{ Second, First } })
	{
		const TArray<TSharedPtr<FJsonValue>> Kept = NativeFactoryBuildings(NativeFactoryPart(Order, Ignored));
		TestEqual(TEXT("one of the duplicates is kept"), Kept.Num(), 1);
		TestTrue(TEXT("the first in sorted order"), Kept.Num() == 1 && Kept[0]->AsObject()->GetStringField(TEXT("name")) == TEXT("first"));
	}

	// Strings are cut as ClampString cuts them.
	FFactoryReading Long = NativeFactoryMachine(TEXT("A"), TEXT("Plate"));
	Long.Name = FString::ChrN(250, TEXT('n'));
	const FString LongOut = NativeFactoryOne(Long);
	TestTrue(TEXT("a 250-character name is cut"), LongOut.Contains(FString::Printf(TEXT("\"name\":\"%s\""), *ClampString(Long.Name))) && !LongOut.Contains(Long.Name));

	// Rates are cut at MaxRatesPerBuilding.
	FFactoryReading Rated = NativeFactoryMachine(TEXT("A"), TEXT("Plate"));
	for (int32 I = 0; I < MaxRatesPerBuilding + 4; ++I)
	{
		Rated.Production.Add(NativeFactoryRate(TEXT("p"), TEXT("Desc_P_C"), 1, 1, 100));
		Rated.Ingredients.Add(NativeFactoryRate(TEXT("i"), TEXT("Desc_I_C"), 1, 1, 100));
	}
	const TArray<TSharedPtr<FJsonValue>> RatedOut = NativeFactoryBuildings(NativeFactoryOne(Rated));
	if (TestEqual(TEXT("one building"), RatedOut.Num(), 1))
	{
		TestEqual(TEXT("production cut at 16"), RatedOut[0]->AsObject()->GetArrayField(TEXT("production")).Num(), MaxRatesPerBuilding);
		TestEqual(TEXT("ingredients cut at 16"), RatedOut[0]->AsObject()->GetArrayField(TEXT("ingredients")).Num(), MaxRatesPerBuilding);
	}
	return true;
}
