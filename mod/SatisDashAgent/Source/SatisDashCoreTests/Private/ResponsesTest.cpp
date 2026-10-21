#include "SatisDash/JsonUtil.h"
#include "SatisDash/Responses.h"
#include "TestUtil.h"

using namespace SatisDash;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResponsesClassifyTest, "SatisDash.Core.Responses.Classify", SatisDash::Test::Flags)

bool FResponsesClassifyTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("200"), ClassifyReply(200, true) == EReplyKind::Ok);
	TestTrue(TEXT("201"), ClassifyReply(201, false) == EReplyKind::Ok);
	TestTrue(TEXT("302 is refused, never followed"), ClassifyReply(302, true) == EReplyKind::Fatal);
	TestTrue(TEXT("401 with a credential halts"), ClassifyReply(401, true) == EReplyKind::AuthRejected);
	TestTrue(TEXT("401 on enrol is a refusal"), ClassifyReply(401, false) == EReplyKind::Fatal);
	TestTrue(TEXT("400"), ClassifyReply(400, true) == EReplyKind::Fatal);
	TestTrue(TEXT("404"), ClassifyReply(404, false) == EReplyKind::Fatal);
	TestTrue(TEXT("429"), ClassifyReply(429, true) == EReplyKind::Transient);
	TestTrue(TEXT("500"), ClassifyReply(500, true) == EReplyKind::Transient);
	TestTrue(TEXT("503"), ClassifyReply(503, true) == EReplyKind::Transient);
	TestTrue(TEXT("no answer"), ClassifyReply(0, true) == EReplyKind::Transient);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResponsesLimitsTest, "SatisDash.Core.Responses.Limits", SatisDash::Test::Flags)

