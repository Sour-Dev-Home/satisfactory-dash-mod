#include "DriverFakes.h"
#include "SatisDash/Driver.h"

using namespace SatisDash;
using SatisDash::Test::FDriverFixture;

// R7: these three tests were the FRM-read half of the world sender's driver wiring (rails read from FRM two minutes after Running).
// They now drive the same wiring through the game's own rails read (the shared logistics pass, 528 s after Running with the fake
// clock's jitter draw of 1.0): the posts go behind snapshots and encodes, a 401 halts, a revoked run leaves nothing behind.

namespace
{
	const TCHAR* DriverWorldSnapshots = TEXT("/agent/v1/snapshots");
	const TCHAR* DriverWorldRailsPost = TEXT("/agent/v1/world/rails");
	const TCHAR* DriverWorldAccepted = TEXT("{\"accepted\":true,\"unchanged\":false}");
	/** The native first read: 480 s x (1 + 10%) with the fake clock's jitter draw of 1.0. */
	constexpr double DriverWorldFirstReadSeconds = 528.0;

	/** The rails read alone is wanted: the game refuses the other readers that would post a layer (nodes, markers, drone routes) or add a part. */
	void DriverWorldRailsOnly(FDriverFixture& F)
	{
		F.Game.bHasWorld = false;
		F.Game.bBeginMarkersOk = false;
		F.Game.bBeginDronesOk = false;
		F.Game.bBeginBuildablesOk = false;
		FRailReading Track;
		Track.Id = TEXT("R1");
		Track.Points = { { 0.0, 0.0 }, { 300.0, 0.0 } };
		F.Game.LogisticsRails = { Track };
		F.Game.LogisticsCounts.Buildables = 3;
		F.Game.LogisticsCounts.Tracks = 1;
	}

