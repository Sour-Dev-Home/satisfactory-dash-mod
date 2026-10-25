#include "DriverFakes.h"
#include "SatisDash/Driver.h"

using namespace SatisDash;
using SatisDash::Test::FDriverFixture;

namespace
{
	const TCHAR* DriverFrmSnapshots = TEXT("/agent/v1/snapshots");

	/** Answers every open snapshot post with 200, and returns the bodies it answered. */
	TArray<FString> DriverFrmDrain(FDriverFixture& F)
	{
		TArray<FString> Bodies;
		while (F.Net.pending(DriverFrmSnapshots) > 0)
		{
			Bodies.Add(F.Net.lastBody(DriverFrmSnapshots));
			F.Net.answer(DriverFrmSnapshots, SatisDash::Test::SnapshotOk(false));
		}
		return Bodies;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverFrmPartsTest, "SatisDash.Core.Driver.FrmParts", SatisDash::Test::Flags)

bool FDriverFrmPartsTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	TestEqual(TEXT("no FRM read before enrolling"), F.Net.sent(TEXT("/getPlayer")), 0);
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	F.run(Driver, 1);
	TestEqual(TEXT("the first pass asks for players"), F.Net.pending(TEXT("/getPlayer")), 1);
	F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, TEXT("[{\"Name\":\"Player One\",\"Online\":true}]")));
	F.run(Driver, 1);
	TestEqual(TEXT("then the factory, one FRM request at a time"), F.Net.pending(TEXT("/getFactory")), 1);
	F.Net.answer(TEXT("/getFactory"), SatisDash::Test::Reply(200, TEXT("[{\"ID\":\"B_1\",\"Name\":\"n\",\"ClassName\":\"c\",\"IsProducing\":true,\"IsPaused\":false}]")));
	DriverFrmDrain(F);
	F.run(Driver, 5);
	TArray<FString> Bodies = DriverFrmDrain(F);
	F.run(Driver, 1);
	Bodies.Append(DriverFrmDrain(F));
	bool bPlayers = false;
	bool bFactory = false;
	for (const FString& Body : Bodies)
	{
		bPlayers = bPlayers || Body.Contains(TEXT("\"players\":{\"available\":true,\"players\":[{\"name\":\"Player One\",\"online\":true}]}"));
		bFactory = bFactory || Body.Contains(TEXT("\"factory\":{\"buildings\":[{\"id\":\"B_1\""));
	}
	TestTrue(TEXT("the next snapshot carries players"), bPlayers);
	TestTrue(TEXT("and one carries the factory"), bFactory);
	F.run(Driver, 10);
	TestEqual(TEXT("factory once per 30 s: no second read yet"), F.Net.sent(TEXT("/getFactory")), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverFrmDownTest, "SatisDash.Core.Driver.FrmDown", SatisDash::Test::Flags)

bool FDriverFrmDownTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	F.run(Driver, 1);
	F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(0, FString()));
	F.run(Driver, 1);
	F.Net.answer(TEXT("/getFactory"), SatisDash::Test::Reply(0, FString()));
	DriverFrmDrain(F);
	F.run(Driver, 5);
	const TArray<FString> Bodies = DriverFrmDrain(F);
	bool bUnavailable = false;
	for (const FString& Body : Bodies)
	{
		bUnavailable = bUnavailable || Body.Contains(TEXT("\"players\":{\"available\":false,\"players\":[]}"));
		TestFalse(TEXT("no factory part"), Body.Contains(TEXT("\"factory\"")));
		TestTrue(TEXT("status keeps flowing"), Body.Contains(TEXT("\"status\"")));
	}
	TestTrue(TEXT("players unavailable"), bUnavailable);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverFrmStopsTest, "SatisDash.Core.Driver.FrmStops", SatisDash::Test::Flags)

