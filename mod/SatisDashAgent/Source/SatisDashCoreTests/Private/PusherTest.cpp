#include "SatisDash/Pusher.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	FHttpReply okReply() { return SatisDash::Test::Reply(200, SatisDash::Test::SnapshotAnswerJson); }

	FHttpReply statusReply(int32 Code, const TCHAR* RetryAfter = TEXT(""))
	{
		FHttpReply Reply = SatisDash::Test::Reply(Code, TEXT("{}"));
		Reply.RetryAfter = RetryAfter;
		return Reply;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPusherBackoffTest, "SatisDash.Core.Pusher.Backoff", SatisDash::Test::Flags)

bool FPusherBackoffTest::RunTest(const FString& Parameters)
{
	// backoff.ts: the ceiling doubles from 1 s to 60 s; the delay is uniform in [ceiling / 2, ceiling].
	TestEqual(TEXT("first, low"), BackoffSeconds(0, 0.0), 0.5);
	TestEqual(TEXT("first, high"), BackoffSeconds(0, 1.0), 1.0);
	TestEqual(TEXT("second"), BackoffSeconds(1, 1.0), 2.0);
	TestEqual(TEXT("sixth"), BackoffSeconds(5, 1.0), 32.0);
	TestEqual(TEXT("capped at 60"), BackoffSeconds(6, 1.0), 60.0);
	TestEqual(TEXT("capped, low"), BackoffSeconds(6, 0.0), 30.0);
	TestEqual(TEXT("far past the cap"), BackoffSeconds(1000, 1.0), 60.0);
	TestEqual(TEXT("a negative attempt is the first"), BackoffSeconds(-3, 1.0), 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPusherRetryAfterTest, "SatisDash.Core.Pusher.RetryAfter", SatisDash::Test::Flags)

bool FPusherRetryAfterTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("seconds"), ParseRetryAfterSeconds(TEXT("120")).Get(-1.0), 120.0);
	TestEqual(TEXT("decimal"), ParseRetryAfterSeconds(TEXT(" 1.5 ")).Get(-1.0), 1.5);
	TestEqual(TEXT("capped at 300"), ParseRetryAfterSeconds(TEXT("1000")).Get(-1.0), 300.0);
	TestFalse(TEXT("empty"), ParseRetryAfterSeconds(TEXT("")).IsSet());
	TestFalse(TEXT("negative"), ParseRetryAfterSeconds(TEXT("-1")).IsSet());
	TestFalse(TEXT("an HTTP date (backendClient.ts ignores it too)"), ParseRetryAfterSeconds(TEXT("Wed, 21 Oct 2015 07:28:00 GMT")).IsSet());
	TestFalse(TEXT("junk"), ParseRetryAfterSeconds(TEXT("soon")).IsSet());
	TestFalse(TEXT("overflow to infinity"), ParseRetryAfterSeconds(TEXT("1e309")).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPusherQueueTest, "SatisDash.Core.Pusher.Queue", SatisDash::Test::Flags)

