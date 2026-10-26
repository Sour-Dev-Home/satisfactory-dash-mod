#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SatisDash/Native/ResourceNodes/NativeResourceNodes.h"
#include "SatisDash/FrmResourceNodes.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	FResourceNodeReading NativeNodesNode(const TCHAR* Resource, ENodePurity Purity, ENodeKind Kind, double XCm, double YCm, bool bExploited)
	{
		FResourceNodeReading R;
		R.Resource = Resource;
		R.Purity = Purity;
		R.Kind = Kind;
		R.XCm = XCm;
		R.YCm = YCm;
		R.bExploited = bExploited;
		return R;
	}

	FResourceNodeReading NativeNodesClassed(FResourceNodeReading R, const TCHAR* ClassName)
	{
		R.ClassName = ClassName;
		return R;
	}

	FString NativeNodesData(const FNativeWorldBody& Body)
	{
		return Body.Json.Mid(Body.DataFrom, Body.DataEnd - Body.DataFrom);
	}

	/** One layer item in the canonical order (x, y, type, purity, nodeType, exploited) used to compare two layers. */
	struct FNativeNodesItem
	{
		int32 X = 0;
		int32 Y = 0;
		FString Type;
		FString Purity;
		FString NodeType;
		bool bExploited = false;
		/** Empty when the record has no className. */
		FString ClassName;
	};

	FString NativeNodesKey(const FNativeNodesItem& Item)
	{
		return FString::Printf(TEXT("%d|%d|%s|%s|%s|%d|%s"), Item.X, Item.Y, *Item.Type, *Item.Purity, *Item.NodeType, Item.bExploited ? 1 : 0, *Item.ClassName);
	}

	/** The items of a layer given as the text of its data array, in canonical order, leaving out one node type (empty = none). */
	TArray<FNativeNodesItem> NativeNodesCanonical(const FString& ArrayText, const TCHAR* SkipNodeType)
	{
		TArray<FNativeNodesItem> Out;
		const FString Wrapped = FString(TEXT("{\"data\":")) + ArrayText + TEXT("}");
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Wrapped);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			return Out;
		}
		for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("data")))
		{
			const TSharedPtr<FJsonObject> Record = Value->AsObject();
			FNativeNodesItem Item;
			Item.X = int32(Record->GetNumberField(TEXT("x")));
			Item.Y = int32(Record->GetNumberField(TEXT("y")));
			Item.Type = Record->GetStringField(TEXT("type"));
			Item.Purity = Record->GetStringField(TEXT("purity"));
			Item.NodeType = Record->GetStringField(TEXT("nodeType"));
			Item.bExploited = Record->GetBoolField(TEXT("exploited"));
			Record->TryGetStringField(TEXT("className"), Item.ClassName);
			if (Item.NodeType != SkipNodeType)
			{
				Out.Add(Item);
			}
		}
		Out.Sort([](const FNativeNodesItem& A, const FNativeNodesItem& B) { return NativeNodesKey(A) < NativeNodesKey(B); });
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeNodesGoldenTest, "SatisDash.Core.NativeNodes.Golden", SatisDash::Test::Flags)

