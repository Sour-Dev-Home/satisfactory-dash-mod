#include "SatisDash/FrmFactory.h"
#include "Dom/JsonObject.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	/** Runs a parse to its end; unset when it was invalid. */
	TOptional<FString> FrmFactoryOf(const FString& Json, int32 Slice = FactorySliceBuildings, int32* Steps = nullptr)
	{
		FFactoryParse Parse{ SatisDash::Test::Utf8(Json) };
		FFactoryParse::EStep Step = FFactoryParse::EStep::More;
		int32 Count = 0;
		while (Step == FFactoryParse::EStep::More)
		{
			Step = Parse.Step(Slice);
			++Count;
		}
		if (Steps)
		{
			*Steps = Count;
		}
		return Step == FFactoryParse::EStep::Done ? TOptional<FString>(Parse.TakeJson()) : TOptional<FString>();
	}

	/** One building; Over replaces or adds fields (a JSON fragment such as "\"IsPaused\":true"). */
	FString FrmFactoryOne(const TCHAR* Over)
	{
		return FString::Printf(TEXT("[{\"ID\":\"B_1\",\"Name\":\"Smelter\",\"ClassName\":\"Build_SmelterMk1_C\",\"Recipe\":\"Iron Ingot\",")
			TEXT("\"IsConfigured\":true,\"IsProducing\":true,\"IsPaused\":false%s%s}]"), *Over ? TEXT(",") : TEXT(""), Over);
	}

	/** The factory part of one building with Over added; the empty string when it was refused (so a Contains fails). */
	FString FrmFactoryOneOut(const TCHAR* Over)
	{
		return FrmFactoryOf(FrmFactoryOne(Over)).Get(FString());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmFactoryGoldenTest, "SatisDash.Core.FrmFactory.Golden", SatisDash::Test::Flags)

bool FFrmFactoryGoldenTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* Name : { TEXT("frm-getFactory-running"), TEXT("frm-getFactory-norecipe") })
	{
		const TOptional<FString> Actual = FrmFactoryOf(SatisDash::Test::Fixture(*(FString(Name) + TEXT(".json"))));
		if (!TestTrue(FString::Printf(TEXT("%s parses"), Name), Actual.IsSet()))
		{
			continue;
		}
		const FString Expected = SatisDash::Test::Fixture(*(FString(Name) + TEXT(".expected.json")));
		TestTrue(FString::Printf(TEXT("%s is the Node agent's output; got %s"), Name, *Actual.GetValue()), SatisDash::Test::SameJson(Actual.GetValue(), Expected));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmFactoryRulesTest, "SatisDash.Core.FrmFactory.Rules", SatisDash::Test::Flags)

