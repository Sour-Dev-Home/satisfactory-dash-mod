#include "SatisDash/FrmPlayers.h"
#include "SatisDash/JsonTokens.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	TOptional<FPlayersPart> FrmPlayersOf(const FString& Json) { return ParsePlayers(SatisDash::Test::Utf8(Json)); }

	/** Walks every token; true when the body reads cleanly to its end. */
	bool JsonTokensWalk(const FString& Json)
	{
		const TArray<uint8> Bytes = SatisDash::Test::Utf8(Json);
		FTokenReader Reader{ Bytes };
		EJsonNotation N;
		while (Reader.Next(N))
		{
		}
		return !Reader.Failed();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJsonTokensWalkTest, "SatisDash.Core.JsonTokens.Walk", SatisDash::Test::Flags)

bool FJsonTokensWalkTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("a body"), JsonTokensWalk(TEXT("[{\"a\":[1,2,{\"b\":null}],\"c\":\"d\"}]")));
	TestFalse(TEXT("empty input"), JsonTokensWalk(FString()));
	TestFalse(TEXT("trailing input"), JsonTokensWalk(TEXT("[] []")));
	TestFalse(TEXT("unclosed"), JsonTokensWalk(TEXT("[{\"a\":1}")));
	TestFalse(TEXT("deeper than 32 is refused before any recursion"), JsonTokensWalk(FString::ChrN(10000, TEXT('[')) + FString::ChrN(10000, TEXT(']'))));
	TestTrue(TEXT("32 deep is fine"), JsonTokensWalk(FString::ChrN(32, TEXT('[')) + FString::ChrN(32, TEXT(']'))));

	{
		// Observed: the engine reader turns 1e999 into +infinity (Atod), it does not refuse it. The players mapper then
		// refuses the non-finite value, as zod refuses Infinity.
		const TArray<uint8> Huge = SatisDash::Test::Utf8(TEXT("[1e999]"));
		FTokenReader HugeReader{ Huge };
		EJsonNotation H;
		HugeReader.Next(H);
		const bool bNumber = HugeReader.Next(H) && H == EJsonNotation::Number;
		TestTrue(TEXT("1e999 is refused or not finite"), !bNumber || !FMath::IsFinite(HugeReader.Number()));
	}
	const TArray<uint8> Bytes = SatisDash::Test::Utf8(TEXT("[\"Zoë\", true]"));
	FTokenReader Reader{ Bytes };
	EJsonNotation N;
	TestTrue(TEXT("array"), Reader.Next(N) && N == EJsonNotation::ArrayStart);
	TestTrue(TEXT("a string"), Reader.Next(N) && N == EJsonNotation::String);
	SatisDash::Test::SameText(*this, TEXT("UTF-8 is decoded"), Reader.String(), TEXT("Zoë"));
	TestTrue(TEXT("a boolean"), Reader.Next(N) && N == EJsonNotation::Boolean && Reader.Boolean());
	TestTrue(TEXT("the end"), Reader.Next(N) && N == EJsonNotation::ArrayEnd);
	TestTrue(TEXT("and nothing after it"), Reader.AtCleanEnd());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmPlayersGoldenTest, "SatisDash.Core.FrmPlayers.Golden", SatisDash::Test::Flags)

bool FFrmPlayersGoldenTest::RunTest(const FString& Parameters)
{
	const TOptional<FPlayersPart> Part = FrmPlayersOf(SatisDash::Test::Fixture(TEXT("frm-getPlayer.json")));
	if (!TestTrue(TEXT("the capture parses"), Part.IsSet()))
	{
		return true;
	}
	const FString Expected = SatisDash::Test::Fixture(TEXT("frm-getPlayer.expected.json"));
	const FString Actual = PlayersToJson(Part.GetValue());
	TestTrue(FString::Printf(TEXT("the Node agent's output; got %s"), *Actual), SatisDash::Test::SameJson(Actual, Expected));
	TestFalse(TEXT("nothing but name and online"), Actual.Contains(TEXT("Inventory")) || Actual.Contains(TEXT("location")) || Actual.Contains(TEXT("Char_Player")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmPlayersRulesTest, "SatisDash.Core.FrmPlayers.Rules", SatisDash::Test::Flags)

bool FFrmPlayersRulesTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("no players is valid"), FrmPlayersOf(TEXT("[]")).IsSet());
	for (const TCHAR* Bad : {
		TEXT("{}"), TEXT("[1]"), TEXT("[{\"Name\":\"a\"}]"), TEXT("[{\"Online\":true}]"), TEXT("[{\"Name\":null,\"Online\":true}]"),
		TEXT("[{\"Name\":\"a\",\"Online\":\"yes\"}]"), TEXT("[{\"Name\":5,\"Online\":true}]"), TEXT("[{\"Name\":\"a\",\"Online\":true}] x") })
	{
		TestFalse(FString::Printf(TEXT("refused like zod: %s"), Bad), FrmPlayersOf(Bad).IsSet());
	}
	FString Many = TEXT("[");
	for (int32 I = 0; I < 300; ++I)
	{
		Many += FString::Printf(TEXT("%s{\"Name\":\"%s\",\"Online\":true}"), I ? TEXT(",") : TEXT(""), *FString::ChrN(250, TEXT('n')));
	}
	Many += TEXT("]");
	const TOptional<FPlayersPart> Capped = FrmPlayersOf(Many);
	TestTrue(TEXT("valid"), Capped.IsSet());
	TestEqual(TEXT("at most 256 kept"), Capped->Players.Num(), 256);
	TestEqual(TEXT("names cut to 200"), Capped->Players[0].Name.Len(), 200);
	TestTrue(TEXT("last key wins, as JSON.parse"), FrmPlayersOf(TEXT("[{\"Name\":\"a\",\"Name\":\"b\",\"Online\":false}]"))->Players[0].Name == TEXT("b"));
	SatisDash::Test::SameText(*this, TEXT("FRM absent"), PlayersToJson(PlayersUnavailable()), TEXT("{\"available\":false,\"players\":[]}"));
	return true;
}
