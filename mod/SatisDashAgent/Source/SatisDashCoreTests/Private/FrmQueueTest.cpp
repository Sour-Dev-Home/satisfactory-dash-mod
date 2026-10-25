#include "DriverFakes.h"
#include "SatisDash/Frm.h"
#include "SatisDash/FrmQueue.h"

using namespace SatisDash;

namespace
{
	// A world-lane endpoint for the tests (3B adds the real ones).
	constexpr FFrmEndpoint FrmQueueTestWorld{ TEXT("getTestWorld"), EFrmLane::World, true, false, FrmSmallCapBytes, 5.f };

	// Marked both large and passable: a mistake in an endpoint row must not let a second large body in.
	constexpr FFrmEndpoint FrmQueueTestBoth{ TEXT("getTestBoth"), EFrmLane::Live, true, true, FrmSmallCapBytes, 5.f };

	FFrmRead FrmQueueRead(const FFrmEndpoint& Endpoint, TArray<int32>* Codes)
	{
		return FFrmRead{ &Endpoint, [Codes](const FHttpReply& Reply) { if (Codes) Codes->Add(Reply.Code); } };
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmRequestTest, "SatisDash.Core.Frm.Request", SatisDash::Test::Flags)

bool FFrmRequestTest::RunTest(const FString& Parameters)
{
	const FRequestSpec Plain = BuildFrmRequest(8080, FString(), FrmGetPlayer);
	SatisDash::Test::SameText(*this, TEXT("GET"), Plain.Verb, TEXT("GET"));
	SatisDash::Test::SameText(*this, TEXT("loopback URL"), Plain.Url, TEXT("http://127.0.0.1:8080/getPlayer"));
	bool bPlain = false;
	TestTrue(TEXT("S1 allows it"), IsAllowedRequestUrl(Plain.Url, bPlain) && bPlain);
	TestEqual(TEXT("no header without a token"), Plain.Headers.Num(), 0);
	TestEqual(TEXT("getPlayer's cap"), Plain.ResponseCapBytes, FrmSmallCapBytes);
	TestEqual(TEXT("getPlayer's timeout"), Plain.TimeoutSeconds, 5.f);
	const FRequestSpec Token = BuildFrmRequest(18081, TEXT("tok"), FrmGetFactory);
	SatisDash::Test::SameText(*this, TEXT("factory URL"), Token.Url, TEXT("http://127.0.0.1:18081/getFactory"));
	TestEqual(TEXT("one header"), Token.Headers.Num(), 1);
	SatisDash::Test::SameText(*this, TEXT("FRM's header name"), Token.Headers[0].Key, TEXT("X-FRM-Authorization"));
	SatisDash::Test::SameText(*this, TEXT("the token"), Token.Headers[0].Value, TEXT("tok"));
	TestEqual(TEXT("getFactory's cap"), Token.ResponseCapBytes, MaxRequestResponseBytes);
	for (const TPair<FString, FString>& Header : Token.Headers)
	{
		TestFalse(TEXT("never the backend's Authorization"), Header.Key.Equals(TEXT("Authorization"), ESearchCase::IgnoreCase));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmQueueLanesTest, "SatisDash.Core.FrmQueue.Lanes", SatisDash::Test::Flags)

bool FFrmQueueLanesTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FFakeTransport Net;
	FFrmQueue FrmLanes{ Net };
	TArray<int32> Codes;
	TestTrue(TEXT("world queued"), FrmLanes.Enqueue(FrmQueueRead(FrmQueueTestWorld, &Codes)));
	TestTrue(TEXT("factory queued"), FrmLanes.Enqueue(FrmQueueRead(FrmGetFactory, &Codes)));
	TestFalse(TEXT("one of each endpoint at a time"), FrmLanes.Enqueue(FrmQueueRead(FrmGetFactory, &Codes)));
	TestTrue(TEXT("players queued"), FrmLanes.Enqueue(FrmQueueRead(FrmGetPlayer, &Codes)));

	FrmLanes.Pump(8080, FString(), false);
	TestEqual(TEXT("the live lane first, in order: factory"), Net.pending(TEXT("/getFactory")), 1);
	FrmLanes.Pump(8080, FString(), false);
	TestEqual(TEXT("one in flight"), Net.Open.Num(), 1);
	TestFalse(TEXT("an endpoint in flight can't be queued again"), FrmLanes.Enqueue(FrmQueueRead(FrmGetFactory, &Codes)));
	Net.answer(TEXT("/getFactory"), SatisDash::Test::Reply(200, TEXT("[]")));
	TestEqual(TEXT("its handler ran"), Codes.Num(), 1);

	// A large body is parsing: only getPlayer may pass it, and the world lane waits.
	FrmLanes.Pump(8080, FString(), true);
	TestEqual(TEXT("players passes the parse"), Net.pending(TEXT("/getPlayer")), 1);
	Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, TEXT("[]")));
	FrmLanes.Pump(8080, FString(), true);
	TestEqual(TEXT("a large world read waits for the parse"), Net.pending(TEXT("/getTestWorld")), 0);
	FrmLanes.Pump(8080, FString(), false);
	TestEqual(TEXT("then goes"), Net.pending(TEXT("/getTestWorld")), 1);
	Net.answer(TEXT("/getTestWorld"), SatisDash::Test::Reply(200, TEXT("[]")));
	// The world read is queued first and the live read second: the live lane must still go first.
	FrmLanes.Enqueue(FrmQueueRead(FrmQueueTestWorld, &Codes));
	FrmLanes.Enqueue(FrmQueueRead(FrmGetPlayer, &Codes));
	FrmLanes.Pump(8080, FString(), false);
	TestEqual(TEXT("live before world, though the world read was queued first"), Net.pending(TEXT("/getPlayer")), 1);
	TestEqual(TEXT("world still queued"), FrmLanes.Queued(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmQueueTokenAndCancelTest, "SatisDash.Core.FrmQueue.TokenAndCancel", SatisDash::Test::Flags)

bool FFrmQueueTokenAndCancelTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	SatisDash::Test::FFakeTransport Net;
	FFrmQueue FrmLanes{ Net };
	TArray<int32> Codes;
	for (int32 I = 0; I < 3; ++I)
	{
		FrmLanes.Enqueue(FrmQueueRead(FrmGetPlayer, &Codes));
		FrmLanes.Pump(8080, TEXT("secret-token"), false);
		Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(I == 1 ? 403 : 401, FString()));
	}
	int32 TokenLines = 0;
	for (const FString& Line : Log.Take())
	{
		TokenLines += Line.Contains(TEXT("FRM requires a token: set frmToken in SatisDashAgent.cfg")) ? 1 : 0;
		TestFalse(TEXT("the token is never logged"), Line.Contains(TEXT("secret-token")));
	}
	TestEqual(TEXT("one line for a streak of 401/403"), TokenLines, 1);
	TestEqual(TEXT("the handler still sees each reply"), Codes.Num(), 3);

	FrmLanes.Enqueue(FrmQueueRead(FrmGetPlayer, &Codes));
	FrmLanes.Pump(8080, FString(), false);
	FrmLanes.Enqueue(FrmQueueRead(FrmGetFactory, &Codes));
	FrmLanes.Cancel();
	TestEqual(TEXT("nothing queued after a cancel"), FrmLanes.Queued(), 0);
	FrmLanes.Enqueue(FrmQueueRead(FrmGetFactory, &Codes));
	FrmLanes.Pump(8080, FString(), false);
	TestEqual(TEXT("the cancelled read still counts as in flight"), Net.pending(TEXT("/getFactory")), 0);
	Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, TEXT("[]")));
	TestEqual(TEXT("its reply is dropped"), Codes.Num(), 3);
	FrmLanes.Pump(8080, FString(), false);
	TestEqual(TEXT("then the next read starts"), Net.pending(TEXT("/getFactory")), 1);
	return SatisDash::Test::NoErrorLines(*this, Log);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmQueueLargeRuleTest, "SatisDash.Core.FrmQueue.LargeRule", SatisDash::Test::Flags)

bool FFrmQueueLargeRuleTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FFakeTransport Net;
	FFrmQueue FrmLanes{ Net };
	FrmLanes.Enqueue(FrmQueueRead(FrmQueueTestBoth, nullptr));
	FrmLanes.Pump(8080, FString(), true);
	TestEqual(TEXT("a large read never starts while a large body is held, even if marked passable"), Net.Open.Num(), 0);
	FrmLanes.Pump(8080, FString(), false);
	TestEqual(TEXT("and starts once the parse is done"), Net.pending(TEXT("/getTestBoth")), 1);
	return true;
}
