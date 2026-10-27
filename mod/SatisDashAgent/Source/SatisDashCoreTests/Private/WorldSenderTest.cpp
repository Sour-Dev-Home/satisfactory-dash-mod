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

	/** A bridge, a sender whose jitter is the top of its range (x1.1: rails at 132 s then every 660 s, nodes and train routes at 528 s then every 1980 s and 660 s), and a fake clock. */
	struct FWorldFixture
	{
		SatisDash::Test::FFakeTransport Net;
		SatisDash::Test::FFakeWorker Worker;
		FFrmBridge Bridge{ Net, Worker };
		/** The jitter draw: 1.0 is the top of the range; a test sets 0 for the bottom (x0.9) before Start. */
		double Random01 = 1.0;
		/** Draws served first, in order (Start draws rails, nodes, train routes, conveyors); then Random01 again. */
		TArray<double> Draws;
		int32 DrawsTaken = 0;
		FWorldSender Sender{ Net, Bridge, [this] { return DrawsTaken < Draws.Num() ? Draws[DrawsTaken++] : Random01; } };
		FAgentConfig Config;
		FCredential Credential;
		double T = 1000;
		FDateTime Utc = FDateTime(2026, 10, 24, 12, 0, 0);
		bool bSnapshotsIdle = true;
		/** Answer a resource-node read with Code 0 when it shows up, so a test about rails isn't held up by it (one FRM request in flight). */
		bool bIgnoreNodes = true;
		/** R3: the native readers (a fake game) are handed to the sender, and the rails read is refused when it shows up. */
		bool bNativeReaders = false;
		SatisDash::Test::FFakeGame Game;
		FStepBudget TrainsBudget{ [this] { return T; } };
		FStepBudget LogisticsBudget{ [this] { return T; } };
		FStepBudget MarkersBudget{ [this] { return T; } };
		FStepBudget DronesBudget{ [this] { return T; } };

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
				if (bNativeReaders)
				{
					Net.answer(WorldRailsRead, SatisDash::Test::Reply(0, FString()));
				}
				Bridge.Tick(T, 8080, FString());
				Sender.Tick({ T, Utc, &Config, &Credential, bSnapshotsIdle, bNativeReaders ? &Game : nullptr, true, &TrainsBudget, &LogisticsBudget, &MarkersBudget, &DronesBudget });
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

		/** Ticks until Path is open, and answers it with Body (200). */
		bool cycle(const TCHAR* Path, const FString& Body)
		{
			const bool bOpen = until([this, Path] { return Net.pending(Path) > 0; }, 700);
			return bOpen && Net.answer(Path, SatisDash::Test::Reply(200, Body));
		}
	};

	/** How many captured LogSatisDash lines contain Needle. */
	int32 countLines(SatisDash::Test::FLogCapture& Log, const TCHAR* Needle)
	{
		int32 Count = 0;
		for (const FString& Line : Log.Take())
		{
			Count += Line.Contains(Needle) ? 1 : 0;
		}
		return Count;
	}

	/** Rails whose mapped body is over the 2,000,000-byte cap (135 segments of 1,000 points) but whose raw body is under FRM's 4 MB. */
	FString bigRails()
	{
		FString Json = TEXT("[");
		Json.Reserve(4000000);
		for (int32 Segment = 0; Segment < 135; ++Segment)
		{
			Json += FString::Printf(TEXT("%s{\"ID\":\"R%d\",\"SplineData\":["), Segment > 0 ? TEXT(",") : TEXT(""), Segment);
			for (int32 Point = 0; Point < 1000; ++Point)
			{
				// Different points (consecutive duplicates are collapsed), each 6 digits in metres: 16 bytes mapped, 28 raw.
				Json.Appendf(TEXT("%s{\"x\":%d,\"y\":99999900}"), Point > 0 ? TEXT(",") : TEXT(""), (900000 + Point) * 100);
			}
			Json += TEXT("]}");
		}
		return Json + TEXT("]");
	}
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

	// An invalid body settles nothing: the same bytes next cycle are hashed and parsed again (not SameRaw). Its warning is
	// written once per streak, and again when a clean cycle came between two bad ones.
	{
		SatisDash::Test::FLogCapture Log;
		FWorldFixture S;
		S.Sender.Start(S.T);
		const FString NotAnArray = TEXT("{\"x\":1}");
		TestTrue(TEXT("an invalid body is read"), S.cycle(WorldRailsRead, NotAnArray));
		S.tick(10);
		TestTrue(TEXT("and again"), S.until([&S] { return S.Net.pending(WorldRailsRead) > 0; }, 700));
		const int32 Before = S.Worker.Launched;
		S.Net.answer(WorldRailsRead, SatisDash::Test::Reply(200, NotAnArray));
		S.tick(10);
		TestEqual(TEXT("an invalid body is not settled: the same bytes are hashed and parsed again"), S.Worker.Launched, Before + 2);
		TestEqual(TEXT("still no post"), S.Net.sent(WorldRailsPost), 0);
		TestEqual(TEXT("one warning across the two bad cycles"), countLines(Log, TEXT("Couldn't read world layer rails")), 1);
		TestTrue(TEXT("a clean cycle"), S.cycle(WorldRailsRead, WorldRailsBody));
		TestTrue(TEXT("is posted"), S.until([&S] { return S.Net.pending(WorldRailsPost) > 0; }, 20));
		S.Net.answer(WorldRailsPost, SatisDash::Test::Reply(200, WorldAccepted));
		S.tick(10);
		TestTrue(TEXT("then a bad one again"), S.cycle(WorldRailsRead, NotAnArray));
		S.tick(10);
		TestEqual(TEXT("warns again after the clean cycle"), countLines(Log, TEXT("Couldn't read world layer rails")), 2);
	}

	// Over the cap: the raw hash IS settled (the same bytes next cycle cost only the hash steps, no parse and no post);
	// one warning per streak, again after a clean cycle.
	{
		SatisDash::Test::FLogCapture Log;
		FWorldFixture O;
		O.Sender.Start(O.T);
		const FString Big = bigRails();
		TestTrue(TEXT("the oversized layer is read"), O.until([&O] { return O.Net.pending(WorldRailsRead) > 0; }, 700));
		int32 Before = O.Worker.Launched;
		O.Net.answer(WorldRailsRead, SatisDash::Test::Reply(200, Big));
		O.tick(60);
		const int32 FirstCost = O.Worker.Launched - Before;
		TestEqual(TEXT("over the cap: nothing is posted"), O.Net.sent(WorldRailsPost), 0);
		TestEqual(TEXT("and it is warned about"), countLines(Log, TEXT("World layer rails is")), 1);
		TestTrue(TEXT("next cycle"), O.until([&O] { return O.Net.pending(WorldRailsRead) > 0; }, 700));
		Before = O.Worker.Launched;
		O.Net.answer(WorldRailsRead, SatisDash::Test::Reply(200, Big));
		O.tick(60);
		const int32 RepeatCost = O.Worker.Launched - Before;
		TestTrue(TEXT("an over-cap raw body is settled: the same bytes cost the hash steps only, no parse"), RepeatCost > 0 && RepeatCost < FirstCost);
		TestEqual(TEXT("and still nothing is posted"), O.Net.sent(WorldRailsPost), 0);
		TestEqual(TEXT("one warning across the two cycles"), countLines(Log, TEXT("World layer rails is")), 1);
		TestTrue(TEXT("a clean cycle"), O.cycle(WorldRailsRead, WorldRailsBody));
		TestTrue(TEXT("is posted"), O.until([&O] { return O.Net.pending(WorldRailsPost) > 0; }, 20));
		O.Net.answer(WorldRailsPost, SatisDash::Test::Reply(200, WorldAccepted));
		O.tick(10);
		TestTrue(TEXT("then over the cap again"), O.cycle(WorldRailsRead, Big));
		O.tick(60);
		TestEqual(TEXT("warns again after the clean cycle"), countLines(Log, TEXT("World layer rails is")), 2);
	}

	// Items left out: one warning per streak; a parsed cycle with none left out ends it.
	{
		SatisDash::Test::FLogCapture Log;
		FWorldFixture D;
		D.Sender.Start(D.T);
		const FString DropA = TEXT("[{\"ID\":\"R1\",\"SplineData\":[{\"x\":0,\"y\":0},{\"x\":300,\"y\":0}]},{\"ID\":\"R2\",\"SplineData\":[{\"x\":0,\"y\":0}]}]");
		const FString DropB = TEXT("[{\"ID\":\"R1\",\"SplineData\":[{\"x\":0,\"y\":0},{\"x\":300,\"y\":0}]},{\"ID\":\"R2\",\"SplineData\":[{\"x\":5,\"y\":0}]}]");
		TestTrue(TEXT("a layer with a rail too short to keep"), D.cycle(WorldRailsRead, DropA));
		TestTrue(TEXT("is posted"), D.until([&D] { return D.Net.pending(WorldRailsPost) > 0; }, 20));
		D.Net.answer(WorldRailsPost, SatisDash::Test::Reply(200, WorldAccepted));
		D.tick(10);
		TestEqual(TEXT("and warned about"), countLines(Log, TEXT("left them out")), 1);
		TestTrue(TEXT("same data, other bytes"), D.cycle(WorldRailsRead, DropB));
		D.tick(10);
		TestEqual(TEXT("nothing more to send"), D.Net.sent(WorldRailsPost), 1);
		TestEqual(TEXT("and no second warning in the same streak"), countLines(Log, TEXT("left them out")), 1);
		TestTrue(TEXT("a cycle with nothing left out"), D.cycle(WorldRailsRead, WorldRailsBody));
		D.tick(10);
		TestTrue(TEXT("then a short rail again"), D.cycle(WorldRailsRead, DropA));
		D.tick(10);
		TestEqual(TEXT("warns again after the clean cycle"), countLines(Log, TEXT("left them out")), 2);
	}
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

	// A post's reply that arrives after a Stop and a new Start belongs to the older run and changes nothing in the new one.
	FWorldFixture H;
	H.Sender.Start(H.T);
	TestTrue(TEXT("a post is open"), H.toRailsPost(WorldRailsBody));
	H.Sender.Stop();
	H.Bridge.Stop();
	H.Sender.Start(H.T);
	H.Net.answer(WorldRailsPost, SatisDash::Test::Reply(401, FString()));
	H.tick();
	TestFalse(TEXT("a stale 401 raises nothing"), H.Sender.TakeAuthRejected());
	TestTrue(TEXT("and the new run is still active"), H.Sender.Active());
	TestTrue(TEXT("and posts as usual"), H.toRailsPost(WorldRailsBody));
	TestEqual(TEXT("the new run's own post, the first it sent"), H.Net.sent(WorldRailsPost), 2);
	H.Net.answer(WorldRailsPost, SatisDash::Test::Reply(200, WorldAccepted));
	H.tick(2);
	TestEqual(TEXT("and its answer is taken"), H.Sender.Held(), 0);
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

namespace
{
	const TCHAR* WorldTrainRoutesPost = TEXT("/agent/v1/world/trainRoutes");

	/** A station with a trailing space in its name, one loading platform, and a train whose timetable stops there. */
	void worldTrainRoutesReadings(SatisDash::Test::FFakeGame& Game)
	{
		FStationReading Station;
		Station.Id = TEXT("Build_TrainStation_C_1");
		Station.Name = TEXT("Depot ");
		FPlatformReading Platform;
		Platform.Id = TEXT("Build_TrainDockingStation_C_1");
		Platform.bLoad = true;
		Platform.Items = { TEXT("Desc_IronPlate_C") };
		Station.Platforms.Add(Platform);
		Game.Stations = { Station };
		FTrainReading Train;
		Train.Id = TEXT("BP_Train_C_1");
		Train.Name = TEXT("Express");
		Train.State = SatisDash::ETrainState::SelfDriving;
		Train.Stops = { TEXT("Depot ") };
		Train.Items = { TEXT("Desc_IronPlate_C") };
		Game.Trains = { Train };
	}

