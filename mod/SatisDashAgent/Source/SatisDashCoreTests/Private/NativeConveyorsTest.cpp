#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SatisDash/Native/Conveyors/NativeConveyors.h"
#include "TestUtil.h"

#include <limits>

using namespace SatisDash;

namespace
{
	const TCHAR* NativeConveyorsObserved = TEXT("2026-10-27T00:00:00.000Z");

	FConveyorReading NativeConveyorsReading(const FString& Id, EConveyorKind Kind, const FString& From, const FString& To, const FString& Tier, double Raw, double XCm, double YCm)
	{
		FConveyorReading Reading;
		Reading.Id = Id;
		Reading.Kind = Kind;
		Reading.FromId = From;
		Reading.ToId = To;
		Reading.Tier = Tier;
		Reading.RawRate = Raw;
		Reading.XCm = XCm;
		Reading.YCm = YCm;
		return Reading;
	}

	/** A valid belt with the given id. */
	FConveyorReading NativeConveyorsBelt(const FString& Id)
	{
		return NativeConveyorsReading(Id, EConveyorKind::Belt, TEXT("Build_MinerMk1_C_1"), TEXT("Build_ConstructorMk1_C_2"), TEXT("Conveyor Belt Mk.1"), 120.0, 0.0, 0.0);
	}

	TOptional<double> NativeConveyorsHalf(EConveyorKind Kind, double Raw)
	{
		return ConveyorCapacityPerMinute(Kind, Raw, TEXT("Conveyor Belt Mk.1"), ECapacityMode::SpeedHalf);
	}

	TOptional<double> NativeConveyorsTable(const TCHAR* Tier, EConveyorKind Kind = EConveyorKind::Belt)
	{
		return ConveyorCapacityPerMinute(Kind, 999.0, Tier, ECapacityMode::ClassTable);
	}

	FString NativeConveyorsData(const FNativeWorldBody& Body)
	{
		return Body.Json.Mid(Body.DataFrom, Body.DataEnd - Body.DataFrom);
	}

	FString NativeConveyorsBody(const FString& Data)
	{
		return FString::Printf(TEXT("{\"observedAt\":\"%s\",\"data\":%s}"), NativeConveyorsObserved, *Data);
	}

	/** The fixture's records (an array of objects), parsed with the JSON helpers. */
	bool NativeConveyorsParse(const TCHAR* File, TArray<TSharedPtr<FJsonValue>>& OutRecords)
	{
		const FString Source = SatisDash::Test::Fixture(File);
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Source);
		return !Source.IsEmpty() && FJsonSerializer::Deserialize(Reader, OutRecords);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeConveyorsKindTest, "SatisDash.Core.NativeConveyors.Kind", SatisDash::Test::Flags)

