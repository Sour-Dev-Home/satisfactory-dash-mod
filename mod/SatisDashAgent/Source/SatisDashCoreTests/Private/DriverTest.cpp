#include "DriverFakes.h"
#include "SatisDash/Driver.h"

using namespace SatisDash;
using SatisDash::Test::FDriverFixture;

namespace
{
	// Prefixed: the unity build merges test files, and RequestsTest has a local named Poll.
	const TCHAR* DriverSnapshots = TEXT("/agent/v1/snapshots");
	const TCHAR* DriverEnroll = TEXT("/agent/v1/enroll");
	const TCHAR* DriverPoll = TEXT("/agent/v1/commands?waitSeconds=25");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverEnrollThenPushTest, "SatisDash.Core.Driver.EnrollThenPush", SatisDash::Test::Flags)

bool FDriverEnrollThenPushTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock() };
	const FAgent& Agent = Driver.GetAgent();
	F.writeConfig(TEXT("http://127.0.0.1:1"), TEXT("AB3D-7XQ2"));
	Driver.Boot();
	Driver.Tick();
	TestEqual(TEXT("one enrol request"), F.Net.pending(DriverEnroll), 1);
	F.run(Driver, 5);
	TestEqual(TEXT("still one while it's out"), F.Net.sent(DriverEnroll), 1);
	TestEqual(TEXT("nothing pushed before enrolling"), F.Net.sent(DriverSnapshots), 0);
	F.Net.answer(DriverEnroll, SatisDash::Test::Reply(201, SatisDash::Test::EnrollJson()));
	TestTrue(TEXT("running"), Agent.GetState() == EAgentState::Running);
	F.run(Driver, 1);
	TestEqual(TEXT("a snapshot goes out"), F.Net.pending(DriverSnapshots), 1);
	SatisDash::Test::SameText(*this, TEXT("with the secret"), F.Net.lastAuth(DriverSnapshots), TEXT("Bearer ") + SatisDash::Test::FakeSecret());
	F.Net.answer(DriverSnapshots, SatisDash::Test::SnapshotOk(false));
	TestEqual(TEXT("and leaves the queue"), Driver.Queued(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverOneAtATimeTest, "SatisDash.Core.Driver.OneAtATime", SatisDash::Test::Flags)

bool FDriverOneAtATimeTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	F.run(Driver, 30);
	// Pusher.h: one FinishSend per StartSend; while the backend is slow, snapshots wait in the queue.
	TestEqual(TEXT("one send out at a time"), F.Net.pending(DriverSnapshots), 1);
	TestTrue(TEXT("the rest are queued"), Driver.Queued() >= 5);
	F.Net.answer(DriverSnapshots, SatisDash::Test::SnapshotOk(false));
	F.run(Driver, 0.5);
	TestEqual(TEXT("the next goes out after the answer"), F.Net.pending(DriverSnapshots), 1);
	F.Net.answer(DriverSnapshots, SatisDash::Test::Reply(0, FString()));
	F.run(Driver, 0.5);
	TestEqual(TEXT("a Code 0 answer (timeout) waits for the backoff"), F.Net.pending(DriverSnapshots), 0);
	F.run(Driver, 2);
	TestEqual(TEXT("then retries"), F.Net.pending(DriverSnapshots), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverPartsTest, "SatisDash.Core.Driver.Parts", SatisDash::Test::Flags)

bool FDriverPartsTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	auto answerAll = [&F] { while (F.Net.answer(TEXT("/agent/v1/snapshots"), SatisDash::Test::SnapshotOk(false))) {} };

	// Status every 5 s (the default cadence), power when due and only with a game running. Passes fall at +0.5 s and
	// +5.5 s (each is due 4.75 s after the last, met on the next half-second tick).
	const int32 Before = F.Net.sent(DriverSnapshots);
	for (int32 I = 0; I < 20; ++I) { F.run(Driver, 0.5); answerAll(); }
	TestEqual(TEXT("two passes in 10 s"), F.Net.sent(DriverSnapshots) - Before, 2);
	TestEqual(TEXT("power read with each"), F.Game.PowerReads, 2);
	TestTrue(TEXT("auto-pause goes with status"), F.Net.lastBody(DriverSnapshots).Contains(TEXT("\"autoPause\":false")));

	// No game (the entry map): status says so and no power is read (spec "Data flow").
	F.Game.Status->bIsGameRunning = false;
	const int32 Reads = F.Game.PowerReads;
	for (int32 I = 0; I < 20; ++I) { F.run(Driver, 0.5); answerAll(); }
	TestEqual(TEXT("no power without a game"), F.Game.PowerReads, Reads);
	TestTrue(TEXT("isGameRunning false is sent"), F.Net.lastBody(DriverSnapshots).Contains(TEXT("\"isGameRunning\":false")));
	TestFalse(TEXT("with no power part"), F.Net.lastBody(DriverSnapshots).Contains(TEXT("\"power\"")));

	// A failed reader leaves only its part out, and is logged once, not per pass.
	F.Game.Status->bIsGameRunning = true;
	F.Game.Power.Reset();
	for (int32 I = 0; I < 40; ++I) { F.run(Driver, 0.5); answerAll(); }
	int32 PowerLines = 0;
	for (const FString& Line : Log.Take())
	{
		PowerLines += Line.Contains(TEXT("power")) && Line.Contains(TEXT("leaving it out")) ? 1 : 0;
	}
	TestEqual(TEXT("one line for the power reader"), PowerLines, 1);
	TestTrue(TEXT("snapshots still go out"), F.Net.sent(DriverSnapshots) > Before + 4);
	SatisDash::Test::NoErrorLines(*this, Log);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverHaltTest, "SatisDash.Core.Driver.Halt", SatisDash::Test::Flags)

