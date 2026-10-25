#include "SatisDash/FrmMapLive.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	/** What one parse produced; bOk is false when the body was refused. */
	struct FFrmMapLiveRun
	{
		bool bOk = false;
		FString Json;
		int32 Kept = 0;
		int32 Dropped = 0;
		int32 Over = 0;
		int32 Steps = 0;
		int64 Bytes = 0;
	};

	template <typename Parser>
	FFrmMapLiveRun FrmMapLiveOf(const FString& Json, int32 Slice = FrmSliceRecords)
	{
		// The alias is the code's own name for the type, which is how check-api knows the calls on it are ours.
		using FMapLiveParser = Parser;
		FMapLiveParser MapLiveParse{ SatisDash::Test::Utf8(Json) };
		FFrmMapLiveRun Run;
		FFrmArrayParse::EStep Step = FFrmArrayParse::EStep::More;
		while (Step == FFrmArrayParse::EStep::More)
		{
			Step = MapLiveParse.Step(Slice);
			++Run.Steps;
		}
		Run.bOk = Step == FFrmArrayParse::EStep::Done;
		Run.Kept = MapLiveParse.Kept();
		Run.Dropped = MapLiveParse.Dropped();
		Run.Over = MapLiveParse.Over();
		Run.Bytes = MapLiveParse.Utf8Bytes();
		if (Run.bOk)
		{
			Run.Json = MapLiveParse.TakeJson();
		}
		return Run;
	}

	/** One valid train; Extra is appended inside the object (a JSON fragment such as ",\"Status\":1"), and a repeated key wins, as in JSON.parse. */
	FString FrmMapLiveTrainWith(const TCHAR* Extra)
	{
		return FString::Printf(TEXT("[{\"ID\":\"T\",\"Name\":\"n\",\"Status\":\"s\",\"location\":{\"x\":150,\"y\":-150,\"z\":0}%s}]"), Extra);
	}

	FString FrmMapLiveStationWith(const TCHAR* Extra)
	{
		return FString::Printf(TEXT("[{\"ID\":\"S\",\"Name\":\"n\",\"location\":{\"x\":150,\"y\":-150,\"z\":0}%s}]"), Extra);
	}

	/** Count valid stations S0.. with x = 100 m apart; BadFirst bad-coordinate ones (X0..) come before them. */
	FString FrmMapLiveStations(int32 Count, int32 BadFirst = 0)
	{
		FString Json = TEXT("[");
		for (int32 I = 0; I < BadFirst; ++I)
		{
			Json += FString::Printf(TEXT("{\"ID\":\"X%d\",\"Name\":\"n\",\"location\":{\"x\":1e12,\"y\":0,\"z\":0}},"), I);
		}
		for (int32 I = 0; I < Count; ++I)
		{
			Json += FString::Printf(TEXT("%s{\"ID\":\"S%d\",\"Name\":\"n%d\",\"location\":{\"x\":%d,\"y\":0,\"z\":0}}"), I > 0 ? TEXT(",") : TEXT(""), I, I, I * 10000);
		}
		return Json + TEXT("]");
	}

	/** Count valid trains T0.. with x = 100 m apart. */
	FString FrmMapLiveTrains(int32 Count)
	{
		FString Json = TEXT("[");
		for (int32 I = 0; I < Count; ++I)
		{
			Json += FString::Printf(TEXT("%s{\"ID\":\"T%d\",\"Name\":\"n%d\",\"Status\":\"s\",\"location\":{\"x\":%d,\"y\":0,\"z\":0}}"), I > 0 ? TEXT(",") : TEXT(""), I, I, I * 10000);
		}
		return Json + TEXT("]");
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmMapLiveRulesTest, "SatisDash.Core.FrmMapLive.Rules", SatisDash::Test::Flags)

bool FFrmMapLiveRulesTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::SameText(*this, TEXT("a train: half-metres go toward +infinity, fields in the Node agent's order"),
		FrmMapLiveOf<FTrainsParse>(FrmMapLiveTrainWith(TEXT(""))).Json, TEXT("[{\"id\":\"T\",\"name\":\"n\",\"x\":2,\"y\":-1,\"status\":\"s\"}]"));
	SatisDash::Test::SameText(*this, TEXT("a station"),
		FrmMapLiveOf<FStationsParse>(FrmMapLiveStationWith(TEXT(""))).Json, TEXT("[{\"id\":\"S\",\"name\":\"n\",\"x\":2,\"y\":-1}]"));
	SatisDash::Test::SameText(*this, TEXT("no trains is valid"), FrmMapLiveOf<FTrainsParse>(TEXT("[]")).Json, TEXT("[]"));

	// Tolerated: fields the map doesn't use are skipped, however they are shaped.
	for (const TCHAR* Extra : { TEXT(",\"Cars\":[{\"a\":[1,2,{\"b\":null}]}]"), TEXT(",\"TimeTable\":[]"), TEXT(",\"ClassName\":\"c\",\"TotalMass\":1.5e3") })
	{
		TestTrue(FString::Printf(TEXT("tolerated: %s"), Extra), FrmMapLiveOf<FTrainsParse>(FrmMapLiveTrainWith(Extra)).bOk);
	}
	// Refused as zod's schemas refuse them: the whole body is left out. A repeated key wins, so the appended one decides.
	for (const TCHAR* Bad : { TEXT(",\"ID\":\"\""), TEXT(",\"ID\":1"), TEXT(",\"Name\":null"),
		TEXT(",\"location\":{\"x\":1,\"y\":2}"), TEXT(",\"location\":{\"x\":1e999,\"y\":2,\"z\":0}"), TEXT(",\"location\":null") })
	{
		TestFalse(FString::Printf(TEXT("train refused: %s"), Bad), FrmMapLiveOf<FTrainsParse>(FrmMapLiveTrainWith(Bad)).bOk);
		TestFalse(FString::Printf(TEXT("station refused: %s"), Bad), FrmMapLiveOf<FStationsParse>(FrmMapLiveStationWith(Bad)).bOk);
	}
	TestFalse(TEXT("a train with a numeric Status"), FrmMapLiveOf<FTrainsParse>(FrmMapLiveTrainWith(TEXT(",\"Status\":1"))).bOk);
	TestFalse(TEXT("a train without Status"), FrmMapLiveOf<FTrainsParse>(TEXT("[{\"ID\":\"T\",\"Name\":\"n\",\"location\":{\"x\":1,\"y\":2,\"z\":3}}]")).bOk);
	TestTrue(TEXT("a station needs no Status (and skips one)"), FrmMapLiveOf<FStationsParse>(FrmMapLiveStationWith(TEXT(",\"Status\":1"))).bOk);
	// Hostile bodies: refused without a crash.
	TArray<FString> Hostile;
	Hostile.Add(FString());
	Hostile.Add(TEXT("{}"));
	Hostile.Add(TEXT("[1]"));
	Hostile.Add(TEXT("[{}]"));
	Hostile.Add(TEXT("[] []"));
	Hostile.Add(TEXT("[]x"));
	Hostile.Add(TEXT("[{\"ID\":\"T\""));
	Hostile.Add(FString::ChrN(10000, TEXT('[')) + FString::ChrN(10000, TEXT(']')));
	for (int32 I = 0; I < Hostile.Num(); ++I)
	{
		TestFalse(FString::Printf(TEXT("hostile body %d is refused"), I), FrmMapLiveOf<FTrainsParse>(Hostile[I]).bOk);
	}

	// Strings: cut to 200 characters, escaped, non-ASCII kept.
	const FString Long = FString::ChrN(201, TEXT('a'));
	TestTrue(TEXT("a name is cut to 200"), FrmMapLiveOf<FTrainsParse>(FrmMapLiveTrainWith(*(TEXT(",\"Name\":\"") + Long + TEXT("\"")))).Json.Contains(TEXT("\"name\":\"") + FString::ChrN(200, TEXT('a')) + TEXT("\",")));
	TestTrue(TEXT("quote and backslash are escaped"), FrmMapLiveOf<FTrainsParse>(FrmMapLiveTrainWith(TEXT(",\"Name\":\"a\\\"b\\\\c\""))).Json.Contains(TEXT("\"name\":\"a\\\"b\\\\c\"")));
	const FString Accented = FString::Printf(TEXT("Z%cge"), TCHAR(0x00FC));
	TestTrue(TEXT("non-ASCII is kept"), FrmMapLiveOf<FTrainsParse>(FrmMapLiveTrainWith(*(TEXT(",\"Name\":\"") + Accented + TEXT("\"")))).Json.Contains(TEXT("\"name\":\"") + Accented + TEXT("\"")));

	// Bounds: 1e8 cm is exactly 1,000,000 m (kept); 100000100 cm is 1,000,001 m (dropped, not refused).
	const FString Edge = TEXT("[{\"ID\":\"T\",\"Name\":\"n\",\"Status\":\"s\",\"location\":{\"x\":100000000,\"y\":-100000000,\"z\":0}}]");
	TestEqual(TEXT("the bound itself is kept"), FrmMapLiveOf<FTrainsParse>(Edge).Kept, 1);
	const FFrmMapLiveRun Past = FrmMapLiveOf<FTrainsParse>(TEXT("[{\"ID\":\"T\",\"Name\":\"n\",\"Status\":\"s\",\"location\":{\"x\":100000100,\"y\":0,\"z\":0}}]"));
	TestTrue(TEXT("one metre past it is dropped, and the body is still valid"), Past.bOk && Past.Kept == 0 && Past.Dropped == 1);
	// The same for a station (its own copy of the record reader).
	TestEqual(TEXT("a station at the bound is kept"), FrmMapLiveOf<FStationsParse>(TEXT("[{\"ID\":\"S\",\"Name\":\"n\",\"location\":{\"x\":100000000,\"y\":-100000000,\"z\":0}}]")).Kept, 1);
	const FFrmMapLiveRun StationPast = FrmMapLiveOf<FStationsParse>(TEXT("[{\"ID\":\"S\",\"Name\":\"n\",\"location\":{\"x\":0,\"y\":-100000100,\"z\":0}}]"));
	TestTrue(TEXT("a station one metre past it is dropped, and the body is still valid"), StationPast.bOk && StationPast.Kept == 0 && StationPast.Dropped == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmMapLiveCapsTest, "SatisDash.Core.FrmMapLive.Caps", SatisDash::Test::Flags)

