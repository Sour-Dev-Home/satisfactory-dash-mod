#include "SatisDash/Records.h"
#include "TestUtil.h"

using namespace SatisDash;

// Moved from FrmMapTest (Metres), FrmMapLiveTest (Join) and FrmResourceNodesTest (NodeType) when the FRM role was deleted (R7):
// they test the shared record writers and numeric rules the native readers use. The assertions are the same.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRecordsMetresTest, "SatisDash.Core.Records.Metres", SatisDash::Test::Flags)

bool FRecordsMetresTest::RunTest(const FString& Parameters)
{
	// JavaScript's Math.round: a half goes toward +infinity.
	TestEqual(TEXT("2.5 is 3"), JsRound(2.5), 3.0);
	TestEqual(TEXT("-2.5 is -2"), JsRound(-2.5), -2.0);
	TestEqual(TEXT("-1.5 is -1"), JsRound(-1.5), -1.0);
	TestEqual(TEXT("0.5 is 1"), JsRound(0.5), 1.0);
	TestEqual(TEXT("-0.5 is 0"), JsRound(-0.5), 0.0);
	TestEqual(TEXT("-0.4 is 0"), JsRound(-0.4), 0.0);
	TestEqual(TEXT("0.49999999999999994 is 0 (floor(x + 0.5), which FMath::RoundToDouble is, gets this one wrong)"), JsRound(0.49999999999999994), 0.0);
	TestEqual(TEXT("a whole number stays"), JsRound(7.0), 7.0);
	TestEqual(TEXT("a huge value stays"), JsRound(1e300), 1e300);
	TestTrue(TEXT("NaN stays NaN"), FMath::IsNaN(JsRound(std::numeric_limits<double>::quiet_NaN())));

	// toWholeMetres: Math.round(x / 100).
	TestEqual(TEXT("150 cm is 2 m"), ToWholeMetres(150.0), 2.0);
	TestEqual(TEXT("-150 cm is -1 m"), ToWholeMetres(-150.0), -1.0);
	TestEqual(TEXT("-100050 cm is -1000 m"), ToWholeMetres(-100050.0), -1000.0);
	TestEqual(TEXT("-102587.95567750931 cm is -1026 m"), ToWholeMetres(-102587.95567750931), -1026.0);
	TestEqual(TEXT("49 cm is 0 m"), ToWholeMetres(49.0), 0.0);

	// inWholeMetreBounds: finite and within +/-1,000,000.
	TestTrue(TEXT("0"), InWholeMetreBounds(0.0));
	TestTrue(TEXT("the bound itself"), InWholeMetreBounds(1000000.0) && InWholeMetreBounds(-1000000.0));
	TestFalse(TEXT("one past it"), InWholeMetreBounds(1000001.0) || InWholeMetreBounds(-1000001.0));
	TestFalse(TEXT("infinity"), InWholeMetreBounds(std::numeric_limits<double>::infinity()));
	TestFalse(TEXT("NaN"), InWholeMetreBounds(std::numeric_limits<double>::quiet_NaN()));
	TestFalse(TEXT("1e12 cm is out"), InWholeMetreBounds(ToWholeMetres(1e12)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRecordsMapLiveJoinTest, "SatisDash.Core.Records.MapLiveJoin", SatisDash::Test::Flags)

bool FRecordsMapLiveJoinTest::RunTest(const FString& Parameters)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRecordsNodeTypeTest, "SatisDash.Core.Records.NodeType", SatisDash::Test::Flags)

bool FRecordsNodeTypeTest::RunTest(const FString& Parameters)
{
	// nodeType.ts: three known values map explicitly; anything else is camel-cased (first word lower case, later words
	// capitalised, split on JavaScript's \s).
	const TCHAR* Cases[][2] = {
		{ TEXT("Node"), TEXT("node") }, { TEXT("Fracking Satellite"), TEXT("frackingSatellite") }, { TEXT("Geyser"), TEXT("geyser") },
		{ TEXT(""), TEXT("") }, { TEXT("   "), TEXT("") }, { TEXT("  padded  "), TEXT("padded") },
		{ TEXT("Some New TYPE"), TEXT("someNewType") }, { TEXT("some  new   TYPE"), TEXT("someNewType") }, { TEXT("ALLCAPS"), TEXT("allcaps") },
		{ TEXT("node"), TEXT("node") }, { TEXT("A B"), TEXT("aB") }, { TEXT("x"), TEXT("x") },
		{ TEXT("constructor"), TEXT("constructor") }, { TEXT("toString"), TEXT("tostring") }, { TEXT("a\tb\nc"), TEXT("aBC") },
	};
	for (const TCHAR* (&Case)[2] : Cases)
	{
		SatisDash::Test::SameText(*this, *FString::Printf(TEXT("'%s'"), Case[0]), MapNodeType(Case[0]), Case[1]);
	}
	SatisDash::Test::SameText(*this, TEXT("a no-break space splits words, as \\s does"), MapNodeType(FString::Printf(TEXT("a%cb"), TCHAR(0x00A0))), TEXT("aB"));
	SatisDash::Test::SameText(*this, TEXT("so does U+2003"), MapNodeType(FString::Printf(TEXT("a%cb"), TCHAR(0x2003))), TEXT("aB"));
	return true;
}
