#include "DriverFakes.h"
#include "SatisDash/Driver.h"

using namespace SatisDash;
using SatisDash::Test::FDriverFixture;

namespace
{
	// Prefixed: the unity build can merge this file with DriverTest and RequestsTest, which have their own names.
	const TCHAR* CommandsPoll = TEXT("/agent/v1/commands?waitSeconds=25");
	const TCHAR* CommandsSnapshots = TEXT("/agent/v1/snapshots");
	const TCHAR* CommandsEnroll = TEXT("/agent/v1/enroll");

	FString commandsItem(const TCHAR* Id, const TCHAR* Type, const TCHAR* Params, const FDateTime& Expires)
	{
		return FString::Printf(TEXT("{\"id\":\"%s\",\"type\":\"%s\",\"params\":%s,\"expiresAt\":\"%s\"}"), Id, Type, Params, *Expires.ToIso8601());
	}

	FHttpReply commandsReply(const FString& Items)
	{
		return SatisDash::Test::Reply(200, FString::Printf(TEXT("{\"commands\":[%s]}"), *Items));
	}

	FString commandsResultPath(const TCHAR* Id) { return FString::Printf(TEXT("/agent/v1/commands/%s/result"), Id); }

	/** Enrolled, with every snapshot answered, until the first poll is out. */
	void commandsReady(FDriverFixture& F, FDriver& Driver)
	{
		F.enrol(Driver, SatisDash::Test::FakeSecret());
		F.run(Driver, 1);
		while (F.Net.answer(CommandsSnapshots, SatisDash::Test::SnapshotOk(false))) {}
	}

	/**
	 * DriverTest's stale sequence with commands: Running on backend A with A's first poll out (snapshots answered), then
	 * the config names backend B with a new code and B enrols. A's late snapshot answers are given (stale, ignored), so
	 * only A's poll, or whatever the caller left out on A, is still open. False if B never enrolled.
	 */
	bool commandsMoveToSecondBackend(FDriverFixture& F, FDriver& Driver, const FString& Secret)
	{
		F.writeConfig(TEXT("http://localhost:2"), TEXT("ZZZZ-2222"));
		F.run(Driver, 61);
		if (F.Net.pending(CommandsEnroll) != 1)
		{
			return false;
		}
		F.Net.answer(CommandsEnroll, SatisDash::Test::Reply(201, FString::Printf(
			TEXT("{\"agentSecret\":\"%s\",\"serverId\":\"srv-2\",\"cadence\":%s}"), *Secret, SatisDash::Test::CadenceJson)));
		while (F.Net.answer(CommandsSnapshots, SatisDash::Test::SnapshotOk(false))) {}
		const FAgent& Agent = Driver.GetAgent();
		return Agent.GetState() == EAgentState::Running;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommandsSetAutoPauseTest, "SatisDash.Core.Commands.SetAutoPause", SatisDash::Test::Flags)

bool FCommandsSetAutoPauseTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock() };
	commandsReady(F, Driver);
	TestEqual(TEXT("a poll is out"), F.Net.pending(CommandsPoll), 1);
	const FDateTime Expires = F.Utc + FTimespan::FromMinutes(5);
	F.Net.answer(CommandsPoll, commandsReply(commandsItem(TEXT("c-1"), TEXT("set_auto_pause"), TEXT("{\"enabled\":true}"), Expires)));
	F.run(Driver, 1);
	TestEqual(TEXT("applied once"), F.Game.Applied.Num(), 1);
	TestTrue(TEXT("with enabled"), F.Game.Applied.Num() == 1 && F.Game.Applied[0]);
	TestEqual(TEXT("its result is posted"), F.Net.pending(*commandsResultPath(TEXT("c-1"))), 1);
	SatisDash::Test::SameText(*this, TEXT("ok"), F.Net.lastBody(*commandsResultPath(TEXT("c-1"))), TEXT("{\"ok\":true}"));
	F.Net.answer(*commandsResultPath(TEXT("c-1")), SatisDash::Test::Reply(204, FString()));
	// The backend hands it out again (its answer was lost): it is reported again, never run again (S6).
	F.run(Driver, 1.5);
	F.Net.answer(CommandsPoll, commandsReply(commandsItem(TEXT("c-1"), TEXT("set_auto_pause"), TEXT("{\"enabled\":true}"), Expires)));
	F.run(Driver, 1);
	TestEqual(TEXT("not applied twice"), F.Game.Applied.Num(), 1);
	TestEqual(TEXT("but reported again"), F.Net.sent(*commandsResultPath(TEXT("c-1"))), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommandsUnsupportedTest, "SatisDash.Core.Commands.Unsupported", SatisDash::Test::Flags)