bool FNativeNodesGoldenTest::RunTest(const FString& Parameters)
{
	// The bridge's golden (frm-getResourceNode.expected.json) in the game's own units: centimetres, enum values.
	// Its "someNewType" record is an FRM-string-only case (a node type the game's EResourceNodeType can never produce),
	// so native has no counterpart for it and the comparison leaves it out.
	TArray<FResourceNodeReading> GameNodes;
	GameNodes.Add(NativeNodesNode(TEXT("Crude Oil"), ENodePurity::Impure, ENodeKind::FrackingSatellite, 50, -50, true));
	GameNodes.Add(NativeNodesNode(TEXT("Geothermal"), ENodePurity::Pure, ENodeKind::Geyser, 0, 0, false));
	GameNodes.Add(NativeNodesClassed(NativeNodesNode(TEXT("Iron Ore"), ENodePurity::Normal, ENodeKind::Node, -12345, 67890, false), TEXT("BP_ResourceNode_C")));
	const FNativeWorldBody Native = BuildNativeResourceNodes(TEXT("2026-10-25T12:00:00.000Z"), GameNodes);
	const FString GoldenText = SatisDash::Test::Fixture(TEXT("frm-getResourceNode.expected.json"));
	if (!TestFalse(TEXT("the golden was found"), GoldenText.IsEmpty()))
	{
		return false;
	}
	const TArray<FNativeNodesItem> FromNative = NativeNodesCanonical(NativeNodesData(Native), TEXT(""));
	const TArray<FNativeNodesItem> FromGolden = NativeNodesCanonical(GoldenText, TEXT("someNewType"));
	TestEqual(TEXT("three comparable records in the golden"), FromGolden.Num(), 3);
	if (TestEqual(TEXT("and as many from the game"), FromNative.Num(), FromGolden.Num()))
	{
		for (int32 I = 0; I < FromGolden.Num(); ++I)
		{
			TestEqual(*FString::Printf(TEXT("record %d equals the golden's"), I), NativeNodesKey(FromNative[I]), NativeNodesKey(FromGolden[I]));
		}
	}

	// Our own deterministic output: sorted by (x, y, resource, purity, type, exploited), four records.
	TArray<FResourceNodeReading> In = GameNodes;
	In.Add(NativeNodesNode(TEXT("Copper Ore"), ENodePurity::Pure, ENodeKind::Node, 100, 200, false));
	const FNativeWorldBody Body = BuildNativeResourceNodes(TEXT("2026-10-25T12:00:00.000Z"), In);
	TestEqual(TEXT("four items"), Body.Items, 4);
	TestEqual(TEXT("the whole body is the world body the bridge posts"), Body.Json,
		FString(TEXT("{\"observedAt\":\"2026-10-25T12:00:00.000Z\",\"data\":[")
			TEXT("{\"type\":\"Iron Ore\",\"purity\":\"normal\",\"nodeType\":\"node\",\"x\":-123,\"y\":679,\"exploited\":false,\"className\":\"BP_ResourceNode_C\"},")
			TEXT("{\"type\":\"Geothermal\",\"purity\":\"pure\",\"nodeType\":\"geyser\",\"x\":0,\"y\":0,\"exploited\":false},")
			TEXT("{\"type\":\"Crude Oil\",\"purity\":\"impure\",\"nodeType\":\"frackingSatellite\",\"x\":1,\"y\":0,\"exploited\":true},")
			TEXT("{\"type\":\"Copper Ore\",\"purity\":\"pure\",\"nodeType\":\"node\",\"x\":1,\"y\":2,\"exploited\":false}]}")));
	TestEqual(TEXT("the data starts at the array"), Body.Json[Body.DataFrom], TEXT('['));
	TestEqual(TEXT("and ends after its bracket"), Body.Json[Body.DataEnd - 1], TEXT(']'));
	TestEqual(TEXT("bytes are the UTF-8 length"), Body.Bytes, int64(Body.Json.Len()));

	// The same record text the FRM parse writes for the same values (one shared writer, so the two sources can't drift).
	FString Direct;
	AppendResourceNodeRecord(Direct, TEXT("Iron Ore"), TEXT("Normal"), TEXT("node"), -123, 679, false, TEXT(""));
	TestEqual(TEXT("the shared record writer lowercases purity"), Direct, FString(TEXT("{\"type\":\"Iron Ore\",\"purity\":\"normal\",\"nodeType\":\"node\",\"x\":-123,\"y\":679,\"exploited\":false}")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeNodesClassNameTest, "SatisDash.Core.NativeNodes.ClassName", SatisDash::Test::Flags)

bool FNativeNodesClassNameTest::RunTest(const FString& Parameters)
{
	// className follows exploited; an empty one writes no key at all (the contract's field is optional).
	TArray<FResourceNodeReading> In;
	In.Add(NativeNodesClassed(NativeNodesNode(TEXT("Iron Ore"), ENodePurity::Normal, ENodeKind::Node, 100, 100, false), TEXT("Desc_OreIron_C")));
	In.Add(NativeNodesNode(TEXT("Iron Ore"), ENodePurity::Normal, ENodeKind::Node, 200, 100, true));
	const FNativeWorldBody Body = BuildNativeResourceNodes(TEXT("T"), In);
	TestEqual(TEXT("className is written after exploited, and omitted when empty"), Body.Json,
		FString(TEXT("{\"observedAt\":\"T\",\"data\":[")
			TEXT("{\"type\":\"Iron Ore\",\"purity\":\"normal\",\"nodeType\":\"node\",\"x\":1,\"y\":1,\"exploited\":false,\"className\":\"Desc_OreIron_C\"},")
			TEXT("{\"type\":\"Iron Ore\",\"purity\":\"normal\",\"nodeType\":\"node\",\"x\":2,\"y\":1,\"exploited\":true}]}")));

	// Two nodes that differ only by className are both kept, ordered by className after the S1 key.
	TArray<FResourceNodeReading> Twins;
	Twins.Add(NativeNodesClassed(NativeNodesNode(TEXT("Iron Ore"), ENodePurity::Pure, ENodeKind::Node, 0, 0, false), TEXT("Desc_OreIron_C")));
	Twins.Add(NativeNodesClassed(NativeNodesNode(TEXT("Iron Ore"), ENodePurity::Pure, ENodeKind::Node, 0, 0, false), TEXT("Desc_OreCopper_C")));
	Twins.Add(NativeNodesNode(TEXT("Iron Ore"), ENodePurity::Pure, ENodeKind::Node, 0, 0, false));
	const FNativeWorldBody TwinBody = BuildNativeResourceNodes(TEXT("T"), Twins);
	TestEqual(TEXT("all three kept"), TwinBody.Items, 3);
	const FString TwinData = NativeNodesData(TwinBody);
	const int32 AtNone = TwinData.Find(TEXT("\"exploited\":false}"));
	const int32 AtCopper = TwinData.Find(TEXT("Desc_OreCopper_C"));
	const int32 AtIron = TwinData.Find(TEXT("Desc_OreIron_C"));
	TestTrue(TEXT("empty, then Copper, then Iron"), AtNone != INDEX_NONE && AtNone < AtCopper && AtCopper < AtIron);

	// The order the game gave them in does not change the body.
	TArray<FResourceNodeReading> Reversed;
	for (int32 I = Twins.Num() - 1; I >= 0; --I)
	{
		Reversed.Add(Twins[I]);
	}
	TestEqual(TEXT("scan order is irrelevant"), BuildNativeResourceNodes(TEXT("T"), Reversed).Json, TwinBody.Json);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeNodesRulesTest, "SatisDash.Core.NativeNodes.Rules", SatisDash::Test::Flags)

bool FNativeNodesRulesTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("nothing found is an empty layer"), BuildNativeResourceNodes(TEXT("T"), {}).Json, FString(TEXT("{\"observedAt\":\"T\",\"data\":[]}")));

	// Halves go toward +infinity, as Math.round does: 150 cm is 2 m, -150 cm is -1 m.
	TArray<FResourceNodeReading> Halves;
	Halves.Add(NativeNodesNode(TEXT("A"), ENodePurity::Pure, ENodeKind::Node, 150, -150, false));
	TestTrue(TEXT("rounding"), NativeNodesData(BuildNativeResourceNodes(TEXT("T"), Halves)).Contains(TEXT("\"x\":2,\"y\":-1")));

	// A node past +/-1,000,000 m is dropped and counted; it is not an error.
	TArray<FResourceNodeReading> Far;
	Far.Add(NativeNodesNode(TEXT("A"), ENodePurity::Pure, ENodeKind::Node, 1.0e12, 0, false));
	Far.Add(NativeNodesNode(TEXT("B"), ENodePurity::Pure, ENodeKind::Node, 0, 0, false));
	const FNativeWorldBody FarBody = BuildNativeResourceNodes(TEXT("T"), Far);
	TestEqual(TEXT("one kept"), FarBody.Items, 1);
	TestEqual(TEXT("one dropped"), FarBody.Dropped, 1);

	// The order the game gave them in does not change the body, so an unchanged world never reposts.
	TArray<FResourceNodeReading> Forward;
	for (int32 I = 0; I < 20; ++I)
	{
		Forward.Add(NativeNodesNode(*FString::Printf(TEXT("R%d"), I % 3), ENodePurity::Normal, ENodeKind::Node, I * 700, (20 - I) * 300, I % 2 == 0));
	}
	TArray<FResourceNodeReading> Backward;
	for (int32 I = Forward.Num() - 1; I >= 0; --I)
	{
		Backward.Add(Forward[I]);
	}
	TestEqual(TEXT("scan order is irrelevant"), BuildNativeResourceNodes(TEXT("T"), Forward).Json, BuildNativeResourceNodes(TEXT("T"), Backward).Json);

	// The cap: 5,001 nodes keep the first 5,000 in sorted order and count the one over.
	TArray<FResourceNodeReading> Many;
	for (int32 I = 0; I < MaxResourceNodes + 1; ++I)
	{
		Many.Add(NativeNodesNode(TEXT("Iron Ore"), ENodePurity::Normal, ENodeKind::Node, double(I) * 100.0, 0, false));
	}
	const FNativeWorldBody ManyBody = BuildNativeResourceNodes(TEXT("T"), Many);
	TestEqual(TEXT("5,000 kept"), ManyBody.Items, MaxResourceNodes);
	TestEqual(TEXT("one over"), ManyBody.Over, 1);
	TestTrue(TEXT("the one left out is the last in sorted order"), !ManyBody.Json.Contains(FString::Printf(TEXT("\"x\":%d,"), MaxResourceNodes)));

	// A long resource name is cut to 200 characters.
	TArray<FResourceNodeReading> Long;
	Long.Add(NativeNodesNode(*FString::ChrN(250, TEXT('r')), ENodePurity::Pure, ENodeKind::Node, 0, 0, false));
	TestTrue(TEXT("name cut"), NativeNodesData(BuildNativeResourceNodes(TEXT("T"), Long)).Contains(FString(TEXT("\"type\":\"")) + FString::ChrN(200, TEXT('r')) + TEXT("\",")));
	return true;
}