bool FDriverFrmStopsTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	F.run(Driver, 1);
	const int32 Asked = F.Net.sent(TEXT("/getPlayer"));
	F.Net.answer(DriverFrmSnapshots, SatisDash::Test::Reply(401, FString()));
	F.run(Driver, 1);
	const FAgent& Agent = Driver.GetAgent();
	TestTrue(TEXT("halted by the 401"), Agent.GetState() != EAgentState::Running);
	F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, TEXT("[{\"Name\":\"Player One\",\"Online\":true}]")));
	F.run(Driver, 30);
	TestEqual(TEXT("no FRM read while not running"), F.Net.sent(TEXT("/getPlayer")), Asked);
	TestEqual(TEXT("nothing pushed after the halt"), F.Net.pending(DriverFrmSnapshots), 0);
	return true;
}

namespace
{
	const TCHAR* DriverFrmPoll = TEXT("/agent/v1/commands?waitSeconds=25");

	/**
	 * Enrols, reads players and a factory, then holds the worker so the next sample (the first to carry the factory) leaves
	 * its encode pending. Snapshot 1 is left unanswered. False if the held encode never showed up.
	 */
	bool DriverFrmHoldEncode(FDriverFixture& F, FDriver& Driver)
	{
		F.enrol(Driver, SatisDash::Test::FakeSecret());
		F.run(Driver, 1);
		F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, TEXT("[{\"Name\":\"Player One\",\"Online\":true}]")));
		F.run(Driver, 1);
		if (F.Net.pending(TEXT("/getFactory")) != 1)
		{
			return false;
		}
		F.Net.answer(TEXT("/getFactory"), SatisDash::Test::Reply(200, TEXT("[{\"ID\":\"B_1\",\"Name\":\"n\",\"ClassName\":\"c\",\"IsProducing\":true,\"IsPaused\":false}]")));
		F.run(Driver, 1.5);
		F.Worker.bHold = true;
		return F.runUntil(Driver, [&F] { return !F.Worker.IsIdle(); }, 10.0);
	}

	/** After release: the factory snapshot goes out first, then the status snapshot that was due, within 2 s. */
	struct FDriverFrmOrder
	{
		int32 FirstPending = 0;
		bool bFirstHasFactory = false;
		bool bNextSent = false;
		bool bNextHasFactory = false;
	};

	FDriverFrmOrder DriverFrmReleaseAndWatch(FDriverFixture& F, FDriver& Driver)
	{
		FDriverFrmOrder Order;
		F.Worker.bHold = false;
		F.Worker.release();
		F.run(Driver, 0.5);
		Order.FirstPending = F.Net.pending(DriverFrmSnapshots);
		Order.bFirstHasFactory = F.Net.lastBody(DriverFrmSnapshots).Contains(TEXT("\"factory\":{"));
		F.Net.answer(DriverFrmSnapshots, SatisDash::Test::SnapshotOk(false));
		Order.bNextSent = F.runUntil(Driver, [&F] { return F.Net.pending(DriverFrmSnapshots) > 0; }, 2.0);
		Order.bNextHasFactory = F.Net.lastBody(DriverFrmSnapshots).Contains(TEXT("\"factory\""));
		return Order;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverFrmEncodeWaitTest, "SatisDash.Core.Driver.FrmEncodeWait", SatisDash::Test::Flags)

bool FDriverFrmEncodeWaitTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	if (!TestTrue(TEXT("an encode is held"), DriverFrmHoldEncode(F, Driver)))
	{
		return false;
	}
	F.Net.answer(DriverFrmSnapshots, SatisDash::Test::SnapshotOk(false));
	F.run(Driver, 0.5);
	const int32 Sent = F.Net.sent(DriverFrmSnapshots);
	F.run(Driver, 6);
	TestEqual(TEXT("no snapshot goes out while the encode is pending"), F.Net.sent(DriverFrmSnapshots), Sent);
	const FDriverFrmOrder Order = DriverFrmReleaseAndWatch(F, Driver);
	TestEqual(TEXT("the encoded snapshot is sent at once"), Order.FirstPending, 1);
	TestTrue(TEXT("and it is the one with the factory, ahead of the status snapshot"), Order.bFirstHasFactory);
	TestTrue(TEXT("the sample that waited is taken right after the encode"), Order.bNextSent);
	TestFalse(TEXT("and it carries no factory"), Order.bNextHasFactory);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverFrmEncodeAutoPauseTest, "SatisDash.Core.Driver.FrmEncodeAutoPause", SatisDash::Test::Flags)

bool FDriverFrmEncodeAutoPauseTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	if (!TestTrue(TEXT("an encode is held"), DriverFrmHoldEncode(F, Driver)))
	{
		return false;
	}
	F.Net.answer(DriverFrmSnapshots, SatisDash::Test::SnapshotOk(false));
	F.run(Driver, 0.5);
	const int32 Sent = F.Net.sent(DriverFrmSnapshots);
	// An applied set_auto_pause makes the next sample immediate (satisfactory-dash #621), here during the wait.
	const FString Item = FString::Printf(TEXT("{\"id\":\"c-1\",\"type\":\"set_auto_pause\",\"params\":{\"enabled\":true},\"expiresAt\":\"%s\"}"), *(F.Utc + FTimespan::FromMinutes(5)).ToIso8601());
	F.Net.answer(DriverFrmPoll, SatisDash::Test::Reply(200, FString::Printf(TEXT("{\"commands\":[%s]}"), *Item)));
	F.run(Driver, 1);
	TestEqual(TEXT("the command was applied"), F.Game.Applied.Num(), 1);
	F.Net.answer(TEXT("/agent/v1/commands/c-1/result"), SatisDash::Test::Reply(204, FString()));
	F.run(Driver, 0.5);
	TestEqual(TEXT("still nothing pushed while the encode is pending"), F.Net.sent(DriverFrmSnapshots), Sent);
	const FDriverFrmOrder Order = DriverFrmReleaseAndWatch(F, Driver);
	TestEqual(TEXT("the encoded snapshot is sent at once"), Order.FirstPending, 1);
	TestTrue(TEXT("and it is the one with the factory, ahead of the status snapshot"), Order.bFirstHasFactory);
	TestTrue(TEXT("the immediate sample is taken right after the encode"), Order.bNextSent);
	TestFalse(TEXT("and it carries no factory"), Order.bNextHasFactory);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverFrmStopsCleanTest, "SatisDash.Core.Driver.FrmStopsClean", SatisDash::Test::Flags)

bool FDriverFrmStopsCleanTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	if (!TestTrue(TEXT("an encode is held"), DriverFrmHoldEncode(F, Driver)))
	{
		return false;
	}
	const FAgent& Agent = Driver.GetAgent();
	F.Net.answer(DriverFrmSnapshots, SatisDash::Test::Reply(401, FString()));
	F.run(Driver, 1);
	TestTrue(TEXT("halted by the 401"), Agent.GetState() != EAgentState::Running);
	// Re-enrol while the old run's encode is still on the worker.
	F.writeConfig(TEXT("http://127.0.0.1:1"), TEXT("ZZZZ-2222-2222"));
	F.run(Driver, 61);
	F.Net.answer(TEXT("/agent/v1/enroll"), SatisDash::Test::Reply(201, SatisDash::Test::EnrollJson()));
	F.run(Driver, 1);
	TestTrue(TEXT("running again"), Agent.GetState() == EAgentState::Running);
	TArray<FString> Bodies = DriverFrmDrain(F);
	F.Worker.bHold = false;
	F.Worker.release();
	F.run(Driver, 6);
	Bodies.Append(DriverFrmDrain(F));
	TestTrue(TEXT("the new run sends snapshots"), Bodies.Num() > 0);
	for (const FString& Body : Bodies)
	{
		TestFalse(TEXT("nothing of the old run's factory reaches the new run"), Body.Contains(TEXT("\"factory\"")));
	}
	return true;
}