bool FFrmMapLiveCapsTest::RunTest(const FString& Parameters)
{
	const FFrmMapLiveRun Many = FrmMapLiveOf<FTrainsParse>(FrmMapLiveTrains(600));
	TestTrue(TEXT("600 trains parse"), Many.bOk);
	TestEqual(TEXT("500 are kept"), Many.Kept, MapLiveMaxItems);
	TestEqual(TEXT("100 are valid but over the cap"), Many.Over, 100);
	TestTrue(TEXT("the first ones: T499 is in, T500 is not"), Many.Json.Contains(TEXT("\"id\":\"T499\"")) && !Many.Json.Contains(TEXT("\"id\":\"T500\"")));

	// Dropped ones (bad coordinates) do not count towards the 500, as sampler.ts counts after the mappers.
	FString Bad;
	for (int32 I = 0; I < 10; ++I)
	{
		Bad += FString::Printf(TEXT("{\"ID\":\"X%d\",\"Name\":\"n\",\"Status\":\"s\",\"location\":{\"x\":1e12,\"y\":0,\"z\":0}},"), I);
	}
	const FFrmMapLiveRun Mixed = FrmMapLiveOf<FTrainsParse>(TEXT("[") + Bad + FrmMapLiveTrains(500).RightChop(1));
	TestTrue(TEXT("10 dropped then 500 valid: all 500 kept"), Mixed.bOk && Mixed.Kept == 500 && Mixed.Dropped == 10 && Mixed.Over == 0);

	// Stations: the same caps, through their own record reader.
	const FFrmMapLiveRun ManyStations = FrmMapLiveOf<FStationsParse>(FrmMapLiveStations(600));
	TestTrue(TEXT("600 stations parse"), ManyStations.bOk);
	TestEqual(TEXT("500 stations are kept"), ManyStations.Kept, MapLiveMaxItems);
	TestEqual(TEXT("100 stations are valid but over the cap"), ManyStations.Over, 100);
	TestTrue(TEXT("the 500th station (S499) is in, the 501st (S500) is not"), ManyStations.Json.Contains(TEXT("\"id\":\"S499\"")) && !ManyStations.Json.Contains(TEXT("\"id\":\"S500\"")));
	const FFrmMapLiveRun MixedStations = FrmMapLiveOf<FStationsParse>(FrmMapLiveStations(500, 10));
	TestTrue(TEXT("10 dropped stations then 500 valid: all 500 kept"), MixedStations.bOk && MixedStations.Kept == 500 && MixedStations.Dropped == 10 && MixedStations.Over == 0);

	// zod validates the whole array before conform cuts it: a bad record past the cap still refuses the body.
	TestFalse(TEXT("an invalid 601st record refuses the body"), FrmMapLiveOf<FTrainsParse>(FrmMapLiveTrains(600).LeftChop(1) + TEXT(",{\"ID\":\"\"}]")).bOk);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmMapLiveSlicesTest, "SatisDash.Core.FrmMapLive.Slices", SatisDash::Test::Flags)

