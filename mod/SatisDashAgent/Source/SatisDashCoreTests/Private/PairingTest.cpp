#include "SatisDash/Pairing.h"
#include "SatisDash/Pusher.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	const TCHAR* PairSecret = TEXT("abcdefghijklmnopqrstuv");
	const TCHAR* PairCode = TEXT("AB3D-7XQ2-K5MN");

	FAgentConfig PairConfig()
	{
		FAgentConfig Out;
		Out.BackendUrl = TEXT("https://example.com");
		return Out;
	}

	FHttpReply PairStartReply(int32 ExpiresIn = 900, int32 Interval = 5)
	{
		return SatisDash::Test::Reply(201, FString::Printf(TEXT("{\"pairingSecret\":\"%s\",\"userCode\":\"%s\",\"expiresIn\":%d,\"interval\":%d}"), PairSecret, PairCode, ExpiresIn, Interval));
	}

	FHttpReply PairPollReply(const ANSICHAR* Status)
	{
		return SatisDash::Test::Reply(200, FString::Printf(TEXT("{\"status\":\"%s\"}"), ANSI_TO_TCHAR(Status)));
	}

	FString PairName() { return TEXT("My Server"); }

	FString PairBody(const FRequestSpec& Spec)
	{
		return FString(FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(Spec.Body.GetData()), Spec.Body.Num()));
	}

	/** Next with the standard name; set or not. */
	bool PairDue(FPairing& P, double Now, FString* Body = nullptr)
	{
		const TOptional<FRequestSpec> Spec = P.Next(PairConfig(), PairName, Now);
		if (Spec.IsSet() && Body)
		{
			*Body = PairBody(*Spec);
		}
		return Spec.IsSet();
	}

	int32 PairLinesWith(const TArray<FString>& Lines, const TCHAR* Part)
	{
		int32 N = 0;
		for (const FString& Line : Lines)
		{
			N += Line.Contains(Part) ? 1 : 0;
		}
		return N;
	}

	/** Open(0), a start answered 201 at 0: the next poll is due at 5. */
	void PairOpenWithCode(FPairing& P)
	{
		P.Open(0);
		PairDue(P, 0);
		P.Finish(PairStartReply(), 0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPairingWindowTest, "SatisDash.Core.Pairing.Window", SatisDash::Test::Flags)

bool FPairingWindowTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Capture;
	FPairing P([] { return 1.0; });
	TestFalse(TEXT("closed before Open"), PairDue(P, 0));
	P.Open(0);
	FString Body;
	TestTrue(TEXT("start"), PairDue(P, 0, &Body));
	TestTrue(TEXT("start carries the name"), Body.Contains(TEXT("serverName")));
	TestFalse(TEXT("one in flight"), PairDue(P, 0));
	P.Finish(PairStartReply(), 0);
	TestFalse(TEXT("not yet at +4"), PairDue(P, 4));
	TestTrue(TEXT("poll at +5"), PairDue(P, 5, &Body));
	TestTrue(TEXT("a poll, with the secret in the body"), Body.Contains(PairSecret) && !Body.Contains(TEXT("agentVersion")));
	P.Finish(PairPollReply("expired"), 5);
	P.Open(1800);
	TestTrue(TEXT("a start in the window"), PairDue(P, 10));
	P.Finish(SatisDash::Test::Reply(503, TEXT("{}")), 10);
	TestFalse(TEXT("Open mid-window changed nothing: closed at 3600"), PairDue(P, 3600));
	TestEqual(TEXT("closed line once"), PairLinesWith(Capture.Take(), TEXT("No pairing code active: join the server or restart it to get a new one")), 1);
	TestFalse(TEXT("again"), PairDue(P, 3601));
	TestEqual(TEXT("not logged again"), PairLinesWith(Capture.Take(), TEXT("No pairing code active")), 1);
	P.Open(3700);
	TestTrue(TEXT("a new window opens"), PairDue(P, 3700));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPairingExpiryTest, "SatisDash.Core.Pairing.Expiry", SatisDash::Test::Flags)