bool FCommandsUnsupportedTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock() };
	commandsReady(F, Driver);
	const FDateTime Expires = F.Utc + FTimespan::FromMinutes(5);
	F.Net.answer(CommandsPoll, commandsReply(commandsItem(TEXT("c-2"), TEXT("SET_AUTO_PAUSE"), TEXT("{\"enabled\":true}"), Expires)
		+ TEXT(",") + commandsItem(TEXT("c-3"), TEXT("set_auto_pause"), TEXT("{\"enabled\":\"yes\"}"), Expires)));
	F.run(Driver, 1);
	SatisDash::Test::SameText(*this, TEXT("a type in another case is unsupported"), F.Net.lastBody(*commandsResultPath(TEXT("c-2"))), TEXT("{\"ok\":false,\"code\":\"unsupported\"}"));
	F.Net.answer(*commandsResultPath(TEXT("c-2")), SatisDash::Test::Reply(204, FString()));
	F.run(Driver, 1);
	SatisDash::Test::SameText(*this, TEXT("a wrong-type param is unsupported"), F.Net.lastBody(*commandsResultPath(TEXT("c-3"))), TEXT("{\"ok\":false,\"code\":\"unsupported\"}"));
	TestEqual(TEXT("nothing applied"), F.Game.Applied.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommandsApplyFailsTest, "SatisDash.Core.Commands.ApplyFails", SatisDash::Test::Flags)

bool FCommandsApplyFailsTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	F.Game.bApplyWorks = false;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock() };
	commandsReady(F, Driver);
	const FDateTime Expires = F.Utc + FTimespan::FromMinutes(5);
	F.Net.answer(CommandsPoll, commandsReply(commandsItem(TEXT("c-4"), TEXT("set_auto_pause"), TEXT("{\"enabled\":false}"), Expires)));
	F.run(Driver, 1);
	SatisDash::Test::SameText(*this, TEXT("upstream_error"), F.Net.lastBody(*commandsResultPath(TEXT("c-4"))), TEXT("{\"ok\":false,\"code\":\"upstream_error\"}"));
	F.Net.answer(*commandsResultPath(TEXT("c-4")), SatisDash::Test::Reply(204, FString()));
	F.run(Driver, 1.5);
	F.Net.answer(CommandsPoll, commandsReply(commandsItem(TEXT("c-4"), TEXT("set_auto_pause"), TEXT("{\"enabled\":false}"), Expires)));
	F.run(Driver, 1);
	TestEqual(TEXT("the ledger keeps the ID: tried once"), F.Game.Applied.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommandsResultRetriesTest, "SatisDash.Core.Commands.ResultRetries", SatisDash::Test::Flags)

bool FCommandsResultRetriesTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock() };
	commandsReady(F, Driver);
	const FString Path = commandsResultPath(TEXT("c-5"));
	F.Net.answer(CommandsPoll, commandsReply(commandsItem(TEXT("c-5"), TEXT("set_auto_pause"), TEXT("{\"enabled\":true}"), F.Utc + FTimespan::FromMinutes(5))));
	for (int32 Attempt = 0; Attempt < 10; ++Attempt)
	{
		F.run(Driver, 20);
		F.Net.answer(*Path, SatisDash::Test::Reply(503, FString()));
	}
	TestEqual(TEXT("five attempts, no more (commandRunner.ts RESULT_ATTEMPTS)"), F.Net.sent(*Path), ResultAttempts);
	int32 Lines = 0;
	for (const FString& Line : Log.Take()) { Lines += Line.Contains(TEXT("Couldn't report")) ? 1 : 0; }
	TestEqual(TEXT("one line when it gives up"), Lines, 1);
	TestTrue(TEXT("polling resumes"), F.Net.pending(CommandsPoll) == 1);
	SatisDash::Test::NoErrorLines(*this, Log);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommandsPollBackoffTest, "SatisDash.Core.Commands.PollBackoff", SatisDash::Test::Flags)

