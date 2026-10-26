#include "SatisDash/FrmResourceNodes.h"
#include "SatisDash/Snapshot.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	const TCHAR* FrmNodesAt = TEXT("2026-10-24T12:00:00.000Z");

	struct FFrmNodesRun
	{
		bool bOk = false;
		FString Json;
		FString Data;
		int32 Kept = 0;
		int32 Dropped = 0;
		int32 Over = 0;
	};

	FFrmNodesRun FrmNodesOf(const FString& Json)
	{
		FResourceNodesParse Parse{ SatisDash::Test::Utf8(Json), FrmNodesAt };
		FFrmNodesRun Run;
		FFrmArrayParse::EStep Step = FFrmArrayParse::EStep::More;
		while (Step == FFrmArrayParse::EStep::More)
		{
			Step = Parse.Step();
		}
		Run.bOk = Step == FFrmArrayParse::EStep::Done;
		Run.Kept = Parse.Kept();
		Run.Dropped = Parse.Dropped();
		Run.Over = Parse.Over();
		if (Run.bOk)
		{
			Run.Data = Parse.PeekJson().Mid(Parse.DataFrom(), Parse.DataEnd() - Parse.DataFrom());
			Run.Json = Parse.TakeJson();
		}
		return Run;
	}

	/** One node; Extra is appended inside the object, and a repeated key wins, as in JSON.parse. */
	FString FrmNodesOne(const TCHAR* Extra)
	{
		return FString::Printf(TEXT("[{\"Name\":\"Iron Ore\",\"Purity\":\"Normal\",\"NodeType\":\"Node\",\"Exploited\":false,\"location\":{\"x\":100,\"y\":200,\"z\":0}%s}]"), Extra);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmResourceNodesRulesTest, "SatisDash.Core.FrmResourceNodes.Rules", SatisDash::Test::Flags)

bool FFrmResourceNodesRulesTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::SameText(*this, TEXT("a node, fields in the Node agent's order"), FrmNodesOf(FrmNodesOne(TEXT(""))).Data,
		TEXT("[{\"type\":\"Iron Ore\",\"purity\":\"normal\",\"nodeType\":\"node\",\"x\":1,\"y\":2,\"exploited\":false}]"));
	SatisDash::Test::SameText(*this, TEXT("the world body wraps it"), FrmNodesOf(FrmNodesOne(TEXT(""))).Json,
		FString::Printf(TEXT("{\"observedAt\":\"%s\",\"data\":[{\"type\":\"Iron Ore\",\"purity\":\"normal\",\"nodeType\":\"node\",\"x\":1,\"y\":2,\"exploited\":false}]}"), FrmNodesAt));
	TestTrue(TEXT("exploited true"), FrmNodesOf(FrmNodesOne(TEXT(",\"Exploited\":true"))).Data.Contains(TEXT("\"exploited\":true")));
	TestTrue(TEXT("FRM's \"Inpure\" typo is not corrected: Purity is used, not EnumPurity"), FrmNodesOf(FrmNodesOne(TEXT(",\"Purity\":\"Inpure\",\"EnumPurity\":\"RP_Inpure\""))).Data.Contains(TEXT("\"purity\":\"inpure\"")));
	TestTrue(TEXT("a fracking satellite"), FrmNodesOf(FrmNodesOne(TEXT(",\"NodeType\":\"Fracking Satellite\""))).Data.Contains(TEXT("\"nodeType\":\"frackingSatellite\"")));

	// Tolerated: the extras FRM sends.
	TestTrue(TEXT("extras are skipped"), FrmNodesOf(FrmNodesOne(TEXT(",\"ID\":\"BP_1\",\"ClassName\":\"c\",\"ResourceForm\":\"RF_SOLID\",\"Extra\":{\"a\":[1,null]}"))).Kept == 1);
	// ClassName, FRM's descriptor class name, becomes className: carried, absent when empty, cut to 200, refused when not a string.
	TestTrue(TEXT("ClassName is carried as className"), FrmNodesOf(FrmNodesOne(TEXT(",\"ClassName\":\"Desc_OreIron_C\""))).Data.Contains(TEXT("\"className\":\"Desc_OreIron_C\"")));
	TestFalse(TEXT("an empty ClassName gives no className key"), FrmNodesOf(FrmNodesOne(TEXT(",\"ClassName\":\"\""))).Data.Contains(TEXT("className")));
	TestFalse(TEXT("no ClassName gives no className key"), FrmNodesOf(FrmNodesOne(TEXT(""))).Data.Contains(TEXT("className")));
	TestFalse(TEXT("a non-string ClassName makes the body invalid"), FrmNodesOf(FrmNodesOne(TEXT(",\"ClassName\":1"))).bOk);
	TestFalse(TEXT("a null ClassName makes the body invalid"), FrmNodesOf(FrmNodesOne(TEXT(",\"ClassName\":null"))).bOk);
	TestTrue(TEXT("className cut to 200"), FrmNodesOf(FrmNodesOne(*(TEXT(",\"ClassName\":\"") + FString::ChrN(201, TEXT('c')) + TEXT("\"")))).Data.Contains(TEXT("\"className\":\"") + FString::ChrN(200, TEXT('c')) + TEXT("\"")));
	// Refused as zod's RawFrmResourceNodeSchema refuses them.
	for (const TCHAR* Bad : { TEXT(",\"Name\":null"), TEXT(",\"Purity\":1"), TEXT(",\"NodeType\":null"), TEXT(",\"Exploited\":\"no\""), TEXT(",\"Exploited\":null"),
		TEXT(",\"location\":{\"x\":1,\"y\":2}"), TEXT(",\"location\":{\"x\":1e999,\"y\":2,\"z\":0}"), TEXT(",\"location\":null") })
	{
		TestFalse(FString::Printf(TEXT("refused: %s"), Bad), FrmNodesOf(FrmNodesOne(Bad)).bOk);
	}
	TestFalse(TEXT("a node without Exploited"), FrmNodesOf(TEXT("[{\"Name\":\"a\",\"Purity\":\"p\",\"NodeType\":\"n\",\"location\":{\"x\":1,\"y\":2,\"z\":3}}]")).bOk);
	TestFalse(TEXT("not an array"), FrmNodesOf(TEXT("{}")).bOk);
	TestFalse(TEXT("trailing input"), FrmNodesOf(TEXT("[] []")).bOk);

	// A bad coordinate drops the node, not the layer.
	const FFrmNodesRun Far = FrmNodesOf(FrmNodesOne(TEXT(",\"location\":{\"x\":1e12,\"y\":0,\"z\":0}")));
	TestTrue(TEXT("out of bounds: dropped"), Far.bOk && Far.Kept == 0 && Far.Dropped == 1);

	// Cuts: type to 200, purity and nodeType to 40 (the contract's bounds); a surrogate pair is never split.
	TestTrue(TEXT("type cut to 200"), FrmNodesOf(FrmNodesOne(*(TEXT(",\"Name\":\"") + FString::ChrN(201, TEXT('a')) + TEXT("\"")))).Data.Contains(TEXT("\"type\":\"") + FString::ChrN(200, TEXT('a')) + TEXT("\",")));
	TestTrue(TEXT("purity cut to 40"), FrmNodesOf(FrmNodesOne(*(TEXT(",\"Purity\":\"") + FString::ChrN(41, TEXT('P')) + TEXT("\"")))).Data.Contains(TEXT("\"purity\":\"") + FString::ChrN(40, TEXT('p')) + TEXT("\",")));
	TestTrue(TEXT("nodeType cut to 40"), FrmNodesOf(FrmNodesOne(*(TEXT(",\"NodeType\":\"") + FString::ChrN(41, TEXT('t')) + TEXT("\"")))).Data.Contains(TEXT("\"nodeType\":\"") + FString::ChrN(40, TEXT('t')) + TEXT("\",")));
	const FString Pair = FString::ChrN(39, TEXT('a')) + FString::Printf(TEXT("%c%c"), TCHAR(0xD83D), TCHAR(0xDE00));
	SatisDash::Test::SameText(*this, TEXT("a pair at the cut is dropped whole"), ClampStringTo(Pair, 40), FString::ChrN(39, TEXT('a')));
	SatisDash::Test::SameText(*this, TEXT("under the cut: unchanged"), ClampStringTo(TEXT("abc"), 40), TEXT("abc"));
	SatisDash::Test::SameText(*this, TEXT("ClampString is the 200 cut"), ClampString(FString::ChrN(201, TEXT('b'))), FString::ChrN(200, TEXT('b')));

	// 5,000 nodes kept; the 5,001st counted.
	FString Many = TEXT("[");
	for (int32 I = 0; I < MaxResourceNodes + 1; ++I)
	{
		Many += FString::Printf(TEXT("%s{\"Name\":\"n%d\",\"Purity\":\"Pure\",\"NodeType\":\"Node\",\"Exploited\":false,\"location\":{\"x\":%d,\"y\":0,\"z\":0}}"), I > 0 ? TEXT(",") : TEXT(""), I, I * 100);
	}
	Many += TEXT("]");
	const FFrmNodesRun Capped = FrmNodesOf(Many);
	TestTrue(TEXT("5,001 nodes: 5,000 kept, one over"), Capped.bOk && Capped.Kept == MaxResourceNodes && Capped.Over == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmResourceNodesNodeTypeTest, "SatisDash.Core.FrmResourceNodes.NodeType", SatisDash::Test::Flags)

bool FFrmResourceNodesNodeTypeTest::RunTest(const FString& Parameters)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmResourceNodesGoldenTest, "SatisDash.Core.FrmResourceNodes.Golden", SatisDash::Test::Flags)

bool FFrmResourceNodesGoldenTest::RunTest(const FString& Parameters)
{
	const FString Body = SatisDash::Test::Fixture(TEXT("frm-getResourceNode.json"));
	const FString Expected = SatisDash::Test::Fixture(TEXT("frm-getResourceNode.expected.json"));
	if (!TestFalse(TEXT("the fixtures were found"), Body.IsEmpty() || Expected.IsEmpty()))
	{
		return false;
	}
	const FFrmNodesRun Run = FrmNodesOf(Body);
	TestTrue(TEXT("parses"), Run.bOk);
	TestEqual(TEXT("the node out of bounds was dropped"), Run.Dropped, 1);
	TestTrue(FString::Printf(TEXT("is the Node agent's resourceNodes layer; got %s"), *Run.Json),
		SatisDash::Test::SameJson(Run.Json, FString::Printf(TEXT("{\"observedAt\":\"%s\",\"data\":%s}"), FrmNodesAt, *Expected)));
	return true;
}
