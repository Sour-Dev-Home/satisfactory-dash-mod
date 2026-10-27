#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SatisDash/Native/DroneRoutes/NativeDroneRoutes.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	const TCHAR* NativeDroneRoutesObserved = TEXT("2026-10-28T00:00:00.000Z");

	FDronePortReading NativeDroneRoutesReading(const FString& Id, const FString& Name, double XCm = 0, double YCm = 0)
	{
		FDronePortReading Reading;
		Reading.Id = Id;
		Reading.Name = Name;
		Reading.XCm = XCm;
		Reading.YCm = YCm;
		return Reading;
	}

	/** Pairs two readings with each other, by name and by id. */
	void NativeDroneRoutesPair(FDronePortReading& A, FDronePortReading& B)
	{
		A.bPaired = true;
		A.PairedName = B.Name;
		A.PairedId = B.Id;
		B.bPaired = true;
		B.PairedName = A.Name;
		B.PairedId = A.Id;
	}

	FString NativeDroneRoutesData(const FNativeWorldBody& Body)
	{
		return Body.Json.Mid(Body.DataFrom, Body.DataEnd - Body.DataFrom);
	}

	FString NativeDroneRoutesId(int32 Index)
	{
		return FString::Printf(TEXT("Build_DroneStation_C_%07d"), Index);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeDroneRoutesMapTest, "SatisDash.Core.NativeDroneRoutes.Map", SatisDash::Test::Flags)

