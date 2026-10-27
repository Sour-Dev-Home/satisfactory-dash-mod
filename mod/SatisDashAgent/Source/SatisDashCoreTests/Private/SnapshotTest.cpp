#include "SatisDash/JsonUtil.h"
#include "SatisDash/Snapshot.h"
#include "TestUtil.h"

#include <limits>

using namespace SatisDash;

namespace
{
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
	const FString WorldFixture = SatisDash::Test::Fixture(TEXT("snapshot-world.json"));
	TestFalse(TEXT("the world fixture was found"), WorldFixture.IsEmpty());
	TestTrue(TEXT("matches snapshot-world.json"), SatisDash::Test::SameJson(SnapshotToJson(World), WorldFixture));

	FSnapshot NoWorld = Base();
	FStatusReading Idle;
	Idle.PlayerLimit = 4;
	Idle.TickRate = 30;
	NoWorld.Status = Idle;
	const FString NoWorldFixture = SatisDash::Test::Fixture(TEXT("snapshot-no-world.json"));
	TestFalse(TEXT("the no-world fixture was found"), NoWorldFixture.IsEmpty());
	TestTrue(TEXT("matches snapshot-no-world.json"), SatisDash::Test::SameJson(SnapshotToJson(NoWorld), NoWorldFixture));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnapshotReachabilityFieldsTest, "SatisDash.Core.Snapshot.ReachabilityFields", SatisDash::Test::Flags)

