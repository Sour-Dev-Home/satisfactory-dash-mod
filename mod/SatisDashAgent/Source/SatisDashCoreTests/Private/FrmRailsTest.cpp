#include "SatisDash/FrmRails.h"
#include "Dom/JsonObject.h"
#include "SatisDash/JsonUtil.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	const TCHAR* FrmRailsAt = TEXT("2026-10-24T12:00:00.000Z");

	struct FFrmRailsRun
	{
		bool bOk = false;
		FString Json;
		FString Data;
		int32 Kept = 0;
		int32 Dropped = 0;
		int32 Over = 0;
		int32 Steps = 0;
		int64 Bytes = 0;
	};

	FFrmRailsRun FrmRailsOf(const FString& Json, int32 Slice = FrmSliceRecords)
	{
		FRailsParse Parse{ SatisDash::Test::Utf8(Json), FrmRailsAt };
		FFrmRailsRun Run;
		FFrmArrayParse::EStep Step = FFrmArrayParse::EStep::More;
		while (Step == FFrmArrayParse::EStep::More)
		{
			Step = Parse.Step(Slice);
			++Run.Steps;
		}
		Run.bOk = Step == FFrmArrayParse::EStep::Done;
		Run.Kept = Parse.Kept();
		Run.Dropped = Parse.Dropped();
		Run.Over = Parse.Over();
		Run.Bytes = Parse.Utf8Bytes();
		if (Run.bOk)
		{
			Run.Data = Parse.PeekJson().Mid(Parse.DataFrom(), Parse.DataEnd() - Parse.DataFrom());
			Run.Json = Parse.TakeJson();
		}
		return Run;
	}

	/** The body a world sender posts for this data array. */
	FString FrmRailsBody(const FString& Data)
	{
		return FString::Printf(TEXT("{\"observedAt\":\"%s\",\"data\":%s}"), FrmRailsAt, *Data);
	}

	/** One segment; Spline is the inside of the SplineData array. */
	FString FrmRailsSegment(const FString& Id, const FString& Spline)
	{
		return FString::Printf(TEXT("{\"ID\":\"%s\",\"SplineData\":[%s]}"), *Id, *Spline);
	}

	/** Count distinct points, one metre apart on x (x = i * 100 cm). */
	FString FrmRailsPoints(int32 Count)
	{
		FString Points;
		for (int32 I = 0; I < Count; ++I)
		{
			Points += FString::Printf(TEXT("%s{\"x\":%d,\"y\":0}"), I > 0 ? TEXT(",") : TEXT(""), I * 100);
		}
		return Points;
	}

	int32 FrmRailsPointCount(const FString& Body, int32 Segment)
	{
		const TSharedPtr<FJsonObject> Parsed = SatisDash::Json::ParseObject(Body);
		if (!Parsed.IsValid())
		{
			return -1;
		}
		const TArray<TSharedPtr<FJsonValue>>& Data = Parsed->GetArrayField(TEXT("data"));
		return Data.IsValidIndex(Segment) ? Data[Segment]->AsObject()->GetArrayField(TEXT("points")).Num() : -1;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmRailsRulesTest, "SatisDash.Core.FrmRails.Rules", SatisDash::Test::Flags)