bool FNativeConveyorsKindTest::RunTest(const FString& Parameters)
{
	struct FRow { const TCHAR* Id; EConveyorNodeKind Kind; };
	const FRow Rows[] = {
		{ TEXT("Build_ConveyorAttachmentSplitterSmart_C_12"), EConveyorNodeKind::Splitter },
		{ TEXT("Build_ConveyorAttachmentSplitterSmartLift_C_12"), EConveyorNodeKind::Splitter },
		{ TEXT("Build_ConveyorAttachmentMerger_C_5"), EConveyorNodeKind::Merger },
		{ TEXT("Build_ConveyorBeltMk3_C_77"), EConveyorNodeKind::Conveyor },
		{ TEXT("Build_ConveyorLiftMk2_C_3"), EConveyorNodeKind::Conveyor },
		{ TEXT("Build_Pipeline_C_9"), EConveyorNodeKind::Conveyor },
		{ TEXT("Build_PipelineMK2_C_9"), EConveyorNodeKind::Conveyor },
		{ TEXT("Build_PipelineJunction_Cross_C_4"), EConveyorNodeKind::Junction },
		{ TEXT("Build_PipelinePump_C_8"), EConveyorNodeKind::Junction },
		{ TEXT("Build_MinerMk2_C_6"), EConveyorNodeKind::Extractor },
		{ TEXT("Build_WaterPump_C_1"), EConveyorNodeKind::Extractor },
		{ TEXT("Build_GeneratorCoal_C_2"), EConveyorNodeKind::Consumer },
		{ TEXT("Build_ResourceSink_C_1"), EConveyorNodeKind::Consumer },
		{ TEXT("Build_StorageContainerMk1_C_3"), EConveyorNodeKind::Storage },
		{ TEXT("Build_IndustrialTank_C_1"), EConveyorNodeKind::Storage },
		{ TEXT("Build_DroneStation_C_2146572476"), EConveyorNodeKind::Station },
		{ TEXT("Build_TrainDockingStation_C_4"), EConveyorNodeKind::Station },
		{ TEXT("Build_ConstructorMk1_C_10"), EConveyorNodeKind::Machine },
		{ TEXT("Build_OilRefinery_C_11"), EConveyorNodeKind::Machine },
		{ TEXT("Build_Foo_C_1"), EConveyorNodeKind::Other },
		{ TEXT("Build_Assembler_C"), EConveyorNodeKind::Machine },
		{ TEXT("build_conveyorbeltmk1_c_1"), EConveyorNodeKind::Conveyor },
		{ TEXT("build_miner_c_1"), EConveyorNodeKind::Other },
		{ TEXT(""), EConveyorNodeKind::Other },
		// Every prefix of the adapter's table (conveyorsMapper.ts classifyConveyorEnd), one class name each; expected kinds are the adapter's rules.
		{ TEXT("Build_ConveyorBeltMk1_C_1"), EConveyorNodeKind::Conveyor },
		{ TEXT("Build_ConveyorLiftMk1_C_1"), EConveyorNodeKind::Conveyor },
		{ TEXT("Build_Pipeline_C_1"), EConveyorNodeKind::Conveyor },
		{ TEXT("Build_PipelineMK2_C_1"), EConveyorNodeKind::Conveyor },
		{ TEXT("Build_PipelineJunction_Cross_C_1"), EConveyorNodeKind::Junction },
		{ TEXT("Build_PipelinePump_C_1"), EConveyorNodeKind::Junction },
		{ TEXT("Build_MinerMk1_C_1"), EConveyorNodeKind::Extractor },
		{ TEXT("Build_WaterPump_C_1"), EConveyorNodeKind::Extractor },
		{ TEXT("Build_OilPump_C_1"), EConveyorNodeKind::Extractor },
		{ TEXT("Build_FrackingExtractor_C_1"), EConveyorNodeKind::Extractor },
		{ TEXT("Build_GeneratorBiomass_Automated_C_1"), EConveyorNodeKind::Consumer },
		{ TEXT("Build_ResourceSink_C_1"), EConveyorNodeKind::Consumer },
		{ TEXT("Build_SpaceElevator_C_1"), EConveyorNodeKind::Consumer },
		{ TEXT("Build_StorageContainerMk2_C_1"), EConveyorNodeKind::Storage },
		{ TEXT("Build_CentralStorage_C_1"), EConveyorNodeKind::Storage },
		{ TEXT("Build_IndustrialTank_C_1"), EConveyorNodeKind::Storage },
		{ TEXT("Build_PipeStorageTank_C_1"), EConveyorNodeKind::Storage },
		{ TEXT("Build_FluidBuffer_C_1"), EConveyorNodeKind::Storage },
		{ TEXT("Build_TrainDockingStation_C_1"), EConveyorNodeKind::Station },
		{ TEXT("Build_TrainStation_C_1"), EConveyorNodeKind::Station },
		{ TEXT("Build_DroneStation_C_2146572476"), EConveyorNodeKind::Station },
		{ TEXT("Build_TruckStation_C_1"), EConveyorNodeKind::Station },
		{ TEXT("Build_ConstructorMk1_C_1"), EConveyorNodeKind::Machine },
		{ TEXT("Build_AssemblerMk1_C_1"), EConveyorNodeKind::Machine },
		{ TEXT("Build_ManufacturerMk1_C_1"), EConveyorNodeKind::Machine },
		{ TEXT("Build_SmelterMk1_C_1"), EConveyorNodeKind::Machine },
		{ TEXT("Build_FoundryMk1_C_1"), EConveyorNodeKind::Machine },
		{ TEXT("Build_Packager_C_1"), EConveyorNodeKind::Machine },
		{ TEXT("Build_OilRefinery_C_1"), EConveyorNodeKind::Machine },
		{ TEXT("Build_Blender_C_1"), EConveyorNodeKind::Machine },
		{ TEXT("Build_HadronCollider_C_1"), EConveyorNodeKind::Machine },
		{ TEXT("Build_Converter_C_1"), EConveyorNodeKind::Machine },
		{ TEXT("Build_QuantumEncoder_C_1"), EConveyorNodeKind::Machine },
		{ TEXT("Build_ConveyorAttachmentSplitter_C_1"), EConveyorNodeKind::Splitter },
		{ TEXT("Build_ConveyorAttachmentMerger_C_1"), EConveyorNodeKind::Merger },
	};
	for (const FRow& Row : Rows)
	{
		TestEqual(*FString::Printf(TEXT("kind of '%s'"), Row.Id), FString(ConveyorNodeKindText(ClassifyConveyorEnd(Row.Id))), FString(ConveyorNodeKindText(Row.Kind)));
	}
	// Only one trailing number is removed, and an underscore must come first.
	TestTrue(TEXT("Build_Pipeline_C_9_8 keeps _9 so it is not the pipeline"), ClassifyConveyorEnd(TEXT("Build_Pipeline_C_9_8")) == EConveyorNodeKind::Other);
	TestTrue(TEXT("digits with no underscore stay"), ClassifyConveyorEnd(TEXT("Build_Pipeline_C9")) == EConveyorNodeKind::Other);

	const EConveyorNodeKind All[] = { EConveyorNodeKind::Machine, EConveyorNodeKind::Splitter, EConveyorNodeKind::Merger, EConveyorNodeKind::Storage, EConveyorNodeKind::Station,
		EConveyorNodeKind::Extractor, EConveyorNodeKind::Consumer, EConveyorNodeKind::Junction, EConveyorNodeKind::Conveyor, EConveyorNodeKind::Other };
	const TCHAR* Texts[] = { TEXT("machine"), TEXT("splitter"), TEXT("merger"), TEXT("storage"), TEXT("station"), TEXT("extractor"), TEXT("consumer"), TEXT("junction"), TEXT("conveyor"), TEXT("other") };
	for (int32 I = 0; I < 10; ++I)
	{
		TestEqual(*FString::Printf(TEXT("text %d"), I), FString(ConveyorNodeKindText(All[I])), FString(Texts[I]));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeConveyorsCapacityTest, "SatisDash.Core.NativeConveyors.Capacity", SatisDash::Test::Flags)

bool FNativeConveyorsCapacityTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("the default mode is SpeedHalf"), NativeConveyorCapacityMode == ECapacityMode::SpeedHalf);
	const double Nan = std::numeric_limits<double>::quiet_NaN();
	TestEqual(TEXT("belt 120"), NativeConveyorsHalf(EConveyorKind::Belt, 120.0).Get(-1.0), 60.0);
	TestEqual(TEXT("belt 240"), NativeConveyorsHalf(EConveyorKind::Belt, 240.0).Get(-1.0), 120.0);
	TestEqual(TEXT("belt 540"), NativeConveyorsHalf(EConveyorKind::Belt, 540.0).Get(-1.0), 270.0);
	TestEqual(TEXT("belt 960"), NativeConveyorsHalf(EConveyorKind::Belt, 960.0).Get(-1.0), 480.0);
	TestEqual(TEXT("lift 960"), NativeConveyorsHalf(EConveyorKind::Lift, 960.0).Get(-1.0), 480.0);
	TestFalse(TEXT("NaN is unset"), NativeConveyorsHalf(EConveyorKind::Belt, Nan).IsSet());
	TestFalse(TEXT("-2 is unset"), NativeConveyorsHalf(EConveyorKind::Belt, -2.0).IsSet());
	TestFalse(TEXT("+inf is unset"), NativeConveyorsHalf(EConveyorKind::Belt, std::numeric_limits<double>::infinity()).IsSet());

	TestEqual(TEXT("table Belt Mk.1"), NativeConveyorsTable(TEXT("Conveyor Belt Mk.1")).Get(-1.0), 60.0);
	TestEqual(TEXT("table Belt Mk.2"), NativeConveyorsTable(TEXT("Conveyor Belt Mk.2")).Get(-1.0), 120.0);
	TestEqual(TEXT("table Belt Mk.3"), NativeConveyorsTable(TEXT("Conveyor Belt Mk.3")).Get(-1.0), 270.0);
	TestEqual(TEXT("table Lift Mk.4"), NativeConveyorsTable(TEXT("Conveyor Lift Mk.4"), EConveyorKind::Lift).Get(-1.0), 480.0);
	TestFalse(TEXT("table Belt Mk.6"), NativeConveyorsTable(TEXT("Conveyor Belt Mk.6")).IsSet());
	TestFalse(TEXT("table Belt Mk.10 is not Mk.1"), NativeConveyorsTable(TEXT("Conveyor Belt Mk.10")).IsSet());
	TestFalse(TEXT("table Pipeline (no Mk.)"), NativeConveyorsTable(TEXT("Pipeline")).IsSet());
	TestFalse(TEXT("table 'Mk.' with no number"), NativeConveyorsTable(TEXT("Belt Mk.")).IsSet());

	for (const ECapacityMode Mode : { ECapacityMode::SpeedHalf, ECapacityMode::ClassTable })
	{
		TestEqual(TEXT("pipe 5 -> 300"), ConveyorCapacityPerMinute(EConveyorKind::Pipe, 5.0, TEXT("Pipeline Mk.1"), Mode).Get(-1.0), 300.0);
		TestEqual(TEXT("pipe 10 -> 600"), ConveyorCapacityPerMinute(EConveyorKind::Pipe, 10.0, TEXT("Pipeline Mk.2"), Mode).Get(-1.0), 600.0);
		TestEqual(TEXT("pipe 4.99999 -> 299.9994"), ConveyorCapacityPerMinute(EConveyorKind::Pipe, 4.99999, TEXT("Pipeline Mk.1"), Mode).Get(-1.0), 299.9994);
		TestFalse(TEXT("pipe NaN"), ConveyorCapacityPerMinute(EConveyorKind::Pipe, Nan, TEXT("Pipeline Mk.1"), Mode).IsSet());
	}
	TestEqual(TEXT("a zero is 0, not -0"), 1.0 / NativeConveyorsHalf(EConveyorKind::Belt, -0.0).Get(-1.0) > 0.0, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeConveyorsMapTest, "SatisDash.Core.NativeConveyors.Map", SatisDash::Test::Flags)

