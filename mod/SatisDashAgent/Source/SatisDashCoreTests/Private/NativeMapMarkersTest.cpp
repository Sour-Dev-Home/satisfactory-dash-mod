#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SatisDash/Native/MapMarkers/NativeMapMarkers.h"
#include "TestUtil.h"

#include <limits>

using namespace SatisDash;

namespace
{
	const TCHAR* NativeMapMarkersObserved = TEXT("2026-10-27T00:00:00.000Z");

	FMapMarkerReading NativeMapMarkersReading(const FString& Id, const FString& Name, double XCm = 0, double YCm = 0, EMapMarkerKind Kind = EMapMarkerKind::Marker, int32 Icon = 1)
	{
		FMapMarkerReading Reading;
		Reading.Id = Id;
		Reading.Name = Name;
		Reading.Kind = Kind;
		Reading.Icon = Icon;
		Reading.XCm = XCm;
		Reading.YCm = YCm;
		return Reading;
	}

	FString NativeMapMarkersData(const FNativeWorldBody& Body)
	{
		return Body.Json.Mid(Body.DataFrom, Body.DataEnd - Body.DataFrom);
	}

	/** A U+1F600-style pair, built from its halves so the source encoding cannot matter. */
	FString NativeMapMarkersPair()
	{
		FString Pair;
		Pair.AppendChar(TCHAR(0xD83D));
		Pair.AppendChar(TCHAR(0xDE00));
		return Pair;
	}

	FString NativeMapMarkersId(int32 Index)
	{
		return FString::Printf(TEXT("%032X"), Index);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeMapMarkersNameTest, "SatisDash.Core.NativeMapMarkers.Name", SatisDash::Test::Flags)

bool FNativeMapMarkersNameTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("spaces trimmed"), MapMarkerName(TEXT("  a ")), FString(TEXT("a")));
	FString Wide;
	Wide.AppendChar(TCHAR(0x00A0));
	Wide.AppendChar(TCHAR(0xFEFF));
	Wide.AppendChar(TCHAR(0x2028));
	Wide += TEXT("a");
	Wide.AppendChar(TCHAR(0x2029));
	Wide.AppendChar(TCHAR(0x3000));
	TestEqual(TEXT("NBSP, BOM and line separators are JavaScript whitespace"), MapMarkerName(Wide), FString(TEXT("a")));
	TestEqual(TEXT("tab and newline trimmed, inner kept"), MapMarkerName(TEXT("\t\nb c\r\n")), FString(TEXT("b c")));

	TestEqual(TEXT("61 characters cut to 60"), MapMarkerName(FString::ChrN(61, TEXT('a'))), FString::ChrN(60, TEXT('a')));
	const FString Pair = NativeMapMarkersPair();
	TestEqual(TEXT("59 characters then a pair: the pair is dropped whole"), MapMarkerName(FString::ChrN(59, TEXT('a')) + Pair), FString::ChrN(59, TEXT('a')));
	const FString Exact = FString::ChrN(58, TEXT('a')) + Pair;
	TestEqual(TEXT("60 units ending on a pair are kept"), MapMarkerName(Exact), Exact);

	FString Lone = TEXT("x");
	Lone.AppendChar(TCHAR(0xD83D));
	Lone += TEXT("y");
	FString Fixed = TEXT("x");
	Fixed.AppendChar(TCHAR(0xFFFD));
	Fixed += TEXT("y");
	TestEqual(TEXT("a lone high surrogate becomes U+FFFD"), MapMarkerName(Lone), Fixed);
	FString LoneLow = TEXT("x");
	LoneLow.AppendChar(TCHAR(0xDE00));
	FString FixedLow = TEXT("x");
	FixedLow.AppendChar(TCHAR(0xFFFD));
	TestEqual(TEXT("a lone low surrogate becomes U+FFFD"), MapMarkerName(LoneLow), FixedLow);
	TestEqual(TEXT("empty stays empty"), MapMarkerName(FString()), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeMapMarkersColorTest, "SatisDash.Core.NativeMapMarkers.Color", SatisDash::Test::Flags)

bool FNativeMapMarkersColorTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("the sRGB conversion is the default (a flip is a visible diff)"), NativeMarkerColorSRGB);
	const FColor Green = FColor(0x6E, 0xA3, 0x7B, 0xFF);
	SatisDash::Test::SameText(*this, TEXT("sRGB round trip"), MapMarkerColorHex(FLinearColor::FromSRGBColor(Green), true), TEXT("#6ea37b"));
	const FLinearColor Plain = FLinearColor(float(Green.R) / 255.0f, float(Green.G) / 255.0f, float(Green.B) / 255.0f, 1.0f);
	SatisDash::Test::SameText(*this, TEXT("plain division round trip"), MapMarkerColorHex(Plain, false), TEXT("#6ea37b"));
	TestNotEqual(TEXT("the two modes differ for the green"), MapMarkerColorHex(FLinearColor::FromSRGBColor(Green), false), FString(TEXT("#6ea37b")));
	for (const bool bSRGB : { true, false })
	{
		SatisDash::Test::SameText(*this, TEXT("white"), MapMarkerColorHex(FLinearColor::White, bSRGB), TEXT("#ffffff"));
		SatisDash::Test::SameText(*this, TEXT("black"), MapMarkerColorHex(FLinearColor(0, 0, 0, 1), bSRGB), TEXT("#000000"));
		SatisDash::Test::SameText(*this, TEXT("alpha never appears"), MapMarkerColorHex(FLinearColor(1, 1, 1, 0), bSRGB), TEXT("#ffffff"));
	}
	const float Nan = std::numeric_limits<float>::quiet_NaN();
	SatisDash::Test::SameText(*this, TEXT("a NaN channel gives white"), MapMarkerColorHex(FLinearColor(0.2f, Nan, 0.2f, 1), true), TEXT("#ffffff"));
	SatisDash::Test::SameText(*this, TEXT("an infinite channel gives white"), MapMarkerColorHex(FLinearColor(0.2f, 0.2f, std::numeric_limits<float>::infinity(), 1), false), TEXT("#ffffff"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeMapMarkersMapTest, "SatisDash.Core.NativeMapMarkers.Map", SatisDash::Test::Flags)

bool FNativeMapMarkersMapTest::RunTest(const FString& Parameters)
{
	const FString Id = TEXT("3FABC3BA00000000000000000000AB01");
	FMapMarkerReading Marker = NativeMapMarkersReading(Id, TEXT("Alpha"), -1246855.859375, 303000, EMapMarkerKind::Marker, 660);
	Marker.Color = FLinearColor::FromSRGBColor(FColor(0x6E, 0xA3, 0x7B, 0xFF));
	const FNativeWorldBody One = BuildNativeMapMarkers(NativeMapMarkersObserved, { Marker });
	SatisDash::Test::SameText(*this, TEXT("a marker, key order id name kind icon color x y"), NativeMapMarkersData(One),
		TEXT("[{\"id\":\"3FABC3BA00000000000000000000AB01\",\"name\":\"Alpha\",\"kind\":\"marker\",\"icon\":660,\"color\":\"#6ea37b\",\"x\":-12469,\"y\":3030}]"));
	TestEqual(TEXT("one item"), One.Items, 1);
	SatisDash::Test::SameText(*this, TEXT("the whole body"), One.Json,
		FString::Printf(TEXT("{\"observedAt\":\"%s\",\"data\":%s}"), NativeMapMarkersObserved, *NativeMapMarkersData(One)));
	TestEqual(TEXT("the byte count is the body's UTF-8 length"), One.Bytes, int64(FTCHARToUTF8(*One.Json).Length()));

	const FNativeWorldBody Stamp = BuildNativeMapMarkers(NativeMapMarkersObserved, { NativeMapMarkersReading(Id, TEXT("Iron outpost"), 0, 0, EMapMarkerKind::Stamp) });
	TestTrue(TEXT("a stamp"), NativeMapMarkersData(Stamp).Contains(TEXT("\"kind\":\"stamp\"")));

	struct FRow { double Cm; int32 Metres; };
	for (const FRow& Row : { FRow{ 150, 2 }, FRow{ -150, -1 }, FRow{ 50, 1 }, FRow{ -50, 0 } })
	{
		const FNativeWorldBody Rounded = BuildNativeMapMarkers(NativeMapMarkersObserved, { NativeMapMarkersReading(TEXT("A"), TEXT("n"), Row.Cm, 0) });
		TestTrue(*FString::Printf(TEXT("%g cm"), Row.Cm), NativeMapMarkersData(Rounded).EndsWith(*FString::Printf(TEXT("\"x\":%d,\"y\":0}]"), Row.Metres)));
	}

	const FNativeWorldBody BadIcon = BuildNativeMapMarkers(NativeMapMarkersObserved, { NativeMapMarkersReading(Id, TEXT("n"), 0, 0, EMapMarkerKind::Marker, 2000000) });
	TestEqual(TEXT("an icon over the cap drops"), BadIcon.Dropped, 1);
	TestEqual(TEXT("and leaves no item"), BadIcon.Items, 0);
	const FNativeWorldBody NegIcon = BuildNativeMapMarkers(NativeMapMarkersObserved, { NativeMapMarkersReading(Id, TEXT("n"), 0, 0, EMapMarkerKind::Marker, -1) });
	TestEqual(TEXT("a negative icon drops"), NegIcon.Dropped, 1);
	const FNativeWorldBody Far = BuildNativeMapMarkers(NativeMapMarkersObserved, { NativeMapMarkersReading(Id, TEXT("n"), 1.0e12, 0) });
	TestEqual(TEXT("a position outside +/-1,000,000 m drops"), Far.Dropped, 1);

	const FNativeWorldBody Kept = BuildNativeMapMarkers(NativeMapMarkersObserved, { NativeMapMarkersReading(Id, TEXT("n")) });
	TestTrue(TEXT("a 32-digit upper-case id is kept as given"), NativeMapMarkersData(Kept).Contains(TEXT("\"id\":\"3FABC3BA00000000000000000000AB01\"")));
	const FNativeWorldBody Long = BuildNativeMapMarkers(NativeMapMarkersObserved, { NativeMapMarkersReading(FString::ChrN(250, TEXT('B')), TEXT("n")) });
	TestTrue(TEXT("a 250-character id is cut to 200"), NativeMapMarkersData(Long).Contains(*(TEXT("\"id\":\"") + FString::ChrN(200, TEXT('B')) + TEXT("\",\"name\""))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeMapMarkersRulesTest, "SatisDash.Core.NativeMapMarkers.Rules", SatisDash::Test::Flags)

bool FNativeMapMarkersRulesTest::RunTest(const FString& Parameters)
{
	const FNativeWorldBody Empty = BuildNativeMapMarkers(NativeMapMarkersObserved, {});
	SatisDash::Test::SameText(*this, TEXT("no markers: a valid empty layer"), Empty.Json, FString::Printf(TEXT("{\"observedAt\":\"%s\",\"data\":[]}"), NativeMapMarkersObserved));
	TestEqual(TEXT("no items"), Empty.Items, 0);

	TArray<FMapMarkerReading> Many;
	for (int32 I = 0; I < 1001; ++I)
	{
		Many.Add(NativeMapMarkersReading(NativeMapMarkersId(I), TEXT("n")));
	}
	const FNativeWorldBody Capped = BuildNativeMapMarkers(NativeMapMarkersObserved, Many);
	TestEqual(TEXT("1,001 valid markers keep 1,000"), Capped.Items, MapMarkersMaxItems);
	TestEqual(TEXT("and count one over"), Capped.Over, 1);

	TArray<FMapMarkerReading> Twenty;
	for (int32 I = 0; I < 20; ++I)
	{
		Twenty.Add(NativeMapMarkersReading(NativeMapMarkersId(I * 7 % 20), FString::Printf(TEXT("m%d"), I), double(I) * 123.0, double(I) * -77.0, (I % 2) ? EMapMarkerKind::Stamp : EMapMarkerKind::Marker, I));
	}
	const FString Ordered = BuildNativeMapMarkers(NativeMapMarkersObserved, Twenty).Json;
	uint32 Seed = 7;
	for (int32 Round = 0; Round < 5; ++Round)
	{
		TArray<FMapMarkerReading> Shuffled = Twenty;
		for (int32 I = Shuffled.Num() - 1; I > 0; --I)
		{
			Seed = Seed * 1664525u + 1013904223u; // a fixed linear congruential step: the same shuffles every run
			Shuffled.Swap(I, int32((Seed >> 8) % uint32(I + 1)));
		}
		SatisDash::Test::SameText(*this, TEXT("any order gives the same bytes"), BuildNativeMapMarkers(NativeMapMarkersObserved, Shuffled).Json, Ordered);
	}

	const FMapMarkerReading First = NativeMapMarkersReading(TEXT("DUP"), TEXT("one"), 0, 0);
	const FMapMarkerReading Second = NativeMapMarkersReading(TEXT("DUP"), TEXT("two"), 500, 0);
	const FNativeWorldBody Forward = BuildNativeMapMarkers(NativeMapMarkersObserved, { First, Second });
	const FNativeWorldBody Backward = BuildNativeMapMarkers(NativeMapMarkersObserved, { Second, First });
	SatisDash::Test::SameText(*this, TEXT("a repeated id gives the same bytes in both orders"), Forward.Json, Backward.Json);
	TestEqual(TEXT("and keeps one"), Forward.Items, 1);

	const FNativeWorldBody NoId = BuildNativeMapMarkers(NativeMapMarkersObserved, { NativeMapMarkersReading(FString(), TEXT("n")) });
	TestEqual(TEXT("an empty id drops"), NoId.Dropped, 1);

	// A golden in the shape of the recorded getMapMarkers output (FRM HTTP only), with synthetic names.
	FMapMarkerReading A = NativeMapMarkersReading(TEXT("BBBBBBBB000000000000000000000002"), TEXT("Alpha"), 120000, -80000, EMapMarkerKind::Marker, 660);
	A.Color = FLinearColor::FromSRGBColor(FColor(0x6E, 0xA3, 0x7B, 0xFF));
	FMapMarkerReading B = NativeMapMarkersReading(TEXT("AAAAAAAA000000000000000000000001"), TEXT("  Iron outpost "), -50000, 25050, EMapMarkerKind::Stamp, 349);
	B.Color = FLinearColor::FromSRGBColor(FColor(0xFF, 0x00, 0x80, 0xFF));
	FMapMarkerReading C = NativeMapMarkersReading(TEXT("CCCCCCCC000000000000000000000003"), FString(), 0, 0, EMapMarkerKind::Stamp, 1);
	const FNativeWorldBody Golden = BuildNativeMapMarkers(NativeMapMarkersObserved, { A, B, C });
	const FString Expected = TEXT("[")
		TEXT("{\"id\":\"AAAAAAAA000000000000000000000001\",\"name\":\"Iron outpost\",\"kind\":\"stamp\",\"icon\":349,\"color\":\"#ff0080\",\"x\":-500,\"y\":251},")
		TEXT("{\"id\":\"BBBBBBBB000000000000000000000002\",\"name\":\"Alpha\",\"kind\":\"marker\",\"icon\":660,\"color\":\"#6ea37b\",\"x\":1200,\"y\":-800},")
		TEXT("{\"id\":\"CCCCCCCC000000000000000000000003\",\"name\":\"\",\"kind\":\"stamp\",\"icon\":1,\"color\":\"#ffffff\",\"x\":0,\"y\":0}]");
	SatisDash::Test::SameText(*this, TEXT("the golden, sorted by id"), NativeMapMarkersData(Golden), Expected);
	TSharedPtr<FJsonValue> Parsed;
	TestTrue(TEXT("the whole body parses as JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Golden.Json), Parsed) && Parsed.IsValid());
	return true;
}