bool FFrmMapLiveSlicesTest::RunTest(const FString& Parameters)
{
	const FString Body = FrmMapLiveTrains(12);
	const FFrmMapLiveRun One = FrmMapLiveOf<FTrainsParse>(Body, 1);
	const FFrmMapLiveRun All = FrmMapLiveOf<FTrainsParse>(Body, FrmSliceRecords);
	TestEqual(TEXT("one record a slice: 12 records and the end"), One.Steps, 13);
	TestEqual(TEXT("2,000 a slice: one step"), All.Steps, 1);
	SatisDash::Test::SameText(*this, TEXT("the slice size changes nothing in the output"), One.Json, All.Json);
	TestEqual(TEXT("and the byte count is the text's UTF-8 length"), All.Bytes, int64(FTCHARToUTF8(*All.Json).Length()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmMapLiveJoinTest, "SatisDash.Core.FrmMapLive.Join", SatisDash::Test::Flags)

bool FFrmMapLiveJoinTest::RunTest(const FString& Parameters)
{
	const TOptional<FString> Joined = JoinMapLive(TEXT("[1]"), 3, TEXT("[2]"), 3);
	SatisDash::Test::SameText(*this, TEXT("the two arrays in one object"), Joined.Get(FString()), TEXT("{\"trains\":[1],\"stations\":[2]}"));
	TestEqual(TEXT("the wrapper's bytes are as counted"), int64(Joined.Get(FString()).Len()), MapLiveWrapperBytes + 3 + 3);
	// The cap is 256,000 UTF-8 bytes of the whole object, and exactly the cap is allowed (sampler.ts: > MAX).
	const int64 Room = MapLiveMaxBytes - MapLiveWrapperBytes;
	TestTrue(TEXT("exactly the cap"), JoinMapLive(TEXT("[]"), Room - 2, TEXT("[]"), 2).IsSet());
	TestFalse(TEXT("one byte over: the whole part is left out"), JoinMapLive(TEXT("[]"), Room - 1, TEXT("[]"), 2).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmMapLiveGoldenTest, "SatisDash.Core.FrmMapLive.Golden", SatisDash::Test::Flags)

bool FFrmMapLiveGoldenTest::RunTest(const FString& Parameters)
{
	const FString TrainsBody = SatisDash::Test::Fixture(TEXT("frm-getTrains.json"));
	const FString StationsBody = SatisDash::Test::Fixture(TEXT("frm-getTrainStation.json"));
	const FString Expected = SatisDash::Test::Fixture(TEXT("frm-mapLive.expected.json"));
	if (!TestFalse(TEXT("the fixtures were found"), TrainsBody.IsEmpty() || StationsBody.IsEmpty() || Expected.IsEmpty()))
	{
		return false;
	}
	const FFrmMapLiveRun Trains = FrmMapLiveOf<FTrainsParse>(TrainsBody);
	const FFrmMapLiveRun Stations = FrmMapLiveOf<FStationsParse>(StationsBody);
	TestTrue(TEXT("both parse"), Trains.bOk && Stations.bOk);
	TestEqual(TEXT("the train out of bounds was dropped"), Trains.Dropped, 1);
	const TOptional<FString> Joined = JoinMapLive(Trains.Json, Trains.Bytes, Stations.Json, Stations.Bytes);
	TestTrue(TEXT("under the cap"), Joined.IsSet());
	TestTrue(FString::Printf(TEXT("is the Node agent's mapLive; got %s"), *Joined.Get(FString())), SatisDash::Test::SameJson(Joined.Get(FString()), Expected));
	return true;
}
