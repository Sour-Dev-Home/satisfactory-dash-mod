#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SatisDash/FrmMapLive.h"
#include "SatisDash/Native/Trains/NativeTrains.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	FTrainReading NativeMapLiveTrain(const TCHAR* Id, const TCHAR* Name, ETrainState State, double XCm, double YCm)
	{
		FTrainReading R;
		R.Id = Id;
		R.Name = Name;
		R.State = State;
		R.XCm = XCm;
		R.YCm = YCm;
		return R;
	}

	FStationReading NativeMapLiveStation(const TCHAR* Id, const TCHAR* Name, double XCm, double YCm)
	{
		FStationReading R;
		R.Id = Id;
		R.Name = Name;
		R.XCm = XCm;
		R.YCm = YCm;
		return R;
	}

	/** The readings the two fixtures hold (frm-getTrains.json, frm-getTrainStation.json): the first two trains and stations, and the far-away third train. */
	void NativeMapLiveFixtureReadings(TArray<FTrainReading>& Trains, TArray<FStationReading>& Stations)
	{
		Trains.Add(NativeMapLiveTrain(TEXT("BP_Train_C_1"), TEXT("Test Train A"), ETrainState::SelfDriving, -102587.95567750931, -147300));
		Trains.Add(NativeMapLiveTrain(TEXT("BP_Train_C_2"), TEXT("Test Train B"), ETrainState::Parked, 150, -150));
		Trains.Add(NativeMapLiveTrain(TEXT("BP_Train_C_3"), TEXT("Far Away"), ETrainState::Manual, 1.0e12, 0));
		Stations.Add(NativeMapLiveStation(TEXT("Build_TrainStation_C_1"), TEXT("Test Station A"), -100050, -147350));
		Stations.Add(NativeMapLiveStation(TEXT("Build_TrainStation_C_2"), TEXT("Test Station B"), 5049, -5051));
	}

	FString NativeMapLivePadded(const TCHAR* Prefix, int32 Index, int32 Length)
	{
		FString Text = FString::Printf(TEXT("%s%05d"), Prefix, Index);
		while (Text.Len() < Length)
		{
			Text.AppendChar(TEXT('x'));
		}
		return Text;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeMapLiveMapTest, "SatisDash.Core.NativeMapLive.Map", SatisDash::Test::Flags)