bool FCommandsPollBackoffTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock() };
	const FAgent& Agent = Driver.GetAgent();
	commandsReady(F, Driver);
	F.Net.answer(CommandsPoll, SatisDash::Test::Reply(0, FString()));
	F.run(Driver, 0.5);
	TestEqual(TEXT("no poll during the backoff"), F.Net.pending(CommandsPoll), 0);
	F.run(Driver, 1.5);
	TestEqual(TEXT("then one"), F.Net.pending(CommandsPoll), 1);
	F.Net.answer(CommandsPoll, SatisDash::Test::Reply(502, FString()));
	F.run(Driver, 4.5);
	F.Net.answer(CommandsPoll, commandsReply(FString()));
	int32 Failed = 0;
	int32 Recovered = 0;
	for (const FString& Line : Log.Take())
	{
		Failed += Line.Contains(TEXT("Polling for commands failed")) ? 1 : 0;
		Recovered += Line.Contains(TEXT("Polling for commands recovered")) ? 1 : 0;
	}
	TestEqual(TEXT("one line when failures start"), Failed, 1);
	TestEqual(TEXT("one when they stop"), Recovered, 1);
	F.run(Driver, 1.5);
	F.Net.answer(CommandsPoll, SatisDash::Test::Reply(401, TEXT("{}")));
	TestTrue(TEXT("a 401 on the poll halts"), Agent.GetState() == EAgentState::Halted);
	SatisDash::Test::NoErrorLines(*this, Log);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommandsSpacingTest, "SatisDash.Core.Commands.Spacing", SatisDash::Test::Flags)

bool FCommandsSpacingTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock() };
	commandsReady(F, Driver);
	F.Net.answer(CommandsPoll, commandsReply(FString()));
	// The next poll is sent; this time it's answered at once, the way an empty long poll can come back.
	F.runUntil(Driver, [&F] { return F.Net.pending(CommandsPoll) > 0; }, 5);
	F.Net.answer(CommandsPoll, commandsReply(FString()));
	F.run(Driver, 0.5);
	TestEqual(TEXT("an empty answer is spaced by 1 s from the poll's start"), F.Net.pending(CommandsPoll), 0);
	F.run(Driver, 0.5);
	TestEqual(TEXT("then polled again"), F.Net.pending(CommandsPoll), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommandsPendingHintTest, "SatisDash.Core.Commands.PendingHint", SatisDash::Test::Flags)

bool FCommandsPendingHintTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock() };
	commandsReady(F, Driver);
	for (int32 I = 0; I < 4; ++I)
	{
		F.runUntil(Driver, [&F] { return F.Net.pending(CommandsPoll) > 0; }, 30);
		F.Net.answer(CommandsPoll, SatisDash::Test::Reply(0, FString()));
	}
	// The fourth failure waits 8 s (2^3 with Random01 = 1), longer than a status cycle.
	F.run(Driver, 0.5);
	TestEqual(TEXT("waiting out a backoff"), F.Net.pending(CommandsPoll), 0);
	// commandRunner.ts notifyPending: a snapshot answer saying a command waits ends the wait.
	F.runUntil(Driver, [&F] { return F.Net.pending(CommandsSnapshots) > 0; }, 6);
	F.Net.answer(CommandsSnapshots, SatisDash::Test::SnapshotOk(true));
	F.run(Driver, 0.5);
	TestEqual(TEXT("polled at once"), F.Net.pending(CommandsPoll), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommandsHaltTest, "SatisDash.Core.Commands.Halt", SatisDash::Test::Flags)

bool FCommandsHaltTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock() };
	const FAgent& Agent = Driver.GetAgent();
	commandsReady(F, Driver);
	const FDateTime Expires = F.Utc + FTimespan::FromMinutes(5);
	F.Net.answer(CommandsPoll, commandsReply(commandsItem(TEXT("c-6"), TEXT("set_auto_pause"), TEXT("{\"enabled\":true}"), Expires)
		+ TEXT(",") + commandsItem(TEXT("c-7"), TEXT("set_auto_pause"), TEXT("{\"enabled\":false}"), Expires)));
	F.run(Driver, 1);
	F.Net.answer(*commandsResultPath(TEXT("c-6")), SatisDash::Test::Reply(401, TEXT("{}")));
	TestTrue(TEXT("a 401 on a result halts"), Agent.GetState() == EAgentState::Halted);
	F.run(Driver, 60);
	TestEqual(TEXT("the waiting command is dropped, not run"), F.Game.Applied.Num(), 1);
	TestEqual(TEXT("and nothing is polled"), F.Net.pending(CommandsPoll), 0);
	// Re-enrolled: the same ID handed out again is reported from the ledger, not run again.
	F.writeConfig(TEXT("http://127.0.0.1:1"), TEXT("ZZZZ-2222"));
	F.run(Driver, 61);
	F.Net.answer(CommandsEnroll, SatisDash::Test::Reply(201, SatisDash::Test::EnrollJson()));
	F.run(Driver, 1);
	while (F.Net.answer(CommandsSnapshots, SatisDash::Test::SnapshotOk(false))) {}
	F.Net.answer(CommandsPoll, commandsReply(commandsItem(TEXT("c-6"), TEXT("set_auto_pause"), TEXT("{\"enabled\":true}"), Expires)));
	F.run(Driver, 1);
	TestEqual(TEXT("still applied once"), F.Game.Applied.Num(), 1);
	TestEqual(TEXT("and reported from the ledger"), F.Net.sent(*commandsResultPath(TEXT("c-6"))), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommandsStalePollAuthTest, "SatisDash.Core.Commands.StalePollAuth", SatisDash::Test::Flags)

bool FCommandsStalePollAuthTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock() };
	const FAgent& Agent = Driver.GetAgent();
	const FString Second = FString::ChrN(42, TEXT('b')) + TEXT("_");
	commandsReady(F, Driver);
	TestTrue(TEXT("enrolled at B"), commandsMoveToSecondBackend(F, Driver, Second));
	TestEqual(TEXT("A's poll is still out"), F.Net.pending(CommandsPoll), 1);
	// A revoked this server: its 401 is about A's credential, never B's.
	F.Net.answer(CommandsPoll, SatisDash::Test::Reply(401, TEXT("{}")));
	TestTrue(TEXT("still running on B"), Agent.GetState() == EAgentState::Running);
	TestTrue(TEXT("B's credential file is kept"), IFileManager::Get().FileExists(*F.CredentialPath));
	F.run(Driver, 1);
	TestEqual(TEXT("then B is polled"), F.Net.pending(CommandsPoll), 1);
	SatisDash::Test::SameText(*this, TEXT("with B's secret"), F.Net.lastAuth(CommandsPoll), TEXT("Bearer ") + Second);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommandsStalePollCommandTest, "SatisDash.Core.Commands.StalePollCommand", SatisDash::Test::Flags)

bool FCommandsStalePollCommandTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock() };
	commandsReady(F, Driver);
	const FDateTime Expires = F.Utc + FTimespan::FromMinutes(5);
	TestTrue(TEXT("enrolled at B"), commandsMoveToSecondBackend(F, Driver, FString::ChrN(42, TEXT('b')) + TEXT("_")));
	// A's late answer brings a command: it was A's to give, so B's agent neither runs it nor records it.
	F.Net.answer(CommandsPoll, commandsReply(commandsItem(TEXT("c-8"), TEXT("set_auto_pause"), TEXT("{\"enabled\":true}"), Expires)));
	F.run(Driver, 2);
	TestEqual(TEXT("not applied"), F.Game.Applied.Num(), 0);
	TestEqual(TEXT("not reported"), F.Net.sent(*commandsResultPath(TEXT("c-8"))), 0);
	// B hands out the same ID: the ledger never saw it, so it runs now.
	while (F.Net.answer(CommandsSnapshots, SatisDash::Test::SnapshotOk(false))) {}
	F.Net.answer(CommandsPoll, commandsReply(commandsItem(TEXT("c-8"), TEXT("set_auto_pause"), TEXT("{\"enabled\":true}"), Expires)));
	F.run(Driver, 1);
	TestEqual(TEXT("B's command runs"), F.Game.Applied.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommandsStaleResultTest, "SatisDash.Core.Commands.StaleResult", SatisDash::Test::Flags)

bool FCommandsStaleResultTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.clock() };
	const FAgent& Agent = Driver.GetAgent();
	const FString Second = FString::ChrN(42, TEXT('b')) + TEXT("_");
	commandsReady(F, Driver);
	F.Net.answer(CommandsPoll, commandsReply(commandsItem(TEXT("c-9"), TEXT("set_auto_pause"), TEXT("{\"enabled\":true}"), F.Utc + FTimespan::FromMinutes(5))));
	F.run(Driver, 1);
	TestEqual(TEXT("A's result is out"), F.Net.pending(*commandsResultPath(TEXT("c-9"))), 1);
	TestTrue(TEXT("enrolled at B"), commandsMoveToSecondBackend(F, Driver, Second));
	F.Net.answer(*commandsResultPath(TEXT("c-9")), SatisDash::Test::Reply(401, TEXT("{}")));
	TestTrue(TEXT("still running on B"), Agent.GetState() == EAgentState::Running);
	TestTrue(TEXT("B's credential file is kept"), IFileManager::Get().FileExists(*F.CredentialPath));
	F.run(Driver, 1);
	TestEqual(TEXT("A's result is never sent to B"), F.Net.sent(*commandsResultPath(TEXT("c-9"))), 1);
	TestEqual(TEXT("B is polled"), F.Net.pending(CommandsPoll), 1);
	SatisDash::Test::SameText(*this, TEXT("with B's secret"), F.Net.lastAuth(CommandsPoll), TEXT("Bearer ") + Second);
	return true;
}
