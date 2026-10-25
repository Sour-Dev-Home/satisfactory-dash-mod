#include "DriverFakes.h"
#include "SatisDash/WorldSender.h"

using namespace SatisDash;

namespace
{
	const TCHAR* WorldRailsBody = TEXT("[{\"ID\":\"R1\",\"SplineData\":[{\"x\":0,\"y\":0},{\"x\":300,\"y\":0}]}]");
	const TCHAR* WorldRailsNoisy = TEXT("[{\"ID\":\"R1\",\"SplineData\":[{\"x\":0,\"y\":0,\"z\":5},{\"x\":300,\"y\":0,\"z\":7}]}]");
	const TCHAR* WorldRailsMoved = TEXT("[{\"ID\":\"R1\",\"SplineData\":[{\"x\":0,\"y\":0},{\"x\":600,\"y\":0}]}]");
	const TCHAR* WorldNodesBody = TEXT("[{\"Name\":\"Iron Ore\",\"Purity\":\"Normal\",\"NodeType\":\"Node\",\"Exploited\":false,\"location\":{\"x\":100,\"y\":200,\"z\":0}}]");
	const TCHAR* WorldRailsRead = TEXT("/getTrainRails");
	const TCHAR* WorldNodesRead = TEXT("/getResourceNode");
	const TCHAR* WorldRailsPost = TEXT("/agent/v1/world/rails");
	const TCHAR* WorldNodesPost = TEXT("/agent/v1/world/resourceNodes");
	const TCHAR* WorldAccepted = TEXT("{\"accepted\":true,\"unchanged\":false}");

	/** A bridge, a sender whose jitter is the top of its range (x1.1: rails at 132 s then every 660 s, nodes at 528 s then every 1980 s), and a fake clock. */
	struct FWorldFixture
	{
		SatisDash::Test::FFakeTransport Net;
		SatisDash::Test::FFakeWorker Worker;
		FFrmBridge Bridge{ Net, Worker };
		FWorldSender Sender{ Net, Bridge, [] { return 1.0; } };
		FAgentConfig Config;
		FCredential Credential;
		double T = 1000;
		FDateTime Utc = FDateTime(2026, 10, 24, 12, 0, 0);
		bool bSnapshotsIdle = true;
		/** Answer a resource-node read with Code 0 when it shows up, so a test about rails isn't held up by it (one FRM request in flight). */
		bool bIgnoreNodes = true;

		FWorldFixture()
		{
			Config.BackendUrl = TEXT("http://127.0.0.1:1");
			Credential.AgentSecret = SatisDash::Test::FakeSecret();
			Credential.ServerId = TEXT("srv-1");
		}

		void tick(double Seconds = 0.5)
		{
			for (double Elapsed = 0; Elapsed < Seconds; Elapsed += 0.5)
			{
				T += 0.5;
				Utc += FTimespan::FromMilliseconds(500);
				if (bIgnoreNodes)
				{
					Net.answer(WorldNodesRead, SatisDash::Test::Reply(0, FString()));
				}
				Bridge.Tick(T, 8080, FString());
				Sender.Tick({ T, Utc, &Config, &Credential, bSnapshotsIdle });
			}
		}

		bool until(const TFunction<bool()>& isDone, double MaxSeconds)
		{
			for (double Spent = 0; Spent < MaxSeconds && !isDone(); Spent += 0.5)
			{
				tick();
			}
			return isDone();
		}

		/** Ticks until Path is open (at most MaxSeconds), then answers it. */
		bool serve(const TCHAR* Path, const FHttpReply& Reply, double MaxSeconds = 1000)
		{
			return until([this, Path] { return Net.pending(Path) > 0; }, MaxSeconds) && Net.answer(Path, Reply);
		}

		/** Starts the sender, serves the rails read with Body, and runs until the post is open. */
		bool toRailsPost(const TCHAR* Body)
		{
			return serve(WorldRailsRead, SatisDash::Test::Reply(200, Body)) && until([this] { return Net.pending(WorldRailsPost) > 0; }, 20);
		}