	/** Requests the fixture sent to FRM-style train read paths, as a count. */
	int32 worldFrmTrainReads(const SatisDash::Test::FFakeTransport& Net)
	{
		return Net.sent(TEXT("/getTrains")) + Net.sent(TEXT("/getTrainStation")) + Net.sent(TEXT("/getTrainRoutes"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSenderTrainRoutesTest, "SatisDash.Core.WorldSender.TrainRoutes", SatisDash::Test::Flags)

bool FWorldSenderTrainRoutesTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	// trainsSource native: the layer scans (a sliced game read), submits, and posts once; the first read is 480 s x 1.1 = 528 s after Start
	// (the resource-node layer's first-read rule: 8 minutes, jitter +/-10%, so a first post inside tick-cost-native's measured span).
	FWorldFixture F;
	F.Config.TrainsSource = ESource::Native;
	F.Config.ResourceNodesSource = ESource::Bridge;
	F.Config.RailsSource = ESource::Bridge; // rails and conveyors default to native since the S3 switch; keep their pass out of this layer's reads
	F.Config.ConveyorsSource = ESource::Bridge;
	F.bNativeReaders = true;
	worldTrainRoutesReadings(F.Game);
	F.Sender.Start(F.T);
	TestFalse(TEXT("no read before 8 minutes less the jitter (432 s)"), F.until([&F] { return F.Game.TrainsBegins > 0 || F.Net.pending(WorldTrainRoutesPost) > 0; }, 420));
	TestTrue(TEXT("the layer is posted"), F.until([&F] { return F.Net.pending(WorldTrainRoutesPost) > 0; }, 200));
	TestEqual(TEXT("one begin"), F.Game.TrainsBegins, 1);
	TestEqual(TEXT("three steps, one a tick"), F.Game.TrainsStepCalls, 3);
	const FString Body = F.Net.lastBody(WorldTrainRoutesPost);
	TestTrue(TEXT("the station comes first, its name exactly as the game gave it"), Body.Contains(TEXT("\"data\":[{\"recordType\":\"station\",\"id\":\"Build_TrainStation_C_1\",\"name\":\"Depot \",")));
	TestTrue(TEXT("the train follows with its stops"), Body.Contains(TEXT("{\"recordType\":\"train\",\"id\":\"BP_Train_C_1\",\"name\":\"Express\",\"stops\":[\"Depot \"],")));
	F.Net.answer(WorldTrainRoutesPost, SatisDash::Test::Reply(200, WorldAccepted));
	F.tick(2);
	TestEqual(TEXT("the accepted body is not held"), F.Sender.Held(), 0);

	// The game thread's part is logged once per read, counts and times only (no ids, no names).
	int32 ReadLines = 0;
	int32 Leaks = 0;
	for (const FString& Line : Log.Take())
	{
		ReadLines += Line.Contains(TEXT("Train routes read (game thread only): 1 stations, 1 trains in 3 steps, longest step ")) ? 1 : 0;
		Leaks += Line.Contains(TEXT("Depot")) || Line.Contains(TEXT("Express")) || Line.Contains(TEXT("BP_Train")) ? 1 : 0;
	}
	TestEqual(TEXT("one Train routes read line"), ReadLines, 1);
	TestEqual(TEXT("no id or name in the log"), Leaks, 0);

	// The next cycle (600 s x 1.1) reads again, finds the same railway and posts nothing.
	TestTrue(TEXT("the next cycle reads again"), F.until([&F] { return F.Game.TrainsBegins >= 2 && F.Game.TrainsStepCalls >= 3; }, 700));
	F.tick(10);
	TestEqual(TEXT("an unchanged body is not posted again"), F.Net.sent(WorldTrainRoutesPost), 1);
	TestEqual(TEXT("FRM is never asked for the layer"), worldFrmTrainReads(F.Net), 0);

	// A changed railway is posted.
	F.Game.Trains[0].Name = TEXT("Local");
	TestTrue(TEXT("a changed layer posts"), F.until([&F] { return F.Net.pending(WorldTrainRoutesPost) > 0; }, 700));
	F.Net.answer(WorldTrainRoutesPost, SatisDash::Test::Reply(200, WorldAccepted));

	// A railway with no trains and no stations is a finished, empty read: an empty layer is posted, never FRM.
	F.Game.Trains.Reset();
	F.Game.Stations.Reset();
	TestTrue(TEXT("an empty railway posts an empty layer"), F.until([&F] { return F.Net.pending(WorldTrainRoutesPost) > 0; }, 700));
	TestTrue(TEXT("with data:[]"), F.Net.lastBody(WorldTrainRoutesPost).Contains(TEXT("\"data\":[]")));
	F.Net.answer(WorldTrainRoutesPost, SatisDash::Test::Reply(200, WorldAccepted));
	TestEqual(TEXT("and FRM is still not asked"), worldFrmTrainReads(F.Net), 0);

	// An abandoned read (a world swap) is not an empty railway: nothing is posted, one warning, and it is read again soon.
	F.Game.bTrainsAbandon = true;
	const int32 Posts = F.Net.sent(WorldTrainRoutesPost);
	const int32 Begun = F.Game.TrainsBegins;
	TestTrue(TEXT("the next read begins"), F.until([&F, Begun] { return F.Game.TrainsBegins > Begun && F.Game.TrainsStepCalls >= 3; }, 700));
	F.tick(1);
	TestTrue(TEXT("and is retried soon, not after a whole cycle"), F.until([&F, Begun] { return F.Game.TrainsBegins > Begun + 1; }, 60));
	TestEqual(TEXT("an abandoned read posts nothing"), F.Net.sent(WorldTrainRoutesPost), Posts);
	TestEqual(TEXT("and FRM is not asked"), worldFrmTrainReads(F.Net), 0);
	TestEqual(TEXT("one warning for the abandoned reads"), countLines(Log, TEXT("train routes read was abandoned")), 1);
	F.Game.bTrainsAbandon = false;

	// A body over the layer cap (WorldLayerMaxBytes) is skipped as the node layer's is: the builder does not apply it, the world read does.
	FStationReading Big;
	Big.Name = TEXT("Big");
	FPlatformReading Wide;
	for (int32 Item = 0; Item < TrainRoutesMaxItemClasses; ++Item)
	{
		Wide.Items.Add(FString::Printf(TEXT("Desc_Item_%02d_C"), Item));
	}
	F.Game.Stations.Reset();
	for (int32 Station = 0; Station < 40; ++Station)
	{
		Big.Id = FString::Printf(TEXT("Build_TrainStation_C_%02d"), Station);
		Big.Platforms.Reset();
		for (int32 P = 0; P < TrainRoutesMaxPlatforms; ++P)
		{
			Wide.Id = FString::Printf(TEXT("Build_TrainDockingStation_C_%02d"), P);
			Big.Platforms.Add(Wide);
		}
		F.Game.Stations.Add(Big);
	}
	F.tick(120);
	TestEqual(TEXT("an over-cap layer is not posted"), F.Net.sent(WorldTrainRoutesPost), Posts);
	TestEqual(TEXT("it is logged once, with its size"), countLines(Log, TEXT("World layer trainRoutes is ")), 1);

	// trainsSource bridge: the layer is never scheduled (no read, no post, no FRM request), however long the sender runs.
	FWorldFixture G;
	G.bNativeReaders = true;
	G.Config.ResourceNodesSource = ESource::Bridge;
	G.Config.TrainsSource = ESource::Bridge;
	G.Config.RailsSource = ESource::Bridge; // the last assertion is about the bridge rails; the default is native since the S3 switch
	worldTrainRoutesReadings(G.Game);
	G.Sender.Start(G.T);
	G.tick(2000);
	TestEqual(TEXT("bridge: no game read"), G.Game.TrainsBegins, 0);
	TestEqual(TEXT("bridge: no post"), G.Net.sent(WorldTrainRoutesPost), 0);
	TestEqual(TEXT("bridge: FRM is not asked for it either"), worldFrmTrainReads(G.Net), 0);
	TestTrue(TEXT("bridge: rails still ask FRM on their own schedule"), G.Net.sent(WorldRailsRead) > 0);
	return true;
}

namespace
{
	const TCHAR* WorldMapMarkersPost = TEXT("/agent/v1/world/mapMarkers");

	/** A fixture with only mapMarkers native (every other layer on the bridge) and the jitter at the bottom of its range (x0.9: 432 s, then every 540 s). */
	void worldMapMarkersFixture(FWorldFixture& F)
	{
		F.Random01 = 0;
		F.Config.ResourceNodesSource = ESource::Bridge;
		F.Config.TrainsSource = ESource::Bridge;
		F.Config.RailsSource = ESource::Bridge;
		F.Config.ConveyorsSource = ESource::Bridge;
		F.bNativeReaders = true;
		// Two markers given out of order: the body sorts them by id.
		FMapMarkerReading B;
		B.Id = TEXT("BBBB0000");
		B.Name = TEXT("Secret base");
		B.Icon = 3;
		FMapMarkerReading A = B;
		A.Id = TEXT("AAAA0000");
		A.Name = TEXT("Hidden cave");
		F.Game.Markers = { B, A };
		F.Game.MarkersCounts.Read = 2;
		F.Game.MarkersCounts.Dropped = 1;
	}

	int32 worldFrmMapMarkerReads(const SatisDash::Test::FFakeTransport& Net)
	{
		return Net.sent(TEXT("/getMapMarkers"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSenderMapMarkersTest, "SatisDash.Core.WorldSender.MapMarkers", SatisDash::Test::Flags)

bool FWorldSenderMapMarkersTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	FWorldFixture F;
	worldMapMarkersFixture(F);
	F.Config.MapMarkersSource = ESource::Native;
	F.Sender.Start(F.T);
	TestFalse(TEXT("no read before 8 minutes less the jitter (432 s)"), F.until([&F] { return F.Game.MarkersBegins > 0; }, 431));
	TestTrue(TEXT("the read begins at 433 s"), F.until([&F] { return F.Game.MarkersBegins > 0; }, 20));
	TestEqual(TEXT("one begin"), F.Game.MarkersBegins, 1);
	F.tick(0.5);
	TestEqual(TEXT("one step a tick"), F.Game.MarkersStepCalls, 1);
	TestTrue(TEXT("the layer is posted"), F.until([&F] { return F.Net.pending(WorldMapMarkersPost) > 0; }, 20));
	TestEqual(TEXT("three steps in all"), F.Game.MarkersStepCalls, 3);
	const FString Body = F.Net.lastBody(WorldMapMarkersPost);
	const int32 First = Body.Find(TEXT("\"id\":\"AAAA0000\""));
	const int32 Second = Body.Find(TEXT("\"id\":\"BBBB0000\""));
	TestTrue(TEXT("both markers are in the body, sorted by id"), First != INDEX_NONE && Second != INDEX_NONE && First < Second);
	F.Net.answer(WorldMapMarkersPost, SatisDash::Test::Reply(200, WorldAccepted));
	F.tick(2);
	TestEqual(TEXT("the accepted body is not held"), F.Sender.Held(), 0);

	// Counts and times only in the log (no id, no name).
	int32 ReadLines = 0;
	int32 Leaks = 0;
	for (const FString& Line : Log.Take())
	{
		ReadLines += Line.Contains(TEXT("Map markers read (game thread only): 2 markers, 1 dropped in 3 steps, longest step ")) ? 1 : 0;
		Leaks += Line.Contains(TEXT("Secret")) || Line.Contains(TEXT("Hidden")) || Line.Contains(TEXT("AAAA")) || Line.Contains(TEXT("BBBB")) ? 1 : 0;
	}
	TestEqual(TEXT("one Map markers read line"), ReadLines, 1);
	TestEqual(TEXT("no id or name in the log"), Leaks, 0);

	// The next cycle reads the same markers again: the same data hash, nothing is posted.
	TestTrue(TEXT("the next cycle reads again"), F.until([&F] { return F.Game.MarkersBegins >= 2 && F.Game.MarkersStepCalls >= 3; }, 700));
	F.tick(10);
	TestEqual(TEXT("an unchanged body is not posted again"), F.Net.sent(WorldMapMarkersPost), 1);
	TestEqual(TEXT("FRM is never asked for the layer"), worldFrmMapMarkerReads(F.Net), 0);

	// A finished read with no markers is a real, empty map: an empty layer is posted (M1).
	F.Game.Markers.Reset();
	TestTrue(TEXT("an empty map posts an empty layer"), F.until([&F] { return F.Net.pending(WorldMapMarkersPost) > 0; }, 700));
	TestTrue(TEXT("with data:[]"), F.Net.lastBody(WorldMapMarkersPost).Contains(TEXT("\"data\":[]")));
	F.Net.answer(WorldMapMarkersPost, SatisDash::Test::Reply(200, WorldAccepted));

	// An abandoned read posts nothing, warns once and is read again after NodeScanRetrySeconds.
	F.Game.bMarkersAbandoned = true;
	const int32 Posts = F.Net.sent(WorldMapMarkersPost);
	const int32 Begun = F.Game.MarkersBegins;
	TestTrue(TEXT("the next read begins"), F.until([&F, Begun] { return F.Game.MarkersBegins > Begun && F.Game.MarkersStepCalls >= 3; }, 700));
	F.tick(1);
	TestTrue(TEXT("and is retried soon, not after a whole cycle"), F.until([&F, Begun] { return F.Game.MarkersBegins > Begun + 1; }, 60));
	TestEqual(TEXT("an abandoned read posts nothing"), F.Net.sent(WorldMapMarkersPost), Posts);
	TestEqual(TEXT("and FRM is not asked"), worldFrmMapMarkerReads(F.Net), 0);
	TestEqual(TEXT("one warning for the abandoned reads"), countLines(Log, TEXT("World layer mapMarkers: the game's read was abandoned")), 1);
	F.Game.bMarkersAbandoned = false;

	// The default config (key unset = bridge): the layer is never begun, posted, requested or logged.
	const int32 LoggedBefore = countLines(Log, TEXT("arker")); // the capture keeps every line: count what the native section wrote
	FWorldFixture G;
	worldMapMarkersFixture(G);
	G.Sender.Start(G.T);
	G.tick(2000);
	TestEqual(TEXT("default: no game read"), G.Game.MarkersBegins, 0);
	TestEqual(TEXT("default: no post"), G.Net.sent(WorldMapMarkersPost), 0);
	TestEqual(TEXT("default: FRM is not asked either"), worldFrmMapMarkerReads(G.Net), 0);
	TestEqual(TEXT("default: nothing logged"), countLines(Log, TEXT("arker")), LoggedBefore);

	// The source flips to bridge under an open read: the read is dropped and the layer rescheduled (never read again).
	FWorldFixture H;
	worldMapMarkersFixture(H);
	H.Config.MapMarkersSource = ESource::Native;
	H.Sender.Start(H.T);
	TestTrue(TEXT("a read is open"), H.until([&H] { return H.Game.MarkersBegins > 0; }, 440));
	H.Config.MapMarkersSource = ESource::Bridge;
	H.tick(1);
	TestEqual(TEXT("the open read was ended"), H.Game.MarkersEnds, 1);
	TestFalse(TEXT("and is no longer open"), H.Game.bMarkersOpen);
	H.tick(1500);
	TestEqual(TEXT("flipped: no second read"), H.Game.MarkersBegins, 1);
	TestEqual(TEXT("flipped: no post"), H.Net.sent(WorldMapMarkersPost), 0);

	// A read that cannot begin (no manager or subsystem) retries every 30 s and says so in one rate-limited, counts-only warning (M-2).
	FWorldFixture I;
	worldMapMarkersFixture(I);
	I.Config.MapMarkersSource = ESource::Native;
	I.Config.DroneRoutesSource = ESource::Native;
	I.Game.bBeginMarkersOk = false;
	I.Game.bBeginDronesOk = false;
	I.Sender.Start(I.T);
	TestTrue(TEXT("a begin that fails is tried again"), I.until([&I] { return I.Game.MarkersBegins >= 6 && I.Game.DronesBegins >= 6; }, 700));
	const int32 Markers = countLines(Log, TEXT("World layer mapMarkers: the game's reader could not start; will try again in 30 s"));
	const int32 Drones = countLines(Log, TEXT("World layer droneRoutes: the game's reader could not start; will try again in 30 s"));
	TestTrue(TEXT("markers warn, but not on every retry"), Markers >= 1 && Markers < I.Game.MarkersBegins);
	TestTrue(TEXT("drones warn, but not on every retry"), Drones >= 1 && Drones < I.Game.DronesBegins);
	TestEqual(TEXT("a begin that fails posts nothing"), I.Net.sent(WorldMapMarkersPost), 0);
	TestEqual(TEXT("and no name is in the log"), countLines(Log, TEXT("Secret")) + countLines(Log, TEXT("Hidden")), 0);
	return true;
}

namespace
{
	const TCHAR* WorldDroneRoutesPost = TEXT("/agent/v1/world/droneRoutes");

	/** A fixture with only droneRoutes native (every other layer on the bridge), jitter at x0.9 (432 s, then every 540 s). */
	void worldDroneRoutesFixture(FWorldFixture& F)
	{
		F.Random01 = 0;
		F.Config.ResourceNodesSource = ESource::Bridge;
		F.Config.TrainsSource = ESource::Bridge;
		F.Config.RailsSource = ESource::Bridge;
		F.Config.ConveyorsSource = ESource::Bridge;
		F.bNativeReaders = true;
		// Two ports given out of order, paired to each other: the body sorts them by id.
		FDronePortReading B;
		B.Id = TEXT("DDDD0000");
		B.Name = TEXT("Dock west");
		B.bPaired = true;
		B.PairedName = TEXT("Dock east");
		B.PairedId = TEXT("CCCC0000");
		FDronePortReading A;
		A.Id = TEXT("CCCC0000");
		A.Name = TEXT("Dock east");
		A.bPaired = true;
		A.PairedName = TEXT("Dock west");
		A.PairedId = TEXT("DDDD0000");
		F.Game.DronePorts = { B, A };
		F.Game.DronesCounts.Read = 2;
		F.Game.DronesCounts.Dropped = 1;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSenderDroneRoutesTest, "SatisDash.Core.WorldSender.DroneRoutes", SatisDash::Test::Flags)

bool FWorldSenderDroneRoutesTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	FWorldFixture F;
	worldDroneRoutesFixture(F);
	F.Config.DroneRoutesSource = ESource::Native;
	F.Sender.Start(F.T);
	TestFalse(TEXT("no read before 8 minutes less the jitter (432 s)"), F.until([&F] { return F.Game.DronesBegins > 0; }, 431));
	TestTrue(TEXT("the read begins at 433 s"), F.until([&F] { return F.Game.DronesBegins > 0; }, 20));
	TestEqual(TEXT("one begin"), F.Game.DronesBegins, 1);
	TestEqual(TEXT("the markers reader is not begun"), F.Game.MarkersBegins, 0);
	F.tick(0.5);
	TestEqual(TEXT("one step a tick"), F.Game.DronesStepCalls, 1);
	TestTrue(TEXT("the layer is posted"), F.until([&F] { return F.Net.pending(WorldDroneRoutesPost) > 0; }, 20));
	TestEqual(TEXT("three steps in all"), F.Game.DronesStepCalls, 3);
	const FString Body = F.Net.lastBody(WorldDroneRoutesPost);
	const int32 First = Body.Find(TEXT("\"id\":\"CCCC0000\""));
	const int32 Second = Body.Find(TEXT("\"id\":\"DDDD0000\""));
	TestTrue(TEXT("both ports are in the body, sorted by id"), First != INDEX_NONE && Second != INDEX_NONE && First < Second);
	TestTrue(TEXT("with their pairedId"), Body.Contains(TEXT("\"pairedId\":\"DDDD0000\"")));
	F.Net.answer(WorldDroneRoutesPost, SatisDash::Test::Reply(200, WorldAccepted));
	F.tick(2);
	TestEqual(TEXT("the accepted body is not held"), F.Sender.Held(), 0);

	// Counts and times only in the log (no id, no name).
	int32 ReadLines = 0;
	int32 Leaks = 0;
	for (const FString& Line : Log.Take())
	{
		ReadLines += Line.Contains(TEXT("Drone routes read (game thread only): 2 ports, 1 dropped in 3 steps, longest step ")) ? 1 : 0;
		Leaks += Line.Contains(TEXT("Dock")) || Line.Contains(TEXT("CCCC")) || Line.Contains(TEXT("DDDD")) ? 1 : 0;
	}
	TestEqual(TEXT("one Drone routes read line"), ReadLines, 1);
	TestEqual(TEXT("no id or name in the log"), Leaks, 0);

	// The next cycle reads the same ports again: the same data hash, nothing is posted.
	TestTrue(TEXT("the next cycle reads again"), F.until([&F] { return F.Game.DronesBegins >= 2 && F.Game.DronesStepCalls >= 3; }, 700));
	F.tick(10);
	TestEqual(TEXT("an unchanged body is not posted again"), F.Net.sent(WorldDroneRoutesPost), 1);
	TestEqual(TEXT("FRM is never asked for the layer"), F.Net.sent(TEXT("/getDroneStation")), 0);

	// A finished read with no ports is a real, empty layer: it is posted.
	F.Game.DronePorts.Reset();
	TestTrue(TEXT("an empty layer is posted"), F.until([&F] { return F.Net.pending(WorldDroneRoutesPost) > 0; }, 700));
	TestTrue(TEXT("with data:[]"), F.Net.lastBody(WorldDroneRoutesPost).Contains(TEXT("\"data\":[]")));
	F.Net.answer(WorldDroneRoutesPost, SatisDash::Test::Reply(200, WorldAccepted));

	// An abandoned read posts nothing, warns once and is read again after NodeScanRetrySeconds.
	F.Game.bDronesAbandoned = true;
	const int32 Posts = F.Net.sent(WorldDroneRoutesPost);
	const int32 Begun = F.Game.DronesBegins;
	TestTrue(TEXT("the next read begins"), F.until([&F, Begun] { return F.Game.DronesBegins > Begun && F.Game.DronesStepCalls >= 3; }, 700));
	F.tick(1);
	TestTrue(TEXT("and is retried soon, not after a whole cycle"), F.until([&F, Begun] { return F.Game.DronesBegins > Begun + 1; }, 60));
	TestEqual(TEXT("an abandoned read posts nothing"), F.Net.sent(WorldDroneRoutesPost), Posts);
	TestEqual(TEXT("and FRM is not asked"), F.Net.sent(TEXT("/getDroneStation")), 0);
	TestEqual(TEXT("one warning for the abandoned reads"), countLines(Log, TEXT("World layer droneRoutes: the game's read was abandoned")), 1);
	F.Game.bDronesAbandoned = false;

	// The default config (key unset = bridge): the layer is never begun, posted, requested or logged.
	const int32 LoggedBefore = countLines(Log, TEXT("Drone routes"));
	FWorldFixture G;
	worldDroneRoutesFixture(G);
	G.Sender.Start(G.T);
	G.tick(2000);
	TestEqual(TEXT("default: no game read"), G.Game.DronesBegins, 0);
	TestEqual(TEXT("default: no post"), G.Net.sent(WorldDroneRoutesPost), 0);
	TestEqual(TEXT("default: FRM is not asked either"), G.Net.sent(TEXT("/getDroneStation")), 0);
	TestEqual(TEXT("default: nothing logged"), countLines(Log, TEXT("Drone routes")), LoggedBefore);

	// The source flips to bridge under an open read: the read is dropped and the layer rescheduled.
	FWorldFixture H;
	worldDroneRoutesFixture(H);
	H.Config.DroneRoutesSource = ESource::Native;
	H.Sender.Start(H.T);
	TestTrue(TEXT("a read is open"), H.until([&H] { return H.Game.DronesBegins > 0; }, 440));
	H.Config.DroneRoutesSource = ESource::Bridge;
	H.tick(1);
	TestEqual(TEXT("the open read was ended"), H.Game.DronesEnds, 1);
	TestFalse(TEXT("and is no longer open"), H.Game.bDronesOpen);
	TestEqual(TEXT("the markers reader was never ended"), H.Game.MarkersEnds, 0);
	H.tick(1500);
	TestEqual(TEXT("flipped: no second read"), H.Game.DronesBegins, 1);
	TestEqual(TEXT("flipped: no post"), H.Net.sent(WorldDroneRoutesPost), 0);

	// Both small layers native: two begins, two budgets, no interference; each posts once.
	FWorldFixture M;
	worldMapMarkersFixture(M);
	worldDroneRoutesFixture(M);
	M.Config.MapMarkersSource = ESource::Native;
	M.Config.DroneRoutesSource = ESource::Native;
	M.Sender.Start(M.T);
	TestTrue(TEXT("both reads begin"), M.until([&M] { return M.Game.MarkersBegins > 0 && M.Game.DronesBegins > 0; }, 440));
	TestEqual(TEXT("one begin each"), M.Game.MarkersBegins + M.Game.DronesBegins, 2);
	TestTrue(TEXT("both finish"), M.until([&M] { return M.Net.pending(WorldMapMarkersPost) > 0 || M.Net.pending(WorldDroneRoutesPost) > 0; }, 20));
	TestEqual(TEXT("the markers budget counted its own three steps"), M.MarkersBudget.Steps(), 3);
	TestEqual(TEXT("the drone budget counted its own three steps"), M.DronesBudget.Steps(), 3);
	for (int32 Round = 0; Round < 2; ++Round)
	{
		M.until([&M] { return M.Net.pending(WorldMapMarkersPost) > 0 || M.Net.pending(WorldDroneRoutesPost) > 0; }, 20);
		M.Net.answer(WorldMapMarkersPost, SatisDash::Test::Reply(200, WorldAccepted));
		M.Net.answer(WorldDroneRoutesPost, SatisDash::Test::Reply(200, WorldAccepted));
		M.tick(2);
	}
	TestEqual(TEXT("markers posted once"), M.Net.sent(WorldMapMarkersPost), 1);
	TestEqual(TEXT("drone routes posted once"), M.Net.sent(WorldDroneRoutesPost), 1);
	TestTrue(TEXT("each body carries its own data"), M.Net.lastBody(WorldMapMarkersPost).Contains(TEXT("AAAA0000")) && !M.Net.lastBody(WorldMapMarkersPost).Contains(TEXT("CCCC0000"))
		&& M.Net.lastBody(WorldDroneRoutesPost).Contains(TEXT("CCCC0000")) && !M.Net.lastBody(WorldDroneRoutesPost).Contains(TEXT("AAAA0000")));

	// One small layer abandoned while the other is open: the other still posts exactly once.
	FWorldFixture P;
	worldMapMarkersFixture(P);
	worldDroneRoutesFixture(P);
	P.Config.MapMarkersSource = ESource::Native;
	P.Config.DroneRoutesSource = ESource::Native;
	P.Game.bDronesAbandoned = true;
	P.Sender.Start(P.T);
	TestTrue(TEXT("abandoned drones: the markers body is posted"), P.until([&P] { return P.Net.pending(WorldMapMarkersPost) > 0; }, 440));
	P.Net.answer(WorldMapMarkersPost, SatisDash::Test::Reply(200, WorldAccepted));
	P.tick(2);
	TestEqual(TEXT("abandoned drones: markers posted once"), P.Net.sent(WorldMapMarkersPost), 1);
	TestEqual(TEXT("abandoned drones: drone routes posted nothing"), P.Net.sent(WorldDroneRoutesPost), 0);

	// One small layer flipped to bridge while the other is open: only that reader ends, the other still posts exactly once.
	FWorldFixture Q;
	worldMapMarkersFixture(Q);
	worldDroneRoutesFixture(Q);
	Q.Config.MapMarkersSource = ESource::Native;
	Q.Config.DroneRoutesSource = ESource::Native;
	Q.Sender.Start(Q.T);
	TestTrue(TEXT("both reads are open"), Q.until([&Q] { return Q.Game.MarkersBegins > 0 && Q.Game.DronesBegins > 0; }, 440));
	Q.Config.DroneRoutesSource = ESource::Bridge;
	TestTrue(TEXT("flipped drones: the markers body is posted"), Q.until([&Q] { return Q.Net.pending(WorldMapMarkersPost) > 0; }, 20));
	Q.Net.answer(WorldMapMarkersPost, SatisDash::Test::Reply(200, WorldAccepted));
	Q.tick(2);
	TestEqual(TEXT("flipped drones: only the drone reader was ended"), Q.Game.DronesEnds, 1);
	TestEqual(TEXT("flipped drones: the markers reader was not ended"), Q.Game.MarkersEnds, 0);
	TestEqual(TEXT("flipped drones: markers posted once"), Q.Net.sent(WorldMapMarkersPost), 1);
	TestEqual(TEXT("flipped drones: drone routes posted nothing"), Q.Net.sent(WorldDroneRoutesPost), 0);
	return true;
}

namespace
{
	/** R5: two tracks given out of order, so the sorted body is the proof the worker built it. */
	void worldLogisticsReadings(SatisDash::Test::FFakeGame& Game)
	{
		FRailReading B;
		B.Id = TEXT("Build_RailroadTrack_C_B");
		B.Points = { { 0.0, 0.0 }, { 300.0, 0.0 } };
		FRailReading A;
		A.Id = TEXT("Build_RailroadTrack_C_A");
		A.Points = { { 0.0, 100.0 }, { 300.0, 100.0 } };
		Game.LogisticsRails = { B, A };
		Game.LogisticsCounts.Buildables = 5;
		Game.LogisticsCounts.Tracks = 2;
	}

	/** A fixture whose rails are native, the other native-capable layers on the bridge (conveyors too: they default to native since the S3 switch), the jitter at the bottom of its range. */
	void worldLogisticsNative(FWorldFixture& F)
	{
		F.Config.RailsSource = ESource::Native;
		F.Config.ConveyorsSource = ESource::Bridge;
		F.Config.ResourceNodesSource = ESource::Bridge;
		F.Config.TrainsSource = ESource::Bridge;
		F.bNativeReaders = true;
		F.Random01 = 0.0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSenderLogisticsTest, "SatisDash.Core.WorldSender.Logistics", SatisDash::Test::Flags)

bool FWorldSenderLogisticsTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	FWorldFixture F;
	worldLogisticsNative(F);
	worldLogisticsReadings(F.Game);
	F.Sender.Start(F.T);
	// railsSource native: the first read is 480 s x 0.9 = 432 s after Start, not the bridge's 108 s.
	TestFalse(TEXT("nothing is begun before 432 s"), F.until([&F] { return F.Game.LogisticsBegins > 0; }, 431));
	TestTrue(TEXT("the pass begins at 432 s"), F.until([&F] { return F.Game.LogisticsBegins > 0; }, 3));
	TestEqual(TEXT("one begin"), F.Game.LogisticsBegins, 1);
	TestTrue(TEXT("with the rails flag"), F.Game.bLastBeginRails);
	TestFalse(TEXT("and not the conveyors flag"), F.Game.bLastBeginConveyors);
	TestEqual(TEXT("not stepped by the tick that begins it"), F.Game.LogisticsStepCalls, 0);
	F.tick();
	TestEqual(TEXT("one step a tick"), F.Game.LogisticsStepCalls, 1);
	F.tick();
	TestEqual(TEXT("still one step a tick"), F.Game.LogisticsStepCalls, 2);
	TestTrue(TEXT("the layer is posted after the third step"), F.until([&F] { return F.Net.pending(WorldRailsPost) > 0; }, 10));
	TestEqual(TEXT("three steps in all"), F.Game.LogisticsStepCalls, 3);
	const FString Body = F.Net.lastBody(WorldRailsPost);
	const int32 First = Body.Find(TEXT("\"id\":\"Build_RailroadTrack_C_A\""));
	const int32 Second = Body.Find(TEXT("\"id\":\"Build_RailroadTrack_C_B\""));
	TestTrue(TEXT("both tracks are in the body"), First != INDEX_NONE && Second != INDEX_NONE);
	TestTrue(TEXT("sorted by id"), First < Second);
	F.Net.answer(WorldRailsPost, SatisDash::Test::Reply(200, WorldAccepted));
	F.tick(2);
	TestEqual(TEXT("one post"), F.Net.sent(WorldRailsPost), 1);
	TestEqual(TEXT("FRM is never asked for rails"), F.Net.sent(WorldRailsRead), 0);
	TestEqual(TEXT("the accepted body is not held"), F.Sender.Held(), 0);

	// A second pass over the same readings posts nothing (same data hash).
	TestTrue(TEXT("the next cycle begins another pass"), F.until([&F] { return F.Game.LogisticsBegins >= 2; }, 700));
	F.tick(10);
	TestEqual(TEXT("the second pass has three steps too"), F.Game.LogisticsStepCalls, 3);
	TestEqual(TEXT("an unchanged layer is not posted again"), F.Net.sent(WorldRailsPost), 1);

	// One log line per pass: counts and times only, no ids.
	int32 ReadLines = 0;
	int32 Leaks = 0;
	for (const FString& Line : Log.Take())
	{
		ReadLines += Line.Contains(TEXT("Logistics read (game thread only): layers rails=1 conveyors=0 powerLines=0, 5 buildables, 2 tracks, 0 belts, 0 lifts, 0 pipes, 0 open ends, 0 ownerless ends, wires 0, expected wires -1 in 3 steps, longest step ")) ? 1 : 0;
		Leaks += Line.Contains(TEXT("Build_RailroadTrack")) ? 1 : 0;
	}
	TestEqual(TEXT("one Logistics read line a pass"), ReadLines, 2);
	TestEqual(TEXT("no id in the log"), Leaks, 0);

	// An abandoned read (a world swap, no subsystem) posts nothing, warns once and is retried after NodeScanRetrySeconds.
	F.Game.bLogisticsAbandoned = true;
	const int32 Posts = F.Net.sent(WorldRailsPost);
	const int32 Begun = F.Game.LogisticsBegins;
	TestTrue(TEXT("the next pass begins"), F.until([&F, Begun] { return F.Game.LogisticsBegins > Begun; }, 700));
	TestTrue(TEXT("and is retried soon, not after a whole cycle"), F.until([&F, Begun] { return F.Game.LogisticsBegins > Begun + 1; }, 45));
	TestTrue(TEXT("and again"), F.until([&F, Begun] { return F.Game.LogisticsBegins > Begun + 2; }, 45));
	TestEqual(TEXT("an abandoned read posts nothing"), F.Net.sent(WorldRailsPost), Posts);
	TestEqual(TEXT("one warning for the abandoned reads"), countLines(Log, TEXT("World layer logistics: the game's read was abandoned")), 1);

	// A read with no buildables at all (the reader reports it abandoned, ruling M5) behaves the same.
	SatisDash::Test::FLogCapture ZeroLog;
	FWorldFixture Z;
	worldLogisticsNative(Z);
	Z.Game.LogisticsCounts.Buildables = 0;
	Z.Game.bLogisticsAbandoned = true;
	Z.Sender.Start(Z.T);
	TestTrue(TEXT("zero buildables: a pass begins"), Z.until([&Z] { return Z.Game.LogisticsBegins >= 2; }, 480));
	Z.tick(5);
	TestEqual(TEXT("zero buildables: nothing posted"), Z.Net.sent(WorldRailsPost), 0);
	TestEqual(TEXT("zero buildables: one warning"), countLines(ZeroLog, TEXT("World layer logistics: the game's read was abandoned")), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSenderLogisticsBridgeRailsTest, "SatisDash.Core.WorldSender.LogisticsBridgeRails", SatisDash::Test::Flags)

bool FWorldSenderLogisticsBridgeRailsTest::RunTest(const FString& Parameters)
{
	// railsSource bridge: FRM is asked at the first due time (120 s x 0.9 = 108 s here) and the pass is never entered.
	FWorldFixture F;
	F.Config.RailsSource = ESource::Bridge;
	F.Config.ConveyorsSource = ESource::Bridge; // conveyors default to native since the S3 switch, and would enter the pass
	F.bNativeReaders = true;
	F.Random01 = 0.0;
	worldLogisticsReadings(F.Game);
	F.Sender.Start(F.T);
	TestTrue(TEXT("FRM is asked for the rails"), F.until([&F] { return F.Net.sent(WorldRailsRead) > 0; }, 120));
	TestTrue(TEXT("at the bridge's first due time"), F.T - 1000.0 >= 107.0 && F.T - 1000.0 <= 112.0);
	F.tick(900);
	TestEqual(TEXT("BeginLogisticsRead is never called"), F.Game.LogisticsBegins, 0);
	TestEqual(TEXT("and nothing is posted from the game"), F.Net.sent(WorldRailsPost), 0);

	// A native pass that is open when the source changes is dropped: the reader is told, the layer is rescheduled (FRM is not asked at once).
	FWorldFixture G;
	worldLogisticsNative(G);
	G.Game.LogisticsStepsToFinish = 1000000;
	G.Sender.Start(G.T);
	TestTrue(TEXT("a pass is open"), G.until([&G] { return G.Game.LogisticsBegins > 0; }, 440));
	G.tick(1);
	TestEqual(TEXT("stepped, not finished"), G.Game.LogisticsStepCalls, 2);
	G.Config.RailsSource = ESource::Bridge;
	G.tick();
	TestEqual(TEXT("the reader is told"), G.Game.LogisticsEnds, 1);
	TestEqual(TEXT("and the pass is not stepped again"), G.Game.LogisticsStepCalls, 2);
	G.tick(60);
	TestEqual(TEXT("the layer waits for its next cycle"), G.Net.sent(WorldRailsRead), 0);
	TestEqual(TEXT("no new pass is begun"), G.Game.LogisticsBegins, 1);
	TestTrue(TEXT("FRM is asked at the rescheduled time"), G.until([&G] { return G.Net.sent(WorldRailsRead) > 0; }, 600));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSenderNativeFirstReadTest, "SatisDash.Core.WorldSender.NativeFirstRead", SatisDash::Test::Flags)

bool FWorldSenderNativeFirstReadTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	// Rails native: not begun at the bridge's time (up to 132 s) but with the node and routes layers, at 480 s x 0.9 = 432 s.
	FWorldFixture F;
	F.Config.RailsSource = ESource::Native;
	F.Config.ResourceNodesSource = ESource::Native;
	F.Config.TrainsSource = ESource::Native;
	F.bNativeReaders = true;
	F.Random01 = 0.0;
	worldLogisticsReadings(F.Game);
	F.Sender.Start(F.T);
	F.tick(140);
	TestEqual(TEXT("rails not begun at 140 s"), F.Game.LogisticsBegins, 0);
	TestFalse(TEXT("nothing begun before 431 s"), F.until([&F] { return F.Game.LogisticsBegins + F.Game.ScanBegins + F.Game.TrainsBegins > 0; }, 290));
	TestTrue(TEXT("begun at 432 s"), F.until([&F] { return F.Game.LogisticsBegins > 0; }, 5));
	TestEqual(TEXT("one rails pass"), F.Game.LogisticsBegins, 1);
	TestEqual(TEXT("the resource nodes keep their 480 s"), F.Game.ScanBegins, 1);
	TestEqual(TEXT("and so do the train routes"), F.Game.TrainsBegins, 1);

	// Rails on the bridge: requested at their own due time, 108 s here.
	FWorldFixture G;
	G.Config.RailsSource = ESource::Bridge;
	G.Config.ResourceNodesSource = ESource::Native;
	G.Config.TrainsSource = ESource::Native;
	G.bNativeReaders = true;
	G.Random01 = 0.0;
	G.Sender.Start(G.T);
	TestFalse(TEXT("bridge rails: not before 107 s"), G.until([&G] { return G.Net.sent(WorldRailsRead) > 0; }, 106));
	TestTrue(TEXT("bridge rails: asked at 108 s"), G.until([&G] { return G.Net.sent(WorldRailsRead) > 0; }, 6));
	TestEqual(TEXT("bridge rails: no pass"), G.Game.LogisticsBegins, 0);

	// A bridge to native flip mid-run (no pass open): the native first-read rule applies from the flip, and FRM is not asked again.
	FWorldFixture H;
	H.Config.RailsSource = ESource::Bridge;
	H.Config.ResourceNodesSource = ESource::Bridge;
	H.Config.TrainsSource = ESource::Bridge;
	H.bNativeReaders = true;
	H.Random01 = 0.0;
	worldLogisticsReadings(H.Game);
	H.Sender.Start(H.T);
	H.tick(50);
	H.Config.RailsSource = ESource::Native;
	TestFalse(TEXT("flip: nothing is begun before 432 s"), H.until([&H] { return H.Game.LogisticsBegins > 0; }, 380));
	TestTrue(TEXT("flip: the pass begins at 432 s"), H.until([&H] { return H.Game.LogisticsBegins > 0; }, 5));
	TestEqual(TEXT("flip: FRM is never asked for rails"), H.Net.sent(WorldRailsRead), 0);
	return true;
}

namespace
{
	const TCHAR* WorldConveyorsPost = TEXT("/agent/v1/world/conveyors");

	/** R6: one belt (end 0 at the origin) from a miner to a constructor, named so the sort order is visible. */
	FConveyorReading worldConveyorsBelt(const FString& Id, const FString& From, const FString& To)
	{
		FConveyorReading Reading;
		Reading.Id = Id;
		Reading.Kind = EConveyorKind::Belt;
		Reading.FromId = From;
		Reading.ToId = To;
		Reading.Tier = TEXT("Conveyor Belt Mk.1");
		Reading.RawRate = 120.0;
		return Reading;
	}

	/** Two belts given out of order, so the sorted body is the proof the worker built it. */
	void worldConveyorsReadings(SatisDash::Test::FFakeGame& Game)
	{
		Game.LogisticsConveyors = {
			worldConveyorsBelt(TEXT("Build_ConveyorBeltMk1_C_B"), TEXT("Build_MinerMk1_C_1"), TEXT("Build_ConstructorMk1_C_2")),
			worldConveyorsBelt(TEXT("Build_ConveyorBeltMk1_C_A"), TEXT("Build_MinerMk1_C_3"), TEXT("Build_ConstructorMk1_C_4")),
		};
		Game.LogisticsCounts.Buildables = 7;
		Game.LogisticsCounts.Belts = 2;
	}

	/** Rails and conveyors native, the other native-capable layers on the bridge, the jitter at the bottom of its range. */
	void worldBothNative(FWorldFixture& F)
	{
		worldLogisticsNative(F);
		F.Config.ConveyorsSource = ESource::Native;
	}

	/** The `data` array of the body the builder makes from the fake's conveyor readings. */
	FString worldConveyorsExpectedData(const TArray<FConveyorReading>& Readings)
	{
		const FNativeWorldBody Body = BuildNativeConveyors(TEXT("2026-10-24T12:00:00.000Z"), Readings);
		return Body.Json.Mid(Body.DataFrom, Body.DataEnd - Body.DataFrom);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSenderLogisticsBothTest, "SatisDash.Core.WorldSender.LogisticsBoth", SatisDash::Test::Flags)

bool FWorldSenderLogisticsBothTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	FWorldFixture F;
	worldBothNative(F);
	worldLogisticsReadings(F.Game);
	worldConveyorsReadings(F.Game);
	// Census data the real reader will fill: 18 tiers (the log keeps 16), 7 differing classes (the log keeps 5), ports.
	FLogisticsCounts& Counts = F.Game.LogisticsCounts;
	Counts.KindChecked = 9;
	Counts.KindDiffer = 7;
	for (int32 I = 0; I < 7; ++I)
	{
		Counts.DifferClasses.Add(FString::Printf(TEXT("Odd_C_%d"), I));
	}
	Counts.EndsIntoPorts = 3;
	Counts.EndsOutOfPorts = 2;
	for (int32 I = 0; I < 18; ++I)
	{
		FTierCount Tier;
		Tier.Kind = I % 3 == 0 ? EConveyorKind::Belt : I % 3 == 1 ? EConveyorKind::Lift : EConveyorKind::Pipe;
		Tier.Name = FString::Printf(TEXT("Tier%02d"), I);
		Tier.RawRate = 2.5;
		Tier.Count = I % 3 + 1;
		Counts.Tiers.Add(Tier);
	}
	// Rails is due at 432 s (480 x 0.9); conveyors' own time is 528 s (480 x 1.1). Start draws rails, nodes, routes, conveyors, in that order.
	F.Draws = { 0.0, 0.0, 0.0, 1.0 };
	F.Sender.Start(F.T);
	TestFalse(TEXT("nothing is begun before 432 s"), F.until([&F] { return F.Game.LogisticsBegins > 0; }, 431));
	TestTrue(TEXT("the pass begins at 432 s"), F.until([&F] { return F.Game.LogisticsBegins > 0; }, 3));
	TestEqual(TEXT("one begin"), F.Game.LogisticsBegins, 1);
	TestTrue(TEXT("with the rails flag"), F.Game.bLastBeginRails);
	TestTrue(TEXT("and the conveyors flag, though its own time (528 s) is later (ruling M6)"), F.Game.bLastBeginConveyors);
	F.tick(3);
	TestEqual(TEXT("one pass of three steps"), F.Game.LogisticsStepCalls, 3);
	TestEqual(TEXT("still one begin"), F.Game.LogisticsBegins, 1);

	// One finished snapshot posts both layers, one after the other (one post in flight).
	TestTrue(TEXT("rails is posted"), F.until([&F] { return F.Net.pending(WorldRailsPost) > 0; }, 10));
	F.Net.answer(WorldRailsPost, SatisDash::Test::Reply(200, WorldAccepted));
	const double RailsAccepted = F.T;
	TestTrue(TEXT("conveyors is posted"), F.until([&F] { return F.Net.pending(WorldConveyorsPost) > 0; }, 10));
	const FString Body = F.Net.lastBody(WorldConveyorsPost);
	const int32 First = Body.Find(TEXT("\"id\":\"Build_ConveyorBeltMk1_C_A\""));
	const int32 Second = Body.Find(TEXT("\"id\":\"Build_ConveyorBeltMk1_C_B\""));
	TestTrue(TEXT("both belts are in the body"), First != INDEX_NONE && Second != INDEX_NONE);
	TestTrue(TEXT("sorted by id"), First < Second);
	TestTrue(TEXT("the data equals the builder's over the fake's readings"), Body.Contains(worldConveyorsExpectedData(F.Game.LogisticsConveyors)));
	F.Net.answer(WorldConveyorsPost, SatisDash::Test::Reply(200, WorldAccepted));
	F.tick(2);
	TestEqual(TEXT("rails posted once"), F.Net.sent(WorldRailsPost), 1);
	TestEqual(TEXT("conveyors posted once"), F.Net.sent(WorldConveyorsPost), 1);
	TestEqual(TEXT("FRM is never asked for rails"), F.Net.sent(WorldRailsRead), 0);
	TestEqual(TEXT("the accepted bodies are not held"), F.Sender.Held(), 0);

	// The next cycle: both layers were rescheduled together (600 s x 0.9), so one pass takes both, and unchanged data is not posted again.
	// Rails is accepted at RailsAccepted and rescheduled 540 s on (600 x 0.9); conveyors a moment later. Neither layer's own earlier time (528 s) starts a pass.
	TestFalse(TEXT("no second pass before the rescheduled time"), F.until([&F] { return F.Game.LogisticsBegins >= 2; }, RailsAccepted + 539.0 - F.T));
	TestTrue(TEXT("the second pass begins 540 s after rails was accepted"), F.until([&F] { return F.Game.LogisticsBegins >= 2; }, 2));
	TestEqual(TEXT("the one pass takes both layers"), F.Game.LogisticsBegins, 2);
	TestTrue(TEXT("with both flags"), F.Game.bLastBeginRails && F.Game.bLastBeginConveyors);
	F.tick(10);
	TestEqual(TEXT("an unchanged rails layer is not posted again"), F.Net.sent(WorldRailsPost), 1);
	TestEqual(TEXT("an unchanged conveyors layer is not posted again"), F.Net.sent(WorldConveyorsPost), 1);

	// The log: one Logistics read line a pass with both flags, the kinds and ports lines each pass (the first 5 differing classes), the
	// tier census on the first pass only and capped at 16 of the 18 tiers, ids nowhere.
	int32 ReadLines = 0;
	int32 KindLines = 0;
	int32 PortLines = 0;
	int32 Leaks = 0;
	int32 Census = 0;
	int32 CensusFirst = 0;
	int32 CensusLift = 0;
	int32 CensusPipe = 0;
	int32 CensusBeyondCap = 0;
	for (const FString& Line : Log.Take())
	{
		ReadLines += Line.Contains(TEXT("Logistics read (game thread only): layers rails=1 conveyors=1 powerLines=0, 7 buildables")) ? 1 : 0;
		KindLines += Line.Contains(TEXT("Conveyor kinds: 9 ends checked, 7 differ from the class table (first classes: Odd_C_0, Odd_C_1, Odd_C_2, Odd_C_3, Odd_C_4)")) ? 1 : 0;
		PortLines += Line.Contains(TEXT("Conveyor drone ports: 3 edges end at a port, 2 start at one")) ? 1 : 0;
		Leaks += Line.Contains(TEXT("Build_ConveyorBelt")) || Line.Contains(TEXT("Build_RailroadTrack")) ? 1 : 0;
		Census += Line.Contains(TEXT("Conveyor tier census: ")) ? 1 : 0;
		CensusFirst += Line.EndsWith(TEXT("Conveyor tier census: belt | Tier00 | speed 2.500 | count 1")) ? 1 : 0;
		CensusLift += Line.EndsWith(TEXT("Conveyor tier census: lift | Tier01 | speed 2.500 | count 2")) ? 1 : 0;
		CensusPipe += Line.EndsWith(TEXT("Conveyor tier census: pipe | Tier02 | speed 2.500 | count 3")) ? 1 : 0;
		CensusBeyondCap += Line.Contains(TEXT("Tier16 ")) || Line.Contains(TEXT("Tier17 ")) ? 1 : 0;
		TestFalse(TEXT("the sixth differing class is not logged"), Line.Contains(TEXT("Odd_C_5")) || Line.Contains(TEXT("Odd_C_6")));
	}
	TestEqual(TEXT("one Logistics read line a pass"), ReadLines, 2);
	TestEqual(TEXT("one kinds line a pass"), KindLines, 2);
	TestEqual(TEXT("one ports line a pass"), PortLines, 2);
	TestEqual(TEXT("no id in the log"), Leaks, 0);
	TestEqual(TEXT("exactly 16 census lines, all on the first pass"), Census, 16);
	TestEqual(TEXT("belt kind text"), CensusFirst, 1);
	TestEqual(TEXT("lift kind text"), CensusLift, 1);
	TestEqual(TEXT("pipe kind text"), CensusPipe, 1);
	TestEqual(TEXT("the 17th and 18th tiers are not logged"), CensusBeyondCap, 0);

	// Start re-arms the census: the first conveyors pass after it logs its 16 lines again, and the pass after that none.
	F.Sender.Start(F.T);
	TestTrue(TEXT("a pass begins after the restart"), F.until([&F] { return F.Game.LogisticsBegins >= 3; }, 440));
	F.tick(10);
	// (the capture keeps every line, so the totals are cumulative: 16 from the first run)
	TestEqual(TEXT("the restart logs the census again"), countLines(Log, TEXT("Conveyor tier census: ")), 32);
	// The restart forgot the data hashes, so both layers post again; accept them so the layers reschedule.
	F.Net.answer(WorldRailsPost, SatisDash::Test::Reply(200, WorldAccepted));
	TestTrue(TEXT("conveyors posts again"), F.until([&F] { return F.Net.pending(WorldConveyorsPost) > 0; }, 10));
	F.Net.answer(WorldConveyorsPost, SatisDash::Test::Reply(200, WorldAccepted));
	TestTrue(TEXT("and the next pass"), F.until([&F] { return F.Game.LogisticsBegins >= 4; }, 700));
	F.tick(10);
	TestEqual(TEXT("logs none"), countLines(Log, TEXT("Conveyor tier census: ")), 32);

	// A refused submit for one layer reschedules only that layer: the conveyors slot still holds an earlier job, rails is posted.
	FWorldFixture G;
	worldBothNative(G);
	worldLogisticsReadings(G.Game);
	worldConveyorsReadings(G.Game);
	G.Sender.Start(G.T);
	TestTrue(TEXT("the bridge's conveyors slot is taken"), G.Bridge.SubmitNativeWorld(EWorldLayer::Conveyors, TEXT("2026-10-24T12:00:00.000Z"),
		FNativeWorldBuilder([](const FString& Observed) { return BuildNativeConveyors(Observed, {}); }), TOptional<FBlake3Hash>()));
	TestTrue(TEXT("the pass begins"), G.until([&G] { return G.Game.LogisticsBegins > 0; }, 440));
	const double Begun = G.T;
	TestTrue(TEXT("rails is posted"), G.until([&G] { return G.Net.pending(WorldRailsPost) > 0; }, 20));
	const double RailsPending = G.T;
	G.tick(5);
	TestEqual(TEXT("one pass"), G.Game.LogisticsBegins, 1);
	TestEqual(TEXT("conveyors is not posted"), G.Net.sent(WorldConveyorsPost), 0);
	G.Net.answer(WorldRailsPost, SatisDash::Test::Reply(200, WorldAccepted));
	G.tick(60);
	TestEqual(TEXT("conveyors is not retried at once (it waits a whole cycle, not the scan retry)"), G.Game.LogisticsBegins, 1);
	TestEqual(TEXT("rails was posted"), G.Net.sent(WorldRailsPost), 1);
	TestEqual(TEXT("and still nothing for conveyors"), G.Net.sent(WorldConveyorsPost), 0);
	// Conveyors was rescheduled a whole interval (540 s) from the pass's end, which is between its begin and rails' post; it takes rails with it.
	TestFalse(TEXT("no pass before the interval is out"), G.until([&G] { return G.Game.LogisticsBegins >= 2; }, Begun + 539.0 - G.T));
	TestTrue(TEXT("the next pass begins by the latest rescheduled time"), G.until([&G] { return G.Game.LogisticsBegins >= 2; }, RailsPending + 541.0 - G.T));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSenderConveyorsOffUnlessNativeTest, "SatisDash.Core.WorldSender.ConveyorsOffUnlessNative", SatisDash::Test::Flags)

bool FWorldSenderConveyorsOffUnlessNativeTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	// conveyorsSource bridge (the fixture pins it; the default is native since the S3 switch): the layer is never scheduled, whatever the rails do.
	FWorldFixture F;
	worldLogisticsNative(F);
	worldLogisticsReadings(F.Game);
	worldConveyorsReadings(F.Game);
	F.Sender.Start(F.T);
	TestTrue(TEXT("rails (native) is posted"), F.until([&F] { return F.Net.pending(WorldRailsPost) > 0; }, 440));
	F.Net.answer(WorldRailsPost, SatisDash::Test::Reply(200, WorldAccepted));
	F.tick(2000);
	TestTrue(TEXT("rails ran more than one pass"), F.Game.LogisticsBegins >= 2);
	TestFalse(TEXT("no pass asked for conveyors"), F.Game.bLastBeginConveyors);
	TestEqual(TEXT("no conveyors post"), F.Net.sent(WorldConveyorsPost), 0);
	TestEqual(TEXT("no FRM request for conveyors (nor a request for rails)"), F.Net.sent(WorldRailsRead), 0);
	int32 Conveyor = 0;
	for (const FString& Line : Log.Take())
	{
		// (the rails pass's own line says conveyors=0, which is not about the layer)
		Conveyor += Line.Contains(TEXT("Conveyor kinds")) || Line.Contains(TEXT("Conveyor drone")) || Line.Contains(TEXT("Conveyor tier")) || Line.Contains(TEXT("conveyors=1")) || Line.Contains(TEXT("layer conveyors")) ? 1 : 0;
	}
	TestEqual(TEXT("and nothing about conveyors is logged"), Conveyor, 0);

	// railsSource bridge and conveyorsSource native: FRM is asked for getTrainRails, and the pass serves conveyors alone.
	FWorldFixture G;
	G.Config.RailsSource = ESource::Bridge;
	G.Config.ConveyorsSource = ESource::Native;
	G.Config.ResourceNodesSource = ESource::Bridge;
	G.Config.TrainsSource = ESource::Bridge;
	G.bNativeReaders = true;
	G.Random01 = 0.0;
	worldConveyorsReadings(G.Game);
	G.Sender.Start(G.T);
	TestTrue(TEXT("FRM is asked for the rails"), G.until([&G] { return G.Net.sent(WorldRailsRead) > 0; }, 120));
	TestFalse(TEXT("no pass before 432 s"), G.until([&G] { return G.Game.LogisticsBegins > 0; }, 320));
	TestTrue(TEXT("the pass begins at 432 s"), G.until([&G] { return G.Game.LogisticsBegins > 0; }, 5));
	TestFalse(TEXT("without the rails flag"), G.Game.bLastBeginRails);
	TestTrue(TEXT("with the conveyors flag"), G.Game.bLastBeginConveyors);
	TestTrue(TEXT("conveyors is posted"), G.until([&G] { return G.Net.pending(WorldConveyorsPost) > 0; }, 10));
	G.Net.answer(WorldConveyorsPost, SatisDash::Test::Reply(200, WorldAccepted));
	G.tick(5);
	TestEqual(TEXT("one conveyors post"), G.Net.sent(WorldConveyorsPost), 1);
	TestEqual(TEXT("no rails post from the game"), G.Net.sent(WorldRailsPost), 0);

	// Flip bridge to native mid-run (before any pass): the layer is scheduled at the first-read rule (432 s here), nothing is sent before it.
	FWorldFixture A;
	A.Config.RailsSource = ESource::Bridge; // rails default to native since the S3 switch; these flips are about conveyors alone
	A.Config.ConveyorsSource = ESource::Bridge;
	A.Config.ResourceNodesSource = ESource::Bridge;
	A.Config.TrainsSource = ESource::Bridge;
	A.bNativeReaders = true;
	A.Random01 = 0.0;
	worldConveyorsReadings(A.Game);
	A.Sender.Start(A.T);
	A.tick(50);
	A.Config.ConveyorsSource = ESource::Native;
	TestFalse(TEXT("flip to native: nothing begun before 432 s"), A.until([&A] { return A.Game.LogisticsBegins > 0; }, 380));
	TestEqual(TEXT("flip to native: nothing posted before it"), A.Net.sent(WorldConveyorsPost), 0);
	TestTrue(TEXT("flip to native: the pass begins at 432 s"), A.until([&A] { return A.Game.LogisticsBegins > 0; }, 5));
	TestTrue(TEXT("flip to native: with the conveyors flag only"), A.Game.bLastBeginConveyors && !A.Game.bLastBeginRails);
	TestTrue(TEXT("flip to native: the layer is posted"), A.until([&A] { return A.Net.pending(WorldConveyorsPost) > 0; }, 10));

	// Flip native to bridge before the pass: nothing is ever begun or posted.
	FWorldFixture B;
	B.Config.RailsSource = ESource::Bridge;
	B.Config.ConveyorsSource = ESource::Native;
	B.Config.ResourceNodesSource = ESource::Bridge;
	B.Config.TrainsSource = ESource::Bridge;
	B.bNativeReaders = true;
	B.Random01 = 0.0;
	worldConveyorsReadings(B.Game);
	B.Sender.Start(B.T);
	B.tick(50);
	B.Config.ConveyorsSource = ESource::Bridge;
	B.tick(1500);
	TestEqual(TEXT("flip to bridge: no pass"), B.Game.LogisticsBegins, 0);
	TestEqual(TEXT("flip to bridge: no post"), B.Net.sent(WorldConveyorsPost), 0);

	// Flip native to bridge under an open pass that holds only conveyors: the reader is told, nothing is posted, the layer waits a cycle.
	FWorldFixture C;
	C.Config.RailsSource = ESource::Bridge;
	C.Config.ConveyorsSource = ESource::Native;
	C.Config.ResourceNodesSource = ESource::Bridge;
	C.Config.TrainsSource = ESource::Bridge;
	C.bNativeReaders = true;
	C.Random01 = 0.0;
	worldConveyorsReadings(C.Game);
	C.Game.LogisticsStepsToFinish = 1000000;
	C.Sender.Start(C.T);
	TestTrue(TEXT("open pass: begun"), C.until([&C] { return C.Game.LogisticsBegins > 0; }, 440));
	C.tick(1);
	C.Config.ConveyorsSource = ESource::Bridge;
	C.tick();
	TestEqual(TEXT("open pass: the reader is told"), C.Game.LogisticsEnds, 1);
	C.tick(120);
	TestEqual(TEXT("open pass: no post"), C.Net.sent(WorldConveyorsPost), 0);
	TestEqual(TEXT("open pass: no new pass"), C.Game.LogisticsBegins, 1);

	// A shared pass keeps the layer that is still native when the other flips to bridge, in either order.
	for (const bool bConveyorsFlips : { true, false })
	{
		FWorldFixture D;
		worldBothNative(D);
		worldLogisticsReadings(D.Game);
		worldConveyorsReadings(D.Game);
		D.Sender.Start(D.T);
		TestTrue(TEXT("shared pass: begun"), D.until([&D] { return D.Game.LogisticsBegins > 0; }, 440));
		TestTrue(TEXT("shared pass: with both flags"), D.Game.bLastBeginRails && D.Game.bLastBeginConveyors);
		TestTrue(TEXT("shared pass: stepped"), D.until([&D] { return D.Game.LogisticsStepCalls >= 1; }, 5));
		if (bConveyorsFlips)
		{
			D.Config.ConveyorsSource = ESource::Bridge;
		}
		else
		{
			D.Config.RailsSource = ESource::Bridge;
		}
		const TCHAR* Kept = bConveyorsFlips ? WorldRailsPost : WorldConveyorsPost;
		const TCHAR* Dropped = bConveyorsFlips ? WorldConveyorsPost : WorldRailsPost;
		TestTrue(TEXT("shared pass: the layer still native is posted"), D.until([&D, Kept] { return D.Net.pending(Kept) > 0; }, 20));
		D.Net.answer(Kept, SatisDash::Test::Reply(200, WorldAccepted));
		D.tick(60);
		TestEqual(TEXT("shared pass: the kept layer posted once"), D.Net.sent(Kept), 1);
		TestEqual(TEXT("shared pass: the dropped layer is not posted"), D.Net.sent(Dropped), 0);
		TestEqual(TEXT("shared pass: still one pass"), D.Game.LogisticsBegins, 1);
		TestTrue(TEXT("shared pass: the next pass serves the kept layer alone"), D.until([&D] { return D.Game.LogisticsBegins >= 2; }, 700));
		TestEqual(TEXT("shared pass: rails flag"), D.Game.bLastBeginRails, bConveyorsFlips);
		TestEqual(TEXT("shared pass: conveyors flag"), D.Game.bLastBeginConveyors, !bConveyorsFlips);
		TestEqual(TEXT("shared pass: the dropped layer is never posted"), D.Net.sent(Dropped), 0);
	}

	// A body over the layer cap is not posted, and warns once.
	SatisDash::Test::FLogCapture CapLog;
	FWorldFixture O;
	O.Config.RailsSource = ESource::Bridge;
	O.Config.ConveyorsSource = ESource::Native;
	O.Config.ResourceNodesSource = ESource::Bridge;
	O.Config.TrainsSource = ESource::Bridge;
	O.bNativeReaders = true;
	O.Random01 = 0.0;
	O.Game.LogisticsConveyors.Reset();
	for (int32 Edge = 0; Edge < 6000; ++Edge)
	{
		// ids of 100 digits: about 530 bytes an edge, so the body is past 2,000,000 bytes
		O.Game.LogisticsConveyors.Add(worldConveyorsBelt(FString::Printf(TEXT("Belt_%095d"), Edge), FString::Printf(TEXT("Build_MinerMk1_C_%095d"), Edge), FString::Printf(TEXT("Build_ConstructorMk1_C_%095d"), Edge)));
	}
	O.Game.LogisticsCounts.Buildables = 6000;
	O.Sender.Start(O.T);
	TestTrue(TEXT("a pass begins"), O.until([&O] { return O.Game.LogisticsBegins > 0; }, 440));
	O.tick(60);
	TestEqual(TEXT("an over-cap layer is not posted"), O.Net.sent(WorldConveyorsPost), 0);
	TestEqual(TEXT("it is logged once, with its size"), countLines(CapLog, TEXT("World layer conveyors is ")), 1);
	return true;
}

namespace
{
	const TCHAR* WorldPowerLinesPost = TEXT("/agent/v1/world/powerLines");

	/** R10: two wires given out of order, so the sorted body is the proof the worker built it. */
	void worldPowerLinesReadings(SatisDash::Test::FFakeGame& Game)
	{
		FPowerLineReading B;
		B.Id = TEXT("Build_PowerLine_C_B");
		B.X0Cm = 0.0;
		B.Y0Cm = 0.0;
		B.X1Cm = 1000.0;
		B.Y1Cm = 0.0;
		FPowerLineReading A;
		A.Id = TEXT("Build_PowerLine_C_A");
		A.X0Cm = 0.0;
		A.Y0Cm = 500.0;
		A.X1Cm = 1000.0;
		A.Y1Cm = 500.0;
		Game.LogisticsPowerLines = { B, A };
		Game.LogisticsCounts.Buildables = 7;
		Game.LogisticsCounts.Wires = 2;
		Game.LogisticsCounts.ExpectedWires = 3;
	}

	/** Only powerLines native (rails and conveyors on the bridge), the jitter at the bottom of its range. */
	void worldPowerLinesOnly(FWorldFixture& F)
	{
		F.Config.RailsSource = ESource::Bridge;
		F.Config.ConveyorsSource = ESource::Bridge;
		F.Config.ResourceNodesSource = ESource::Bridge;
		F.Config.TrainsSource = ESource::Bridge;
		F.Config.PowerLinesSource = ESource::Native;
		F.bNativeReaders = true;
		F.Random01 = 0.0;
	}

	/** All three logistics layers native; rails is due first (432 s), the others later (528 s) so the pulled-forward rule is what starts them. */
	void worldAllThreeNative(FWorldFixture& F)
	{
		worldBothNative(F);
		F.Config.PowerLinesSource = ESource::Native;
		F.Draws = { 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0 };
	}

	void worldAllThreeReadings(SatisDash::Test::FFakeGame& Game)
	{
		worldLogisticsReadings(Game);
		worldConveyorsReadings(Game);
		worldPowerLinesReadings(Game);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldSenderLogisticsPowerLinesTest, "SatisDash.Core.WorldSender.LogisticsPowerLines", SatisDash::Test::Flags)

bool FWorldSenderLogisticsPowerLinesTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	// (a) only powerLines native: its own pass at 432 s, rails asked of FRM as before, conveyors not sent.
	FWorldFixture F;
	worldPowerLinesOnly(F);
	worldAllThreeReadings(F.Game);
	F.Sender.Start(F.T);
	TestTrue(TEXT("FRM is asked for the rails"), F.until([&F] { return F.Net.sent(WorldRailsRead) > 0; }, 120));
	TestFalse(TEXT("no pass before 432 s"), F.until([&F] { return F.Game.LogisticsBegins > 0; }, 431.0 - (F.T - 1000.0)));
	TestTrue(TEXT("the pass begins at 432 s"), F.until([&F] { return F.Game.LogisticsBegins > 0; }, 3));
	TestEqual(TEXT("one begin"), F.Game.LogisticsBegins, 1);
	TestTrue(TEXT("with the powerLines flag"), F.Game.bLastBeginPowerLines);
	TestFalse(TEXT("and not the rails flag"), F.Game.bLastBeginRails);
	TestFalse(TEXT("nor the conveyors flag"), F.Game.bLastBeginConveyors);
	TestTrue(TEXT("the layer is posted after the third step"), F.until([&F] { return F.Net.pending(WorldPowerLinesPost) > 0; }, 10));
	TestEqual(TEXT("three steps in all"), F.Game.LogisticsStepCalls, 3);
	const FString Body = F.Net.lastBody(WorldPowerLinesPost);
	const int32 First = Body.Find(TEXT("\"id\":\"Build_PowerLine_C_A\""));
	const int32 Second = Body.Find(TEXT("\"id\":\"Build_PowerLine_C_B\""));
	TestTrue(TEXT("both wires are in the body, sorted by id"), First != INDEX_NONE && Second != INDEX_NONE && First < Second);
	F.Net.answer(WorldPowerLinesPost, SatisDash::Test::Reply(200, WorldAccepted));
	F.tick(2);
	TestEqual(TEXT("one post"), F.Net.sent(WorldPowerLinesPost), 1);
	TestEqual(TEXT("no conveyors post"), F.Net.sent(WorldConveyorsPost), 0);
	TestEqual(TEXT("no rails post from the game"), F.Net.sent(WorldRailsPost), 0);
	TestEqual(TEXT("the accepted body is not held"), F.Sender.Held(), 0);
	int32 ReadLines = 0;
	int32 Leaks = 0;
	int32 WireWarnings = 0;
	for (const FString& Line : Log.Take())
	{
		ReadLines += Line.Contains(TEXT("Logistics read (game thread only): layers rails=0 conveyors=0 powerLines=1, 7 buildables, 2 tracks, 2 belts, 0 lifts, 0 pipes, 0 open ends, 0 ownerless ends, wires 2, expected wires 3 in 3 steps, longest step ")) ? 1 : 0;
		Leaks += Line.Contains(TEXT("Build_PowerLine")) ? 1 : 0;
		WireWarnings += Line.Contains(TEXT("World layer powerLines: ")) ? 1 : 0;
	}
	TestEqual(TEXT("one Logistics read line with the wires counts"), ReadLines, 1);
	TestEqual(TEXT("no id in the log"), Leaks, 0);
	TestEqual(TEXT("2 wires read of 3 counted: no cast-count warning"), WireWarnings, 0);

	// The cast-count check (Q6): zero wires read, or a gap past max(5, 5%) counting the dropped ones.
	TestTrue(TEXT("zero wires read is suspect"), SatisDash::WireCountSuspect(0, 0, 3));
	TestTrue(TEXT("zero wires read is suspect when the count is unknown"), SatisDash::WireCountSuspect(0, 0, -1));
	TestFalse(TEXT("an unknown count checks nothing else"), SatisDash::WireCountSuspect(10, 0, -1));
	TestFalse(TEXT("a gap of 5 is allowed"), SatisDash::WireCountSuspect(95, 0, 100));
	TestTrue(TEXT("a gap of 6 on 100 is suspect"), SatisDash::WireCountSuspect(94, 0, 100));
	TestFalse(TEXT("dropped wires count as seen"), SatisDash::WireCountSuspect(90, 10, 100));
	// M-3: the reader counts a wire that was gone, one with a null end and one past the cap all in WiresDropped, so none hides from the check.
	TestFalse(TEXT("over-cap wires (WiresDropped) count as seen"), SatisDash::WireCountSuspect(2001, 3000, 5001));
	TestTrue(TEXT("over-cap wires left uncounted would be suspect"), SatisDash::WireCountSuspect(2001, 0, 5001));
	TestFalse(TEXT("5% of 1000 is 50"), SatisDash::WireCountSuspect(950, 0, 1000));
	TestTrue(TEXT("51 short of 1000 is suspect"), SatisDash::WireCountSuspect(949, 0, 1000));
	TestTrue(TEXT("more read than counted, past the gap, is suspect too"), SatisDash::WireCountSuspect(200, 0, 100));
	// The sender logs it once, with counts and no id, and still posts what it read.
	SatisDash::Test::FLogCapture GapLog;
	FWorldFixture W;
	worldPowerLinesOnly(W);
	worldAllThreeReadings(W.Game);
	W.Game.LogisticsCounts.ExpectedWires = 300;
	// Rail and conveyor drops sit in the pass-wide Dropped; they must not hide a wire gap.
	W.Game.LogisticsCounts.Dropped = 298;
	W.Sender.Start(W.T);
	TestTrue(TEXT("gap: the layer is still posted"), W.until([&W] { return W.Net.pending(WorldPowerLinesPost) > 0; }, 440));
	int32 GapLines = 0;
	int32 GapLeaks = 0;
	for (const FString& Line : GapLog.Take())
	{
		GapLines += Line.Contains(TEXT("World layer powerLines: 2 wires read, 0 dropped, the subsystem counts 300; check that wires are in the buildable list")) ? 1 : 0;
		GapLeaks += Line.Contains(TEXT("Build_PowerLine")) ? 1 : 0;
	}
	TestEqual(TEXT("gap: one cast-count warning"), GapLines, 1);
	TestEqual(TEXT("gap: no id in the log"), GapLeaks, 0);
	// A second pass inside the 600 s limiter window reads the same gap and must stay quiet.
	W.Net.answer(WorldPowerLinesPost, SatisDash::Test::Reply(200, WorldAccepted));
	W.tick(2);
	TestTrue(TEXT("gap: a second pass begins"), W.until([&W] { return W.Game.LogisticsBegins >= 2; }, 700));
	W.tick(10);
	int32 GapLinesAfter = 0;
	int32 GapReads = 0;
	for (const FString& Line : GapLog.Take())
	{
		GapReads += Line.Contains(TEXT("Logistics read (game thread only)")) ? 1 : 0;
		GapLinesAfter += Line.Contains(TEXT("World layer powerLines: 2 wires read, 0 dropped, the subsystem counts 300; check that wires are in the buildable list")) ? 1 : 0;
	}
	TestEqual(TEXT("gap: the second pass finished its read"), GapReads, 2);
	TestEqual(TEXT("gap: still one cast-count warning after the second pass"), GapLinesAfter, 1);

	// (b) all three native: the first due layer (rails, 432 s) pulls the others forward; one snapshot, three bodies, each once.
	FWorldFixture G;
	worldAllThreeNative(G);
	worldAllThreeReadings(G.Game);
	G.Sender.Start(G.T);
	TestTrue(TEXT("the pass begins at 432 s"), G.until([&G] { return G.Game.LogisticsBegins > 0; }, 440));
	TestEqual(TEXT("one begin"), G.Game.LogisticsBegins, 1);
	TestTrue(TEXT("with all three flags (ruling M6)"), G.Game.bLastBeginRails && G.Game.bLastBeginConveyors && G.Game.bLastBeginPowerLines);
	TestTrue(TEXT("rails is posted"), G.until([&G] { return G.Net.pending(WorldRailsPost) > 0; }, 10));
	TestTrue(TEXT("rails carries its own data only"), G.Net.lastBody(WorldRailsPost).Contains(TEXT("Build_RailroadTrack_C_A")) && !G.Net.lastBody(WorldRailsPost).Contains(TEXT("Build_PowerLine")));
	G.Net.answer(WorldRailsPost, SatisDash::Test::Reply(200, WorldAccepted));
	TestTrue(TEXT("conveyors is posted"), G.until([&G] { return G.Net.pending(WorldConveyorsPost) > 0; }, 10));
	TestTrue(TEXT("conveyors carries its own data only"), G.Net.lastBody(WorldConveyorsPost).Contains(TEXT("Build_ConveyorBeltMk1_C_A")) && !G.Net.lastBody(WorldConveyorsPost).Contains(TEXT("Build_PowerLine")));
	G.Net.answer(WorldConveyorsPost, SatisDash::Test::Reply(200, WorldAccepted));
	TestTrue(TEXT("powerLines is posted"), G.until([&G] { return G.Net.pending(WorldPowerLinesPost) > 0; }, 10));
	TestTrue(TEXT("powerLines carries its own data only"), G.Net.lastBody(WorldPowerLinesPost).Contains(TEXT("Build_PowerLine_C_A")) && !G.Net.lastBody(WorldPowerLinesPost).Contains(TEXT("Build_RailroadTrack"))
		&& !G.Net.lastBody(WorldPowerLinesPost).Contains(TEXT("Build_ConveyorBelt")));
	G.Net.answer(WorldPowerLinesPost, SatisDash::Test::Reply(200, WorldAccepted));
	G.tick(2);
	TestEqual(TEXT("rails posted once"), G.Net.sent(WorldRailsPost), 1);
	TestEqual(TEXT("conveyors posted once"), G.Net.sent(WorldConveyorsPost), 1);
	TestEqual(TEXT("powerLines posted once"), G.Net.sent(WorldPowerLinesPost), 1);
	// The next pass reads the same data: each layer's own data hash says unchanged.
	TestTrue(TEXT("the next pass begins"), G.until([&G] { return G.Game.LogisticsBegins >= 2; }, 700));
	TestTrue(TEXT("with all three flags again"), G.Game.bLastBeginRails && G.Game.bLastBeginConveyors && G.Game.bLastBeginPowerLines);
	G.tick(10);
	TestEqual(TEXT("unchanged rails is not posted again"), G.Net.sent(WorldRailsPost), 1);
	TestEqual(TEXT("unchanged conveyors is not posted again"), G.Net.sent(WorldConveyorsPost), 1);
	TestEqual(TEXT("unchanged powerLines is not posted again"), G.Net.sent(WorldPowerLinesPost), 1);

	// A refused submit for one layer reschedules only that layer: the powerLines slot still holds an earlier job.
	FWorldFixture R;
	worldAllThreeNative(R);
	worldAllThreeReadings(R.Game);
	R.Sender.Start(R.T);
	TestTrue(TEXT("the bridge's powerLines slot is taken"), R.Bridge.SubmitNativeWorld(EWorldLayer::PowerLines, TEXT("2026-10-24T12:00:00.000Z"),
		FNativeWorldBuilder([](const FString& Observed) { return BuildNativePowerLines(Observed, {}); }), TOptional<FBlake3Hash>()));
	TestTrue(TEXT("the pass begins"), R.until([&R] { return R.Game.LogisticsBegins > 0; }, 440));
	TestTrue(TEXT("rails is posted"), R.until([&R] { return R.Net.pending(WorldRailsPost) > 0; }, 20));
	R.Net.answer(WorldRailsPost, SatisDash::Test::Reply(200, WorldAccepted));
	TestTrue(TEXT("conveyors is posted"), R.until([&R] { return R.Net.pending(WorldConveyorsPost) > 0; }, 20));
	R.Net.answer(WorldConveyorsPost, SatisDash::Test::Reply(200, WorldAccepted));
	R.tick(60);
	TestEqual(TEXT("one pass: powerLines is not retried at once (it waits a whole cycle)"), R.Game.LogisticsBegins, 1);
	TestEqual(TEXT("rails was posted"), R.Net.sent(WorldRailsPost), 1);
	TestEqual(TEXT("conveyors was posted"), R.Net.sent(WorldConveyorsPost), 1);
	TestEqual(TEXT("and nothing for powerLines"), R.Net.sent(WorldPowerLinesPost), 0);

	// (c) powerLines flips to bridge under an open pass: only that layer drops out, the pass goes on for the other two.
	FWorldFixture H;
	worldAllThreeNative(H);
	worldAllThreeReadings(H.Game);
	H.Game.LogisticsStepsToFinish = 6;
	H.Sender.Start(H.T);
	TestTrue(TEXT("a pass is open"), H.until([&H] { return H.Game.LogisticsBegins > 0; }, 440));
	H.tick(1);
	H.Config.PowerLinesSource = ESource::Bridge;
	H.tick();
	TestEqual(TEXT("the reader is not told: rails and conveyors are still in the pass"), H.Game.LogisticsEnds, 0);
	TestTrue(TEXT("rails is posted"), H.until([&H] { return H.Net.pending(WorldRailsPost) > 0; }, 10));
	H.Net.answer(WorldRailsPost, SatisDash::Test::Reply(200, WorldAccepted));
	TestTrue(TEXT("conveyors is posted"), H.until([&H] { return H.Net.pending(WorldConveyorsPost) > 0; }, 10));
	H.Net.answer(WorldConveyorsPost, SatisDash::Test::Reply(200, WorldAccepted));
	H.tick(1500);
	TestEqual(TEXT("powerLines is never posted"), H.Net.sent(WorldPowerLinesPost), 0);
	TestTrue(TEXT("a later pass ran"), H.Game.LogisticsBegins >= 2);
	TestFalse(TEXT("and later passes do not ask for it"), H.Game.bLastBeginPowerLines);

	// (d) an abandoned read: all three layers wait NodeScanRetrySeconds together, one warning per layer, nothing posted.
	SatisDash::Test::FLogCapture AbandonLog;
	FWorldFixture Z;
	worldAllThreeNative(Z);
	worldAllThreeReadings(Z.Game);
	Z.Game.bLogisticsAbandoned = true;
	Z.Sender.Start(Z.T);
	TestTrue(TEXT("abandoned: a pass begins"), Z.until([&Z] { return Z.Game.LogisticsBegins >= 1; }, 440));
	TestTrue(TEXT("abandoned: retried soon, not after a whole cycle"), Z.until([&Z] { return Z.Game.LogisticsBegins >= 3; }, 100));
	TestTrue(TEXT("abandoned: all three flags every time"), Z.Game.bLastBeginRails && Z.Game.bLastBeginConveyors && Z.Game.bLastBeginPowerLines);
	Z.tick(5);
	TestEqual(TEXT("abandoned: rails posts nothing"), Z.Net.sent(WorldRailsPost), 0);
	TestEqual(TEXT("abandoned: conveyors posts nothing"), Z.Net.sent(WorldConveyorsPost), 0);
	TestEqual(TEXT("abandoned: powerLines posts nothing"), Z.Net.sent(WorldPowerLinesPost), 0);
	TestEqual(TEXT("abandoned: one warning per layer"), countLines(AbandonLog, TEXT("World layer logistics: the game's read was abandoned")), 3);

	// (e) the default config (powerLinesSource unset): a rails-only pass never asks for the wires and nothing is posted for them.
	FWorldFixture D;
	worldLogisticsNative(D);
	worldAllThreeReadings(D.Game);
	D.Sender.Start(D.T);
	TestTrue(TEXT("default: rails posts"), D.until([&D] { return D.Net.pending(WorldRailsPost) > 0; }, 440));
	D.Net.answer(WorldRailsPost, SatisDash::Test::Reply(200, WorldAccepted));
	D.tick(2000);
	TestTrue(TEXT("default: passes ran"), D.Game.LogisticsBegins >= 2);
	TestFalse(TEXT("default: no pass asked for powerLines"), D.Game.bLastBeginPowerLines);
	TestEqual(TEXT("default: no powerLines post"), D.Net.sent(WorldPowerLinesPost), 0);
	return true;
}
