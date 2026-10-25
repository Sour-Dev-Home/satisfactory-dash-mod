#include "DriverFakes.h"
#include "SatisDash/Driver.h"

using namespace SatisDash;
using SatisDash::Test::FDriverFixture;

namespace
{
	const TCHAR* DriverWorldSnapshots = TEXT("/agent/v1/snapshots");
	const TCHAR* DriverWorldRailsRead = TEXT("/getTrainRails");
	const TCHAR* DriverWorldRailsPost = TEXT("/agent/v1/world/rails");
	const TCHAR* DriverWorldRails = TEXT("[{\"ID\":\"R1\",\"SplineData\":[{\"x\":0,\"y\":0},{\"x\":300,\"y\":0}]}]");
	const TCHAR* DriverWorldAccepted = TEXT("{\"accepted\":true,\"unchanged\":false}");

	/** The routine traffic: snapshots accepted (unless the test wants one open), live and node FRM reads failing fast. */
	void DriverWorldRoutine(FDriverFixture& F, bool bSnapshots)
	{
		while (bSnapshots && F.Net.pending(DriverWorldSnapshots) > 0)
		{
			F.Net.answer(DriverWorldSnapshots, SatisDash::Test::SnapshotOk(false));
		}
		for (const TCHAR* Path : { TEXT("/getPlayer"), TEXT("/getFactory"), TEXT("/getTrains"), TEXT("/getTrainStation"), TEXT("/getResourceNode") })
		{
			F.Net.answer(Path, SatisDash::Test::Reply(0, FString()));
		}
	}

	using FDriverWorldCheck = TFunction<bool()>;

	/** Runs the driver up to Seconds, answering the routine traffic each tick; true as soon as the check is. */
	bool DriverWorldRun(FDriverFixture& F, FDriver& Driver, const FDriverWorldCheck& reached, double Seconds, bool bSnapshots = true)
	{
		for (double Spent = 0; Spent < Seconds; Spent += 0.5)
		{
			DriverWorldRoutine(F, bSnapshots);
			if (reached())
			{
				return true;
			}
			F.run(Driver, 0.5);
		}
		DriverWorldRoutine(F, bSnapshots);
		return reached();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverWorldTest, "SatisDash.Core.Driver.World", SatisDash::Test::Flags)

bool FDriverWorldTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	const FAgent& Agent = Driver.GetAgent();
	TestEqual(TEXT("no FRM read before enrolling"), F.Net.sent(DriverWorldRailsRead), 0);
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	const double StartedAt = F.T;
	TestTrue(TEXT("rails are read once the agent has run for about 2 minutes"), DriverWorldRun(F, Driver, [&F] { return F.Net.pending(DriverWorldRailsRead) > 0; }, 200));
	const double Waited = F.T - StartedAt;
	TestTrue(TEXT("between 120 s - 10% and 120 s + 10% (plus a tick or two)"), Waited >= 108.0 && Waited <= 140.0);

	// Posts go behind snapshots (Driver.cpp's idle gate). Hold a snapshot open, then let the rails body become ready.
	TestTrue(TEXT("a snapshot is in flight"), DriverWorldRun(F, Driver, [&F] { return F.Net.pending(DriverWorldSnapshots) > 0; }, 20, false));
	F.Net.answer(DriverWorldRailsRead, SatisDash::Test::Reply(200, DriverWorldRails));
	DriverWorldRun(F, Driver, []() { return false; }, 10, false);
	TestEqual(TEXT("no world post while a snapshot is in flight"), F.Net.sent(DriverWorldRailsPost), 0);
	// The send fails and backs off (Retry-After 20 s): nothing in flight, but snapshots are queued.
	FHttpReply Unwell = SatisDash::Test::Reply(503, FString());
	Unwell.RetryAfter = TEXT("20");
	F.Net.answer(DriverWorldSnapshots, Unwell);
	DriverWorldRun(F, Driver, []() { return false; }, 15, false);
	TestEqual(TEXT("nothing in flight"), F.Net.pending(DriverWorldSnapshots), 0);
	TestTrue(TEXT("snapshots are queued"), Driver.Queued() > 0);
	TestEqual(TEXT("no world post while snapshots are queued, even with none in flight"), F.Net.sent(DriverWorldRailsPost), 0);
	// Once the snapshot side drains, the ready body goes.
	TestTrue(TEXT("the layer is posted"), DriverWorldRun(F, Driver, [&F] { return F.Net.pending(DriverWorldRailsPost) > 0; }, 30));
	TestTrue(TEXT("with the layer's data"), F.Net.lastBody(DriverWorldRailsPost).Contains(TEXT("\"data\":[{\"id\":\"R1\",\"points\":[[0,0],[3,0]]}]")));
	SatisDash::Test::SameText(*this, TEXT("and the credential"), F.Net.lastAuth(DriverWorldRailsPost), TEXT("Bearer ") + SatisDash::Test::FakeSecret());
	F.Net.answer(DriverWorldRailsPost, SatisDash::Test::Reply(200, DriverWorldAccepted));
	DriverWorldRun(F, Driver, []() { return false; }, 20);
	TestEqual(TEXT("one post"), F.Net.sent(DriverWorldRailsPost), 1);
	TestTrue(TEXT("and the agent is still running"), Agent.GetState() == EAgentState::Running);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverWorldHaltTest, "SatisDash.Core.Driver.WorldHalt", SatisDash::Test::Flags)