bool FPairingExpiryTest::RunTest(const FString& Parameters)
{
	FPairing P([] { return 1.0; });
	PairOpenWithCode(P);
	FString Body;
	TestTrue(TEXT("poll at 5"), PairDue(P, 5, &Body));
	TestTrue(TEXT("is a poll"), Body.Contains(PairSecret));
	P.Finish(PairPollReply("pending"), 5);
	TestTrue(TEXT("start at 905: the code expired locally"), PairDue(P, 905, &Body));
	TestTrue(TEXT("is a start"), Body.Contains(TEXT("agentVersion")) && !Body.Contains(PairSecret));
	P.Finish(PairStartReply(), 905);
	TestTrue(TEXT("poll"), PairDue(P, 910));
	P.Finish(PairPollReply("expired"), 910);
	TestTrue(TEXT("expired: a start at the same time"), PairDue(P, 910, &Body));
	TestTrue(TEXT("is a start"), !Body.Contains(PairSecret));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPairingSlowDownTest, "SatisDash.Core.Pairing.SlowDown", SatisDash::Test::Flags)

bool FPairingSlowDownTest::RunTest(const FString& Parameters)
{
	FPairing P([] { return 1.0; });
	PairOpenWithCode(P);
	TestTrue(TEXT("poll at 5"), PairDue(P, 5));
	P.Finish(PairPollReply("slow_down"), 5);
	TestFalse(TEXT("not at 10"), PairDue(P, 10));
	TestFalse(TEXT("not at 14"), PairDue(P, 14));
	TestTrue(TEXT("at 15"), PairDue(P, 15));
	P.Finish(PairPollReply("slow_down"), 15);
	TestFalse(TEXT("not at 29"), PairDue(P, 29));
	TestTrue(TEXT("at 30 (+15)"), PairDue(P, 30));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPairingApprovedTest, "SatisDash.Core.Pairing.Approved", SatisDash::Test::Flags)

bool FPairingApprovedTest::RunTest(const FString& Parameters)
{
	FPairing P([] { return 1.0; });
	PairOpenWithCode(P);
	TestTrue(TEXT("poll"), PairDue(P, 5));
	const FString Json = SatisDash::Test::EnrollJson().LeftChop(1) + TEXT(",\"status\":\"approved\",\"approvedBy\":\"Player-4F2K\"}");
	const TOptional<FPairingPoll> Done = P.Finish(SatisDash::Test::Reply(201, Json), 5);
	TestTrue(TEXT("approved"), Done.IsSet() && Done->Status == EPairingPoll::Approved && Done->Enrolled.IsSet());
	if (Done.IsSet())
	{
		SatisDash::Test::SameText(*this, TEXT("approvedBy"), Done->ApprovedBy, TEXT("Player-4F2K"));
	}
	TestFalse(TEXT("stopped"), PairDue(P, 6));
	TestFalse(TEXT("stopped for good"), PairDue(P, 100000));
	TestFalse(TEXT("a late reply is ignored"), P.Finish(PairPollReply("pending"), 7).IsSet());
	P.OnPlayers(0, 8);
	P.OnPlayers(1, 9);
	TestFalse(TEXT("a join does not reopen a stopped pairing"), PairDue(P, 10));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPairingErrorsTest, "SatisDash.Core.Pairing.Errors", SatisDash::Test::Flags)

