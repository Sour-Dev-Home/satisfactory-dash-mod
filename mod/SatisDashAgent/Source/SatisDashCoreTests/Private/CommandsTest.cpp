#include "SatisDash/Commands.h"
#include "SatisDash/JsonUtil.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	const FDateTime CommandsNow{2026, 10, 20, 12, 0, 0};

	FAgentCommand makeCommand(const TCHAR* Id, const TCHAR* Type, const TCHAR* Params, FTimespan ExpiresIn = FTimespan::FromMinutes(5))
	{
		FAgentCommand C;
		C.Id = Id;
		C.Type = Type;
		C.Params = SatisDash::Json::ParseObject(Params);
		C.ExpiresAt = CommandsNow + ExpiresIn;
		return C;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommandsLedgerTest, "SatisDash.Core.Commands.Ledger", SatisDash::Test::Flags)

bool FCommandsLedgerTest::RunTest(const FString& Parameters)
{
	FCommandLedger Ledger;
	FCommandResult Done;
	const FDateTime Later = CommandsNow + FTimespan::FromMinutes(5);
	TestTrue(TEXT("first sight"), Ledger.Begin(TEXT("a"), Later, CommandsNow, Done) == FCommandLedger::EVerdict::New);
	TestTrue(TEXT("again while running"), Ledger.Begin(TEXT("a"), Later, CommandsNow, Done) == FCommandLedger::EVerdict::Running);
	Ledger.Finish(TEXT("a"), CommandOk());
	TestTrue(TEXT("again once finished"), Ledger.Begin(TEXT("a"), Later, CommandsNow, Done) == FCommandLedger::EVerdict::Done && Done.bOk);

	// Past its expiry an entry is purged: the backend won't send it again.
	TestTrue(TEXT("expired entries are purged"), Ledger.Begin(TEXT("b"), Later, Later + FTimespan::FromSeconds(1), Done) == FCommandLedger::EVerdict::New);
	TestTrue(TEXT("so a is new again"), Ledger.Begin(TEXT("a"), Later + FTimespan::FromMinutes(5), Later + FTimespan::FromSeconds(1), Done) == FCommandLedger::EVerdict::New);

	FCommandLedger Full;
	for (int32 I = 0; I <= FCommandLedger::MaxEntries; ++I)
	{
		Full.Begin(FString::Printf(TEXT("id%d"), I), Later, CommandsNow, Done);
	}
	TestEqual(TEXT("bounded"), Full.Num(), FCommandLedger::MaxEntries);
	TestTrue(TEXT("the oldest was evicted"), Full.Begin(TEXT("id0"), Later, CommandsNow, Done) == FCommandLedger::EVerdict::New);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommandsCaseTest, "SatisDash.Core.Commands.CaseSensitive", SatisDash::Test::Flags)

