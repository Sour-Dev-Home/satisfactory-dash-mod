#include "SatisDash/FrmMap.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	/** Reads one location value; false when ReadFrmLocation refused it or anything follows it. */
	bool FrmMapLocation(const TCHAR* Json, double& X, double& Y)
	{
		const TArray<uint8> Bytes = SatisDash::Test::Utf8(Json);
		FTokenReader Reader{ Bytes };
		EJsonNotation N;
		return Reader.Next(N) && ReadFrmLocation(Reader, N, X, Y) && Reader.AtCleanEnd();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmMapMetresTest, "SatisDash.Core.FrmMap.Metres", SatisDash::Test::Flags)

bool FFrmMapMetresTest::RunTest(const FString& Parameters)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmMapLocationTest, "SatisDash.Core.FrmMap.Location", SatisDash::Test::Flags)

bool FFrmMapLocationTest::RunTest(const FString& Parameters)
{
	double X = 0;
	double Y = 0;
	TestTrue(TEXT("x, y and z"), FrmMapLocation(TEXT("{\"x\":1,\"y\":2,\"z\":3}"), X, Y));
	TestTrue(TEXT("x and y are read"), X == 1.0 && Y == 2.0);
	TestTrue(TEXT("rotation, pitch and unknown nested fields are fine"), FrmMapLocation(TEXT("{\"x\":-4.5,\"y\":6,\"z\":0,\"rotation\":270,\"pitch\":0,\"extra\":{\"a\":[1,{\"b\":null}]}}"), X, Y));
	TestTrue(TEXT("the values"), X == -4.5 && Y == 6.0);
	TestTrue(TEXT("a repeated key: the last one wins, as JSON.parse does"), FrmMapLocation(TEXT("{\"x\":1,\"x\":5,\"y\":2,\"z\":3}"), X, Y) && X == 5.0);
	// Each refused, as zod's RawFrmLocationSchema refuses it.
	for (const TCHAR* Bad : {
		TEXT("{\"x\":1,\"y\":2}"),                        // z is required
		TEXT("{\"y\":2,\"z\":3}"),
		TEXT("{\"x\":\"1\",\"y\":2,\"z\":3}"),            // a string
		TEXT("{\"x\":1,\"y\":null,\"z\":3}"),
		TEXT("{\"x\":1e999,\"y\":2,\"z\":3}"),            // not finite
		TEXT("{\"x\":1,\"y\":2,\"z\":3,\"rotation\":null}"),
		TEXT("{\"x\":1,\"y\":2,\"z\":3,\"rotation\":1e999}"),
		TEXT("[1,2,3]"), TEXT("5"), TEXT("{\"x\":1,\"y\":2,\"z\":3} 1") })
	{
		TestFalse(FString::Printf(TEXT("refused: %s"), Bad), FrmMapLocation(Bad, X, Y));
	}
	return true;
}
