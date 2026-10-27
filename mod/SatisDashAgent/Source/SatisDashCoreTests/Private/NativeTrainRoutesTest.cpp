#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SatisDash/Native/TrainRoutes/NativeTrainRoutes.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	const TCHAR* NativeRoutesObserved = TEXT("2026-10-26T00:00:00.000Z");

	FPlatformReading NativeRoutesPlatform(const TCHAR* Id, bool bLoad, std::initializer_list<const TCHAR*> Items)
	{
		FPlatformReading P;
		P.Id = Id;
		P.bLoad = bLoad;
		for (const TCHAR* Item : Items)
		{
			P.Items.Add(Item);
		}
		return P;
	}

	FStationReading NativeRoutesStation(const TCHAR* Id, const TCHAR* Name, TArray<FPlatformReading> Platforms)
	{
		FStationReading S;
		S.Id = Id;
		S.Name = Name;
		S.Platforms = MoveTemp(Platforms);
		return S;
	}

	FTrainReading NativeRoutesTrain(const TCHAR* Id, const TCHAR* Name, std::initializer_list<const TCHAR*> Stops, std::initializer_list<const TCHAR*> Items)
	{
		FTrainReading T;
		T.Id = Id;
		T.Name = Name;
		// A Parked train sends no stops, so the fixtures that check stops run self-driving.
		T.State = ETrainState::SelfDriving;
		for (const TCHAR* Stop : Stops)
		{
			T.Stops.Add(Stop);
		}
		for (const TCHAR* Item : Items)
		{
			T.Items.Add(Item);
		}
		return T;
	}

	/** The data array of a body, parsed; empty when the body is not {"observedAt":string,"data":array}. */
	bool NativeRoutesParse(const FNativeWorldBody& Body, TArray<TSharedPtr<FJsonValue>>& OutData)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body.Json);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid() || Root->Values.Num() != 2)
		{
			return false;
		}
		FString Observed;
		if (!Root->TryGetStringField(TEXT("observedAt"), Observed) || Root->GetArrayField(TEXT("data")).Num() < 0)
		{
			return false;
		}
		OutData = Root->GetArrayField(TEXT("data"));
		return true;
	}

	/** True when Object has exactly the string-typed keys in Keys and no others (z.object without extras). */
	bool NativeRoutesKeys(const TSharedPtr<FJsonObject>& Object, std::initializer_list<const TCHAR*> Keys)
	{
		if (!Object.IsValid() || Object->Values.Num() != int32(Keys.size()))
		{
			return false;
		}
		for (const TCHAR* Key : Keys)
		{
			if (!Object->HasField(Key))
			{
				return false;
			}
		}
		return true;
	}

	/** z.array(z.string().min(1).max(200)).max(Max). */
	bool NativeRoutesStrings(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, int32 Max, int32 Min)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!Object->TryGetArrayField(Field, Array) || Array->Num() > Max)
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Array)
		{
			FString Text;
			if (!Value->TryGetString(Text) || Text.Len() < Min || Text.Len() > 200)
			{
				return false;
			}
		}
		return true;
	}

	/** TrainRoutesLayerDataSchema (harness/contract/src/map.ts) checked by hand: the field set, the types and the caps. */
	bool NativeRoutesFitsSchema(const TArray<TSharedPtr<FJsonValue>>& Data)
	{
		if (Data.Num() > 4000)
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : Data)
		{
			const TSharedPtr<FJsonObject> Record = Value->AsObject();
			FString Type, Id, Name;
			if (!Record.IsValid() || !Record->TryGetStringField(TEXT("recordType"), Type) || !Record->TryGetStringField(TEXT("id"), Id) || !Record->TryGetStringField(TEXT("name"), Name) || Id.Len() > 200 || Name.Len() > 200)
			{
				return false;
			}
			if (Type == TEXT("station"))
			{
				const TArray<TSharedPtr<FJsonValue>>* Platforms = nullptr;
				if (!NativeRoutesKeys(Record, { TEXT("recordType"), TEXT("id"), TEXT("name"), TEXT("platforms") }) || !Record->TryGetArrayField(TEXT("platforms"), Platforms) || Platforms->Num() > 64)
				{
					return false;
				}
				for (const TSharedPtr<FJsonValue>& PlatformValue : *Platforms)
				{
					const TSharedPtr<FJsonObject> Platform = PlatformValue->AsObject();
					FString PlatformId, Mode;
					if (!NativeRoutesKeys(Platform, { TEXT("id"), TEXT("mode"), TEXT("items") }) || !Platform->TryGetStringField(TEXT("id"), PlatformId) || PlatformId.Len() > 200
						|| !Platform->TryGetStringField(TEXT("mode"), Mode) || (Mode != TEXT("load") && Mode != TEXT("unload")) || !NativeRoutesStrings(Platform, TEXT("items"), 64, 1))
					{
						return false;
					}
				}
			}
			else if (Type == TEXT("train"))
			{
				if (!NativeRoutesKeys(Record, { TEXT("recordType"), TEXT("id"), TEXT("name"), TEXT("stops"), TEXT("items") }) || !NativeRoutesStrings(Record, TEXT("stops"), 200, 0) || !NativeRoutesStrings(Record, TEXT("items"), 64, 1))
				{
					return false;
				}
			}
			else
			{
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeTrainRoutesMapTest, "SatisDash.Core.NativeTrainRoutes.Map", SatisDash::Test::Flags)

bool FNativeTrainRoutesMapTest::RunTest(const FString& Parameters)
{
	TArray<FStationReading> Stations;
	// Platforms given unsorted, with unsorted repeated items; one station has no cargo platform at all.
	Stations.Add(NativeRoutesStation(TEXT("S2"), TEXT("Smelter"), { NativeRoutesPlatform(TEXT("P2"), false, { TEXT("Desc_B_C"), TEXT("Desc_A_C"), TEXT("Desc_B_C") }), NativeRoutesPlatform(TEXT("P1"), true, { TEXT("Desc_A_C") }) }));
	Stations.Add(NativeRoutesStation(TEXT("S1"), TEXT("Halt"), {}));
	TArray<FTrainReading> Trains;
	Trains.Add(NativeRoutesTrain(TEXT("T1"), TEXT("Shuttle"), { TEXT("Smelter"), TEXT("Halt"), TEXT("Smelter") }, { TEXT("Desc_Z_C"), TEXT("Desc_Y_C"), TEXT("Desc_Z_C") }));

	const FNativeWorldBody Body = BuildNativeTrainRoutes(NativeRoutesObserved, Stations, Trains);
	SatisDash::Test::SameText(*this, TEXT("stations by id, then trains; platforms by id; items sorted and distinct; stops in order"), Body.Json,
		TEXT("{\"observedAt\":\"2026-10-26T00:00:00.000Z\",\"data\":[")
		TEXT("{\"recordType\":\"station\",\"id\":\"S1\",\"name\":\"Halt\",\"platforms\":[]},")
		TEXT("{\"recordType\":\"station\",\"id\":\"S2\",\"name\":\"Smelter\",\"platforms\":[{\"id\":\"P1\",\"mode\":\"load\",\"items\":[\"Desc_A_C\"]},{\"id\":\"P2\",\"mode\":\"unload\",\"items\":[\"Desc_A_C\",\"Desc_B_C\"]}]},")
		TEXT("{\"recordType\":\"train\",\"id\":\"T1\",\"name\":\"Shuttle\",\"stops\":[\"Smelter\",\"Halt\",\"Smelter\"],\"items\":[\"Desc_Y_C\",\"Desc_Z_C\"]}]}"));
	TestEqual(TEXT("three records"), Body.Items, 3);
	TestEqual(TEXT("data array starts at its ["), Body.Json.Mid(Body.DataFrom, 1), FString(TEXT("[")));
	TestEqual(TEXT("data array ends after its ]"), Body.Json.Mid(Body.DataEnd - 1, 1), FString(TEXT("]")));
	TestEqual(TEXT("bytes are the UTF-8 length"), Body.Bytes, int64(Body.Json.Len()));

	// The same readings in the opposite order give the same bytes.
	// (stations and their platforms given in the other order, the repeats in another place).
	Stations.Reset();
	Stations.Add(NativeRoutesStation(TEXT("S1"), TEXT("Halt"), {}));
	Stations.Add(NativeRoutesStation(TEXT("S2"), TEXT("Smelter"), { NativeRoutesPlatform(TEXT("P1"), true, { TEXT("Desc_A_C") }), NativeRoutesPlatform(TEXT("P2"), false, { TEXT("Desc_A_C"), TEXT("Desc_B_C"), TEXT("Desc_B_C") }) }));
	const FNativeWorldBody Other = BuildNativeTrainRoutes(NativeRoutesObserved, Stations, Trains);
	SatisDash::Test::SameText(*this, TEXT("order-independent bytes"), Other.Json, Body.Json);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeTrainRoutesRulesTest, "SatisDash.Core.NativeTrainRoutes.Rules", SatisDash::Test::Flags)

bool FNativeTrainRoutesRulesTest::RunTest(const FString& Parameters)
{
	// 65 platforms keep 64 (the lowest 64 ids), and 65 distinct items keep 64 (the lowest 64, sorted).
	FStationReading Wide;
	Wide.Id = TEXT("S");
	Wide.Name = TEXT("Wide");
	for (int32 I = 64; I >= 0; --I)
	{
		FPlatformReading P;
		P.Id = FString::Printf(TEXT("P%02d"), I);
		P.bLoad = true;
		Wide.Platforms.Add(P);
	}
	FTrainReading Long;
	Long.Id = TEXT("T");
	Long.Name = TEXT("Long");
	Long.State = ETrainState::SelfDriving;
	for (int32 I = 0; I < 201; ++I)
	{
		Long.Stops.Add(FString::Printf(TEXT("Stop%03d"), I));
	}
	for (int32 I = 64; I >= 0; --I)
	{
		Long.Items.Add(FString::Printf(TEXT("Desc_%02d_C"), I));
	}
	Long.Items.Add(TEXT("Desc_10_C"));
	Long.Items.Add(FString());
	const FNativeWorldBody Body = BuildNativeTrainRoutes(NativeRoutesObserved, { Wide }, { Long });
	TArray<TSharedPtr<FJsonValue>> Data;
	if (!TestTrue(TEXT("the body parses"), NativeRoutesParse(Body, Data)) || !TestEqual(TEXT("two records"), Data.Num(), 2))
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>& Platforms = Data[0]->AsObject()->GetArrayField(TEXT("platforms"));
	TestEqual(TEXT("65 platforms keep 64"), Platforms.Num(), 64);
	TestEqual(TEXT("the first is the lowest id"), Platforms[0]->AsObject()->GetStringField(TEXT("id")), FString(TEXT("P00")));
	TestEqual(TEXT("the last kept is P63"), Platforms[63]->AsObject()->GetStringField(TEXT("id")), FString(TEXT("P63")));
	const TSharedPtr<FJsonObject> Train = Data[1]->AsObject();
	const TArray<TSharedPtr<FJsonValue>>& Stops = Train->GetArrayField(TEXT("stops"));
	TestEqual(TEXT("201 stops keep 200"), Stops.Num(), 200);
	TestEqual(TEXT("in timetable order"), Stops[199]->AsString(), FString(TEXT("Stop199")));
	const TArray<TSharedPtr<FJsonValue>>& Items = Train->GetArrayField(TEXT("items"));
	TestEqual(TEXT("65 distinct items (a repeat and an empty one added) keep 64"), Items.Num(), 64);
	TestEqual(TEXT("sorted, the lowest first"), Items[0]->AsString(), FString(TEXT("Desc_00_C")));
	TestEqual(TEXT("and the 64th is Desc_63_C"), Items[63]->AsString(), FString(TEXT("Desc_63_C")));
	TestTrue(TEXT("the capped layer fits the contract"), NativeRoutesFitsSchema(Data));

	// Duplicated and unsorted item classes become sorted and distinct; a name with a trailing space is kept as it is.
	FStationReading Spaced = FStationReading();
	Spaced.Id = TEXT("S1");
	Spaced.Name = TEXT("Silica/Crstal Out ");
	Spaced.Platforms.Add(NativeRoutesPlatform(TEXT("P"), false, { TEXT("Desc_C_C"), TEXT("Desc_A_C"), TEXT("Desc_C_C"), TEXT("Desc_B_C"), TEXT("Desc_A_C") }));
	const FNativeWorldBody SpacedBody = BuildNativeTrainRoutes(NativeRoutesObserved, { Spaced }, {});
	TestTrue(TEXT("items sorted and distinct"), SpacedBody.Json.Contains(TEXT("\"items\":[\"Desc_A_C\",\"Desc_B_C\",\"Desc_C_C\"]"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("the trailing space stays"), SpacedBody.Json.Contains(TEXT("\"name\":\"Silica/Crstal Out \""), ESearchCase::CaseSensitive));

	// An empty platform (FRM lists Build_TrainPlatformEmpty_02_C as unload with no items, run 40378567334) is kept.
	FStationReading WithEmpty = FStationReading();
	WithEmpty.Id = TEXT("S2");
	WithEmpty.Name = TEXT("s");
	WithEmpty.Platforms.Add(NativeRoutesPlatform(TEXT("Build_TrainPlatformEmpty_02_C_1"), false, {}));
	const FNativeWorldBody EmptyBody = BuildNativeTrainRoutes(NativeRoutesObserved, { WithEmpty }, {});
	TestTrue(TEXT("the empty platform is unload with no items"), EmptyBody.Json.Contains(TEXT("\"id\":\"Build_TrainPlatformEmpty_02_C_1\",\"mode\":\"unload\",\"items\":[]"), ESearchCase::CaseSensitive));
	TArray<TSharedPtr<FJsonValue>> EmptyData;
	if (TestTrue(TEXT("the layer with an empty platform parses"), NativeRoutesParse(EmptyBody, EmptyData)))
	{
		TestTrue(TEXT("a layer with an empty platform fits the contract"), NativeRoutesFitsSchema(EmptyData));
	}

	// The layer is cut at 4,000 records, stations first; the empty id is left out and counted.
	TArray<FStationReading> ManyStations;
	TArray<FTrainReading> ManyTrains;
	for (int32 I = 0; I < 3000; ++I)
	{
		FStationReading S;
		S.Id = FString::Printf(TEXT("S%04d"), I);
		S.Name = TEXT("s");
		ManyStations.Add(S);
	}
	for (int32 I = 0; I < 1500; ++I)
	{
		FTrainReading T;
		T.Id = FString::Printf(TEXT("T%04d"), I);
		T.Name = TEXT("t");
		ManyTrains.Add(T);
	}
	FTrainReading NoId;
	NoId.Name = TEXT("no id");
	ManyTrains.Add(NoId);
	const FNativeWorldBody Many = BuildNativeTrainRoutes(NativeRoutesObserved, ManyStations, ManyTrains);
	TestEqual(TEXT("4,000 records kept"), Many.Items, TrainRoutesMaxItems);
	TestEqual(TEXT("500 over"), Many.Over, 500);
	TestEqual(TEXT("the reading without an id is dropped"), Many.Dropped, 1);
	TArray<TSharedPtr<FJsonValue>> ManyData;
	if (TestTrue(TEXT("the full body parses"), NativeRoutesParse(Many, ManyData)))
	{
		TestEqual(TEXT("4,000 in the array"), ManyData.Num(), 4000);
		TestTrue(TEXT("the field set validates"), NativeRoutesFitsSchema(ManyData));
		TestEqual(TEXT("the 3,001st is the first train"), ManyData[3000]->AsObject()->GetStringField(TEXT("id")), FString(TEXT("T0000")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeTrainRoutesParkedTest, "SatisDash.Core.NativeTrainRoutes.Parked", SatisDash::Test::Flags)

bool FNativeTrainRoutesParkedTest::RunTest(const FString& Parameters)
{
	// Run 40439238142: FRM listed no timetable stops for a Parked train, the mod sent its four; a Parked train now sends none,
	// with its cargo unchanged, and a Self-Driving train keeps its stops. Manual and Derailed also keep them (no capture shows either).
	FTrainReading Parked = NativeRoutesTrain(TEXT("T1"), TEXT("Parked"), { TEXT("A"), TEXT("B"), TEXT("C"), TEXT("D") }, { TEXT("Desc_Sulfur_C") });
	Parked.State = ETrainState::Parked;
	const FTrainReading Driving = NativeRoutesTrain(TEXT("T2"), TEXT("Driving"), { TEXT("A"), TEXT("B") }, { TEXT("Desc_Silica_C") });
	FTrainReading Manual = NativeRoutesTrain(TEXT("T3"), TEXT("Manual"), { TEXT("A") }, {});
	Manual.State = ETrainState::Manual;
	const FNativeWorldBody Body = BuildNativeTrainRoutes(NativeRoutesObserved, {}, { Driving, Manual, Parked });
	SatisDash::Test::SameText(*this, TEXT("a Parked train has empty stops and its cargo; the others keep their stops"), Body.Json,
		TEXT("{\"observedAt\":\"2026-10-26T00:00:00.000Z\",\"data\":[")
		TEXT("{\"recordType\":\"train\",\"id\":\"T1\",\"name\":\"Parked\",\"stops\":[],\"items\":[\"Desc_Sulfur_C\"]},")
		TEXT("{\"recordType\":\"train\",\"id\":\"T2\",\"name\":\"Driving\",\"stops\":[\"A\",\"B\"],\"items\":[\"Desc_Silica_C\"]},")
		TEXT("{\"recordType\":\"train\",\"id\":\"T3\",\"name\":\"Manual\",\"stops\":[\"A\"],\"items\":[]}]}"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeTrainRoutesGoldenTest, "SatisDash.Core.NativeTrainRoutes.Golden", SatisDash::Test::Flags)

bool FNativeTrainRoutesGoldenTest::RunTest(const FString& Parameters)
{
	// The readings are the recorded FRM output of 2026-10-21 (frm-getTrainStation-2026-10-21-trimmed.json and
	// frm-getTrains-2026-10-21-trimmed.json), in the capture's order: ID, Name, CargoInventory[].ID, LoadingMode and
	// Inventory[].ClassName for a station; ID, Name, TimeTable[].StationName and the Vehicles[] inventories for the train.
	TArray<FStationReading> Stations;
	Stations.Add(NativeRoutesStation(TEXT("Build_TrainStation_C_2147412474"), TEXT("Silica/Crstal Out "), {
		NativeRoutesPlatform(TEXT("Build_TrainDockingStation_C_2147408936"), true, { TEXT("Desc_QuartzCrystal_C") }),
		NativeRoutesPlatform(TEXT("Build_TrainDockingStation_C_2147407665"), true, { TEXT("Desc_Silica_C") }) }));
	Stations.Add(NativeRoutesStation(TEXT("Build_TrainStation_C_2146503764"), TEXT("Computer-Dropoff"), {
		NativeRoutesPlatform(TEXT("Build_TrainDockingStation_C_2146498514"), false, { TEXT("Desc_QuartzCrystal_C") }),
		NativeRoutesPlatform(TEXT("Build_TrainDockingStation_C_2146497249"), false, { TEXT("Desc_Silica_C") }) }));
	TArray<FTrainReading> Trains;
	Trains.Add(NativeRoutesTrain(TEXT("BP_Train_C_2146471703"), TEXT("Silica/Crystal - Computer Dropoff"), { TEXT("Silica/Crstal Out "), TEXT("Computer-Dropoff") }, { TEXT("Desc_QuartzCrystal_C"), TEXT("Desc_Silica_C") }));

	// Written by hand from the capture's records: stations by id (2146503764 before 2147412474), each one's platforms by id,
	// items by class name, "Loading" is "load" and "Unloading" is "unload", the trailing space of "Silica/Crstal Out " kept.
	const FString Expected =
		TEXT("{\"observedAt\":\"2026-10-26T00:00:00.000Z\",\"data\":[")
		TEXT("{\"recordType\":\"station\",\"id\":\"Build_TrainStation_C_2146503764\",\"name\":\"Computer-Dropoff\",\"platforms\":[")
		TEXT("{\"id\":\"Build_TrainDockingStation_C_2146497249\",\"mode\":\"unload\",\"items\":[\"Desc_Silica_C\"]},")
		TEXT("{\"id\":\"Build_TrainDockingStation_C_2146498514\",\"mode\":\"unload\",\"items\":[\"Desc_QuartzCrystal_C\"]}]},")
		TEXT("{\"recordType\":\"station\",\"id\":\"Build_TrainStation_C_2147412474\",\"name\":\"Silica/Crstal Out \",\"platforms\":[")
		TEXT("{\"id\":\"Build_TrainDockingStation_C_2147407665\",\"mode\":\"load\",\"items\":[\"Desc_Silica_C\"]},")
		TEXT("{\"id\":\"Build_TrainDockingStation_C_2147408936\",\"mode\":\"load\",\"items\":[\"Desc_QuartzCrystal_C\"]}]},")
		TEXT("{\"recordType\":\"train\",\"id\":\"BP_Train_C_2146471703\",\"name\":\"Silica/Crystal - Computer Dropoff\",")
		TEXT("\"stops\":[\"Silica/Crstal Out \",\"Computer-Dropoff\"],\"items\":[\"Desc_QuartzCrystal_C\",\"Desc_Silica_C\"]}]}");

	const FNativeWorldBody Body = BuildNativeTrainRoutes(NativeRoutesObserved, Stations, Trains);
	SatisDash::Test::SameText(*this, TEXT("the capture's records give the hand-written layer"), Body.Json, Expected);
	TestEqual(TEXT("three records"), Body.Items, 3);
	TArray<TSharedPtr<FJsonValue>> Data;
	if (TestTrue(TEXT("the layer parses"), NativeRoutesParse(Body, Data)))
	{
		TestTrue(TEXT("and fits the contract's schema"), NativeRoutesFitsSchema(Data));
	}
	return true;
}