		int32 posts() const { return Net.sent(WorldRailsPost) + Net.sent(WorldNodesPost); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSenderScheduleTest, "SatisDash.Core.WorldSender.Schedule", SatisDash::Test::Flags)

bool FWorldSenderScheduleTest::RunTest(const FString& Parameters)
{
	// worldReader.ts jitteredIntervalMs: [0.9, 1.1] of the base; NaN is the middle; out of range is clamped.
	TestEqual(TEXT("random 0 is 0.9"), JitteredSeconds(100.0, 0.0), 90.0);
	TestEqual(TEXT("random 0.5 is 1.0"), JitteredSeconds(100.0, 0.5), 100.0);
	TestEqual(TEXT("random 1 is 1.1"), JitteredSeconds(100.0, 1.0), 110.0);
	TestEqual(TEXT("NaN is 1.0"), JitteredSeconds(100.0, std::numeric_limits<double>::quiet_NaN()), 100.0);
	TestEqual(TEXT("above 1 is clamped"), JitteredSeconds(100.0, 5.0), 110.0);
	TestEqual(TEXT("below 0 is clamped"), JitteredSeconds(100.0, -1.0), 90.0);

	FWorldFixture F;
	F.bIgnoreNodes = false;
	TestEqual(TEXT("nothing is read before the sender starts"), F.Net.sent(WorldRailsRead), 0);
	F.tick(300);
	TestEqual(TEXT("still nothing: no Start, no reads"), F.Net.sent(WorldRailsRead), 0);

	F.Sender.Start(F.T);
	F.tick(130);
	TestEqual(TEXT("rails are not read before 120 s x 1.1"), F.Net.sent(WorldRailsRead), 0);
	F.tick(4);
	TestEqual(TEXT("rails are read at about 132 s"), F.Net.pending(WorldRailsRead), 1);
	F.Net.answer(WorldRailsRead, SatisDash::Test::Reply(0, FString())); // FRM fails: a full interval is skipped
	F.tick(380);
	TestEqual(TEXT("resource nodes are not read before 480 s x 1.1"), F.Net.sent(WorldNodesRead), 0);
	F.tick(20);
	TestEqual(TEXT("resource nodes are read at about 528 s"), F.Net.pending(WorldNodesRead), 1);
	F.Net.answer(WorldNodesRead, SatisDash::Test::Reply(0, FString()));
	// The failed rails read waited a whole interval (660 s) from the end of its cycle (about 132 s, so due at about 792 s):
	// not read again at 600 s, nor at 789 s.
	F.tick(255);
	TestEqual(TEXT("rails are still not read at 789 s"), F.Net.sent(WorldRailsRead), 1);
	TestEqual(TEXT("rails are not read again early"), F.Net.sent(WorldRailsRead), 1);
	TestTrue(TEXT("and are read again after 660 s"), F.until([&F] { return F.Net.pending(WorldRailsRead) > 0; }, 300));

	// No reads while the agent is not running.
	FWorldFixture Idle;
	Idle.bIgnoreNodes = false;
	Idle.Sender.Start(Idle.T);
	Idle.Sender.Stop();
	Idle.tick(1200);
	TestEqual(TEXT("Stop: nothing is read"), Idle.Net.sent(WorldRailsRead) + Idle.Net.sent(WorldNodesRead), 0);
	TestFalse(TEXT("and it isn't active"), Idle.Sender.Active());
	Idle.Sender.Start(Idle.T);
	Idle.tick(130);
	TestEqual(TEXT("Start re-arms the boot offset"), Idle.Net.sent(WorldRailsRead), 0);
	Idle.tick(6);
	TestEqual(TEXT("rails are read again 132 s after the new Start"), Idle.Net.pending(WorldRailsRead), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSenderSendTest, "SatisDash.Core.WorldSender.Send", SatisDash::Test::Flags)

bool FWorldSenderSendTest::RunTest(const FString& Parameters)
{
	FWorldFixture F;
	F.Sender.Start(F.T);
	TestTrue(TEXT("the first rails read ends in a post"), F.toRailsPost(WorldRailsBody));
	const FString Posted = F.Net.lastBody(WorldRailsPost);
	TestTrue(TEXT("the body starts with observedAt (when FRM was asked, about 132 s after the fixture's 12:00:00)"), Posted.StartsWith(TEXT("{\"observedAt\":\"2026-10-24T12:02:"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("and ends with the data, in the Node agent's shape"), Posted.EndsWith(TEXT("Z\",\"data\":[{\"id\":\"R1\",\"points\":[[0,0],[3,0]]}]}"), ESearchCase::CaseSensitive));
	SatisDash::Test::SameText(*this, TEXT("with the credential"), F.Net.lastAuth(WorldRailsPost), TEXT("Bearer ") + SatisDash::Test::FakeSecret());
	TestEqual(TEXT("one post"), F.Net.sent(WorldRailsPost), 1);
	F.Net.answer(WorldRailsPost, SatisDash::Test::Reply(200, WorldAccepted));

	// The same raw body next cycle: one hash step on the worker, no parse and no post.
	TestTrue(TEXT("the next cycle reads rails again"), F.until([&F] { return F.Net.pending(WorldRailsRead) > 0; }, 700));
	int32 Launched = F.Worker.Launched;
	F.Net.answer(WorldRailsRead, SatisDash::Test::Reply(200, WorldRailsBody));
	F.tick(10);
	TestEqual(TEXT("an unchanged raw body costs one worker task"), F.Worker.Launched, Launched + 1);
	TestEqual(TEXT("and sends nothing"), F.Net.sent(WorldRailsPost), 1);

	// A different raw body with the same mapped data: parsed, not sent.
	TestTrue(TEXT("and again"), F.until([&F] { return F.Net.pending(WorldRailsRead) > 0; }, 700));
	Launched = F.Worker.Launched;
	F.Net.answer(WorldRailsRead, SatisDash::Test::Reply(200, WorldRailsNoisy));
	F.tick(10);
	TestEqual(TEXT("a changed raw body is hashed and parsed"), F.Worker.Launched, Launched + 2);
	TestEqual(TEXT("but the data is the same: nothing is sent"), F.Net.sent(WorldRailsPost), 1);

	// A changed layer is sent.
	TestTrue(TEXT("and again, with a moved rail"), F.until([&F] { return F.Net.pending(WorldRailsRead) > 0; }, 700));
	F.Net.answer(WorldRailsRead, SatisDash::Test::Reply(200, WorldRailsMoved));
	TestTrue(TEXT("a changed layer is posted"), F.until([&F] { return F.Net.pending(WorldRailsPost) > 0; }, 20));
	TestTrue(TEXT("with its new points"), F.Net.lastBody(WorldRailsPost).Contains(TEXT("[[0,0],[6,0]]")));
	F.Net.answer(WorldRailsPost, SatisDash::Test::Reply(200, WorldAccepted));
	TestEqual(TEXT("two posts in all"), F.Net.sent(WorldRailsPost), 2);

	// A restart forgets the hashes: the first read after a start always posts, even the same body, even an empty layer.
	F.Sender.Stop();
	F.Bridge.Stop();
	F.Sender.Start(F.T);
	TestTrue(TEXT("after a restart the same body posts again"), F.toRailsPost(WorldRailsBody));
	F.Net.answer(WorldRailsPost, SatisDash::Test::Reply(200, WorldAccepted));

	FWorldFixture Empty;
	Empty.Sender.Start(Empty.T);
	TestTrue(TEXT("an empty layer is posted the first time"), Empty.toRailsPost(TEXT("[]")));
	TestTrue(TEXT("with an empty data array"), Empty.Net.lastBody(WorldRailsPost).EndsWith(TEXT("\"data\":[]}")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSenderRetryTest, "SatisDash.Core.WorldSender.Retry", SatisDash::Test::Flags)

bool FWorldSenderRetryTest::RunTest(const FString& Parameters)
{
	FWorldFixture F;
	F.Sender.Start(F.T);
	TestTrue(TEXT("a post is open"), F.toRailsPost(WorldRailsBody));
	const FString First = F.Net.lastBody(WorldRailsPost);

	// 503 with Retry-After: 30. The delay is max(Retry-After, backoff) = 30 s, and the same body is sent again.
	FHttpReply Unavailable = SatisDash::Test::Reply(503, TEXT("{}"));
	Unavailable.RetryAfter = TEXT("30");
	F.Net.answer(WorldRailsPost, Unavailable);
	F.tick(25);
	TestEqual(TEXT("no retry before the Retry-After"), F.Net.sent(WorldRailsPost), 1);
	F.tick(6);
	TestEqual(TEXT("retried after it"), F.Net.sent(WorldRailsPost), 2);
	SatisDash::Test::SameText(*this, TEXT("the same body, observedAt included"), F.Net.lastBody(WorldRailsPost), First);
	TestEqual(TEXT("without reading FRM again"), F.Net.sent(WorldRailsRead), 1);

	// 429 with no Retry-After: the second backoff, ceiling 2 s, delay 2 s (random 1).
	F.Net.answer(WorldRailsPost, SatisDash::Test::Reply(429, FString()));
	F.tick(1.5);
	TestEqual(TEXT("429: not yet"), F.Net.sent(WorldRailsPost), 2);
	F.tick(1);
	TestEqual(TEXT("429: retried after the backoff"), F.Net.sent(WorldRailsPost), 3);
	// No answer at all: the third backoff, 4 s.
	F.Net.answer(WorldRailsPost, SatisDash::Test::Reply(0, FString()));
	F.tick(3.5);
	TestEqual(TEXT("no answer: not yet"), F.Net.sent(WorldRailsPost), 3);
	F.tick(1);
	TestEqual(TEXT("no answer: retried"), F.Net.sent(WorldRailsPost), 4);
	// A 2xx that is not { accepted, unchanged } is transient too (backendClient.ts): the fourth backoff, 8 s.
	F.Net.answer(WorldRailsPost, SatisDash::Test::Reply(200, TEXT("{\"nope\":1}")));
	F.tick(7.5);
	TestEqual(TEXT("a 2xx in the wrong form: not yet"), F.Net.sent(WorldRailsPost), 4);
	F.tick(1);
	TestEqual(TEXT("and retried"), F.Net.sent(WorldRailsPost), 5);
	F.Net.answer(WorldRailsPost, SatisDash::Test::Reply(202, WorldAccepted));
	F.tick(100);
	TestEqual(TEXT("accepted: no more posts until the layer changes"), F.Net.sent(WorldRailsPost), 5);
	TestEqual(TEXT("and the held body is freed"), F.Sender.Held(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSenderRefusedTest, "SatisDash.Core.WorldSender.Refused", SatisDash::Test::Flags)

bool FWorldSenderRefusedTest::RunTest(const FString& Parameters)
{
	FWorldFixture F;
	F.Sender.Start(F.T);
	TestTrue(TEXT("a post is open"), F.toRailsPost(WorldRailsBody));
	F.Net.answer(WorldRailsPost, SatisDash::Test::Reply(400, TEXT("{\"error\":{\"code\":\"bad_request\"}}")));
	F.tick(300);
	TestEqual(TEXT("another 4xx: given up, not retried"), F.Net.sent(WorldRailsPost), 1);
	TestEqual(TEXT("and the held body is freed"), F.Sender.Held(), 0);
	// Not remembered: the next cycle reads the same raw body again and posts it again (an older backend may not know the route yet).
	TestTrue(TEXT("the next cycle reads again"), F.until([&F] { return F.Net.pending(WorldRailsRead) > 0; }, 700));
	F.Net.answer(WorldRailsRead, SatisDash::Test::Reply(200, WorldRailsBody));
	TestTrue(TEXT("and posts it again"), F.until([&F] { return F.Net.pending(WorldRailsPost) > 0; }, 20));
	TestEqual(TEXT("a second post"), F.Net.sent(WorldRailsPost), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSenderHaltTest, "SatisDash.Core.WorldSender.Halt", SatisDash::Test::Flags)

bool FWorldSenderHaltTest::RunTest(const FString& Parameters)
{
	FWorldFixture F;
	F.Sender.Start(F.T);
	TestTrue(TEXT("a post is open"), F.toRailsPost(WorldRailsBody));
	F.Net.answer(WorldRailsPost, SatisDash::Test::Reply(401, FString()));
	F.tick();
	TestTrue(TEXT("a 401 halts the agent (the driver reads this)"), F.Sender.TakeAuthRejected());
	TestFalse(TEXT("once"), F.Sender.TakeAuthRejected());
	TestFalse(TEXT("and the sender stops"), F.Sender.Active());
	TestEqual(TEXT("holding nothing"), F.Sender.Held(), 0);
	F.tick(2000);
	TestEqual(TEXT("no more reads"), F.Net.sent(WorldRailsRead), 1);
	TestEqual(TEXT("and no more posts"), F.Net.sent(WorldRailsPost), 1);
	F.Sender.Start(F.T);
	TestTrue(TEXT("a new Start runs it again"), F.Sender.Active() && !F.Sender.TakeAuthRejected());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSenderBehindTest, "SatisDash.Core.WorldSender.Behind", SatisDash::Test::Flags)

bool FWorldSenderBehindTest::RunTest(const FString& Parameters)
{
	FWorldFixture F;
	F.bIgnoreNodes = false;
	F.bSnapshotsIdle = false;
	F.Sender.Start(F.T);
	TestTrue(TEXT("rails are read"), F.serve(WorldRailsRead, SatisDash::Test::Reply(200, WorldRailsBody)));
	TestTrue(TEXT("resource nodes are read"), F.serve(WorldNodesRead, SatisDash::Test::Reply(200, WorldNodesBody)));
	F.tick(20);
	TestEqual(TEXT("both bodies are ready"), F.Sender.Held(), 2);
	TestEqual(TEXT("but a snapshot is queued or in flight: no world post goes"), F.posts(), 0);
	F.bSnapshotsIdle = true;
	F.tick(1);
	TestEqual(TEXT("the snapshots are idle: exactly one post, one at a time"), F.Net.pending(WorldRailsPost) + F.Net.pending(WorldNodesPost), 1);
	TestEqual(TEXT("rails first"), F.Net.pending(WorldRailsPost), 1);
	F.tick(5);
	TestEqual(TEXT("the second waits for the first's answer"), F.posts(), 1);
	F.Net.answer(WorldRailsPost, SatisDash::Test::Reply(200, WorldAccepted));
	F.tick(1);
	TestEqual(TEXT("then resource nodes go"), F.Net.pending(WorldNodesPost), 1);
	F.Net.answer(WorldNodesPost, SatisDash::Test::Reply(200, WorldAccepted));
	F.tick(1);
	TestEqual(TEXT("nothing is held after both are accepted"), F.Sender.Held(), 0);

	// A snapshot becoming busy while a body waits holds the post back again; Stop frees what is held.
	FWorldFixture G;
	G.bSnapshotsIdle = false;
	G.Sender.Start(G.T);
	TestTrue(TEXT("rails read"), G.serve(WorldRailsRead, SatisDash::Test::Reply(200, WorldRailsBody)));
	G.tick(10);
	TestEqual(TEXT("one body held"), G.Sender.Held(), 1);
	TestEqual(TEXT("and with the snapshots busy, no post goes"), G.posts(), 0);
	G.Sender.Stop();
	TestEqual(TEXT("Stop frees it"), G.Sender.Held(), 0);
	return true;
}