	/** The routine traffic: snapshots accepted (unless the test wants one open). */
	void DriverWorldRoutine(FDriverFixture& F, bool bSnapshots)
	{
		while (bSnapshots && F.Net.pending(DriverWorldSnapshots) > 0)
		{
			F.Net.answer(DriverWorldSnapshots, SatisDash::Test::SnapshotOk(false));
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
	DriverWorldRailsOnly(F);
	F.Game.LogisticsStepsToFinish = 1000000; // the pass stays open until the test lets it finish
	FTrainReading Train;
	Train.Id = TEXT("T1");
	Train.Name = TEXT("A");
	Train.State = ETrainState::SelfDriving;
	Train.XCm = 15000;
	Train.YCm = -15000;
	F.Game.Trains = { Train };
	FStationReading Station;
	Station.Id = TEXT("S1");
	Station.Name = TEXT("B");
	Station.XCm = 504900;
	Station.YCm = -505100;
	F.Game.Stations = { Station };
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	const FAgent& Agent = Driver.GetAgent();
	TestEqual(TEXT("no pass before enrolling"), F.Game.LogisticsBegins, 0);
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	const double StartedAt = F.T;
	TestTrue(TEXT("rails are read once the agent has run for about 8 minutes"), DriverWorldRun(F, Driver, [&F] { return F.Game.LogisticsBegins > 0; }, 600));
	const double Waited = F.T - StartedAt;
	TestTrue(TEXT("between 528 s and 528 s plus a tick or two"), Waited >= DriverWorldFirstReadSeconds && Waited <= DriverWorldFirstReadSeconds + 30.0);

	// Posts go behind snapshots (Driver.cpp's idle gate). Hold a snapshot open, then let the rails body become ready.
	TestTrue(TEXT("a snapshot is in flight"), DriverWorldRun(F, Driver, [&F] { return F.Net.pending(DriverWorldSnapshots) > 0; }, 20, false));
	F.Game.LogisticsStepsToFinish = 3;
	DriverWorldRun(F, Driver, []() { return false; }, 10, false);
	TestEqual(TEXT("no world post while a snapshot is in flight"), F.Net.sent(DriverWorldRailsPost), 0);
	// The send fails and backs off (Retry-After 120 s): nothing in flight, but snapshots are queued.
	FHttpReply Unwell = SatisDash::Test::Reply(503, FString());
	Unwell.RetryAfter = TEXT("120");
	F.Net.answer(DriverWorldSnapshots, Unwell);
	DriverWorldRun(F, Driver, []() { return false; }, 15, false);
	TestEqual(TEXT("nothing in flight"), F.Net.pending(DriverWorldSnapshots), 0);
	TestTrue(TEXT("snapshots are queued"), Driver.Queued() > 0);
	TestEqual(TEXT("no world post while snapshots are queued, even with none in flight"), F.Net.sent(DriverWorldRailsPost), 0);
	// A snapshot carrying mapLive is being encoded (the worker is held), still in the backoff: the encode is queued work too.
	F.Worker.bHold = true;
	bool bEncoding = false;
	for (double Spent = 0; Spent < 100 && !bEncoding; Spent += 0.5)
	{
		F.run(Driver, 0.5);
		bEncoding = Driver.EncodePending();
		if (!bEncoding)
		{
			F.Worker.release(); // a build job: let it finish
		}
	}
	TestTrue(TEXT("a snapshot with mapLive is held in its encode"), bEncoding);
	// The backoff ends and the queue drains; the encode is still held (no new snapshot is taken while it is).
	TestTrue(TEXT("the queued snapshots are all sent"), DriverWorldRun(F, Driver, [&F, &Driver] { return Driver.Queued() == 0 && F.Net.pending(DriverWorldSnapshots) == 0; }, 300));
	DriverWorldRun(F, Driver, []() { return false; }, 15);
	TestTrue(TEXT("still encoding"), Driver.EncodePending());
	TestEqual(TEXT("no world post while a snapshot is being encoded, with nothing queued or in flight"), F.Net.sent(DriverWorldRailsPost), 0);
	F.Worker.bHold = false;
	F.Worker.release();
	// Once the snapshot side is idle again, the ready body goes.
	TestTrue(TEXT("the layer is posted"), DriverWorldRun(F, Driver, [&F]
	{
		F.Net.answer(TEXT("/agent/v1/world/trainRoutes"), SatisDash::Test::Reply(200, DriverWorldAccepted)); // posts one at a time: the train routes may be first
		return F.Net.pending(DriverWorldRailsPost) > 0;
	}, 30));
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
	DriverWorldRailsOnly(F);
	F.Game.bTrainsWorld = false; // no train routes layer, so the rails are the first world post
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	const FAgent& Agent = Driver.GetAgent();
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	TestTrue(TEXT("rails are read"), DriverWorldRun(F, Driver, [&F] { return F.Game.LogisticsBegins > 0; }, 600));
	TestTrue(TEXT("and posted"), DriverWorldRun(F, Driver, [&F] { return F.Net.pending(DriverWorldRailsPost) > 0; }, 30));
	F.Net.answer(DriverWorldRailsPost, SatisDash::Test::Reply(401, FString()));
	F.run(Driver, 1);
	TestTrue(TEXT("a 401 on a world post halts the agent, as a snapshot 401 does"), Agent.GetState() != EAgentState::Running);
	const int32 Begins = F.Game.LogisticsBegins;
	DriverWorldRun(F, Driver, []() { return false; }, 700);
	TestEqual(TEXT("no more passes"), F.Game.LogisticsBegins, Begins);
	TestEqual(TEXT("no more world posts"), F.Net.sent(DriverWorldRailsPost), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverWorldStopsTest, "SatisDash.Core.Driver.WorldStops", SatisDash::Test::Flags)

bool FDriverWorldStopsTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	DriverWorldRailsOnly(F);
	F.Game.bTrainsWorld = false;
	F.Game.LogisticsStepsToFinish = 1000000; // the pass is still open when the agent is revoked
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	const FAgent& Agent = Driver.GetAgent();
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	TestTrue(TEXT("rails are read"), DriverWorldRun(F, Driver, [&F] { return F.Game.LogisticsStepCalls >= 1; }, 600));

	// The agent is revoked (a snapshot 401) while the rails pass is open.
	TestTrue(TEXT("a snapshot is open"), DriverWorldRun(F, Driver, [&F] { return F.Net.pending(DriverWorldSnapshots) > 0; }, 20, false));
	const int32 Ends = F.Game.LogisticsEnds;
	F.Net.answer(DriverWorldSnapshots, SatisDash::Test::Reply(401, FString()));
	F.run(Driver, 1);
	TestTrue(TEXT("halted"), Agent.GetState() != EAgentState::Running);
	TestTrue(TEXT("the open pass was ended"), F.Game.LogisticsEnds > Ends);
	F.Game.LogisticsStepsToFinish = 3;
	F.run(Driver, 30);
	TestEqual(TEXT("nothing of the old run is posted"), F.Net.sent(DriverWorldRailsPost), 0);

	// Re-enrol: the schedule starts again from the new Running edge.
	F.writeConfig(TEXT("http://127.0.0.1:1"), TEXT("ZZZZ-2222-2222"));
	F.run(Driver, 61);
	F.Net.answer(TEXT("/agent/v1/enroll"), SatisDash::Test::Reply(201, SatisDash::Test::EnrollJson()));
	F.run(Driver, 1);
	TestTrue(TEXT("running again"), Agent.GetState() == EAgentState::Running);
	const double RestartedAt = F.T;
	const int32 Begins = F.Game.LogisticsBegins;
	TestFalse(TEXT("no pass in the first 500 s of the new run"), DriverWorldRun(F, Driver, [&F, Begins] { return F.Game.LogisticsBegins > Begins; }, 500));
	TestEqual(TEXT("and still nothing posted"), F.Net.sent(DriverWorldRailsPost), 0);
	TestTrue(TEXT("then rails are read again, about 8 minutes after the restart"), DriverWorldRun(F, Driver, [&F, Begins] { return F.Game.LogisticsBegins > Begins; }, 60));
	TestTrue(TEXT("not before 500 s"), F.T - RestartedAt >= 500.0);
	return true;
}