bool FNativeDroneRoutesMapTest::RunTest(const FString& Parameters)
{
	FDronePortReading Home = NativeDroneRoutesReading(TEXT("Build_DroneStation_C_1"), TEXT("Home port"), -1246855.859375, 303000);
	Home.Input = { TEXT("Desc_Battery_C"), TEXT("Desc_Battery_C"), TEXT("Desc_Battery_C") };
	Home.Fuel = { TEXT("Desc_Battery_C") };
	FDronePortReading Partner = NativeDroneRoutesReading(TEXT("Build_DroneStation_C_2"), TEXT("Far port"), 20000, -40000);
	Partner.Output = { TEXT("Desc_Battery_C") };
	NativeDroneRoutesPair(Home, Partner);
	const FDronePortReading Lone = NativeDroneRoutesReading(TEXT("Build_DroneStation_C_3"), TEXT("Lone port"));

	const FNativeWorldBody Body = BuildNativeDroneRoutes(NativeDroneRoutesObserved, { Home, Partner, Lone });
	SatisDash::Test::SameText(*this, TEXT("key order id name paired pairedId input output fuel x y; deduplicated classes"), NativeDroneRoutesData(Body),
		TEXT("[")
		TEXT("{\"id\":\"Build_DroneStation_C_1\",\"name\":\"Home port\",\"paired\":\"Far port\",\"pairedId\":\"Build_DroneStation_C_2\",\"input\":[\"Desc_Battery_C\"],\"output\":[],\"fuel\":[\"Desc_Battery_C\"],\"x\":-12469,\"y\":3030},")
		TEXT("{\"id\":\"Build_DroneStation_C_2\",\"name\":\"Far port\",\"paired\":\"Home port\",\"pairedId\":\"Build_DroneStation_C_1\",\"input\":[],\"output\":[\"Desc_Battery_C\"],\"fuel\":[],\"x\":200,\"y\":-400},")
		TEXT("{\"id\":\"Build_DroneStation_C_3\",\"name\":\"Lone port\",\"paired\":null,\"pairedId\":null,\"input\":[],\"output\":[],\"fuel\":[],\"x\":0,\"y\":0}]"));
	TestEqual(TEXT("three items"), Body.Items, 3);
	TestEqual(TEXT("none dropped"), Body.Dropped, 0);
	SatisDash::Test::SameText(*this, TEXT("the whole body"), Body.Json,
		FString::Printf(TEXT("{\"observedAt\":\"%s\",\"data\":%s}"), NativeDroneRoutesObserved, *NativeDroneRoutesData(Body)));
	TestEqual(TEXT("the byte count is the body's UTF-8 length"), Body.Bytes, int64(FTCHARToUTF8(*Body.Json).Length()));

	struct FRow { double Cm; int32 Metres; };
	for (const FRow& Row : { FRow{ 150, 2 }, FRow{ -150, -1 }, FRow{ 50, 1 }, FRow{ -50, 0 } })
	{
		const FNativeWorldBody Rounded = BuildNativeDroneRoutes(NativeDroneRoutesObserved, { NativeDroneRoutesReading(TEXT("A"), TEXT("n"), Row.Cm, 0) });
		TestTrue(*FString::Printf(TEXT("%g cm"), Row.Cm), NativeDroneRoutesData(Rounded).EndsWith(*FString::Printf(TEXT("\"x\":%d,\"y\":0}]"), Row.Metres)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeDroneRoutesPairedIdTest, "SatisDash.Core.NativeDroneRoutes.PairedId", SatisDash::Test::Flags)

bool FNativeDroneRoutesPairedIdTest::RunTest(const FString& Parameters)
{
	// The partner is dropped for a 201-character name: pairedId is null, paired keeps the name.
	FDronePortReading Kept = NativeDroneRoutesReading(TEXT("P1"), TEXT("Kept"));
	FDronePortReading Gone = NativeDroneRoutesReading(TEXT("P2"), FString::ChrN(201, TEXT('n')));
	Kept.bPaired = true;
	Kept.PairedName = Gone.Name;
	Kept.PairedId = Gone.Id;
	const FNativeWorldBody WithGone = BuildNativeDroneRoutes(NativeDroneRoutesObserved, { Kept, Gone });
	TestEqual(TEXT("the long-named partner is dropped"), WithGone.Dropped, 1);
	TestEqual(TEXT("one item stays"), WithGone.Items, 1);
	TestTrue(TEXT("pairedId is null for a partner not in the layer"), NativeDroneRoutesData(WithGone).Contains(TEXT("\"pairedId\":null")));
	TestFalse(TEXT("and never names the dropped port"), NativeDroneRoutesData(WithGone).Contains(TEXT("\"P2\"")));

	// A partner id that no reading carries at all.
	Kept.PairedName = TEXT("Elsewhere");
	Kept.PairedId = TEXT("Missing");
	const FNativeWorldBody Missing = BuildNativeDroneRoutes(NativeDroneRoutesObserved, { Kept });
	TestTrue(TEXT("pairedId is null for a partner that does not exist"), NativeDroneRoutesData(Missing).Contains(TEXT("\"paired\":\"Elsewhere\",\"pairedId\":null")));

	// Paired, but the id is empty (the partner's buildable is gone).
	Kept.PairedId = FString();
	TestTrue(TEXT("an empty PairedId is null"), NativeDroneRoutesData(BuildNativeDroneRoutes(NativeDroneRoutesObserved, { Kept })).Contains(TEXT("\"pairedId\":null")));

	// Paired to itself: the id is the port's own (allowed; only the parity judge reports it).
	FDronePortReading Self = NativeDroneRoutesReading(TEXT("S1"), TEXT("Self"));
	NativeDroneRoutesPair(Self, Self);
	TestTrue(TEXT("a self-paired port reports its own id"), NativeDroneRoutesData(BuildNativeDroneRoutes(NativeDroneRoutesObserved, { Self })).Contains(TEXT("\"paired\":\"Self\",\"pairedId\":\"S1\"")));

	// bPaired with an empty PairedName: paired is null.
	FDronePortReading NoName = NativeDroneRoutesReading(TEXT("N1"), TEXT("A"));
	FDronePortReading NoNamePartner = NativeDroneRoutesReading(TEXT("N2"), TEXT(""));
	NativeDroneRoutesPair(NoName, NoNamePartner);
	const FString NoNameData = NativeDroneRoutesData(BuildNativeDroneRoutes(NativeDroneRoutesObserved, { NoName, NoNamePartner }));
	TestTrue(TEXT("an empty partner name gives paired:null"), NoNameData.Contains(TEXT("\"id\":\"N1\",\"name\":\"A\",\"paired\":null,\"pairedId\":\"N2\"")));
	TestTrue(TEXT("and the id is still the partner's"), NoNameData.Contains(TEXT("\"id\":\"N2\",\"name\":\"\",\"paired\":\"A\",\"pairedId\":\"N1\"")));

	// The cap: a partner cut by the 2,000-port cap is not in the layer either.
	TArray<FDronePortReading> Many;
	for (int32 I = 0; I < DroneRoutesMaxItems; ++I)
	{
		Many.Add(NativeDroneRoutesReading(NativeDroneRoutesId(I), TEXT("n")));
	}
	Many[0].bPaired = true;
	Many[0].PairedName = TEXT("n");
	Many[0].PairedId = NativeDroneRoutesId(DroneRoutesMaxItems);
	Many.Add(NativeDroneRoutesReading(NativeDroneRoutesId(DroneRoutesMaxItems), TEXT("n")));
	const FNativeWorldBody Capped = BuildNativeDroneRoutes(NativeDroneRoutesObserved, Many);
	TestEqual(TEXT("the 2,001st is over"), Capped.Over, 1);
	TestTrue(TEXT("a partner cut by the cap gives pairedId null"), Capped.Json.Contains(TEXT("\"pairedId\":null")));
	TestFalse(TEXT("and is not named"), Capped.Json.Contains(*(TEXT("\"pairedId\":\"") + NativeDroneRoutesId(DroneRoutesMaxItems))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeDroneRoutesRulesTest, "SatisDash.Core.NativeDroneRoutes.Rules", SatisDash::Test::Flags)

bool FNativeDroneRoutesRulesTest::RunTest(const FString& Parameters)
{
	const FNativeWorldBody Empty = BuildNativeDroneRoutes(NativeDroneRoutesObserved, {});
	SatisDash::Test::SameText(*this, TEXT("no ports: a valid empty layer"), Empty.Json, FString::Printf(TEXT("{\"observedAt\":\"%s\",\"data\":[]}"), NativeDroneRoutesObserved));
	TestEqual(TEXT("no items"), Empty.Items, 0);

	TArray<FDronePortReading> Many;
	for (int32 I = 0; I < DroneRoutesMaxItems + 1; ++I)
	{
		Many.Add(NativeDroneRoutesReading(NativeDroneRoutesId(I), TEXT("n")));
	}
	const FNativeWorldBody Capped = BuildNativeDroneRoutes(NativeDroneRoutesObserved, Many);
	TestEqual(TEXT("2,001 valid ports keep 2,000"), Capped.Items, DroneRoutesMaxItems);
	TestEqual(TEXT("and count one over"), Capped.Over, 1);
	TestFalse(TEXT("the largest id is cut"), Capped.Json.Contains(NativeDroneRoutesId(DroneRoutesMaxItems)));

	TArray<FDronePortReading> Twenty;
	for (int32 I = 0; I < 20; ++I)
	{
		FDronePortReading Reading = NativeDroneRoutesReading(NativeDroneRoutesId(I * 7 % 20), FString::Printf(TEXT("m%d"), I), double(I) * 123.0, double(I) * -77.0);
		Reading.Input = { FString::Printf(TEXT("Desc_%d_C"), (I * 3) % 5), TEXT("Desc_A_C") };
		if (I % 3 == 0)
		{
			Reading.bPaired = true;
			Reading.PairedName = TEXT("x");
			Reading.PairedId = NativeDroneRoutesId((I + 1) % 20);
		}
		Twenty.Add(Reading);
	}
	const FString Ordered = BuildNativeDroneRoutes(NativeDroneRoutesObserved, Twenty).Json;
	uint32 Seed = 7;
	for (int32 Round = 0; Round < 5; ++Round)
	{
		TArray<FDronePortReading> Shuffled = Twenty;
		for (int32 I = Shuffled.Num() - 1; I > 0; --I)
		{
			Seed = Seed * 1664525u + 1013904223u; // a fixed linear congruential step: the same shuffles every run
			Shuffled.Swap(I, int32((Seed >> 8) % uint32(I + 1)));
		}
		SatisDash::Test::SameText(*this, TEXT("any order gives the same bytes"), BuildNativeDroneRoutes(NativeDroneRoutesObserved, Shuffled).Json, Ordered);
	}

	const FDronePortReading First = NativeDroneRoutesReading(TEXT("DUP"), TEXT("one"), 0, 0);
	const FDronePortReading Second = NativeDroneRoutesReading(TEXT("DUP"), TEXT("two"), 500, 0);
	const FNativeWorldBody Forward = BuildNativeDroneRoutes(NativeDroneRoutesObserved, { First, Second });
	const FNativeWorldBody Backward = BuildNativeDroneRoutes(NativeDroneRoutesObserved, { Second, First });
	SatisDash::Test::SameText(*this, TEXT("a repeated id gives the same bytes in both orders"), Forward.Json, Backward.Json);
	TestEqual(TEXT("and keeps one"), Forward.Items, 1);
	TestTrue(TEXT("the reading that sorts first survives"), Forward.Json.Contains(TEXT("\"name\":\"one\"")));
	TestFalse(TEXT("and the other is gone"), Forward.Json.Contains(TEXT("\"name\":\"two\"")));
	TestEqual(TEXT("the repeat counts as dropped"), Forward.Dropped, 1);

	FDronePortReading Classes = NativeDroneRoutesReading(TEXT("C1"), TEXT("c"));
	for (int32 I = 64; I >= 0; --I)
	{
		Classes.Input.Add(FString::Printf(TEXT("Desc_%03d_C"), I));
	}
	Classes.Input.Add(FString());
	Classes.Input.Add(FString::ChrN(201, TEXT('z')));
	const FString ClassData = NativeDroneRoutesData(BuildNativeDroneRoutes(NativeDroneRoutesObserved, { Classes }));
	TestTrue(TEXT("65 distinct classes are cut to 64, smallest first"), ClassData.Contains(TEXT("\"input\":[\"Desc_000_C\",\"Desc_001_C\"")) && ClassData.Contains(TEXT("\"Desc_063_C\"]")));
	TestFalse(TEXT("the 65th is gone"), ClassData.Contains(TEXT("Desc_064_C")));
	TestFalse(TEXT("an empty and a 201-character class are skipped"), ClassData.Contains(TEXT("zzzz")) || ClassData.Contains(TEXT("\"\"")));

	FDronePortReading Edge = NativeDroneRoutesReading(FString::ChrN(200, TEXT('i')), FString::ChrN(200, TEXT('n')));
	Edge.Input = { FString::ChrN(200, TEXT('c')) };
	const FNativeWorldBody EdgeBody = BuildNativeDroneRoutes(NativeDroneRoutesObserved, { Edge });
	TestEqual(TEXT("200 characters are kept"), EdgeBody.Items, 1);
	TestTrue(TEXT("a 200-character class is kept"), EdgeBody.Json.Contains(FString::ChrN(200, TEXT('c'))));

	const FNativeWorldBody LongId = BuildNativeDroneRoutes(NativeDroneRoutesObserved, { NativeDroneRoutesReading(FString::ChrN(201, TEXT('i')), TEXT("n")) });
	TestEqual(TEXT("an id of 201 characters drops"), LongId.Dropped, 1);
	const FNativeWorldBody LongName = BuildNativeDroneRoutes(NativeDroneRoutesObserved, { NativeDroneRoutesReading(TEXT("A"), FString::ChrN(201, TEXT('n'))) });
	TestEqual(TEXT("a name of 201 characters drops"), LongName.Dropped, 1);
	const FNativeWorldBody Far = BuildNativeDroneRoutes(NativeDroneRoutesObserved, { NativeDroneRoutesReading(TEXT("A"), TEXT("n"), 1.0e12, 0) });
	TestEqual(TEXT("a position outside +/-1,000,000 m drops"), Far.Dropped, 1);
	TestEqual(TEXT("and leaves no item"), Far.Items + LongId.Items + LongName.Items, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeDroneRoutesGoldenTest, "SatisDash.Core.NativeDroneRoutes.Golden", SatisDash::Test::Flags)

bool FNativeDroneRoutesGoldenTest::RunTest(const FString& Parameters)
{
	// In the shape of the recorded getDroneStation output (FRM HTTP only), with synthetic names: a pair and an unpaired port.
	FDronePortReading Beta = NativeDroneRoutesReading(TEXT("Build_DroneStation_C_200"), TEXT("Beta"), 120000, -80000);
	Beta.Input = { TEXT("Desc_Cable_C"), TEXT("Desc_Battery_C"), TEXT("Desc_Cable_C") };
	Beta.Fuel = { TEXT("Desc_Battery_C") };
	FDronePortReading Alpha = NativeDroneRoutesReading(TEXT("Build_DroneStation_C_100"), TEXT("Alpha"), -50000, 25050);
	Alpha.Output = { TEXT("Desc_Cable_C"), TEXT("Desc_Battery_C") };
	NativeDroneRoutesPair(Alpha, Beta);
	const FDronePortReading Gamma = NativeDroneRoutesReading(TEXT("Build_DroneStation_C_300"), TEXT("Gamma"));

	const FNativeWorldBody Golden = BuildNativeDroneRoutes(NativeDroneRoutesObserved, { Beta, Gamma, Alpha });
	const FString Expected = TEXT("[")
		TEXT("{\"id\":\"Build_DroneStation_C_100\",\"name\":\"Alpha\",\"paired\":\"Beta\",\"pairedId\":\"Build_DroneStation_C_200\",\"input\":[],\"output\":[\"Desc_Battery_C\",\"Desc_Cable_C\"],\"fuel\":[],\"x\":-500,\"y\":251},")
		TEXT("{\"id\":\"Build_DroneStation_C_200\",\"name\":\"Beta\",\"paired\":\"Alpha\",\"pairedId\":\"Build_DroneStation_C_100\",\"input\":[\"Desc_Battery_C\",\"Desc_Cable_C\"],\"output\":[],\"fuel\":[\"Desc_Battery_C\"],\"x\":1200,\"y\":-800},")
		TEXT("{\"id\":\"Build_DroneStation_C_300\",\"name\":\"Gamma\",\"paired\":null,\"pairedId\":null,\"input\":[],\"output\":[],\"fuel\":[],\"x\":0,\"y\":0}]");
	SatisDash::Test::SameText(*this, TEXT("the golden, sorted by id"), NativeDroneRoutesData(Golden), Expected);

	TSharedPtr<FJsonValue> Parsed;
	if (!TestTrue(TEXT("the whole body parses as JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Golden.Json), Parsed) && Parsed.IsValid()))
	{
		return false;
	}

	// The shape of the dash fixture droneRoutesSampleWithPairedId: observedAt, and data of nine-key ports, pairedId a string or null.
	const TSharedPtr<FJsonObject> Root = Parsed->AsObject();
	const TArray<TSharedPtr<FJsonValue>>* Data = nullptr;
	if (!TestTrue(TEXT("an object with observedAt and a data array"), Root.IsValid() && Root->HasTypedField<EJson::String>(TEXT("observedAt")) && Root->TryGetArrayField(TEXT("data"), Data)))
	{
		return false;
	}
	TestEqual(TEXT("three ports"), Data->Num(), 3);
	for (const TSharedPtr<FJsonValue>& Value : *Data)
	{
		const TSharedPtr<FJsonObject> Port = Value->AsObject();
		if (!TestTrue(TEXT("a port is an object"), Port.IsValid()))
		{
			continue;
		}
		TestEqual(TEXT("nine keys"), Port->Values.Num(), 9);
		TestTrue(TEXT("id and name are strings"), Port->HasTypedField<EJson::String>(TEXT("id")) && Port->HasTypedField<EJson::String>(TEXT("name")));
		TestTrue(TEXT("paired is a string or null"), Port->HasTypedField<EJson::String>(TEXT("paired")) || Port->HasTypedField<EJson::Null>(TEXT("paired")));
		TestTrue(TEXT("pairedId is a string or null"), Port->HasTypedField<EJson::String>(TEXT("pairedId")) || Port->HasTypedField<EJson::Null>(TEXT("pairedId")));
		TestTrue(TEXT("input, output and fuel are arrays"), Port->HasTypedField<EJson::Array>(TEXT("input")) && Port->HasTypedField<EJson::Array>(TEXT("output")) && Port->HasTypedField<EJson::Array>(TEXT("fuel")));
		TestTrue(TEXT("x and y are numbers"), Port->HasTypedField<EJson::Number>(TEXT("x")) && Port->HasTypedField<EJson::Number>(TEXT("y")));
	}
	const TSharedPtr<FJsonObject> First = (*Data)[0]->AsObject();
	const TSharedPtr<FJsonObject> Second = (*Data)[1]->AsObject();
	TestEqual(TEXT("each side of the pair carries the other's id"), First->GetStringField(TEXT("pairedId")), Second->GetStringField(TEXT("id")));
	TestEqual(TEXT("both ways"), Second->GetStringField(TEXT("pairedId")), First->GetStringField(TEXT("id")));
	TestTrue(TEXT("the unpaired port has a null pairedId"), (*Data)[2]->AsObject()->HasTypedField<EJson::Null>(TEXT("pairedId")));
	return true;
}
