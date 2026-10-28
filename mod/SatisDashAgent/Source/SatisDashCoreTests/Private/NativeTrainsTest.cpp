#include "SatisDash/Native/TrainRoutes/NativeTrainRoutes.h"
#include "SatisDash/Native/Trains/NativeTrains.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	FStationReading NativeTrainsRateStation(const TCHAR* Id, TOptional<double> Rate)
	{
		FStationReading Reading;
		Reading.Id = Id;
		Reading.Name = TEXT("Test Station A");
		Reading.XCm = -100050;
		Reading.YCm = -147350;
		Reading.TransferPerMinute = Rate;
		return Reading;
	}

	FString NativeTrainsRateJson(TOptional<double> Rate)
	{
		TArray<FStationReading> Stations;
		Stations.Add(NativeTrainsRateStation(TEXT("Build_TrainStation_C_1"), Rate));
		FString Json;
		NativeMapLiveJson(TArray<FTrainReading>(), Stations, Json);
		return Json;
	}

	const TCHAR* const NativeTrainsRateHead = TEXT("{\"trains\":[],\"stations\":[{\"id\":\"Build_TrainStation_C_1\",\"name\":\"Test Station A\",\"x\":-1000,\"y\":-1473");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeTrainsStationRatesTest, "SatisDash.Core.NativeTrains.StationRates", SatisDash::Test::Flags)

bool FNativeTrainsStationRatesTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("a rate prints after the existing keys, to 3 decimals"), NativeTrainsRateJson(12.34567),
		FString(NativeTrainsRateHead) + TEXT(",\"transferPerMinute\":12.346}]}"));
	TestEqual(TEXT("a rate of 0 prints 0"), NativeTrainsRateJson(0.0), FString(NativeTrainsRateHead) + TEXT(",\"transferPerMinute\":0}]}"));
	TestEqual(TEXT("a whole rate prints without a fraction"), NativeTrainsRateJson(60.0), FString(NativeTrainsRateHead) + TEXT(",\"transferPerMinute\":60}]}"));
	// Open question 4: mapLive and trainRoutes both print the one FStationReading::Id (the game reader sets it once, in TrainsReadStation).
	TArray<FStationReading> One;
	One.Add(NativeTrainsRateStation(TEXT("Build_TrainStation_C_7"), 1.0));
	FString MapLive;
	NativeMapLiveJson(TArray<FTrainReading>(), One, MapLive);
	const FNativeWorldBody Routes = BuildNativeTrainRoutes(TEXT("2026-10-27T00:00:00Z"), One, TArray<FTrainReading>());
	TestTrue(TEXT("mapLive prints the station id"), MapLive.Contains(TEXT("\"id\":\"Build_TrainStation_C_7\"")));
	TestTrue(TEXT("trainRoutes prints the same station id"), Routes.Json.Contains(TEXT("\"Build_TrainStation_C_7\"")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeTrainsStationRatesAbsentTest, "SatisDash.Core.NativeTrains.StationRatesAbsent", SatisDash::Test::Flags)

bool FNativeTrainsStationRatesAbsentTest::RunTest(const FString& Parameters)
{
	const FString Bare = FString(NativeTrainsRateHead) + TEXT("}]}");
	TestEqual(TEXT("no rate prints no key (absent is not 0)"), NativeTrainsRateJson(TOptional<double>()), Bare);
	TestEqual(TEXT("a negative rate prints no key"), NativeTrainsRateJson(-1.0), Bare);
	TestEqual(TEXT("NaN prints no key"), NativeTrainsRateJson(std::numeric_limits<double>::quiet_NaN()), Bare);
	TestEqual(TEXT("infinity prints no key"), NativeTrainsRateJson(std::numeric_limits<double>::infinity()), Bare);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeTrainsStationRateFactorTest, "SatisDash.Core.NativeTrains.StationRateFactor", SatisDash::Test::Flags)

bool FNativeTrainsStationRateFactorTest::RunTest(const FString& Parameters)
{
	// R4 / B6: assumed, NEEDS VERIFICATION by the first parity run. A change is a ruling PR and shows here.
	TestEqual(TEXT("NativeStationRateToPerMinute is 1.0"), NativeStationRateToPerMinute, 1.0);
	return true;
}