bool FCommandsCaseTest::RunTest(const FString& Parameters)
{
	FCommandLedger Ledger;
	FCommandResult Done;
	const FDateTime Later = CommandsNow + FTimespan::FromMinutes(5);
	Ledger.Begin(TEXT("abc"), Later, CommandsNow, Done);
	TestTrue(TEXT("ABC is another command"), Ledger.Begin(TEXT("ABC"), Later, CommandsNow, Done) == FCommandLedger::EVerdict::New);
	const FCommandPlan Upper = PlanCommand(makeCommand(TEXT("c9"), TEXT("SET_AUTO_PAUSE"), TEXT("{\"enabled\":true}")), Ledger, CommandsNow);
	TestTrue(TEXT("a type in the wrong case is unsupported"), Upper.Action == ECommandAction::Report && !Upper.Result.bOk);
	SatisDash::Test::SameText(*this, TEXT("its code"), Upper.Result.Code, TEXT("unsupported"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommandsPlanTest, "SatisDash.Core.Commands.Plan", SatisDash::Test::Flags)

bool FCommandsPlanTest::RunTest(const FString& Parameters)
{
	FCommandLedger Ledger;
	const FCommandPlan Run = PlanCommand(makeCommand(TEXT("c1"), TEXT("set_auto_pause"), TEXT("{\"enabled\":false}")), Ledger, CommandsNow);
	TestTrue(TEXT("set_auto_pause runs"), Run.Action == ECommandAction::SetAutoPause && !Run.bEnabled);
	TestTrue(TEXT("not twice while it runs"), PlanCommand(makeCommand(TEXT("c1"), TEXT("set_auto_pause"), TEXT("{\"enabled\":false}")), Ledger, CommandsNow).Action == ECommandAction::Skip);
	Ledger.Finish(TEXT("c1"), CommandFailed(TEXT("upstream_error")));
	const FCommandPlan Again = PlanCommand(makeCommand(TEXT("c1"), TEXT("set_auto_pause"), TEXT("{\"enabled\":false}")), Ledger, CommandsNow);
	TestTrue(TEXT("a repeat reports the first result, never runs again"), Again.Action == ECommandAction::Report && !Again.Result.bOk);
	SatisDash::Test::SameText(*this, TEXT("the first result's code"), Again.Result.Code, TEXT("upstream_error"));

	auto expectUnsupported = [&](const TCHAR* Id, const TCHAR* Type, const TCHAR* Params)
	{
		const FCommandPlan P = PlanCommand(makeCommand(Id, Type, Params), Ledger, CommandsNow);
		TestTrue(FString::Printf(TEXT("%s %s is unsupported"), Type, Params), P.Action == ECommandAction::Report && P.Result.Code.Equals(TEXT("unsupported"), ESearchCase::CaseSensitive));
	};
	expectUnsupported(TEXT("u1"), TEXT("restart_server"), TEXT("{}"));
	expectUnsupported(TEXT("u2"), TEXT("set_auto_pause"), TEXT("{}"));
	expectUnsupported(TEXT("u3"), TEXT("set_auto_pause"), TEXT("{\"enabled\":\"true\"}"));
	expectUnsupported(TEXT("u4"), TEXT("set_auto_pause"), TEXT("{\"enabled\":true,\"x\":1}"));
	TestTrue(TEXT("an unsupported repeat is reported, not re-planned"), PlanCommand(makeCommand(TEXT("u1"), TEXT("restart_server"), TEXT("{}")), Ledger, CommandsNow).Action == ECommandAction::Report);

	TestTrue(TEXT("expired: skipped"), PlanCommand(makeCommand(TEXT("e1"), TEXT("set_auto_pause"), TEXT("{\"enabled\":true}"), FTimespan::FromSeconds(-1)), Ledger, CommandsNow).Action == ECommandAction::Skip);
	TestTrue(TEXT("over 10 minutes out: skipped"), PlanCommand(makeCommand(TEXT("e2"), TEXT("set_auto_pause"), TEXT("{\"enabled\":true}"), FTimespan::FromMinutes(11)), Ledger, CommandsNow).Action == ECommandAction::Skip);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommandsResultTest, "SatisDash.Core.Commands.Result", SatisDash::Test::Flags)

bool FCommandsResultTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::SameText(*this, TEXT("ok"), CommandResultToJson(CommandOk()), TEXT("{\"ok\":true}"));
	SatisDash::Test::SameText(*this, TEXT("failed"), CommandResultToJson(CommandFailed(TEXT("unsupported"))), TEXT("{\"ok\":false,\"code\":\"unsupported\"}"));
	SatisDash::Test::SameText(*this, TEXT("never free text"), CommandFailed(TEXT("disk on fire")).Code, TEXT("upstream_error"));
	const FDateTime Later = CommandsNow + FTimespan::FromMinutes(1);
	TestTrue(TEXT("retry a transient failure"), ShouldRetryResult(1, EReplyKind::Transient, Later, CommandsNow));
	TestFalse(TEXT("not after 5 attempts"), ShouldRetryResult(5, EReplyKind::Transient, Later, CommandsNow));
	TestFalse(TEXT("not a refusal"), ShouldRetryResult(1, EReplyKind::Fatal, Later, CommandsNow));
	TestFalse(TEXT("not once expired"), ShouldRetryResult(1, EReplyKind::Transient, CommandsNow, Later));
	return true;
}
