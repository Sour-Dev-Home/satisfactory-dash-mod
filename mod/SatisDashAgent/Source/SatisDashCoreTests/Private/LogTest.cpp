#include "SatisDash/Agent.h"
#include "SatisDash/Pusher.h"
#include "SatisDash/RateLimit.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	int32 Count(const TArray<FString>& Lines, const TCHAR* Part)
	{
		int32 N = 0;
		for (const FString& Line : Lines)
		{
			N += Line.Contains(Part) ? 1 : 0;
		}
		return N;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLogRateLimitTest, "SatisDash.Core.Log.RateLimit", SatisDash::Test::Flags)

bool FLogRateLimitTest::RunTest(const FString& Parameters)
{
	FRateLimitedLog Limit{600.0};
	TestTrue(TEXT("first"), Limit.ShouldLog(TEXT("power"), 0));
	TestFalse(TEXT("again too soon"), Limit.ShouldLog(TEXT("power"), 599));
	TestTrue(TEXT("another part"), Limit.ShouldLog(TEXT("status"), 1));
	TestTrue(TEXT("after the interval"), Limit.ShouldLog(TEXT("power"), 600));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLogOnceTest, "SatisDash.Core.Log.OncePerStreak", SatisDash::Test::Flags)

bool FLogOnceTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Capture;
	FPusher Pusher{[] { return 1.0; }};
	Pusher.Enqueue(TEXT("{}"));
	double Now = 0;
	for (int32 I = 0; I < 5; ++I)
	{
		Pusher.StartSend(Now);
		Pusher.FinishSend(SatisDash::Test::Reply(503, TEXT("{}")), Now);
		Now += 100;
	}
	Pusher.StartSend(Now);
	Pusher.FinishSend(SatisDash::Test::Reply(200, SatisDash::Test::SnapshotAnswerJson), Now);
	const TArray<FString> Lines = Capture.Take();
	TestEqual(TEXT("one line when failures start"), Count(Lines, TEXT("Pushing snapshots failed")), 1);
	TestEqual(TEXT("one line when they stop"), Count(Lines, TEXT("Pushing snapshots recovered")), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLogNoSecretsTest, "SatisDash.Core.Log.NoSecrets", SatisDash::Test::Flags)

bool FLogNoSecretsTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Capture;
	const SatisDash::Test::FTempDir Dir;
	const FString ConfigPath = Dir.Path / TEXT("SatisDashAgent.cfg");
	auto writeCode = [&](const TCHAR* Code)
	{
		SatisDash::Test::WriteText(ConfigPath, FString::Printf(TEXT("{\"backendUrl\":\"https://example.com\",\"enrollmentCode\":\"%s\"}"), Code));
	};

	// Every logging path: enrol with a failure, a pasted code while enrolled, revoke, a rejected code, a pusher streak.
	writeCode(TEXT("AB3D-7XQ2"));
	FAgent Agent{ConfigPath, Dir.Path / TEXT("credential.json"), [] { return 1.0; }};
	Agent.Boot();
	Agent.StartEnroll(0);
	Agent.FinishEnroll(SatisDash::Test::Reply(503, TEXT("{}")), 0);
	Agent.StartEnroll(10);
	Agent.FinishEnroll(SatisDash::Test::Reply(201, SatisDash::Test::EnrollJson()), 10);
	TestTrue(TEXT("running after enrolling"), Agent.GetState() == EAgentState::Running);
	writeCode(TEXT("ZZZZ-7777"));
	Agent.CheckConfig();
	TestTrue(TEXT("still running with a pasted code"), Agent.GetState() == EAgentState::Running);
	Agent.OnAuthRejected();
	TestTrue(TEXT("halted"), Agent.GetState() == EAgentState::Halted);
	writeCode(TEXT("QQQQ-2222"));
	Agent.CheckConfig();
	TestTrue(TEXT("enrolling again"), Agent.GetState() == EAgentState::Enrolling);
	Agent.StartEnroll(20);
	Agent.FinishEnroll(SatisDash::Test::Reply(404, TEXT("{}")), 20);
	TestTrue(TEXT("code rejected"), Agent.GetState() == EAgentState::CodeRejected);
	FPusher Pusher{[] { return 1.0; }};
	Pusher.Enqueue(TEXT("{}"));
	Pusher.StartSend(0);
	Pusher.FinishSend(SatisDash::Test::Reply(400, TEXT("{}")), 0);

	const TArray<FString> Lines = Capture.Take();
	TestTrue(TEXT("lines were captured"), Lines.Num() >= 5);
	TestEqual(TEXT("the already-enrolled line was written"), Count(Lines, TEXT("already enrolled")), 1);
	for (const FString& Line : Lines)
	{
		TestFalse(TEXT("no secret"), Line.Contains(SatisDash::Test::FakeSecret(), ESearchCase::CaseSensitive));
		TestFalse(TEXT("no part of the secret"), Line.Contains(SatisDash::Test::FakeSecret().Left(8), ESearchCase::CaseSensitive));
		TestFalse(TEXT("no first code"), Line.Contains(TEXT("AB3D-7XQ2"), ESearchCase::IgnoreCase));
		TestFalse(TEXT("no pasted code"), Line.Contains(TEXT("ZZZZ-7777"), ESearchCase::IgnoreCase));
		TestFalse(TEXT("no rejected code"), Line.Contains(TEXT("QQQQ-2222"), ESearchCase::IgnoreCase));
		TestFalse(TEXT("no Bearer"), Line.Contains(TEXT("Bearer"), ESearchCase::IgnoreCase));
	}
	SatisDash::Test::NoErrorLines(*this, Capture);
	return true;
}
