#include "Dom/JsonObject.h"
#include "SatisDash/JsonUtil.h"
#include "SatisDash/Native/Rails/NativeRails.h"
#include "TestUtil.h"

#include <limits>

using namespace SatisDash;

namespace
{
	const TCHAR* NativeRailsObserved = TEXT("2026-10-27T00:00:00.000Z");

	FRailReading NativeRailsReading(const FString& Id, std::initializer_list<FRailPointCm> Points)
	{
		FRailReading Reading;
		Reading.Id = Id;
		Reading.Points = TArray<FRailPointCm>(Points);
		return Reading;
	}

	FString NativeRailsData(const FNativeWorldBody& Body)
	{
		return Body.Json.Mid(Body.DataFrom, Body.DataEnd - Body.DataFrom);
	}

	/** Count distinct metres on x (x = i * 100 cm). */
	FRailReading NativeRailsLine(const FString& Id, int32 Count)
	{
		FRailReading Reading;
		Reading.Id = Id;
		for (int32 I = 0; I < Count; ++I)
		{
			Reading.Points.Add({ double(I) * 100.0, 0.0 });
		}
		return Reading;
	}

	FString NativeRailsBody(const FString& Data)
	{
		return FString::Printf(TEXT("{\"observedAt\":\"%s\",\"data\":%s}"), NativeRailsObserved, *Data);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeRailsPlanTest, "SatisDash.Core.NativeRails.Plan", SatisDash::Test::Flags)

bool FNativeRailsPlanTest::RunTest(const FString& Parameters)
{
	const double Inf = std::numeric_limits<double>::infinity();
	const double Nan = std::numeric_limits<double>::quiet_NaN();
	TestEqual(TEXT("0 -> 1"), RailSampleIntervals(0.0), 1);
	TestEqual(TEXT("-5 -> 1"), RailSampleIntervals(-5.0), 1);
	TestEqual(TEXT("NaN -> 1"), RailSampleIntervals(Nan), 1);
	TestEqual(TEXT("+inf -> 1"), RailSampleIntervals(Inf), 1);
	TestEqual(TEXT("100 -> 1"), RailSampleIntervals(100.0), 1);
	TestEqual(TEXT("100.5 -> 2"), RailSampleIntervals(100.5), 2);
	TestEqual(TEXT("1236.9 -> 13"), RailSampleIntervals(1236.9), 13);
	TestEqual(TEXT("9500 -> 95"), RailSampleIntervals(9500.0), 95);
	TestEqual(TEXT("99900 -> 999"), RailSampleIntervals(99900.0), 999);
	TestEqual(TEXT("100000 -> 999"), RailSampleIntervals(100000.0), 999);
	TestEqual(TEXT("1500000 -> 999"), RailSampleIntervals(1500000.0), 999);

	TestEqual(TEXT("index 0 is the start"), RailSampleDistanceCm(9500.0, 95, 0), 0.0);
	TestEqual(TEXT("the last index is the length exactly"), RailSampleDistanceCm(9500.0, 95, 95), 9500.0);
	TestTrue(TEXT("index 10 of 95 is 1000 cm"), FMath::Abs(RailSampleDistanceCm(9500.0, 95, 10) - 1000.0) < 1e-9);
	TestEqual(TEXT("a clamped track still ends on its length exactly"), RailSampleDistanceCm(1500000.0, 999, 999), 1500000.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeRailsMapTest, "SatisDash.Core.NativeRails.Map", SatisDash::Test::Flags)

bool FNativeRailsMapTest::RunTest(const FString& Parameters)
{
	// The fixture's first track, points in cm as its SplineData gives them.
	const FNativeWorldBody Mapped = BuildNativeRails(NativeRailsObserved, { NativeRailsReading(TEXT("R1"),
		{ { 0, 0 }, { 10, 10 }, { 250, 0 }, { 250.4, 0 }, { 500, 1.0e12 }, { 1050, -50 } }) });
	SatisDash::Test::SameText(*this, TEXT("collapse, rounding, the bad point skipped, -0 printed 0"), NativeRailsData(Mapped), TEXT("[{\"id\":\"R1\",\"points\":[[0,0],[3,0],[11,0]]}]"));
	TestEqual(TEXT("one item"), Mapped.Items, 1);
	SatisDash::Test::SameText(*this, TEXT("the whole body, with observedAt"), Mapped.Json, NativeRailsBody(NativeRailsData(Mapped)));
	TestEqual(TEXT("the byte count is the body's UTF-8 length"), Mapped.Bytes, int64(FTCHARToUTF8(*Mapped.Json).Length()));

	struct FRow { double Cm; int32 Metres; };
	for (const FRow& Row : { FRow{ 150, 2 }, FRow{ -150, -1 }, FRow{ 50, 1 }, FRow{ -50, 0 }, FRow{ 249, 2 }, FRow{ 250, 3 } })
	{
		const FNativeWorldBody One = BuildNativeRails(NativeRailsObserved, { NativeRailsReading(TEXT("R"), { { Row.Cm, 0 }, { 100000, 0 } }) });
		SatisDash::Test::SameText(*this, *FString::Printf(TEXT("%g cm"), Row.Cm), NativeRailsData(One), FString::Printf(TEXT("[{\"id\":\"R\",\"points\":[[%d,0],[1000,0]]}]"), Row.Metres));
	}

	const FNativeWorldBody Empty = BuildNativeRails(NativeRailsObserved, {});
	SatisDash::Test::SameText(*this, TEXT("no tracks: a valid empty layer"), Empty.Json, NativeRailsBody(TEXT("[]")));
	TestEqual(TEXT("no items"), Empty.Items, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeRailsGoldenTest, "SatisDash.Core.NativeRails.Golden", SatisDash::Test::Flags)

bool FNativeRailsGoldenTest::RunTest(const FString& Parameters)
{
	const FString Source = SatisDash::Test::Fixture(TEXT("frm-getTrainRails.json"));
	const FString Expected = SatisDash::Test::Fixture(TEXT("frm-getTrainRails.expected.json"));
	if (!TestFalse(TEXT("the fixtures were found"), Source.IsEmpty() || Expected.IsEmpty()))
	{
		return false;
	}
	// The three tracks of the fixture, read the way the game would give them (cm).
	const FNativeWorldBody Body = BuildNativeRails(NativeRailsObserved, {
		NativeRailsReading(TEXT("Build_RailroadTrack_C_1"), { { 0, 0 }, { 10, 10 }, { 250, 0 }, { 250.4, 0 }, { 500, 1.0e12 }, { 1050, -50 } }),
		NativeRailsReading(TEXT("Build_RailroadTrack_C_2"), { { 100, 100 }, { 120, 120 } }),
		NativeRailsReading(TEXT("Build_RailroadTrack_C_3"), {}) });
	TestEqual(TEXT("one track kept"), Body.Items, 1);
	TestEqual(TEXT("two dropped"), Body.Dropped, 2);
	TestTrue(FString::Printf(TEXT("is the bridge's rails layer; got %s"), *Body.Json), SatisDash::Test::SameJson(Body.Json, NativeRailsBody(Expected)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeRailsRulesTest, "SatisDash.Core.NativeRails.Rules", SatisDash::Test::Flags)

bool FNativeRailsRulesTest::RunTest(const FString& Parameters)
{
	// 5,001 valid tracks: 5,000 kept.
	TArray<FRailReading> Many;
	for (int32 I = 0; I < MaxRailSegments + 1; ++I)
	{
		Many.Add(NativeRailsReading(FString::Printf(TEXT("R%05d"), I), { { 0, 0 }, { 300, 0 } }));
	}
	const FNativeWorldBody Capped = BuildNativeRails(NativeRailsObserved, Many);
	TestEqual(TEXT("5,000 kept"), Capped.Items, MaxRailSegments);
	TestEqual(TEXT("one over"), Capped.Over, 1);
	TestFalse(TEXT("the one sorting last is the one cut"), Capped.Json.Contains(FString::Printf(TEXT("\"id\":\"R%05d\""), MaxRailSegments)));

	// Order does not matter.
	TArray<FRailReading> Twenty;
	for (int32 I = 0; I < 20; ++I)
	{
		Twenty.Add(NativeRailsReading(FString::Printf(TEXT("T%d"), (I * 7) % 20), { { 0, 0 }, { 100.0 * (I + 1), 50 } }));
	}
	const FString Forward = BuildNativeRails(NativeRailsObserved, Twenty).Json;
	TArray<FRailReading> Shuffled;
	for (int32 I = 0; I < 20; ++I)
	{
		Shuffled.Add(Twenty[(I * 3 + 1) % 20]);
	}
	SatisDash::Test::SameText(*this, TEXT("shuffled readings, the same bytes"), BuildNativeRails(NativeRailsObserved, Shuffled).Json, Forward);

	// A repeated id: the track whose points sort first wins, in either input order.
	const FRailReading A = NativeRailsReading(TEXT("D"), { { 0, 0 }, { 300, 0 } });
	const FRailReading B = NativeRailsReading(TEXT("D"), { { 0, 0 }, { 500, 0 } });
	const FNativeWorldBody AB = BuildNativeRails(NativeRailsObserved, { A, B });
	const FNativeWorldBody BA = BuildNativeRails(NativeRailsObserved, { B, A });
	SatisDash::Test::SameText(*this, TEXT("a duplicate id gives the same bytes in both orders"), AB.Json, BA.Json);
	SatisDash::Test::SameText(*this, TEXT("the first by points stays"), NativeRailsData(AB), TEXT("[{\"id\":\"D\",\"points\":[[0,0],[3,0]]}]"));
	TestEqual(TEXT("one item"), AB.Items, 1);

	// Ids: cut at 200, an empty one dropped.
	const FNativeWorldBody Long = BuildNativeRails(NativeRailsObserved, { NativeRailsReading(FString::ChrN(250, TEXT('a')), { { 0, 0 }, { 300, 0 } }) });
	TestTrue(TEXT("a 250-character id is cut to 200"), NativeRailsData(Long).StartsWith(TEXT("[{\"id\":\"") + FString::ChrN(200, TEXT('a')) + TEXT("\",")));
	const FNativeWorldBody NoId = BuildNativeRails(NativeRailsObserved, { NativeRailsReading(TEXT(""), { { 0, 0 }, { 300, 0 } }) });
	TestTrue(TEXT("an empty id is dropped"), NoId.Items == 0 && NoId.Dropped == 1);

	// Point count: 1,000 kept, 1,001 dropped.
	const FNativeWorldBody Exactly = BuildNativeRails(NativeRailsObserved, { NativeRailsLine(TEXT("L"), MaxRailPoints) });
	TestTrue(TEXT("1,000 points keep the track"), Exactly.Items == 1 && Exactly.Dropped == 0);
	const FNativeWorldBody TooLong = BuildNativeRails(NativeRailsObserved, { NativeRailsLine(TEXT("L"), MaxRailPoints + 1) });
	TestTrue(TEXT("1,001 points drop it"), TooLong.Items == 0 && TooLong.Dropped == 1);

	// A closed loop keeps its last point, equal to the first.
	const FNativeWorldBody Loop = BuildNativeRails(NativeRailsObserved, { NativeRailsReading(TEXT("C"), { { 0, 0 }, { 500, 0 }, { 500, 500 }, { 0, 0 } }) });
	SatisDash::Test::SameText(*this, TEXT("a closed loop keeps all four points"), NativeRailsData(Loop), TEXT("[{\"id\":\"C\",\"points\":[[0,0],[5,0],[5,5],[0,0]]}]"));
	return true;
}