bool FPairingErrorsTest::RunTest(const FString& Parameters)
{
	{
		FPairing P([] { return 1.0; });
		P.Open(0);
		FString Body;
		TestTrue(TEXT("start"), PairDue(P, 0, &Body));
		TestTrue(TEXT("with a name"), Body.Contains(TEXT("serverName")));
		P.Finish(SatisDash::Test::Reply(400, TEXT("{}")), 0);
		TestTrue(TEXT("again at once"), PairDue(P, 0, &Body));
		TestFalse(TEXT("without the name"), Body.Contains(TEXT("serverName")));
		P.Finish(SatisDash::Test::Reply(400, TEXT("{}")), 0);
		TestFalse(TEXT("waits"), PairDue(P, 3599));
		// the retry falls on the window's end: only a new Open (a join, or a restart) starts again
		TestFalse(TEXT("closed with the window"), PairDue(P, 3600));
		P.Open(3600);
		TestTrue(TEXT("a new window"), PairDue(P, 3600));
	}
	{
		FPairing P([] { return 1.0; });
		P.Open(0);
		PairDue(P, 0);
		FHttpReply Limited = SatisDash::Test::Reply(429, TEXT("{}"));
		Limited.RetryAfter = TEXT("120");
		P.Finish(Limited, 0);
		TestFalse(TEXT("429 at +119"), PairDue(P, 119));
		TestTrue(TEXT("429 at +120"), PairDue(P, 120));
	}
	{
		FPairing P([] { return 1.0; });
		P.Open(0);
		PairDue(P, 0);
		P.Finish(SatisDash::Test::Reply(503, TEXT("{}")), 0);
		const double Wait = BackoffSeconds(0, 1.0);
		TestFalse(TEXT("503 backoff not over"), PairDue(P, Wait - 0.01));
		TestTrue(TEXT("503 backoff over"), PairDue(P, Wait));
	}
	{
		FPairing P([] { return 1.0; });
		P.Open(0);
		PairDue(P, 0);
		P.Finish(SatisDash::Test::Reply(201, TEXT("{\"nope\":1}")), 0);
		TestTrue(TEXT("a 201 not in form backs off, then starts again"), PairDue(P, 60));
	}
	{
		FPairing P([] { return 1.0; });
		PairOpenWithCode(P);
		PairDue(P, 5);
		P.Finish(SatisDash::Test::Reply(503, TEXT("{}")), 5);
		FString Body;
		TestTrue(TEXT("poll again, not a start"), PairDue(P, 5 + BackoffSeconds(0, 1.0), &Body));
		TestTrue(TEXT("with the same secret"), Body.Contains(PairSecret));
	}
	{
		// A poll 400 clears the code but backs off before the next start; "expired" restarts at once.
		FPairing P([] { return 1.0; });
		PairOpenWithCode(P);
		PairDue(P, 5);
		P.Finish(SatisDash::Test::Reply(400, TEXT("{}")), 5);
		const double Wait = BackoffSeconds(0, 1.0);
		TestFalse(TEXT("poll 400: backoff not over"), PairDue(P, 5 + Wait - 0.01));
		FString Body;
		TestTrue(TEXT("poll 400: a start after the backoff"), PairDue(P, 5 + Wait, &Body));
		TestFalse(TEXT("a start, not a poll"), Body.Contains(PairSecret));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPairingPlayersTest, "SatisDash.Core.Pairing.Players", SatisDash::Test::Flags)

bool FPairingPlayersTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("1 new"), ShouldPostCode(1, true, false));
	TestTrue(TEXT("1 joined"), ShouldPostCode(1, false, true));
	TestFalse(TEXT("0"), ShouldPostCode(0, true, false));
	TestFalse(TEXT("2"), ShouldPostCode(2, true, false));
	TestFalse(TEXT("unknown"), ShouldPostCode(-1, true, true));
	TestFalse(TEXT("1 neither"), ShouldPostCode(1, false, false));
	{
		FPairing P([] { return 1.0; });
		PairOpenWithCode(P);
		P.OnPlayers(0, 1);
		P.OnPlayers(1, 2);
		TestTrue(TEXT("chat on a join"), P.TakeChatLine().IsSet());
		TestFalse(TEXT("once"), P.TakeChatLine().IsSet());
	}
	{
		FPairing P([] { return 1.0; });
		PairOpenWithCode(P);
		P.OnPlayers(2, 1);
		P.OnPlayers(1, 2);
		TestFalse(TEXT("2 to 1 is not a join"), P.TakeChatLine().IsSet());
		P.OnPlayers(-1, 3);
		P.OnPlayers(1, 4);
		TestFalse(TEXT("unknown changes nothing"), P.TakeChatLine().IsSet());
	}
	{
		FPairing P([] { return 1.0; });
		P.Open(0);
		P.OnPlayers(1, 0);
		TestFalse(TEXT("the first reading is not a join"), P.TakeChatLine().IsSet());
	}
	{
		FPairing P([] { return 1.0; });
		P.Open(0);
		PairDue(P, 0);
		P.Finish(SatisDash::Test::Reply(503, TEXT("{}")), 0);
		P.OnPlayers(0, 100);
		P.OnPlayers(1, 3700);
		TestTrue(TEXT("a join after the window reopens it"), PairDue(P, 3700));
		P.Finish(PairStartReply(), 3700);
		TestTrue(TEXT("chat for the new code"), P.TakeChatLine().IsSet());
	}
	{
		FPairing P([] { return 1.0; });
		P.Open(0);
		P.OnPlayers(1, 0);
		P.OnPlayers(1, 1);
		PairDue(P, 1);
		P.Finish(PairStartReply(), 1);
		TestTrue(TEXT("with 1 player, a new code chats"), P.TakeChatLine().IsSet());
	}
	{
		FPairing P([] { return 1.0; });
		P.Open(0);
		P.OnPlayers(2, 0);
		PairDue(P, 0);
		P.Finish(PairStartReply(), 0);
		TestFalse(TEXT("with 2 players, no chat"), P.TakeChatLine().IsSet());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPairingLineTest, "SatisDash.Core.Pairing.Line", SatisDash::Test::Flags)