bool FDriverHaltTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock() };
	const FAgent& Agent = Driver.GetAgent();
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	F.run(Driver, 20);
	TestTrue(TEXT("a queue built up"), Driver.Queued() > 1);
	F.Net.answer(DriverSnapshots, SatisDash::Test::Reply(401, TEXT("{}")));
	TestTrue(TEXT("halted"), Agent.GetState() == EAgentState::Halted);
	TestFalse(TEXT("credential deleted"), IFileManager::Get().FileExists(*F.CredentialPath));
	const int32 Pushed = F.Net.sent(DriverSnapshots);
	const int32 Polled = F.Net.sent(DriverPoll);
	// Task 5: polls were going out before the halt, so "nothing more polled" below can fail.
	TestTrue(TEXT("a poll went out before the halt"), Polled > 0);
	F.run(Driver, 120);
	TestEqual(TEXT("queue cleared"), Driver.Queued(), 0);
	TestEqual(TEXT("nothing more pushed"), F.Net.sent(DriverSnapshots), Pushed);
	TestEqual(TEXT("and nothing more polled"), F.Net.sent(DriverPoll), Polled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverReboundTest, "SatisDash.Core.Driver.Rebound", SatisDash::Test::Flags)

bool FDriverReboundTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock() };
	const FAgent& Agent = Driver.GetAgent();
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	F.run(Driver, 20);
	TestEqual(TEXT("a send is out"), F.Net.pending(DriverSnapshots), 1);
	// The config now names another backend (no code): the credential belongs to the first one (S10).
	F.writeConfig(TEXT("https://other.example"), TEXT(""));
	F.run(Driver, 61);
	TestTrue(TEXT("no credential for the new backend"), Agent.GetCredential() == nullptr);
	TestTrue(TEXT("the queue waits for the send in flight"), Driver.Queued() > 0);
	F.Net.answer(DriverSnapshots, SatisDash::Test::SnapshotOk(false));
	F.run(Driver, 1);
	TestEqual(TEXT("then it's cleared"), Driver.Queued(), 0);
	for (const SatisDash::FRequestSpec& Spec : F.Net.Sent)
	{
		TestFalse(TEXT("nothing ever went to the new backend"), Spec.Url.StartsWith(TEXT("https://other.example")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverReenrolTest, "SatisDash.Core.Driver.Reenrol", SatisDash::Test::Flags)

bool FDriverReenrolTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	F.run(Driver, 1);
	F.Net.answer(DriverSnapshots, SatisDash::Test::Reply(401, TEXT("{}")));
	F.run(Driver, 10);
	// A new code from the dashboard: the next config check enrols again, with a new secret.
	F.writeConfig(TEXT("http://127.0.0.1:1"), TEXT("ZZZZ-2222"));
	F.run(Driver, 61);
	TestEqual(TEXT("a second enrolment"), F.Net.pending(DriverEnroll), 1);
	const FString Second = FString::ChrN(42, TEXT('b')) + TEXT("_");
	F.Net.answer(DriverEnroll, SatisDash::Test::Reply(201, FString::Printf(
		TEXT("{\"agentSecret\":\"%s\",\"serverId\":\"srv-1\",\"cadence\":%s}"), *Second, SatisDash::Test::CadenceJson)));
	F.run(Driver, 1);
	SatisDash::Test::SameText(*this, TEXT("the new secret is used"), F.Net.lastAuth(DriverSnapshots), TEXT("Bearer ") + Second);
	TestTrue(TEXT("only fresh snapshots are queued"), Driver.Queued() <= 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverCadenceTest, "SatisDash.Core.Driver.Cadence", SatisDash::Test::Flags)

bool FDriverCadenceTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock() };
	const FAgent& Agent = Driver.GetAgent();
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	F.run(Driver, 1);
	F.Net.answer(DriverSnapshots, SatisDash::Test::Reply(200,
		TEXT("{\"cadence\":{\"statusSeconds\":20,\"powerSeconds\":60,\"factorySeconds\":30},\"commandsPending\":false}")));
	TestEqual(TEXT("the backend's cadence is taken"), Agent.GetCadence().StatusSeconds, 20);
	const int32 Before = F.Net.sent(DriverSnapshots);
	for (int32 I = 0; I < 80; ++I)
	{
		F.run(Driver, 0.5);
		while (F.Net.answer(DriverSnapshots, SatisDash::Test::Reply(200,
			TEXT("{\"cadence\":{\"statusSeconds\":20,\"powerSeconds\":60,\"factorySeconds\":30},\"commandsPending\":false}")))) {}
	}
	TestEqual(TEXT("one pass per 20 s"), F.Net.sent(DriverSnapshots) - Before, 2);
	return true;
}