bool FDriverWorldHaltTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	const FAgent& Agent = Driver.GetAgent();
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	TestTrue(TEXT("rails are read"), DriverWorldRun(F, Driver, [&F] { return F.Net.pending(DriverWorldRailsRead) > 0; }, 200));
	F.Net.answer(DriverWorldRailsRead, SatisDash::Test::Reply(200, DriverWorldRails));
	TestTrue(TEXT("and posted"), DriverWorldRun(F, Driver, [&F] { return F.Net.pending(DriverWorldRailsPost) > 0; }, 30));
	F.Net.answer(DriverWorldRailsPost, SatisDash::Test::Reply(401, FString()));
	F.run(Driver, 1);
	TestTrue(TEXT("a 401 on a world post halts the agent, as a snapshot 401 does"), Agent.GetState() != EAgentState::Running);
	DriverWorldRun(F, Driver, []() { return false; }, 700);
	TestEqual(TEXT("no more FRM reads"), F.Net.sent(DriverWorldRailsRead), 1);
	TestEqual(TEXT("no more world posts"), F.Net.sent(DriverWorldRailsPost), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverWorldStopsTest, "SatisDash.Core.Driver.WorldStops", SatisDash::Test::Flags)

bool FDriverWorldStopsTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	const FAgent& Agent = Driver.GetAgent();
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	TestTrue(TEXT("rails are read"), DriverWorldRun(F, Driver, [&F] { return F.Net.pending(DriverWorldRailsRead) > 0; }, 200));

	// The agent is revoked (a snapshot 401) while the rails read is in flight.
	TestTrue(TEXT("a snapshot is open"), DriverWorldRun(F, Driver, [&F] { return F.Net.pending(DriverWorldSnapshots) > 0; }, 20, false));
	F.Net.answer(DriverWorldSnapshots, SatisDash::Test::Reply(401, FString()));
	F.run(Driver, 1);
	TestTrue(TEXT("halted"), Agent.GetState() != EAgentState::Running);
	F.Net.answer(DriverWorldRailsRead, SatisDash::Test::Reply(200, DriverWorldRails)); // the old run's reply, late
	F.run(Driver, 30);
	TestEqual(TEXT("nothing of the old run is posted"), F.Net.sent(DriverWorldRailsPost), 0);

	// Re-enrol: the schedule starts again from the new Running edge.
	F.writeConfig(TEXT("http://127.0.0.1:1"), TEXT("ZZZZ-2222-2222"));
	F.run(Driver, 61);
	F.Net.answer(TEXT("/agent/v1/enroll"), SatisDash::Test::Reply(201, SatisDash::Test::EnrollJson()));
	F.run(Driver, 1);
	TestTrue(TEXT("running again"), Agent.GetState() == EAgentState::Running);
	const double RestartedAt = F.T;
	const int32 Reads = F.Net.sent(DriverWorldRailsRead);
	TestFalse(TEXT("no read in the first 100 s of the new run"), DriverWorldRun(F, Driver, [&F, Reads] { return F.Net.sent(DriverWorldRailsRead) > Reads; }, 100));
	TestTrue(TEXT("then rails are read again, about 2 minutes after the restart"), DriverWorldRun(F, Driver, [&F, Reads] { return F.Net.sent(DriverWorldRailsRead) > Reads; }, 60));
	TestTrue(TEXT("not before 108 s"), F.T - RestartedAt >= 108.0);
	TestEqual(TEXT("and still nothing posted"), F.Net.sent(DriverWorldRailsPost), 0);
	return true;
}