bool FFrmRailsRulesTest::RunTest(const FString& Parameters)
{
	const FFrmRailsRun Simple = FrmRailsOf(TEXT("[") + FrmRailsSegment(TEXT("R1"), TEXT("{\"x\":0,\"y\":0},{\"x\":300,\"y\":0}")) + TEXT("]"));
	SatisDash::Test::SameText(*this, TEXT("the whole world body, with observedAt, in the Node agent's field order"), Simple.Json, FrmRailsBody(TEXT("[{\"id\":\"R1\",\"points\":[[0,0],[3,0]]}]")));
	SatisDash::Test::SameText(*this, TEXT("DataFrom and DataEnd bound the data array"), Simple.Data, TEXT("[{\"id\":\"R1\",\"points\":[[0,0],[3,0]]}]"));
	TestEqual(TEXT("the byte count is the body's UTF-8 length"), Simple.Bytes, int64(FTCHARToUTF8(*Simple.Json).Length()));
	SatisDash::Test::SameText(*this, TEXT("no rails: an empty layer, which still posts the first time"), FrmRailsOf(TEXT("[]")).Json, FrmRailsBody(TEXT("[]")));

	// railsMapper.ts: rounding (2.5 is 3, 10.5 is 11, -0.5 is 0), consecutive duplicates collapsed, a bad point skipped.
	const FFrmRailsRun Mapped = FrmRailsOf(TEXT("[") + FrmRailsSegment(TEXT("R1"),
		TEXT("{\"x\":0,\"y\":0,\"z\":0},{\"x\":10,\"y\":10},{\"x\":250,\"y\":0},{\"x\":250.4,\"y\":0},{\"x\":500,\"y\":1.0e12},{\"x\":1050,\"y\":-50}")) + TEXT("]"));
	SatisDash::Test::SameText(*this, TEXT("the fixture's first rail"), Mapped.Data, TEXT("[{\"id\":\"R1\",\"points\":[[0,0],[3,0],[11,0]]}]"));
	// A segment with fewer than two points left is dropped, not refused.
	const FFrmRailsRun Short = FrmRailsOf(TEXT("[") + FrmRailsSegment(TEXT("R1"), TEXT("{\"x\":0,\"y\":0},{\"x\":1e12,\"y\":0}")) + TEXT(",") + FrmRailsSegment(TEXT("R2"), TEXT("")) + TEXT("]"));
	TestTrue(TEXT("one point left and none: both dropped, the body still valid"), Short.bOk && Short.Kept == 0 && Short.Dropped == 2);
	SatisDash::Test::SameText(*this, TEXT("and the layer is empty"), Short.Data, TEXT("[]"));

	// Tolerated: fields the map doesn't use, however shaped.
	TestTrue(TEXT("extra fields, z and pitch are skipped"), FrmRailsOf(TEXT("[{\"ID\":\"R1\",\"Connected0\":true,\"location\":{\"x\":1},\"Cars\":[[1,[2]]],\"SplineData\":[{\"x\":0,\"y\":0,\"z\":9,\"pitch\":1},{\"x\":300,\"y\":0}]}]")).Kept == 1);
	// Refused as zod's RawFrmTrainRailSchema refuses them: the whole layer is left out.
	for (const TCHAR* Bad : {
		TEXT("[{\"ID\":\"\",\"SplineData\":[]}]"), TEXT("[{\"SplineData\":[]}]"), TEXT("[{\"ID\":1,\"SplineData\":[]}]"),
		TEXT("[{\"ID\":\"R\"}]"), TEXT("[{\"ID\":\"R\",\"SplineData\":null}]"), TEXT("[{\"ID\":\"R\",\"SplineData\":{}}]"),
		TEXT("[{\"ID\":\"R\",\"SplineData\":[1]}]"), TEXT("[{\"ID\":\"R\",\"SplineData\":[{\"x\":0}]}]"),
		TEXT("[{\"ID\":\"R\",\"SplineData\":[{\"x\":\"0\",\"y\":0}]}]"), TEXT("[{\"ID\":\"R\",\"SplineData\":[{\"x\":1e999,\"y\":0}]}]"),
		TEXT("[1]"), TEXT("{}"), TEXT("[] []"), TEXT("") })
	{
		TestFalse(FString::Printf(TEXT("refused: %s"), Bad), FrmRailsOf(Bad).bOk);
	}
	TestFalse(TEXT("10,000 deep"), FrmRailsOf(FString::ChrN(10000, TEXT('[')) + FString::ChrN(10000, TEXT(']'))).bOk);
	TestFalse(TEXT("deep inside a skipped field"), FrmRailsOf(TEXT("[{\"ID\":\"R\",\"SplineData\":[],\"x\":") + FString::ChrN(10000, TEXT('[')) + FString::ChrN(10000, TEXT(']')) + TEXT("}]")).bOk);

	// Strings: the id is cut to 200 (the contract's bound), escaped.
	const FFrmRailsRun Long = FrmRailsOf(TEXT("[") + FrmRailsSegment(FString::ChrN(201, TEXT('a')), TEXT("{\"x\":0,\"y\":0},{\"x\":300,\"y\":0}")) + TEXT("]"));
	TestTrue(TEXT("a 201-character id is cut to 200"), Long.Data.Contains(TEXT("\"id\":\"") + FString::ChrN(200, TEXT('a')) + TEXT("\",")));

	// Slices: the output doesn't depend on the slice size.
	FString Many = TEXT("[");
	for (int32 I = 0; I < 12; ++I)
	{
		Many += (I > 0 ? TEXT(",") : TEXT("")) + FrmRailsSegment(FString::Printf(TEXT("R%d"), I), TEXT("{\"x\":0,\"y\":0},{\"x\":300,\"y\":0}"));
	}
	Many += TEXT("]");
	const FFrmRailsRun OneAtATime = FrmRailsOf(Many, 1);
	const FFrmRailsRun AllAtOnce = FrmRailsOf(Many);
	TestEqual(TEXT("one record a slice: 12 records and the end"), OneAtATime.Steps, 13);
	TestEqual(TEXT("2,000 a slice: one step"), AllAtOnce.Steps, 1);
	SatisDash::Test::SameText(*this, TEXT("the same output"), OneAtATime.Json, AllAtOnce.Json);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmRailsCapsTest, "SatisDash.Core.FrmRails.Caps", SatisDash::Test::Flags)