bool FPusherQueueTest::RunTest(const FString& Parameters)
{
	FPusher Pusher{ [] { return 1.0; } };
	for (int32 I = 0; I <= MaxQueuedSnapshots; ++I)
	{
		Pusher.Enqueue(FString::Printf(TEXT("s%d"), I));
	}
	TestEqual(TEXT("at most 120"), Pusher.Num(), MaxQueuedSnapshots);
	SatisDash::Test::SameText(*this, TEXT("the oldest was dropped"), Pusher.StartSend(0).Get(FString()), TEXT("s1"));
	Pusher.Clear();
	TestEqual(TEXT("cleared"), Pusher.Num(), 0);
	TestFalse(TEXT("nothing to send"), Pusher.StartSend(0).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPusherSendTest, "SatisDash.Core.Pusher.Send", SatisDash::Test::Flags)

bool FPusherSendTest::RunTest(const FString& Parameters)
{
	FPusher Pusher{ [] { return 1.0; } };
	Pusher.Enqueue(TEXT("a"));
	TestTrue(TEXT("send"), Pusher.StartSend(0).IsSet());
	TestFalse(TEXT("one at a time"), Pusher.StartSend(0).IsSet());
	const FPusher::FOutcome Sent = Pusher.FinishSend(okReply(),0);
	TestTrue(TEXT("sent: the answer is parsed"), Sent.Response.IsSet() && !Sent.bHalt);
	TestEqual(TEXT("and the snapshot is gone"), Pusher.Num(), 0);

	Pusher.Enqueue(TEXT("b"));
	Pusher.StartSend(0);
	Pusher.FinishSend(statusReply(503), 0);
	TestEqual(TEXT("kept after a 503"), Pusher.Num(), 1);
	TestFalse(TEXT("waits the first backoff"), Pusher.StartSend(0.99).IsSet());
	TestTrue(TEXT("retries after 1 s"), Pusher.StartSend(1.0).IsSet());
	Pusher.FinishSend(statusReply(0), 1.0);
	TestFalse(TEXT("then 2 s"), Pusher.StartSend(2.99).IsSet());
	TestTrue(TEXT("retries after 2 s more"), Pusher.StartSend(3.0).IsSet());
	Pusher.FinishSend(statusReply(429, TEXT("120")), 3.0);
	TestFalse(TEXT("Retry-After wins over a shorter backoff"), Pusher.StartSend(122.9).IsSet());
	TestTrue(TEXT("after Retry-After"), Pusher.StartSend(123.0).IsSet());
	Pusher.FinishSend(statusReply(503, TEXT("100000")), 123.0);
	TestTrue(TEXT("Retry-After capped at 300 s"), Pusher.StartSend(423.0).IsSet());
	Pusher.FinishSend(SatisDash::Test::Reply(200, TEXT("{\"nope\":1}")), 423.0);
	TestEqual(TEXT("a 2xx not in the expected form is retried"), Pusher.Num(), 1);
	TestEqual(TEXT("and counts as a failure"), Pusher.Failures(), 5);
	Pusher.StartSend(10000);
	Pusher.FinishSend(okReply(),10000);
	TestEqual(TEXT("recovered"), Pusher.Failures(), 0);

	Pusher.Enqueue(TEXT("c"));
	Pusher.StartSend(10000);
	Pusher.FinishSend(statusReply(400), 10000);
	TestEqual(TEXT("a refusal drops the snapshot"), Pusher.Num(), 0);

	Pusher.Enqueue(TEXT("d"));
	Pusher.StartSend(10000);
	TestTrue(TEXT("401 halts"), Pusher.FinishSend(statusReply(401), 10000).bHalt);
	TestEqual(TEXT("and keeps the snapshot (2B clears it on halt)"), Pusher.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPusherInFlightTest, "SatisDash.Core.Pusher.InFlightDropped", SatisDash::Test::Flags)

bool FPusherInFlightTest::RunTest(const FString& Parameters)
{
	FPusher Pusher{ [] { return 1.0; } };
	for (int32 I = 0; I < MaxQueuedSnapshots; ++I)
	{
		Pusher.Enqueue(FString::Printf(TEXT("s%d"), I));
	}
	SatisDash::Test::SameText(*this, TEXT("s0 in flight"), Pusher.StartSend(0).Get(FString()), TEXT("s0"));
	Pusher.Enqueue(TEXT("s120")); // the queue is full: s0, in flight, is dropped for room
	Pusher.FinishSend(okReply(),0);
	TestEqual(TEXT("s0's success doesn't pop s1"), Pusher.Num(), MaxQueuedSnapshots);
	SatisDash::Test::SameText(*this, TEXT("s1 is next"), Pusher.StartSend(0).Get(FString()), TEXT("s1"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPusherFullAgainTest, "SatisDash.Core.Pusher.FullAgain", SatisDash::Test::Flags)

bool FPusherFullAgainTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	FPusher Pusher{ [] { return 1.0; } };
	auto fullLines = [&Log]
	{
		int32 N = 0;
		for (const FString& Line : Log.Take()) { N += Line.Contains(TEXT("queue is full")) ? 1 : 0; }
		return N;
	};
	// A slow backend that answers every send: the queue overflows with no failure in between (2A final review).
	for (int32 I = 0; I < 130; ++I) { Pusher.Enqueue(TEXT("{}")); }
	TestEqual(TEXT("logged once"), fullLines(), 1);
	for (int32 I = 0; I < 10; ++I) { Pusher.Enqueue(TEXT("{}")); }
	TestEqual(TEXT("still once while it stays full"), fullLines(), 1);
	while (Pusher.Num() > MaxQueuedSnapshots / 2)
	{
		Pusher.StartSend(0);
		Pusher.FinishSend(SatisDash::Test::Reply(200, SatisDash::Test::SnapshotAnswerJson), 0);
	}
	for (int32 I = 0; I < 100; ++I) { Pusher.Enqueue(TEXT("{}")); }
	TestEqual(TEXT("a second episode is logged again"), fullLines(), 2);
	return true;
}
