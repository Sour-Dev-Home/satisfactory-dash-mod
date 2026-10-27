#include "SatisDash/FrmWorld.h"
#include "SatisDash/Native/PowerLines/NativePowerLines.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	const TCHAR* NativePowerLinesObserved = TEXT("2026-10-28T00:00:00.000Z");

	FPowerLineReading NativePowerLinesReading(const FString& Id, double X0Cm, double Y0Cm, double X1Cm, double Y1Cm)
	{
		FPowerLineReading Reading;
		Reading.Id = Id;
		Reading.X0Cm = X0Cm;
		Reading.Y0Cm = Y0Cm;
		Reading.X1Cm = X1Cm;
		Reading.Y1Cm = Y1Cm;
		return Reading;
	}

	FString NativePowerLinesData(const FNativeWorldBody& Body)
	{
		return Body.Json.Mid(Body.DataFrom, Body.DataEnd - Body.DataFrom);
	}

	FString NativePowerLinesId(int32 Index)
	{
		return FString::Printf(TEXT("Build_PowerLine_C_%010d"), Index);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativePowerLinesMapTest, "SatisDash.Core.NativePowerLines.Map", SatisDash::Test::Flags)

bool FNativePowerLinesMapTest::RunTest(const FString& Parameters)
{
	const FNativeWorldBody Body = BuildNativePowerLines(NativePowerLinesObserved,
		{ NativePowerLinesReading(TEXT("Build_PowerLine_C_2100000001"), -1246855.8, 303000.2, -1250000, 303100) });
	SatisDash::Test::SameText(*this, TEXT("id, then both ends in the given order"), NativePowerLinesData(Body),
		TEXT("[{\"id\":\"Build_PowerLine_C_2100000001\",\"points\":[[-12469,3030],[-12500,3031]]}]"));
	TestEqual(TEXT("one item"), Body.Items, 1);
	TestEqual(TEXT("none dropped"), Body.Dropped, 0);
	SatisDash::Test::SameText(*this, TEXT("the whole body"), Body.Json,
		FString::Printf(TEXT("{\"observedAt\":\"%s\",\"data\":%s}"), NativePowerLinesObserved, *NativePowerLinesData(Body)));
	TestEqual(TEXT("the byte count is the body's UTF-8 length"), Body.Bytes, int64(FTCHARToUTF8(*Body.Json).Length()));

	// The ends are not sorted: the second end may be the smaller.
	const FNativeWorldBody Order = BuildNativePowerLines(NativePowerLinesObserved, { NativePowerLinesReading(TEXT("A"), 500000, 0, -500000, 0) });
	TestTrue(TEXT("the end order is kept"), NativePowerLinesData(Order).EndsWith(TEXT("\"points\":[[5000,0],[-5000,0]]}]")));

	struct FRow { double Cm; int32 Metres; };
	for (const FRow& Row : { FRow{ 150, 2 }, FRow{ -150, -1 }, FRow{ 50, 1 }, FRow{ -50, 0 } })
	{
		const FNativeWorldBody Rounded = BuildNativePowerLines(NativePowerLinesObserved, { NativePowerLinesReading(TEXT("A"), Row.Cm, Row.Cm, 0, 0) });
		TestTrue(*FString::Printf(TEXT("%g cm"), Row.Cm), NativePowerLinesData(Rounded).EndsWith(*FString::Printf(TEXT("\"points\":[[%d,%d],[0,0]]}]"), Row.Metres, Row.Metres)));
	}
	TestFalse(TEXT("-50 cm prints 0, never -0"), BuildNativePowerLines(NativePowerLinesObserved, { NativePowerLinesReading(TEXT("A"), -50, -50, -50, -50) }).Json.Contains(TEXT("-0")));

	// Two ends in the same metre: the line is kept.
	const FNativeWorldBody Same = BuildNativePowerLines(NativePowerLinesObserved, { NativePowerLinesReading(TEXT("A"), 10, 10, 20, 20) });
	TestEqual(TEXT("equal-metre ends are kept"), Same.Items, 1);
	TestEqual(TEXT("and not dropped"), Same.Dropped, 0);
	TestTrue(TEXT("both print"), NativePowerLinesData(Same).EndsWith(TEXT("\"points\":[[0,0],[0,0]]}]")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativePowerLinesRulesTest, "SatisDash.Core.NativePowerLines.Rules", SatisDash::Test::Flags)

bool FNativePowerLinesRulesTest::RunTest(const FString& Parameters)
{
	const FNativeWorldBody Empty = BuildNativePowerLines(NativePowerLinesObserved, {});
	SatisDash::Test::SameText(*this, TEXT("no wires: a valid empty layer"), Empty.Json, FString::Printf(TEXT("{\"observedAt\":\"%s\",\"data\":[]}"), NativePowerLinesObserved));
	TestEqual(TEXT("no items"), Empty.Items, 0);

	TArray<FPowerLineReading> Many;
	for (int32 I = 0; I < PowerLinesMaxItems + 1; ++I)
	{
		Many.Add(NativePowerLinesReading(NativePowerLinesId(I), 0, 0, 100, 100));
	}
	const FNativeWorldBody Capped = BuildNativePowerLines(NativePowerLinesObserved, Many);
	TestEqual(TEXT("20,001 valid wires keep 20,000"), Capped.Items, PowerLinesMaxItems);
	TestEqual(TEXT("and count one over"), Capped.Over, 1);
	TestFalse(TEXT("the largest id is cut"), Capped.Json.Contains(NativePowerLinesId(PowerLinesMaxItems)));

	TArray<FPowerLineReading> Twenty;
	for (int32 I = 0; I < 20; ++I)
	{
		Twenty.Add(NativePowerLinesReading(NativePowerLinesId(I * 7 % 20), double(I) * 123.0, double(I) * -77.0, double(I) * 5000.0, double(I) * 31.0));
	}
	const FString Ordered = BuildNativePowerLines(NativePowerLinesObserved, Twenty).Json;
	uint32 Seed = 7;
	for (int32 Round = 0; Round < 5; ++Round)
	{
		TArray<FPowerLineReading> Shuffled = Twenty;
		for (int32 I = Shuffled.Num() - 1; I > 0; --I)
		{
			Seed = Seed * 1664525u + 1013904223u; // a fixed linear congruential step: the same shuffles every run
			Shuffled.Swap(I, int32((Seed >> 8) % uint32(I + 1)));
		}
		SatisDash::Test::SameText(*this, TEXT("any order gives the same bytes"), BuildNativePowerLines(NativePowerLinesObserved, Shuffled).Json, Ordered);
	}

	const FPowerLineReading First = NativePowerLinesReading(TEXT("DUP"), 0, 0, 100, 0);
	const FPowerLineReading Second = NativePowerLinesReading(TEXT("DUP"), 500, 0, 900, 0);
	const FNativeWorldBody Forward = BuildNativePowerLines(NativePowerLinesObserved, { First, Second });
	const FNativeWorldBody Backward = BuildNativePowerLines(NativePowerLinesObserved, { Second, First });
	SatisDash::Test::SameText(*this, TEXT("a repeated id gives the same bytes in both orders"), Forward.Json, Backward.Json);
	TestEqual(TEXT("and keeps one"), Forward.Items, 1);
	TestTrue(TEXT("the wire that sorts first survives"), Forward.Json.Contains(TEXT("[[0,0],[1,0]]")));
	TestEqual(TEXT("the repeat counts as dropped"), Forward.Dropped, 1);

	const FNativeWorldBody NoId = BuildNativePowerLines(NativePowerLinesObserved, { NativePowerLinesReading(TEXT(""), 0, 0, 100, 0) });
	TestEqual(TEXT("an empty id drops"), NoId.Dropped, 1);
	const FNativeWorldBody Far = BuildNativePowerLines(NativePowerLinesObserved, { NativePowerLinesReading(TEXT("A"), 0, 0, 0, 1.0e12) });
	TestEqual(TEXT("an end outside +/-1,000,000 m drops"), Far.Dropped, 1);
	const FNativeWorldBody FarFirst = BuildNativePowerLines(NativePowerLinesObserved, { NativePowerLinesReading(TEXT("A"), -1.0e12, 0, 0, 0) });
	TestEqual(TEXT("so does the first end"), FarFirst.Dropped, 1);
	TestEqual(TEXT("and none leaves an item"), NoId.Items + Far.Items + FarFirst.Items, 0);

	const FNativeWorldBody Long = BuildNativePowerLines(NativePowerLinesObserved, { NativePowerLinesReading(FString::ChrN(250, TEXT('i')), 0, 0, 100, 0) });
	TestEqual(TEXT("a 250-character id is kept"), Long.Items, 1);
	TestTrue(TEXT("cut to 200"), Long.Json.Contains(TEXT("\"id\":\"") + FString::ChrN(200, TEXT('i')) + TEXT("\",")));
	TestFalse(TEXT("not 201"), Long.Json.Contains(FString::ChrN(201, TEXT('i'))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativePowerLinesGoldenTest, "SatisDash.Core.NativePowerLines.Golden", SatisDash::Test::Flags)

bool FNativePowerLinesGoldenTest::RunTest(const FString& Parameters)
{
	// In the shape of the recorded getCables output (FRM HTTP only), with synthetic ids, given out of order.
	const FNativeWorldBody Body = BuildNativePowerLines(NativePowerLinesObserved, {
		NativePowerLinesReading(TEXT("Build_PowerLine_C_2100000030"), 120050, -80050, 120450, -80450),
		NativePowerLinesReading(TEXT("Build_PowerLine_C_2100000010"), -1246855.8, 303000.2, -1250000, 303100),
		NativePowerLinesReading(TEXT("Build_PowerLine_C_2100000020"), 0, 0, 0, 0) });
	SatisDash::Test::SameText(*this, TEXT("three wires, sorted by id"), Body.Json,
		TEXT("{\"observedAt\":\"2026-10-28T00:00:00.000Z\",\"data\":[")
		TEXT("{\"id\":\"Build_PowerLine_C_2100000010\",\"points\":[[-12469,3030],[-12500,3031]]},")
		TEXT("{\"id\":\"Build_PowerLine_C_2100000020\",\"points\":[[0,0],[0,0]]},")
		TEXT("{\"id\":\"Build_PowerLine_C_2100000030\",\"points\":[[1201,-800],[1205,-804]]}]}"));
	TestEqual(TEXT("three items"), Body.Items, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativePowerLinesBytesTest, "SatisDash.Core.NativePowerLines.Bytes", SatisDash::Test::Flags)

bool FNativePowerLinesBytesTest::RunTest(const FString& Parameters)
{
	// The layer cap is reached before the byte cap: 20,000 wires with 30-character ids stay under WorldLayerMaxBytes.
	TArray<FPowerLineReading> Many;
	Many.Reserve(PowerLinesMaxItems);
	for (int32 I = 0; I < PowerLinesMaxItems; ++I)
	{
		Many.Add(NativePowerLinesReading(FString::Printf(TEXT("Build_PowerLine_C_%012d"), I),-1246855.8, 303000.2, -1250000, 303100));
	}
	TestEqual(TEXT("the ids are 30 characters"), Many[0].Id.Len(), 30);
	const FNativeWorldBody Body = BuildNativePowerLines(NativePowerLinesObserved, Many);
	TestEqual(TEXT("20,000 kept"), Body.Items, PowerLinesMaxItems);
	TestTrue(TEXT("under the layer byte cap"), Body.Bytes < WorldLayerMaxBytes);
	return true;
}