bool FNativeConveyorsMapTest::RunTest(const FString& Parameters)
{
	const FNativeWorldBody Miner = BuildNativeConveyors(NativeConveyorsObserved, { NativeConveyorsReading(TEXT("B1"), EConveyorKind::Belt, TEXT("Build_MinerMk1_C_7"), TEXT("Build_ConstructorMk1_C_8"),
		TEXT("Conveyor Belt Mk.1"), 120.0, -5555497.7, -12860636.0) });
	SatisDash::Test::SameText(*this, TEXT("a miner-fed belt, exact bytes"), NativeConveyorsData(Miner),
		TEXT("[{\"id\":\"B1\",\"kind\":\"belt\",\"from\":\"Build_MinerMk1_C_7\",\"fromKind\":\"extractor\",\"to\":\"Build_ConstructorMk1_C_8\",\"toKind\":\"machine\",\"capacityPerMinute\":60,\"tier\":\"Conveyor Belt Mk.1\",\"at\":{\"x\":-55555,\"y\":-128606}}]"));
	SatisDash::Test::SameText(*this, TEXT("the whole body, with observedAt"), Miner.Json, NativeConveyorsBody(NativeConveyorsData(Miner)));
	TestEqual(TEXT("one item"), Miner.Items, 1);
	TestEqual(TEXT("the byte count is the body's UTF-8 length"), Miner.Bytes, int64(FTCHARToUTF8(*Miner.Json).Length()));

	const FNativeWorldBody Open = BuildNativeConveyors(NativeConveyorsObserved, { NativeConveyorsReading(TEXT("B2"), EConveyorKind::Belt, TEXT("Build_ConveyorAttachmentSplitter_C_1"), TEXT(""),
		TEXT("Conveyor Belt Mk.1"), 120.0, 0.0, 0.0) });
	SatisDash::Test::SameText(*this, TEXT("an open end is null, null"), NativeConveyorsData(Open),
		TEXT("[{\"id\":\"B2\",\"kind\":\"belt\",\"from\":\"Build_ConveyorAttachmentSplitter_C_1\",\"fromKind\":\"splitter\",\"to\":null,\"toKind\":null,\"capacityPerMinute\":60,\"tier\":\"Conveyor Belt Mk.1\",\"at\":{\"x\":0,\"y\":0}}]"));
	const FNativeWorldBody OpenFrom = BuildNativeConveyors(NativeConveyorsObserved, { NativeConveyorsReading(TEXT("B3"), EConveyorKind::Lift, TEXT(""), TEXT("Build_StorageContainerMk1_C_1"),
		TEXT("Conveyor Lift Mk.2"), 240.0, 0.0, 0.0) });
	SatisDash::Test::SameText(*this, TEXT("an open end 0 on a lift"), NativeConveyorsData(OpenFrom),
		TEXT("[{\"id\":\"B3\",\"kind\":\"lift\",\"from\":null,\"fromKind\":null,\"to\":\"Build_StorageContainerMk1_C_1\",\"toKind\":\"storage\",\"capacityPerMinute\":120,\"tier\":\"Conveyor Lift Mk.2\",\"at\":{\"x\":0,\"y\":0}}]"));

	const FNativeWorldBody Pipe = BuildNativeConveyors(NativeConveyorsObserved, { NativeConveyorsReading(TEXT("P1"), EConveyorKind::Pipe, TEXT("Build_WaterPump_C_1"), TEXT("Build_PipelineJunction_Cross_C_2"),
		TEXT("Pipeline Mk.1"), 5.0, 0.0, 0.0) });
	SatisDash::Test::SameText(*this, TEXT("a pipe, 5 m3/s is 300 a minute"), NativeConveyorsData(Pipe),
		TEXT("[{\"id\":\"P1\",\"kind\":\"pipe\",\"from\":\"Build_WaterPump_C_1\",\"fromKind\":\"extractor\",\"to\":\"Build_PipelineJunction_Cross_C_2\",\"toKind\":\"junction\",\"capacityPerMinute\":300,\"tier\":\"Pipeline Mk.1\",\"at\":{\"x\":0,\"y\":0}}]"));

	const FNativeWorldBody Unset = BuildNativeConveyors(NativeConveyorsObserved, { NativeConveyorsReading(TEXT("B4"), EConveyorKind::Belt, TEXT("Build_Foo_C_1"), TEXT("Build_Foo_C_2"),
		TEXT("Conveyor Belt Mk.1"), std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0) });
	SatisDash::Test::SameText(*this, TEXT("an unset capacity is null, an unknown class is other"), NativeConveyorsData(Unset),
		TEXT("[{\"id\":\"B4\",\"kind\":\"belt\",\"from\":\"Build_Foo_C_1\",\"fromKind\":\"other\",\"to\":\"Build_Foo_C_2\",\"toKind\":\"other\",\"capacityPerMinute\":null,\"tier\":\"Conveyor Belt Mk.1\",\"at\":{\"x\":0,\"y\":0}}]"));

	struct FRow { double Cm; int32 Metres; };
	for (const FRow& Row : { FRow{ 150, 2 }, FRow{ -150, -1 }, FRow{ 50, 1 }, FRow{ -50, 0 }, FRow{ 249, 2 }, FRow{ 250, 3 } })
	{
		const FNativeWorldBody One = BuildNativeConveyors(NativeConveyorsObserved, { NativeConveyorsReading(TEXT("R"), EConveyorKind::Belt, TEXT(""), TEXT(""), TEXT("T"), 120.0, Row.Cm, Row.Cm) });
		TestTrue(*FString::Printf(TEXT("%g cm is %d m, never -0"), Row.Cm, Row.Metres),
			NativeConveyorsData(One).EndsWith(FString::Printf(TEXT("\"at\":{\"x\":%d,\"y\":%d}}]"), Row.Metres, Row.Metres)));
	}

	const FNativeWorldBody Empty = BuildNativeConveyors(NativeConveyorsObserved, {});
	SatisDash::Test::SameText(*this, TEXT("no edges: a valid empty layer"), Empty.Json, NativeConveyorsBody(TEXT("[]")));
	TestEqual(TEXT("no items"), Empty.Items, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeConveyorsRulesTest, "SatisDash.Core.NativeConveyors.Rules", SatisDash::Test::Flags)

bool FNativeConveyorsRulesTest::RunTest(const FString& Parameters)
{
	TArray<FConveyorReading> Many;
	for (int32 I = 0; I < ConveyorsMaxEdges + 1; ++I)
	{
		Many.Add(NativeConveyorsBelt(FString::Printf(TEXT("E%05d"), I)));
	}
	const FNativeWorldBody Capped = BuildNativeConveyors(NativeConveyorsObserved, Many);
	TestEqual(TEXT("50,000 kept"), Capped.Items, ConveyorsMaxEdges);
	TestEqual(TEXT("one over"), Capped.Over, 1);
	TestFalse(TEXT("the one sorting last is the one cut"), Capped.Json.Contains(FString::Printf(TEXT("\"id\":\"E%05d\""), ConveyorsMaxEdges)));

	TArray<FConveyorReading> Twenty;
	for (int32 I = 0; I < 20; ++I)
	{
		Twenty.Add(NativeConveyorsBelt(FString::Printf(TEXT("T%d"), (I * 7) % 20)));
	}
	TArray<FConveyorReading> Shuffled;
	for (int32 I = 0; I < 20; ++I)
	{
		Shuffled.Add(Twenty[(I * 3 + 1) % 20]);
	}
	SatisDash::Test::SameText(*this, TEXT("shuffled readings, the same bytes"), BuildNativeConveyors(NativeConveyorsObserved, Shuffled).Json, BuildNativeConveyors(NativeConveyorsObserved, Twenty).Json);

	// Code-unit order: "B10" before "B2", uppercase before lowercase.
	const FNativeWorldBody Ordered = BuildNativeConveyors(NativeConveyorsObserved, { NativeConveyorsBelt(TEXT("b")), NativeConveyorsBelt(TEXT("B2")), NativeConveyorsBelt(TEXT("B10")) });
	const int32 At10 = Ordered.Json.Find(TEXT("\"id\":\"B10\""));
	const int32 At2 = Ordered.Json.Find(TEXT("\"id\":\"B2\""));
	const int32 AtLower = Ordered.Json.Find(TEXT("\"id\":\"b\""));
	TestTrue(TEXT("B10 < B2 < b"), At10 != INDEX_NONE && At10 < At2 && At2 < AtLower);

	// A repeated id: the one with the lowest (from, to, tier) stays, in either input order.
	FConveyorReading A = NativeConveyorsBelt(TEXT("D"));
	FConveyorReading B = NativeConveyorsBelt(TEXT("D"));
	B.FromId = TEXT("Build_MinerMk1_C_0");
	const FNativeWorldBody AB = BuildNativeConveyors(NativeConveyorsObserved, { A, B });
	const FNativeWorldBody BA = BuildNativeConveyors(NativeConveyorsObserved, { B, A });
	SatisDash::Test::SameText(*this, TEXT("a duplicate id gives the same bytes in both orders"), AB.Json, BA.Json);
	TestEqual(TEXT("one item"), AB.Items, 1);
	TestTrue(TEXT("the lower from stays"), AB.Json.Contains(TEXT("Build_MinerMk1_C_0")));

	const FString Wide = FString::ChrN(129, TEXT('a'));
	FConveyorReading LongId = NativeConveyorsBelt(Wide);
	FConveyorReading LongEnd = NativeConveyorsBelt(TEXT("X"));
	LongEnd.ToId = Wide;
	FConveyorReading NoTier = NativeConveyorsBelt(TEXT("Y"));
	NoTier.Tier = TEXT("");
	FConveyorReading Far = NativeConveyorsBelt(TEXT("Z"));
	Far.XCm = 1.0e12;
	FConveyorReading NoId = NativeConveyorsBelt(TEXT(""));
	const FNativeWorldBody Dropped = BuildNativeConveyors(NativeConveyorsObserved, { LongId, LongEnd, NoTier, Far, NoId, NativeConveyorsBelt(TEXT("OK")) });
	TestEqual(TEXT("five dropped"), Dropped.Dropped, 5);
	TestEqual(TEXT("one kept"), Dropped.Items, 1);
	const FNativeWorldBody Edge128 = BuildNativeConveyors(NativeConveyorsObserved, { NativeConveyorsBelt(FString::ChrN(128, TEXT('a'))) });
	TestEqual(TEXT("a 128-character id is kept"), Edge128.Items, 1);

	FConveyorReading LongTier = NativeConveyorsBelt(TEXT("L"));
	LongTier.Tier = FString::ChrN(65, TEXT('t'));
	TestTrue(TEXT("a 65-character tier is cut to 64"), NativeConveyorsData(BuildNativeConveyors(NativeConveyorsObserved, { LongTier })).Contains(
		TEXT("\"tier\":\"") + FString::ChrN(64, TEXT('t')) + TEXT("\",\"at\"")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeConveyorsGoldenTest, "SatisDash.Core.NativeConveyors.Golden", SatisDash::Test::Flags)

bool FNativeConveyorsGoldenTest::RunTest(const FString& Parameters)
{
	// The readings the game would give for the recorded FRM records: ConnectedTo0/1 are the end ids, Name the tier, location0 the end-0 position (cm).
	// A belt's or lift's ItemsPerMinute x 2 is the raw speed (SpeedHalf); a pipe's Speed (m3/s) is the raw rate.
	TArray<FConveyorReading> Readings;
	struct FFile { const TCHAR* Name; EConveyorKind Kind; int32 Count; };
	for (const FFile& File : { FFile{ TEXT("frm-getBelts.json"), EConveyorKind::Belt, 7 }, FFile{ TEXT("frm-getLifts.json"), EConveyorKind::Lift, 2 }, FFile{ TEXT("frm-getPipes.json"), EConveyorKind::Pipe, 2 } })
	{
		TArray<TSharedPtr<FJsonValue>> Records;
		if (!TestTrue(*FString::Printf(TEXT("%s was found and parses as an array"), File.Name), NativeConveyorsParse(File.Name, Records)))
		{
			return false;
		}
		TestEqual(*FString::Printf(TEXT("%s record count"), File.Name), Records.Num(), File.Count);
		for (const TSharedPtr<FJsonValue>& Value : Records)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			FConveyorReading Reading;
			Reading.Id = Object->GetStringField(TEXT("ID"));
			Reading.Kind = File.Kind;
			Reading.FromId = Object->GetStringField(TEXT("ConnectedTo0"));
			Reading.ToId = Object->GetStringField(TEXT("ConnectedTo1"));
			Reading.Tier = Object->GetStringField(TEXT("Name"));
			Reading.RawRate = File.Kind == EConveyorKind::Pipe ? Object->GetNumberField(TEXT("Speed")) : Object->GetNumberField(TEXT("ItemsPerMinute")) * 2.0;
			Reading.XCm = Object->GetObjectField(TEXT("location0"))->GetNumberField(TEXT("x"));
			Reading.YCm = Object->GetObjectField(TEXT("location0"))->GetNumberField(TEXT("y"));
			Readings.Add(MoveTemp(Reading));
		}
	}
	TestEqual(TEXT("eleven records in all"), Readings.Num(), 11);

	// Written out by hand from the captures and the adapter's rules (conveyorsMapper.ts, and the expectations in its test): the ids sort by
	// code unit; the end kinds come from the class part of each end id; at is end 0 in whole metres (Math.round of cm / 100), so
	// -55554.97 cm -> -556, -125998.9 -> -1260, -126950.21 -> -1270, 9960 -> 100, -62999.9999999999 -> -630, -37099.99 -> -371;
	// the capacity is the capture's ItemsPerMinute (60/480/270) and a pipe's Speed 5 x 60 = 300.
	const FString Expected = FString(TEXT("["))
		+ TEXT("{\"id\":\"Build_ConveyorBeltMk1_C_2146952248\",\"kind\":\"belt\",\"from\":\"Build_ConveyorAttachmentSplitter_C_2146955812\",\"fromKind\":\"splitter\",\"to\":\"Build_GeneratorBiomass_Automated_C_2146972747\",\"toKind\":\"consumer\",\"capacityPerMinute\":60,\"tier\":\"Conveyor Belt Mk.1\",\"at\":{\"x\":-544,\"y\":-1432}},")
		+ TEXT("{\"id\":\"Build_ConveyorBeltMk1_C_2146953393\",\"kind\":\"belt\",\"from\":\"Build_ConveyorAttachmentSplitter_C_2146953396\",\"fromKind\":\"splitter\",\"to\":null,\"toKind\":null,\"capacityPerMinute\":60,\"tier\":\"Conveyor Belt Mk.1\",\"at\":{\"x\":-600,\"y\":-1431}},")
		+ TEXT("{\"id\":\"Build_ConveyorBeltMk1_C_2146977182\",\"kind\":\"belt\",\"from\":\"Build_ConveyorBeltMk1_C_2146979166\",\"fromKind\":\"conveyor\",\"to\":\"Build_ConveyorBeltMk1_C_2146976023\",\"toKind\":\"conveyor\",\"capacityPerMinute\":60,\"tier\":\"Conveyor Belt Mk.1\",\"at\":{\"x\":-534,\"y\":-1270}},")
		+ TEXT("{\"id\":\"Build_ConveyorBeltMk1_C_2146979166\",\"kind\":\"belt\",\"from\":\"Build_ConveyorAttachmentMerger_C_2146982118\",\"fromKind\":\"merger\",\"to\":\"Build_ConveyorBeltMk1_C_2146977182\",\"toKind\":\"conveyor\",\"capacityPerMinute\":60,\"tier\":\"Conveyor Belt Mk.1\",\"at\":{\"x\":-553,\"y\":-1260}},")
		+ TEXT("{\"id\":\"Build_ConveyorBeltMk1_C_2146994301\",\"kind\":\"belt\",\"from\":\"Build_MinerMk1_C_2147011597\",\"fromKind\":\"extractor\",\"to\":\"Build_ConveyorAttachmentSplitter_C_2146996403\",\"toKind\":\"splitter\",\"capacityPerMinute\":60,\"tier\":\"Conveyor Belt Mk.1\",\"at\":{\"x\":-556,\"y\":-1286}},")
		+ TEXT("{\"id\":\"Build_ConveyorBeltMk1_C_2146995466\",\"kind\":\"belt\",\"from\":\"Build_ConveyorAttachmentSplitter_C_2146996403\",\"fromKind\":\"splitter\",\"to\":\"Build_ConstructorMk1_C_2147002363\",\"toKind\":\"machine\",\"capacityPerMinute\":60,\"tier\":\"Conveyor Belt Mk.1\",\"at\":{\"x\":-554,\"y\":-1278}},")
		+ TEXT("{\"id\":\"Build_ConveyorBeltMk4_C_2147211837\",\"kind\":\"belt\",\"from\":\"Build_ConveyorAttachmentMerger_C_2147222312\",\"fromKind\":\"merger\",\"to\":\"Build_ConveyorAttachmentMerger_C_2147381846\",\"toKind\":\"merger\",\"capacityPerMinute\":480,\"tier\":\"Conveyor Belt Mk.4\",\"at\":{\"x\":602,\"y\":-630}},")
		+ TEXT("{\"id\":\"Build_ConveyorLiftMk1_C_2146826510\",\"kind\":\"lift\",\"from\":\"Build_OilRefinery_C_2146845148\",\"fromKind\":\"machine\",\"to\":\"Build_ConveyorBeltMk3_C_2146694474\",\"toKind\":\"conveyor\",\"capacityPerMinute\":60,\"tier\":\"Conveyor Lift Mk.1\",\"at\":{\"x\":-367,\"y\":-1906}},")
		+ TEXT("{\"id\":\"Build_ConveyorLiftMk3_C_2147447582\",\"kind\":\"lift\",\"from\":\"Build_StorageContainerMk2_C_2146542989\",\"fromKind\":\"storage\",\"to\":\"Build_CentralStorage_C_2147449429\",\"toKind\":\"storage\",\"capacityPerMinute\":270,\"tier\":\"Conveyor Lift Mk.3\",\"at\":{\"x\":-402,\"y\":-1448}},")
		+ TEXT("{\"id\":\"Build_Pipeline_C_2146766654\",\"kind\":\"pipe\",\"from\":\"Build_PipelineJunction_Cross_C_2146766657\",\"fromKind\":\"junction\",\"to\":null,\"toKind\":null,\"capacityPerMinute\":300,\"tier\":\"Pipeline Mk.1\",\"at\":{\"x\":-371,\"y\":-1879}},")
		+ TEXT("{\"id\":\"Build_Pipeline_C_2147376300\",\"kind\":\"pipe\",\"from\":\"Build_WaterPump_C_2147439854\",\"fromKind\":\"extractor\",\"to\":\"Build_PipelineJunction_Cross_C_2147377123\",\"toKind\":\"junction\",\"capacityPerMinute\":300,\"tier\":\"Pipeline Mk.1\",\"at\":{\"x\":100,\"y\":30}}")
		+ TEXT("]");

	const FNativeWorldBody Body = BuildNativeConveyors(NativeConveyorsObserved, Readings);
	TestEqual(TEXT("eleven kept"), Body.Items, 11);
	TestEqual(TEXT("none dropped"), Body.Dropped, 0);
	SatisDash::Test::SameText(*this, TEXT("is the adapter's layer for the captures"), NativeConveyorsData(Body), Expected);

	// The records in the reverse order give the same bytes.
	TArray<FConveyorReading> Reversed;
	for (int32 I = Readings.Num() - 1; I >= 0; --I)
	{
		Reversed.Add(Readings[I]);
	}
	SatisDash::Test::SameText(*this, TEXT("sort-insensitive"), BuildNativeConveyors(NativeConveyorsObserved, Reversed).Json, Body.Json);
	return true;
}
