#include "SatisDash/JsonUtil.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	FString Arrays(int32 Levels)
	{
		return FString::ChrN(Levels, TEXT('[')) + FString::ChrN(Levels, TEXT(']'));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJsonDepthCapTest, "SatisDash.Core.Json.DepthCap", SatisDash::Test::Flags)

bool FJsonDepthCapTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("the cap"), SatisDash::Json::MaxDepth, 32);
	TestTrue(TEXT("arrays at the cap"), SatisDash::Json::WithinDepth(Arrays(SatisDash::Json::MaxDepth)));
	TestFalse(TEXT("arrays one over the cap"), SatisDash::Json::WithinDepth(Arrays(SatisDash::Json::MaxDepth + 1)));
	TestTrue(TEXT("objects at the cap"), SatisDash::Json::WithinDepth(SatisDash::Test::NestedObjects(SatisDash::Json::MaxDepth)));
	TestFalse(TEXT("objects one over the cap"), SatisDash::Json::WithinDepth(SatisDash::Test::NestedObjects(SatisDash::Json::MaxDepth + 1)));
	TestFalse(TEXT("64 KB of ["), SatisDash::Json::WithinDepth(FString::ChrN(MaxResponseBytes, TEXT('['))));
	TestTrue(TEXT("siblings don't add up"), SatisDash::Json::WithinDepth(TEXT("{\"a\":") + Arrays(SatisDash::Json::MaxDepth - 1) + TEXT(",\"b\":") + Arrays(SatisDash::Json::MaxDepth - 1) + TEXT("}")));
	TestFalse(TEXT("a stray close doesn't buy depth"), SatisDash::Json::WithinDepth(TEXT("]]") + FString::ChrN(SatisDash::Json::MaxDepth + 1, TEXT('['))));

	TestTrue(TEXT("ParseObject at the cap"), SatisDash::Json::ParseObject(SatisDash::Test::NestedObjects(SatisDash::Json::MaxDepth)).IsValid());
	TestFalse(TEXT("ParseObject one over the cap"), SatisDash::Json::ParseObject(SatisDash::Test::NestedObjects(SatisDash::Json::MaxDepth + 1)).IsValid());
	TestFalse(TEXT("ParseObject on 64 KB of {\"a\":"), SatisDash::Json::ParseObject(SatisDash::Test::NestedObjects(MaxResponseBytes / 6)).IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJsonDepthStringsTest, "SatisDash.Core.Json.DepthStrings", SatisDash::Test::Flags)

bool FJsonDepthStringsTest::RunTest(const FString& Parameters)
{
	const FString Deep = FString::ChrN(SatisDash::Json::MaxDepth * 2, TEXT('['));
	TestTrue(TEXT("brackets in a string"), SatisDash::Json::WithinDepth(TEXT("{\"a\":\"") + Deep + TEXT("\"}")));
	TestTrue(TEXT("braces in a key"), SatisDash::Json::WithinDepth(TEXT("{\"") + FString::ChrN(SatisDash::Json::MaxDepth * 2, TEXT('{')) + TEXT("\":1}")));
	TestTrue(TEXT("after an escaped quote, still in the string"), SatisDash::Json::WithinDepth(TEXT("{\"a\":\"\\\"") + Deep + TEXT("\\\"\"}")));
	TestTrue(TEXT("ParseObject with brackets in a string"), SatisDash::Json::ParseObject(TEXT("{\"a\":\"\\\"") + Deep + TEXT("\\\"\"}")).IsValid());
	// An escaped backslash ends the escape: the quote after it closes the string, and the brackets that follow count.
	TestFalse(TEXT("after an escaped backslash, out of the string"),
		SatisDash::Json::WithinDepth(TEXT("{\"a\":\"x\\\\\",\"b\":") + Arrays(SatisDash::Json::MaxDepth) + TEXT("}")));
	TestTrue(TEXT("the same, at the cap"), SatisDash::Json::WithinDepth(TEXT("{\"a\":\"x\\\\\",\"b\":") + Arrays(SatisDash::Json::MaxDepth - 1) + TEXT("}")));
	return true;
}