namespace
{
	/**
	 * The review's sequence: Running on backend A with a push in flight, then the config names backend B with a new
	 * code, and B's enrolment succeeds while A's push is still out. Returns false if B never enrolled.
	 */
	bool driverMoveToSecondBackend(FDriverFixture& F, FDriver& Driver, const FString& Secret)
	{
		F.enrol(Driver, SatisDash::Test::FakeSecret());
		F.run(Driver, 20);
		F.writeConfig(TEXT("http://localhost:2"), TEXT("ZZZZ-2222"));
		F.run(Driver, 61);
		if (F.Net.pending(DriverSnapshots) != 1 || F.Net.pending(DriverEnroll) != 1)
		{
			return false;
		}
		F.Net.answer(DriverEnroll, SatisDash::Test::Reply(201, FString::Printf(
			TEXT("{\"agentSecret\":\"%s\",\"serverId\":\"srv-2\",\"cadence\":%s}"), *Secret, SatisDash::Test::CadenceJson)));
		const FAgent& Agent = Driver.GetAgent();
		return Agent.GetState() == EAgentState::Running;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverStaleAuthTest, "SatisDash.Core.Driver.StaleAuth", SatisDash::Test::Flags)

bool FDriverStaleAuthTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock() };
	const FAgent& Agent = Driver.GetAgent();
	const FString Second = FString::ChrN(42, TEXT('b')) + TEXT("_");
	TestTrue(TEXT("enrolled at B with A's push still out"), driverMoveToSecondBackend(F, Driver, Second));
	// A revoked this server (the usual first step of a move): its 401 is about A's credential, never B's.
	F.Net.answer(DriverSnapshots, SatisDash::Test::Reply(401, TEXT("{}")));
	TestTrue(TEXT("still running on B"), Agent.GetState() == EAgentState::Running);
	TestTrue(TEXT("B's credential file is kept"), IFileManager::Get().FileExists(*F.CredentialPath));
	const FCredential* Held = Agent.GetCredential();
	TestTrue(TEXT("B's credential is held"), Held != nullptr);
	if (Held != nullptr)
	{
		SatisDash::Test::SameText(*this, TEXT("B's secret"), Held->AgentSecret, Second);
	}
	F.run(Driver, 1);
	TestEqual(TEXT("the old queue was cleared, then a fresh snapshot sent"), F.Net.pending(DriverSnapshots), 1);
	SatisDash::Test::SameText(*this, TEXT("to B, with B's secret"), F.Net.lastAuth(DriverSnapshots), TEXT("Bearer ") + Second);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverStaleCadenceTest, "SatisDash.Core.Driver.StaleCadence", SatisDash::Test::Flags)

bool FDriverStaleCadenceTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock() };
	const FAgent& Agent = Driver.GetAgent();
	TestTrue(TEXT("enrolled at B with A's push still out"), driverMoveToSecondBackend(F, Driver, FString::ChrN(42, TEXT('b')) + TEXT("_")));
	F.Net.answer(DriverSnapshots, SatisDash::Test::Reply(200,
		TEXT("{\"cadence\":{\"statusSeconds\":20,\"powerSeconds\":60,\"factorySeconds\":30},\"commandsPending\":false}")));
	TestEqual(TEXT("A's late cadence is not taken"), Agent.GetCadence().StatusSeconds, 5);
	TestTrue(TEXT("still running on B"), Agent.GetState() == EAgentState::Running);
	return true;
}