bool FPairingLineTest::RunTest(const FString& Parameters)
{
	FAgentConfig Default;
	Default.BackendUrl = DefaultBackendUrl;
	SatisDash::Test::SameText(*this, TEXT("default"), PairingCodeLine(Default, PairCode, 900),
		TEXT("Pairing code AB3D-7XQ2-K5MN: sign in at https://satis-manager.com/pair and enter it (expires in 15 minutes)"));
	SatisDash::Test::SameText(*this, TEXT("custom"), PairingCodeLine(PairConfig(), PairCode, 900),
		TEXT("Pairing code AB3D-7XQ2-K5MN: sign in on your dashboard's Pair page and enter it (expires in 15 minutes)"));
	SatisDash::Test::SameText(*this, TEXT("30 s"), PairingCodeLine(Default, PairCode, 30),
		TEXT("Pairing code AB3D-7XQ2-K5MN: sign in at https://satis-manager.com/pair and enter it (expires in 1 minute)"));
	TestTrue(TEXT("60 s"), PairingCodeLine(Default, PairCode, 60).EndsWith(TEXT("(expires in 1 minute)")));
	TestTrue(TEXT("120 s"), PairingCodeLine(Default, PairCode, 120).EndsWith(TEXT("(expires in 2 minutes)")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPairingNoSecretInLogTest, "SatisDash.Core.Pairing.NoSecretInLog", SatisDash::Test::Flags)

bool FPairingNoSecretInLogTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Capture;
	FPairing P([] { return 1.0; });
	PairOpenWithCode(P);
	double Now = 5;
	auto Step = [&](const FHttpReply& Reply)
	{
		PairDue(P, Now);
		P.Finish(Reply, Now);
		Now += 100;
	};
	Step(SatisDash::Test::Reply(400,FString::Printf(TEXT("{\"pairingSecret\":\"%s\"}"), PairSecret)));
	// the code is gone after a 400: a fresh start, then polls on it
	PairDue(P, Now);
	P.Finish(PairStartReply(), Now);
	Now += 10;
	Step(SatisDash::Test::Reply(503, FString::Printf(TEXT("%s"), PairSecret)));
	Step(PairPollReply("later"));
	Step(SatisDash::Test::Reply(201, TEXT("{\"status\":\"approved\"}")));
	Step(SatisDash::Test::Reply(429, FString::Printf(TEXT("%s"), PairSecret)));
	Step(PairPollReply("expired"));
	Step(SatisDash::Test::Reply(201, FString::Printf(TEXT("{\"pairingSecret\":\"%s\"}"), PairSecret)));
	Step(SatisDash::Test::Reply(429, TEXT("{}")));
	Step(PairStartReply());
	PairDue(P, Now);
	const FString Approved = SatisDash::Test::EnrollJson().LeftChop(1) + TEXT(",\"status\":\"approved\"}");
	TestTrue(TEXT("approved"), P.Finish(SatisDash::Test::Reply(201, Approved), Now).IsSet());
	const TArray<FString> Lines = Capture.Take();
	for (const FString& Line : Lines)
	{
		TestFalse(TEXT("no secret in a log line"), Line.Contains(PairSecret));
	}
	TestTrue(TEXT("the code is shown"), PairLinesWith(Lines, PairCode) >= 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPairingEmptyNameTest, "SatisDash.Core.Pairing.EmptyName", SatisDash::Test::Flags)

bool FPairingEmptyNameTest::RunTest(const FString& Parameters)
{
	FPairing P([] { return 1.0; });
	P.Open(0);
	TestTrue(TEXT("start"), P.Next(PairConfig(), [] { return FString(); }, 0).IsSet());
	P.Finish(SatisDash::Test::Reply(400, TEXT("{}")), 0);
	TestFalse(TEXT("no name was sent, so the 400 waits an hour"), PairDue(P, 0));
	TestFalse(TEXT("still waiting"), PairDue(P, 3599));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPairingWindowEndTest, "SatisDash.Core.Pairing.WindowEnd", SatisDash::Test::Flags)

bool FPairingWindowEndTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Capture;
	FPairing P([] { return 1.0; });
	P.Open(0);
	TestTrue(TEXT("start"), PairDue(P, 3500));
	P.Finish(PairStartReply(), 3500);
	FString Body;
	TestTrue(TEXT("poll"), PairDue(P, 3505));
	P.Finish(PairPollReply("pending"), 3505);
	TestTrue(TEXT("poll past the window end"), PairDue(P, 3650, &Body) && Body.Contains(PairSecret));
	P.Finish(PairPollReply("pending"), 3650);
	P.OnPlayers(0, 3651);
	P.OnPlayers(1, 3652);
	TestTrue(TEXT("a join chats the open code"), P.TakeChatLine().IsSet());
	TestTrue(TEXT("and polls, with no new start"), PairDue(P, 3700, &Body) && Body.Contains(PairSecret));
	P.Finish(PairPollReply("pending"), 3700);
	TestFalse(TEXT("the code expired"), PairDue(P, 4400));
	TestFalse(TEXT("closed"), PairDue(P, 4401));
	TestEqual(TEXT("closed line once"), PairLinesWith(Capture.Take(), TEXT("No pairing code active")), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPairingWaitsTest, "SatisDash.Core.Pairing.Waits", SatisDash::Test::Flags)

bool FPairingWaitsTest::RunTest(const FString& Parameters)
{
	{
		FPairing P([] { return 1.0; });
		P.Open(0);
		PairDue(P, 0);
		P.Finish(SatisDash::Test::Reply(429, TEXT("{}")), 0);
		TestFalse(TEXT("429 without Retry-After: 59"), PairDue(P, 59));
		TestTrue(TEXT("60"), PairDue(P, 60));
	}
	{
		FPairing P([] { return 1.0; });
		PairOpenWithCode(P);
		PairDue(P, 5);
		FHttpReply Limited = SatisDash::Test::Reply(429, TEXT("{}"));
		Limited.RetryAfter = TEXT("30");
		P.Finish(Limited, 5);
		FString Body;
		TestFalse(TEXT("poll 429 at +29"), PairDue(P, 34));
		TestTrue(TEXT("poll 429 at +30, still a poll"), PairDue(P, 35, &Body) && Body.Contains(PairSecret));
	}
	{
		FPairing P([] { return 1.0; });
		PairOpenWithCode(P);
		PairDue(P, 5);
		P.Finish(PairPollReply("slow_down"), 5);
		TestTrue(TEXT("slowed poll"), PairDue(P, 15));
		P.Finish(PairPollReply("expired"), 15);
		TestTrue(TEXT("new start"), PairDue(P, 15));
		P.Finish(PairStartReply(), 15);
		TestFalse(TEXT("the new code polls every 5 s again: not at 19"), PairDue(P, 19));
		TestTrue(TEXT("at 20"), PairDue(P, 20));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPairingSupersededTest, "SatisDash.Core.Pairing.Superseded", SatisDash::Test::Flags)

bool FPairingSupersededTest::RunTest(const FString& Parameters)
{
	FPairing P([] { return 1.0; });
	P.Open(0);
	TestTrue(TEXT("start"), PairDue(P, 0));
	P.Stop();
	P.Open(1);
	TestFalse(TEXT("one in flight, even across Stop and Open"), PairDue(P, 1));
	TestFalse(TEXT("the old reply is ignored"), P.Finish(PairStartReply(), 2).IsSet());
	FString Body;
	TestTrue(TEXT("then a fresh start"), PairDue(P, 2, &Body));
	TestTrue(TEXT("the old code was not adopted"), Body.Contains(TEXT("agentVersion")) && !Body.Contains(PairSecret));
	return true;
}