bool FFrmRailsCapsTest::RunTest(const FString& Parameters)
{
	// 5,000 segments: the first 5,000 kept, the rest counted (the contract's cap; the Node agent would post them all and be refused).
	FString Many = TEXT("[");
	for (int32 I = 0; I < MaxRailSegments + 1; ++I)
	{
		Many += (I > 0 ? TEXT(",") : TEXT("")) + FrmRailsSegment(FString::Printf(TEXT("R%d"), I), TEXT("{\"x\":0,\"y\":0},{\"x\":300,\"y\":0}"));
	}
	Many += TEXT("]");
	const FFrmRailsRun Capped = FrmRailsOf(Many);
	TestTrue(TEXT("5,001 segments parse"), Capped.bOk);
	TestEqual(TEXT("5,000 kept"), Capped.Kept, MaxRailSegments);
	TestEqual(TEXT("one over the cap"), Capped.Over, 1);
	TestFalse(TEXT("the 5,001st is not in the layer"), Capped.Json.Contains(FString::Printf(TEXT("\"id\":\"R%d\""), MaxRailSegments)));

	// 1,000 points are kept; 1,001 drop the segment (a partial line would mislead).
	const FFrmRailsRun Exactly = FrmRailsOf(TEXT("[") + FrmRailsSegment(TEXT("R1"), FrmRailsPoints(MaxRailPoints)) + TEXT("]"));
	TestEqual(TEXT("1,000 points: kept whole"), FrmRailsPointCount(Exactly.Json, 0), MaxRailPoints);
	const FFrmRailsRun TooLong = FrmRailsOf(TEXT("[") + FrmRailsSegment(TEXT("R1"), FrmRailsPoints(MaxRailPoints + 1)) + TEXT(",") + FrmRailsSegment(TEXT("R2"), FrmRailsPoints(3)) + TEXT("]"));
	TestTrue(TEXT("1,001 points: the segment is dropped, the next one kept"), TooLong.bOk && TooLong.Dropped == 1 && TooLong.Kept == 1 && FrmRailsPointCount(TooLong.Json, 0) == 3);

	// zod validates every segment before the cap applies: a bad one past the cap still refuses the layer.
	TestFalse(TEXT("an invalid 5,002nd segment refuses the layer"), FrmRailsOf(Many.LeftChop(1) + TEXT(",{\"ID\":\"\"}]")).bOk);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmRailsGoldenTest, "SatisDash.Core.FrmRails.Golden", SatisDash::Test::Flags)

bool FFrmRailsGoldenTest::RunTest(const FString& Parameters)
{
	const FString Body = SatisDash::Test::Fixture(TEXT("frm-getTrainRails.json"));
	const FString Expected = SatisDash::Test::Fixture(TEXT("frm-getTrainRails.expected.json"));
	if (!TestFalse(TEXT("the fixtures were found"), Body.IsEmpty() || Expected.IsEmpty()))
	{
		return false;
	}
	const FFrmRailsRun Run = FrmRailsOf(Body);
	TestTrue(TEXT("parses"), Run.bOk);
	TestEqual(TEXT("two segments dropped"), Run.Dropped, 2);
	TestTrue(FString::Printf(TEXT("is the Node agent's rails layer; got %s"), *Run.Json), SatisDash::Test::SameJson(Run.Json, FrmRailsBody(Expected)));
	return true;
}