bool FResponsesLimitsTest::RunTest(const FString& Parameters)
{
	const FString Answer = SatisDash::Test::SnapshotAnswerJson;
	TestTrue(TEXT("json with a charset"), ParseSnapshotResponse(SatisDash::Test::Reply(200, Answer, TEXT("application/json; charset=utf-8"))).IsSet());
	TestFalse(TEXT("html"), ParseSnapshotResponse(SatisDash::Test::Reply(200, Answer, TEXT("text/html"))).IsSet());
	TestFalse(TEXT("no content type"), ParseSnapshotResponse(SatisDash::Test::Reply(200, Answer, TEXT(""))).IsSet());
	const FString Exact = FString::ChrN(MaxResponseBytes - Answer.Len(), TEXT(' ')) + Answer;
	TestTrue(TEXT("exactly 64 KB"), ParseSnapshotResponse(SatisDash::Test::Reply(200, Exact)).IsSet());
	TestFalse(TEXT("64 KB and one byte"), ParseSnapshotResponse(SatisDash::Test::Reply(200, TEXT(" ") + Exact)).IsSet());
	TestFalse(TEXT("malformed"), ParseSnapshotResponse(SatisDash::Test::Reply(200, TEXT("{\"cadence\":"))).IsSet());
	TestFalse(TEXT("an array"), ParseSnapshotResponse(SatisDash::Test::Reply(200, TEXT("[]"))).IsSet());
	TestFalse(TEXT("an empty body"), ParseSnapshotResponse(SatisDash::Test::Reply(200, TEXT(""))).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResponsesNestingTest, "SatisDash.Core.Responses.Nesting", SatisDash::Test::Flags)

bool FResponsesNestingTest::RunTest(const FString& Parameters)
{
	// Security review F1: a hostile body never reaches the JSON parser deeper than Json::MaxDepth.
	TestFalse(TEXT("64 KB of ["), ParseSnapshotResponse(SatisDash::Test::Reply(200, FString::ChrN(MaxResponseBytes, TEXT('[')))).IsSet());
	TestFalse(TEXT("cadence then 64 KB of ["),
		ParseSnapshotResponse(SatisDash::Test::Reply(200, TEXT("{\"cadence\":") + FString::ChrN(MaxResponseBytes - 11, TEXT('[')))).IsSet());
	const FString Head = TEXT("{\"cadence\":{\"statusSeconds\":5,\"powerSeconds\":5,\"factorySeconds\":30},\"commandsPending\":false,\"x\":");
	// The answer itself is one level, so the extra field holds MaxDepth - 1 more at the cap.
	TestTrue(TEXT("an extra field at the cap"),
		ParseSnapshotResponse(SatisDash::Test::Reply(200, Head + SatisDash::Test::NestedObjects(SatisDash::Json::MaxDepth - 1) + TEXT("}"))).IsSet());
	TestFalse(TEXT("an extra field one over the cap"),
		ParseSnapshotResponse(SatisDash::Test::Reply(200, Head + SatisDash::Test::NestedObjects(SatisDash::Json::MaxDepth) + TEXT("}"))).IsSet());
	TestFalse(TEXT("enrol answer one over the cap"),
		ParseEnrollResponse(SatisDash::Test::Reply(201, FString::Printf(TEXT("{\"agentSecret\":\"%s\",\"serverId\":\"srv-1\",\"cadence\":%s,\"x\":%s}"),
			*SatisDash::Test::FakeSecret(), SatisDash::Test::CadenceJson, *SatisDash::Test::NestedObjects(SatisDash::Json::MaxDepth)))).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResponsesEnrollTest, "SatisDash.Core.Responses.Enroll", SatisDash::Test::Flags)

bool FResponsesEnrollTest::RunTest(const FString& Parameters)
{
	const TOptional<FEnrollResponse> Ok = ParseEnrollResponse(SatisDash::Test::Reply(201, SatisDash::Test::EnrollJson()));
	TestTrue(TEXT("valid"), Ok.IsSet());
	if (Ok.IsSet())
	{
		SatisDash::Test::SameText(*this, TEXT("secret"), Ok->Credential.AgentSecret, SatisDash::Test::FakeSecret());
		SatisDash::Test::SameText(*this, TEXT("server id"), Ok->Credential.ServerId, TEXT("srv-1"));
		TestEqual(TEXT("cadence"), Ok->Cadence.StatusSeconds, 5);
	}
	TestFalse(TEXT("200 is not the contract's 201"), ParseEnrollResponse(SatisDash::Test::Reply(200, SatisDash::Test::EnrollJson())).IsSet());
	auto body = [](const TCHAR* Secret, const TCHAR* ServerId)
	{
		return FString::Printf(TEXT("{\"agentSecret\":%s,\"serverId\":%s,\"cadence\":%s}"), Secret, ServerId, SatisDash::Test::CadenceJson);
	};
	TestFalse(TEXT("secret of the wrong type"), ParseEnrollResponse(SatisDash::Test::Reply(201, body(TEXT("5"), TEXT("\"srv-1\"")))).IsSet());
	TestFalse(TEXT("secret with CR LF"), ParseEnrollResponse(SatisDash::Test::Reply(201, body(TEXT("\"ab\\r\\nX: 1\""), TEXT("\"srv-1\"")))).IsSet());
	TestFalse(TEXT("bad server id"), ParseEnrollResponse(SatisDash::Test::Reply(201,
		body(*FString::Printf(TEXT("\"%s\""), *SatisDash::Test::FakeSecret()), TEXT("\"Not An Id\"")))).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResponsesCadenceTest, "SatisDash.Core.Responses.Cadence", SatisDash::Test::Flags)

bool FResponsesCadenceTest::RunTest(const FString& Parameters)
{
	auto status = [](const TCHAR* Value) -> TOptional<int32>
	{
		const TOptional<FSnapshotResponse> R = ParseSnapshotResponse(SatisDash::Test::Reply(200, FString::Printf(
			TEXT("{\"cadence\":{\"statusSeconds\":%s,\"powerSeconds\":5,\"factorySeconds\":30},\"commandsPending\":true}"), Value)));
		return R.IsSet() ? TOptional<int32>(R->Cadence.StatusSeconds) : TOptional<int32>();
	};
	TestEqual(TEXT("0 is clamped to 1"), status(TEXT("0")).Get(-1), 1);
	TestEqual(TEXT("10^6 is clamped to 3600"), status(TEXT("1000000")).Get(-1), 3600);
	TestEqual(TEXT("1e300 is clamped to 3600"), status(TEXT("1e300")).Get(-1), 3600);
	TestEqual(TEXT("-5 is clamped to 1"), status(TEXT("-5")).Get(-1), 1);
	TestEqual(TEXT("2.6 rounds to 3"), status(TEXT("2.6")).Get(-1), 3);
	TestFalse(TEXT("a string rejects the response"), status(TEXT("\"5\"")).IsSet());
	TestFalse(TEXT("null rejects the response"), status(TEXT("null")).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResponsesSnapshotTest, "SatisDash.Core.Responses.Snapshot", SatisDash::Test::Flags)

bool FResponsesSnapshotTest::RunTest(const FString& Parameters)
{
	const TOptional<FSnapshotResponse> Ok = ParseSnapshotResponse(SatisDash::Test::Reply(200, FString::Printf(
		TEXT("{\"cadence\":%s,\"commandsPending\":true,\"extra\":1}"), SatisDash::Test::CadenceJson)));
	TestTrue(TEXT("valid, unknown field ignored"), Ok.IsSet() && Ok->bCommandsPending);
	TestFalse(TEXT("commandsPending missing"), ParseSnapshotResponse(SatisDash::Test::Reply(200, FString::Printf(
		TEXT("{\"cadence\":%s}"), SatisDash::Test::CadenceJson))).IsSet());
	TestFalse(TEXT("a 503 has no answer to parse"), ParseSnapshotResponse(SatisDash::Test::Reply(503, SatisDash::Test::SnapshotAnswerJson)).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResponsesCommandsTest, "SatisDash.Core.Responses.Commands", SatisDash::Test::Flags)

bool FResponsesCommandsTest::RunTest(const FString& Parameters)
{
	auto one = [](const FString& Id, const TCHAR* Params, const TCHAR* ExpiresAt)
	{
		return FString::Printf(TEXT("{\"id\":\"%s\",\"type\":\"set_auto_pause\",\"params\":%s,\"expiresAt\":\"%s\",\"x\":1}"),
			*Id, Params, ExpiresAt);
	};
	auto parse = [](const FString& Items)
	{
		return ParseCommandsResponse(SatisDash::Test::Reply(200, FString::Printf(TEXT("{\"commands\":[%s]}"), *Items)));
	};
	const TCHAR* Expires = TEXT("2026-10-20T12:05:00.000Z");

	TestTrue(TEXT("empty"), parse(TEXT("")).IsSet() && parse(TEXT(""))->Num() == 0);
	const TOptional<TArray<FAgentCommand>> Ok = parse(one(TEXT("c1"), TEXT("{\"enabled\":true}"), Expires));
	TestTrue(TEXT("one command"), Ok.IsSet() && Ok->Num() == 1);
	if (Ok.IsSet() && Ok->Num() == 1)
	{
		SatisDash::Test::SameText(*this, TEXT("id"), (*Ok)[0].Id, TEXT("c1"));
		TestTrue(TEXT("expiry parsed"), (*Ok)[0].ExpiresAt == FDateTime(2026, 10, 20, 12, 5, 0));
	}
	TestFalse(TEXT("empty id"), parse(one(TEXT(""), TEXT("{}"), Expires)).IsSet());
	TestFalse(TEXT("101-character id"), parse(one(FString::ChrN(101, TEXT('i')), TEXT("{}"), Expires)).IsSet());
	TestTrue(TEXT("100-character id"), parse(one(FString::ChrN(100, TEXT('i')), TEXT("{}"), Expires)).IsSet());
	TestFalse(TEXT("params not an object"), parse(one(TEXT("c1"), TEXT("[]"), Expires)).IsSet());
	TestFalse(TEXT("bad expiry"), parse(one(TEXT("c1"), TEXT("{}"), TEXT("tomorrow"))).IsSet());

	TArray<FString> Many;
	for (int32 I = 0; I < 25; ++I)
	{
		Many.Add(one(FString::Printf(TEXT("c%d"), I), TEXT("{}"), Expires));
	}
	const TOptional<TArray<FAgentCommand>> Capped = parse(FString::Join(Many, TEXT(",")));
	TestEqual(TEXT("at most 20 per poll"), Capped.IsSet() ? Capped->Num() : -1, MaxCommandsPerPoll);
	return true;
}