bool FSnapshotReachabilityFieldsTest::RunTest(const FString& Parameters)
{
	// With none set, the body is exactly what it was before S4-A.
	const FString Old = SnapshotToJson(Base());
	TestFalse(TEXT("no gameReachable key"), Old.Contains(TEXT("gameReachable")));
	TestFalse(TEXT("no lastGameReadAt key"), Old.Contains(TEXT("lastGameReadAt")));
	TestFalse(TEXT("no modVersion key"), Old.Contains(TEXT("modVersion")));

	FSnapshot Full = Base();
	Full.bGameReachable = true;
	Full.LastGameReadAt = FDateTime(2026, 10, 27, 12, 0, 0, 0);
	Full.ModVersion = TEXT("mod/0.1.0");
	const TSharedPtr<FJsonObject> Parsed = SatisDash::Json::ParseObject(SnapshotToJson(Full));
	TestTrue(TEXT("parses"), Parsed.IsValid());
	if (!Parsed.IsValid())
	{
		return false;
	}
	TestTrue(TEXT("gameReachable true"), SatisDash::Json::Field(Parsed, TEXT("gameReachable"), EJson::Boolean).IsValid() && Parsed->GetBoolField(TEXT("gameReachable")));
	SatisDash::Test::SameText(*this, TEXT("lastGameReadAt is UTC ISO"), Parsed->GetStringField(TEXT("lastGameReadAt")), TEXT("2026-10-27T12:00:00.000Z"));
	SatisDash::Test::SameText(*this, TEXT("modVersion"), Parsed->GetStringField(TEXT("modVersion")), TEXT("mod/0.1.0"));
	TestTrue(TEXT("reachable stays true"), Parsed->GetBoolField(TEXT("reachable")));
	TestFalse(TEXT("not inside status"), Parsed->HasField(TEXT("status")));

	FSnapshot Long = Full;
	Long.ModVersion = FString::ChrN(70, TEXT('v'));
	TestEqual(TEXT("modVersion cut to 64"), SatisDash::Json::ParseObject(SnapshotToJson(Long))->GetStringField(TEXT("modVersion")).Len(), 64);

	FSnapshot Empty = Full;
	Empty.ModVersion.Empty();
	TestFalse(TEXT("empty modVersion writes no key"), SatisDash::Json::ParseObject(SnapshotToJson(Empty))->HasField(TEXT("modVersion")));

	FSnapshot Failed = Base();
	Failed.bGameReachable = false;
	const TSharedPtr<FJsonObject> F = SatisDash::Json::ParseObject(SnapshotToJson(Failed));
	TestTrue(TEXT("gameReachable false is written"), SatisDash::Json::Field(F, TEXT("gameReachable"), EJson::Boolean).IsValid() && !F->GetBoolField(TEXT("gameReachable")));
	TestFalse(TEXT("and no lastGameReadAt before a good read"), F->HasField(TEXT("lastGameReadAt")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnapshotSpliceTest, "SatisDash.Core.Snapshot.Splice", SatisDash::Test::Flags)

bool FSnapshotSpliceTest::RunTest(const FString& Parameters)
{
	// SnapshotToJson splices finished part JSON before the root's closing brace, which is only right for the condensed
	// writer's shape. Pin that shape here so a change in Json::Write fails loudly.
	const TSharedRef<FJsonObject> One = MakeShared<FJsonObject>();
	One->SetNumberField(TEXT("a"), 1);
	SatisDash::Test::SameText(*this, TEXT("Json::Write is condensed and ends with the root's brace"), SatisDash::Json::Write(One), TEXT("{\"a\":1}"));

	FSnapshot Snapshot = Base();
	Snapshot.PlayersJson = TEXT("{\"available\":false,\"players\":[]}");
	Snapshot.FactoryJson = TEXT("{\"buildings\":[]}");
	const FString Json = SnapshotToJson(Snapshot);
	const TSharedPtr<FJsonObject> Parsed = SatisDash::Json::ParseObject(Json);
	TestTrue(TEXT("valid JSON with both parts spliced in"), Parsed.IsValid() && Parsed->HasField(TEXT("players")) && Parsed->HasField(TEXT("factory")));
	TestTrue(TEXT("and the root's own fields kept"), Parsed.IsValid() && Parsed->HasField(TEXT("reachable")));
	TestTrue(TEXT("players before factory"), Json.Find(TEXT("\"players\":")) < Json.Find(TEXT("\"factory\":")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnapshotMapLiveTest, "SatisDash.Core.Snapshot.MapLive", SatisDash::Test::Flags)

bool FSnapshotMapLiveTest::RunTest(const FString& Parameters)
{
	const FString Live = SatisDash::Test::Fixture(TEXT("frm-mapLive.expected.json"));
	if (!TestFalse(TEXT("the mapLive fixture was found"), Live.IsEmpty()))
	{
		return false;
	}
	FSnapshot Snapshot = Base();
	Snapshot.PlayersJson = TEXT("{\"available\":false,\"players\":[]}");
	Snapshot.FactoryJson = TEXT("{\"buildings\":[]}");
	Snapshot.MapLiveJson = Live;
	const FString Json = SnapshotToJson(Snapshot);
	const TSharedPtr<FJsonObject> Parsed = SatisDash::Json::ParseObject(Json);
	if (!TestTrue(TEXT("valid JSON"), Parsed.IsValid()) || !TestTrue(TEXT("with mapLive"), Parsed->HasField(TEXT("mapLive"))))
	{
		return false;
	}
	TestEqual(TEXT("two trains"), Parsed->GetObjectField(TEXT("mapLive"))->GetArrayField(TEXT("trains")).Num(), 2);
	TestEqual(TEXT("two stations"), Parsed->GetObjectField(TEXT("mapLive"))->GetArrayField(TEXT("stations")).Num(), 2);
	TestTrue(TEXT("players, then factory, then mapLive"),
		Json.Find(TEXT("\"players\":")) < Json.Find(TEXT("\"factory\":")) && Json.Find(TEXT("\"factory\":")) < Json.Find(TEXT("\"mapLive\":")));
	TestFalse(TEXT("no mapLive without a part"), SatisDash::Json::ParseObject(SnapshotToJson(Base()))->HasField(TEXT("mapLive")));
	FSnapshot OnlyLive = Base();
	OnlyLive.MapLiveJson = Live;
	TestTrue(TEXT("mapLive alone is spliced too"), SatisDash::Json::ParseObject(SnapshotToJson(OnlyLive)).IsValid());
	return true;
}