bool FNativeMapLiveMapTest::RunTest(const FString& Parameters)
{
	TArray<FTrainReading> Trains;
	TArray<FStationReading> Stations;
	NativeMapLiveFixtureReadings(Trains, Stations);

	FNativeMapLiveCounts Counts;
	FString Json;
	if (!TestTrue(TEXT("the fixture's readings build"), NativeMapLiveJson(Trains, Stations, Json, &Counts)))
	{
		return false;
	}
	const FString Golden = SatisDash::Test::Fixture(TEXT("frm-mapLive.expected.json"));
	if (!TestFalse(TEXT("the golden was found"), Golden.IsEmpty()))
	{
		return false;
	}
	TestTrue(TEXT("equal to the bridge's golden"), SatisDash::Test::SameJson(Json, Golden));
	SatisDash::Test::SameText(*this, TEXT("compact, trains then stations, in the bridge's field order"), Json,
		TEXT("{\"trains\":[{\"id\":\"BP_Train_C_1\",\"name\":\"Test Train A\",\"x\":-1026,\"y\":-1473,\"status\":\"Self-Driving\"},")
		TEXT("{\"id\":\"BP_Train_C_2\",\"name\":\"Test Train B\",\"x\":2,\"y\":-1,\"status\":\"Parked\"}],")
		TEXT("\"stations\":[{\"id\":\"Build_TrainStation_C_1\",\"name\":\"Test Station A\",\"x\":-1000,\"y\":-1473},")
		TEXT("{\"id\":\"Build_TrainStation_C_2\",\"name\":\"Test Station B\",\"x\":50,\"y\":-51}]}"));
	TestEqual(TEXT("two trains kept"), Counts.Trains, 2);
	TestEqual(TEXT("the 1e12 cm train dropped and counted"), Counts.TrainsDropped, 1);
	TestEqual(TEXT("two stations kept"), Counts.Stations, 2);
	TestEqual(TEXT("no station dropped"), Counts.StationsDropped, 0);

	// The same readings in the opposite order give the same bytes.
	TArray<FTrainReading> ReversedTrains;
	for (int32 I = Trains.Num() - 1; I >= 0; --I)
	{
		ReversedTrains.Add(Trains[I]);
	}
	TArray<FStationReading> ReversedStations;
	for (int32 I = Stations.Num() - 1; I >= 0; --I)
	{
		ReversedStations.Add(Stations[I]);
	}
	FString Reversed;
	TestTrue(TEXT("reversed readings build"), NativeMapLiveJson(ReversedTrains, ReversedStations, Reversed));
	SatisDash::Test::SameText(*this, TEXT("order-independent bytes"), Reversed, Json);

	// 501 trains and stations keep 500 each (the 501st by id is the one left out), and nothing is dropped.
	TArray<FTrainReading> ManyTrains;
	TArray<FStationReading> ManyStations;
	for (int32 I = 0; I < 501; ++I)
	{
		ManyTrains.Add(NativeMapLiveTrain(*FString::Printf(TEXT("T%03d"), I), TEXT("n"), ETrainState::Parked, I * 10000.0, 0));
		ManyStations.Add(NativeMapLiveStation(*FString::Printf(TEXT("S%03d"), I), TEXT("n"), I * 10000.0, 0));
	}
	FNativeMapLiveCounts ManyCounts;
	FString ManyJson;
	TestTrue(TEXT("501 each build"), NativeMapLiveJson(ManyTrains, ManyStations, ManyJson, &ManyCounts));
	TestEqual(TEXT("500 trains kept"), ManyCounts.Trains, MapLiveMaxItems);
	TestEqual(TEXT("one train over"), ManyCounts.TrainsOver, 1);
	TestEqual(TEXT("500 stations kept"), ManyCounts.Stations, MapLiveMaxItems);
	TestEqual(TEXT("one station over"), ManyCounts.StationsOver, 1);
	TestTrue(TEXT("T499 is the last train kept"), ManyJson.Contains(TEXT("\"T499\"")));
	TestFalse(TEXT("T500 is cut"), ManyJson.Contains(TEXT("\"T500\"")));

	// Dropped readings do not count towards the 500: 500 good trains plus 3 far-away ones keep all 500.
	TArray<FTrainReading> WithFar;
	for (int32 I = 0; I < 500; ++I)
	{
		WithFar.Add(NativeMapLiveTrain(*FString::Printf(TEXT("T%03d"), I), TEXT("n"), ETrainState::Parked, 100.0 * I, 0));
	}
	for (int32 I = 0; I < 3; ++I)
	{
		WithFar.Add(NativeMapLiveTrain(*FString::Printf(TEXT("A%d"), I), TEXT("far"), ETrainState::Parked, 1.0e12, 0));
	}
	FNativeMapLiveCounts FarCounts;
	FString FarJson;
	TestTrue(TEXT("far-away trains build"), NativeMapLiveJson(WithFar, TArray<FStationReading>(), FarJson, &FarCounts));
	TestEqual(TEXT("all 500 good trains kept"), FarCounts.Trains, 500);
	TestEqual(TEXT("three dropped"), FarCounts.TrainsDropped, 3);
	TestEqual(TEXT("none over"), FarCounts.TrainsOver, 0);

	// Over MapLiveMaxBytes: false, and the output is left as it was (the part is left out, as JoinMapLive does).
	TArray<FTrainReading> BigTrains;
	TArray<FStationReading> BigStations;
	for (int32 I = 0; I < 500; ++I)
	{
		BigTrains.Add(NativeMapLiveTrain(*NativeMapLivePadded(TEXT("T"), I, 200), *NativeMapLivePadded(TEXT("n"), I, 200), ETrainState::Parked, 100.0 * I, 0));
		BigStations.Add(NativeMapLiveStation(*NativeMapLivePadded(TEXT("S"), I, 200), *NativeMapLivePadded(TEXT("n"), I, 200), 100.0 * I, 0));
	}
	FString Left = TEXT("untouched");
	TestFalse(TEXT("over MapLiveMaxBytes is refused"), NativeMapLiveJson(BigTrains, BigStations, Left));
	SatisDash::Test::SameText(*this, TEXT("and the output is left alone"), Left, TEXT("untouched"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeMapLiveStatusTest, "SatisDash.Core.NativeMapLive.Status", SatisDash::Test::Flags)

bool FNativeMapLiveStatusTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::SameText(*this, TEXT("Parked"), NativeTrainStatusText(ETrainState::Parked), TEXT("Parked"));
	SatisDash::Test::SameText(*this, TEXT("Manual"), NativeTrainStatusText(ETrainState::Manual), TEXT("Manual"));
	SatisDash::Test::SameText(*this, TEXT("SelfDriving"), NativeTrainStatusText(ETrainState::SelfDriving), TEXT("Self-Driving"));
	SatisDash::Test::SameText(*this, TEXT("Derailed"), NativeTrainStatusText(ETrainState::Derailed), TEXT("Derailed"));

	// "Self-Driving" is the one word that must equal FRM's: read it from the recorded getTrains output.
	const FString Recorded = SatisDash::Test::Fixture(TEXT("frm-getTrains.json"));
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FString(TEXT("{\"v\":")) + Recorded + TEXT("}"));
	if (!TestTrue(TEXT("the recorded getTrains parses"), FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid()))
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>& Items = Root->GetArrayField(TEXT("v"));
	if (!TestTrue(TEXT("the recording has a train"), Items.Num() > 0))
	{
		return false;
	}
	SatisDash::Test::SameText(*this, TEXT("the recorded Status of the self-driving train"), NativeTrainStatusText(ETrainState::SelfDriving), Items[0]->AsObject()->GetStringField(TEXT("Status")));

	// The position source is one constant, FirstVehicle until the parity run says otherwise.
	TestTrue(TEXT("position source is the first vehicle"), NativeTrainPositionSource == ETrainPositionSource::FirstVehicle);
	return true;
}