bool FFrmFactoryRulesTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("no buildings is valid"), FrmFactoryOf(TEXT("[]")).IsSet());
	TestTrue(TEXT("a minimal building is valid"), FrmFactoryOf(FrmFactoryOne(TEXT(""))).IsSet());
	// Each refused as the Node agent's zod schema refuses it, so the whole part is left out.
	for (const TCHAR* Bad : {
		TEXT("\"ID\":\"\""), TEXT("\"Recipe\":null"), TEXT("\"Recipe\":\"\""), TEXT("\"IsPaused\":\"no\""),
		TEXT("\"production\":[{\"Name\":\"a\",\"ClassName\":\"c\",\"Amount\":-0.5,\"CurrentProd\":1,\"MaxProd\":1,\"ProdPercent\":1}]"),
		TEXT("\"production\":[{\"Name\":\"a\",\"ClassName\":\"c\",\"Amount\":1,\"CurrentProd\":1e999,\"MaxProd\":1,\"ProdPercent\":1}]"),
		TEXT("\"production\":[{\"Name\":\"a\",\"Amount\":1,\"CurrentProd\":1,\"MaxProd\":1,\"ProdPercent\":1}]"),
		TEXT("\"PowerInfo\":{\"CircuitGroupID\":1.5,\"CircuitID\":1,\"PowerConsumed\":1,\"MaxPowerConsumed\":1}"),
		TEXT("\"location\":{\"x\":1,\"y\":2}"), TEXT("\"OutputInventory\":null") })
	{
		TestFalse(FString::Printf(TEXT("refused: %s"), Bad), FrmFactoryOf(FrmFactoryOne(Bad)).IsSet());
	}
	TestFalse(TEXT("a building without IsPaused"), FrmFactoryOf(TEXT("[{\"ID\":\"B\",\"Name\":\"n\",\"ClassName\":\"c\",\"IsProducing\":true}]")).IsSet());
	TestFalse(TEXT("trailing input"), FrmFactoryOf(TEXT("[] 1")).IsSet());

	// ManuSpeed is tolerant (.catch(undefined)): an odd value is dropped, never a failure.
	for (const TCHAR* Odd : { TEXT("\"ManuSpeed\":\"fast\""), TEXT("\"ManuSpeed\":0"), TEXT("\"ManuSpeed\":1e999") })
	{
		const TOptional<FString> Result = FrmFactoryOf(FrmFactoryOne(Odd));
		if (TestTrue(FString::Printf(TEXT("%s is still a valid body"), Odd), Result.IsSet()))
		{
			TestFalse(FString::Printf(TEXT("%s is dropped"), Odd), Result.GetValue().Contains(TEXT("clockSpeedPercent")));
		}
	}
	TestTrue(TEXT("ManuSpeed 160 is kept"), FrmFactoryOneOut(TEXT("\"ManuSpeed\":160")).Contains(TEXT("\"clockSpeedPercent\":160")));

	const TOptional<FString> Unassigned = FrmFactoryOf(FrmFactoryOne(TEXT("\"Recipe\":\"Unassigned\",\"production\":[{\"Name\":\"Unassigned\",\"ClassName\":\"x\",\"Amount\":0,\"CurrentProd\":0,\"MaxProd\":0,\"ProdPercent\":0}]")));
	if (TestTrue(TEXT("Unassigned parses"), Unassigned.IsSet()))
	{
		TestTrue(TEXT("Unassigned is no recipe"), Unassigned.GetValue().Contains(TEXT("\"recipe\":null")));
		TestTrue(TEXT("and no production"), Unassigned.GetValue().Contains(TEXT("\"production\":[]")));
		TestTrue(TEXT("no PowerInfo is group -1"), Unassigned.GetValue().Contains(TEXT("\"circuitGroupId\":-1")));
	}
	TestTrue(TEXT("float noise is raised to 0"), FrmFactoryOneOut(TEXT("\"production\":[{\"Name\":\"a\",\"ClassName\":\"c\",\"Amount\":1,\"CurrentProd\":-0.0005,\"MaxProd\":1,\"ProdPercent\":1}]")).Contains(TEXT("\"currentPerMinute\":0")));
	TestTrue(TEXT("a yaw is normalised and centimetres become metres"),
		FrmFactoryOneOut(TEXT("\"location\":{\"x\":150,\"y\":-200,\"z\":0,\"rotation\":-90,\"pitch\":3}")).Contains(TEXT("\"location\":{\"xM\":1.5,\"yM\":-2,\"zM\":0,\"rotationDeg\":270}")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmFactoryNonFiniteTest, "SatisDash.Core.FrmFactory.NonFinite", SatisDash::Test::Flags)

bool FFrmFactoryNonFiniteTest::RunTest(const FString& Parameters)
{
	// 1e999 is a number JSON allows and a double cannot hold. However the reader takes it (infinity or a refusal), the
	// factory part is left out, never sent with a non-finite value.
	for (const TCHAR* Huge : {
		TEXT("\"production\":[{\"Name\":\"a\",\"ClassName\":\"c\",\"Amount\":1e999,\"CurrentProd\":1,\"MaxProd\":1,\"ProdPercent\":1}]"),
		TEXT("\"production\":[{\"Name\":\"a\",\"ClassName\":\"c\",\"Amount\":1,\"CurrentProd\":1,\"MaxProd\":1e999,\"ProdPercent\":1}]"),
		TEXT("\"ingredients\":[{\"Name\":\"a\",\"ClassName\":\"c\",\"Amount\":1,\"CurrentConsumed\":1,\"MaxConsumed\":1,\"ConsPercent\":1e999}]"),
		TEXT("\"production\":[{\"Name\":\"a\",\"ClassName\":\"c\",\"Amount\":1,\"CurrentProd\":-1e999,\"MaxProd\":1,\"ProdPercent\":1}]"),
		TEXT("\"OutputInventory\":[{\"Name\":\"a\",\"ClassName\":\"c\",\"Amount\":1e999,\"MaxAmount\":1}]"),
		TEXT("\"location\":{\"x\":1e999,\"y\":0,\"z\":0}"),
		TEXT("\"PowerInfo\":{\"CircuitGroupID\":1,\"CircuitID\":1,\"PowerConsumed\":1e999,\"MaxPowerConsumed\":1}") })
	{
		TestFalse(FString::Printf(TEXT("refused: %s"), Huge), FrmFactoryOf(FrmFactoryOne(Huge)).IsSet());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmFactorySlicesTest, "SatisDash.Core.FrmFactory.Slices", SatisDash::Test::Flags)

bool FFrmFactorySlicesTest::RunTest(const FString& Parameters)
{
	FString Many = TEXT("[");
	for (int32 I = 0; I < 20005; ++I)
	{
		Many += FString::Printf(TEXT("%s{\"ID\":\"B_%d\",\"Name\":\"n\",\"ClassName\":\"c\",\"IsProducing\":false,\"IsPaused\":false}"), I ? TEXT(",") : TEXT(""), I);
	}
	Many += TEXT("]");
	int32 Steps = 0;
	FFactoryParse Parse{ SatisDash::Test::Utf8(Many) };
	FFactoryParse::EStep Step = FFactoryParse::EStep::More;
	while (Step == FFactoryParse::EStep::More)
	{
		Step = Parse.Step(2000);
		++Steps;
	}
	TestTrue(TEXT("done"), Step == FFactoryParse::EStep::Done);
	TestEqual(TEXT("2,000 buildings a slice: 11 slices"), Steps, 11);
	TestEqual(TEXT("20,000 kept"), Parse.Buildings(), 20000);
	TestEqual(TEXT("5 dropped past the cap"), Parse.Dropped(), 5);
	TestTrue(TEXT("a bad building past the cap still refuses the body, as zod does"),
		!FrmFactoryOf(Many.LeftChop(1) + TEXT(",{\"ID\":\"\"}]")).IsSet());
	const FString Json = Parse.TakeJson();
	TestTrue(TEXT("valid JSON"), SatisDash::Json::ParseObject(Json).IsValid());

	FString Rates = TEXT("[{\"ID\":\"B\",\"Name\":\"n\",\"ClassName\":\"c\",\"Recipe\":\"r\",\"IsProducing\":true,\"IsPaused\":false,\"production\":[");
	for (int32 I = 0; I < 20; ++I)
	{
		Rates += FString::Printf(TEXT("%s{\"Name\":\"i%d\",\"ClassName\":\"c\",\"Amount\":1,\"CurrentProd\":1,\"MaxProd\":1,\"ProdPercent\":1}"), I ? TEXT(",") : TEXT(""), I);
	}
	Rates += TEXT("]}]");
	const TSharedPtr<FJsonObject> Out = SatisDash::Json::ParseObject(FrmFactoryOf(Rates).Get(FString()));
	if (TestTrue(TEXT("rates parse"), Out.IsValid()))
	{
		TestEqual(TEXT("at most 16 rates, the first kept"), Out->GetArrayField(TEXT("buildings"))[0]->AsObject()->GetArrayField(TEXT("production")).Num(), 16);
	}
	return true;
}
