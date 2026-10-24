#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "SatisDash/JsonUtil.h"
#include "SatisDash/Snapshot.h"
#include "TestUtil.h"

#include <limits>

using namespace SatisDash;

namespace
{
	FString Fixture(const TCHAR* Name)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SatisDashAgent"));
		FString Text;
		if (Plugin.IsValid())
		{
			FFileHelper::LoadFileToString(Text, *(Plugin->GetBaseDir() / TEXT("Source/SatisDashCoreTests/Fixtures") / Name));
		}
		return Text;
	}

	FSnapshot Base()
	{
		FSnapshot Snapshot;
		Snapshot.AgentVersion = TEXT("mod/0.1.0");
		Snapshot.ObservedAt = FDateTime(2026, 10, 20, 13, 26, 42, 134);
		FAutoPauseReading AutoPause;
		AutoPause.bEnabled = true;
		Snapshot.AutoPause = AutoPause;
		return Snapshot;
	}

	/** Same JSON value, compared as data: field order and 5 vs 5.0 don't matter. */
	bool SameJson(const FString& A, const FString& B)
	{
		const TSharedPtr<FJsonObject> Left = SatisDash::Json::ParseObject(A);
		const TSharedPtr<FJsonObject> Right = SatisDash::Json::ParseObject(B);
		return Left.IsValid() && Right.IsValid() &&
			FJsonValue::CompareEqual(FJsonValueObject(Left), FJsonValueObject(Right));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnapshotClampTest, "SatisDash.Core.Snapshot.Clamp", SatisDash::Test::Flags)

bool FSnapshotClampTest::RunTest(const FString& Parameters)
{
	const double NaN = std::numeric_limits<double>::quiet_NaN();
	const double Inf = std::numeric_limits<double>::infinity();
	TestEqual(TEXT("a long string is cut to 200"), ClampString(FString::ChrN(250, TEXT('x'))).Len(), 200);
	FString Emoji = FString::ChrN(199, TEXT('x'));
	Emoji.AppendChar(TCHAR(0xD83D));
	Emoji.AppendChar(TCHAR(0xDE00));
	Emoji.AppendChar(TEXT('y'));
	TestEqual(TEXT("a surrogate pair is never split"), ClampString(Emoji).Len(), 199);
	TestEqual(TEXT("negative to 0"), ClampNonNegative(-1.0), 0.0);
	TestEqual(TEXT("NaN to 0"), ClampNonNegative(NaN), 0.0);
	TestEqual(TEXT("infinity to 0"), ClampNonNegative(Inf), 0.0);
	TestEqual(TEXT("a value is kept"), ClampNonNegative(5.5), 5.5);
	TestEqual(TEXT("a finite negative is kept"), ClampFinite(-12.5), -12.5);
	TestEqual(TEXT("NaN to 0 (signed)"), ClampFinite(NaN), 0.0);

	FCircuitReading Circuit;
	Circuit.BatteryPercent = 100.0000001;
	Circuit.ConsumptionMW = -0.0000001;
	Circuit.BatteryDifferentialMW = NaN;
	const FCircuitReading Conformed = ConformCircuit(Circuit);
	TestEqual(TEXT("battery percent at most 100"), Conformed.BatteryPercent, 100.0);
	TestEqual(TEXT("float noise below 0"), Conformed.ConsumptionMW, 0.0);
	TestEqual(TEXT("differential NaN"), Conformed.BatteryDifferentialMW, 0.0);

	FStatusReading Status;
	Status.ConnectedPlayers = -3;
	Status.TickRate = NaN;
	Status.SessionName = FString::ChrN(250, TEXT('s'));
	const FStatusReading Clean = ConformStatus(Status);
	TestEqual(TEXT("players at least 0"), Clean.ConnectedPlayers, 0);
	TestEqual(TEXT("tick rate NaN"), Clean.TickRate, 0.0);
	TestEqual(TEXT("session name cut"), Clean.SessionName.Len(), 200);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnapshotFixturesTest, "SatisDash.Core.Snapshot.Fixtures", SatisDash::Test::Flags)

bool FSnapshotFixturesTest::RunTest(const FString& Parameters)
{
	FSnapshot World = Base();
	FStatusReading Status;
	Status.bTickHealthy = true;
	Status.bIsGameRunning = true;
	Status.SessionName = TEXT("Golden Session");
	Status.ConnectedPlayers = 1;
	Status.PlayerLimit = 4;
	Status.TickRate = 28.4;
	Status.TotalGameDurationSeconds = 123456;
	World.Status = Status;
	FCircuitReading Circuit;
	Circuit.CircuitGroupId = 1;
	Circuit.ProductionMW = 1200.5;
	Circuit.ConsumptionMW = -0.0000001; // float noise: conformed to 0
	Circuit.CapacityMW = 1500;
	Circuit.MaxConsumptionMW = 1400;
	Circuit.BatteryCapacityMWh = 100;
	Circuit.BatteryPercent = 100.0000001; // float noise: conformed to 100
	Circuit.BatteryDifferentialMW = -12.5;
	World.Power = TArray<FCircuitReading>{ Circuit };
	const FString WorldFixture = Fixture(TEXT("snapshot-world.json"));
	TestFalse(TEXT("the world fixture was found"), WorldFixture.IsEmpty());
	TestTrue(TEXT("matches snapshot-world.json"), SameJson(SnapshotToJson(World), WorldFixture));

	FSnapshot NoWorld = Base();
	FStatusReading Idle;
	Idle.PlayerLimit = 4;
	Idle.TickRate = 30;
	NoWorld.Status = Idle;
	const FString NoWorldFixture = Fixture(TEXT("snapshot-no-world.json"));
	TestFalse(TEXT("the no-world fixture was found"), NoWorldFixture.IsEmpty());
	TestTrue(TEXT("matches snapshot-no-world.json"), SameJson(SnapshotToJson(NoWorld), NoWorldFixture));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnapshotShapeTest, "SatisDash.Core.Snapshot.Shape", SatisDash::Test::Flags)

bool FSnapshotShapeTest::RunTest(const FString& Parameters)
{
	// No status read: paused is null and there's no status part; reachable is still true (the mod never sends false).
	const TSharedPtr<FJsonObject> Bare = SatisDash::Json::ParseObject(SnapshotToJson(Base()));
	TestTrue(TEXT("paused is null"), SatisDash::Json::Field(Bare, TEXT("paused"), EJson::Null).IsValid());
	TestFalse(TEXT("no status"), Bare->HasField(TEXT("status")));
	TestTrue(TEXT("reachable"), SatisDash::Json::Field(Bare, TEXT("reachable"), EJson::Boolean).IsValid() && Bare->GetBoolField(TEXT("reachable")));

	FSnapshot NoSettings = Base();
	NoSettings.AutoPause.Reset();
	TestFalse(TEXT("no settings when auto-pause wasn't read"), SatisDash::Json::ParseObject(SnapshotToJson(NoSettings))->HasField(TEXT("settings")));

	FSnapshot Many = Base();
	Many.Power = TArray<FCircuitReading>();
	Many.Power->SetNum(MaxCircuits + 1);
	const TSharedPtr<FJsonObject> Capped = SatisDash::Json::ParseObject(SnapshotToJson(Many));
	const TSharedPtr<FJsonValue> Circuits = SatisDash::Json::Field(Capped->GetObjectField(TEXT("power")), TEXT("circuits"), EJson::Array);
	TestEqual(TEXT("at most 1000 circuits"), Circuits.IsValid() ? Circuits->AsArray().Num() : -1, MaxCircuits);
	return true;
}
