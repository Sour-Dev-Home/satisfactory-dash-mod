#include "SatisDash/JsonUtil.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	FString Quote(const FString& Value)
	{
		FString Out;
		SatisDash::Json::AppendString(Out, Value);
		return Out;
	}

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJsonAppendStringTest, "SatisDash.Core.Json.AppendString", SatisDash::Test::Flags)

bool FJsonAppendStringTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::SameText(*this, TEXT("empty"), Quote(FString()), TEXT("\"\""));
	SatisDash::Test::SameText(*this, TEXT("plain"), Quote(TEXT("Iron Ore")), TEXT("\"Iron Ore\""));
	SatisDash::Test::SameText(*this, TEXT("quote and backslash"), Quote(TEXT("a\"b\\c")), TEXT("\"a\\\"b\\\\c\""));
	SatisDash::Test::SameText(*this, TEXT("named controls"), Quote(TEXT("\n\r\t\b\f")), TEXT("\"\\n\\r\\t\\b\\f\""));
	SatisDash::Test::SameText(*this, TEXT("other controls as \\u00xx, lower case"), Quote(FString::Printf(TEXT("%c%c"), TCHAR(0x01), TCHAR(0x1F))), TEXT("\"\\u0001\\u001f\""));
	const FString Accented = FString::Printf(TEXT("Z%cge"), TCHAR(0x00FC));
	SatisDash::Test::SameText(*this, TEXT("non-ASCII passes through"), Quote(Accented), TEXT("\"") + Accented + TEXT("\""));
	// Round trip through the engine's parser: what is written reads back as the same text.
	const FString Nasty = FString::Printf(TEXT("a\"\\/\n%cz"), TCHAR(0x07));
	const TSharedPtr<FJsonObject> Back = SatisDash::Json::ParseObject(TEXT("{\"v\":") + Quote(Nasty) + TEXT("}"));
	SatisDash::Test::SameText(*this, TEXT("round trip"), Back.IsValid() ? Back->GetStringField(TEXT("v")) : FString(), Nasty);
	return true;
}
