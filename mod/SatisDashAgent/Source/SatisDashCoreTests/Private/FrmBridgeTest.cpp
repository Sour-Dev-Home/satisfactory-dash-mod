#include "DriverFakes.h"
#include "SatisDash/FrmBridge.h"

using namespace SatisDash;

namespace
{
	const TCHAR* FrmBridgeOnePlayer = TEXT("[{\"Name\":\"Player One\",\"Online\":true}]");
	const TCHAR* FrmBridgeOneBuilding = TEXT("[{\"ID\":\"B_1\",\"Name\":\"n\",\"ClassName\":\"c\",\"IsProducing\":true,\"IsPaused\":false}]");

	struct FFrmBridgeFixture
	{
		SatisDash::Test::FFakeTransport Net;
		SatisDash::Test::FFakeWorker Worker;
		FFrmBridge Bridge{ Net, Worker };
		double T = 100;
		void tick(int32 Times = 1)
		{
			for (int32 I = 0; I < Times; ++I)
			{
				T += 0.5;
				Bridge.Tick(T, 8080, FString());
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmBridgePlayersTest, "SatisDash.Core.FrmBridge.Players", SatisDash::Test::Flags)

bool FFrmBridgePlayersTest::RunTest(const FString& Parameters)
{
	FFrmBridgeFixture F;
	F.Bridge.RequestPass(false);
	F.tick();
	TestEqual(TEXT("getPlayer asked"), F.Net.pending(TEXT("/getPlayer")), 1);
	F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, FrmBridgeOnePlayer));
	TestFalse(TEXT("not ready before the worker parsed it"), F.Bridge.TakePlayers(F.T, 10).IsSet());
	F.tick(2);
	TestEqual(TEXT("parsed on the worker"), F.Worker.Launched, 1);
	const TOptional<FString> Players = F.Bridge.TakePlayers(F.T, 10);
	TestTrue(TEXT("ready"), Players.IsSet() && Players->Contains(TEXT("Player One")));
	TestFalse(TEXT("taken once"), F.Bridge.TakePlayers(F.T, 10).IsSet());

	F.Bridge.RequestPass(false);
	F.tick();
	F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(0, FString()));
	F.tick();
	SatisDash::Test::SameText(*this, TEXT("FRM down: unavailable, without the worker"), F.Bridge.TakePlayers(F.T, 10).Get(FString()), TEXT("{\"available\":false,\"players\":[]}"));
	TestEqual(TEXT("nothing parsed for it"), F.Worker.Launched, 1);

	F.Bridge.RequestPass(false);
	F.tick();
	F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, TEXT("[{\"Name\":1}]")));
	F.tick(2);
	TestFalse(TEXT("an invalid body: no part"), F.Bridge.TakePlayers(F.T, 10).IsSet());

	F.Bridge.RequestPass(false);
	F.tick();
	F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, FrmBridgeOnePlayer));
	F.tick(2);
	TestFalse(TEXT("a part older than its age is dropped"), F.Bridge.TakePlayers(F.T + 11, 10).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmBridgeFactoryTest, "SatisDash.Core.FrmBridge.Factory", SatisDash::Test::Flags)

bool FFrmBridgeFactoryTest::RunTest(const FString& Parameters)
{
	FFrmBridgeFixture F;
	F.Worker.bHold = true;
	F.Bridge.RequestPass(true);
	F.tick();
	TestEqual(TEXT("one FRM request in flight: players first"), F.Net.Open.Num(), 1);
	F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, FrmBridgeOnePlayer));
	F.tick();
	TestEqual(TEXT("then the factory"), F.Net.pending(TEXT("/getFactory")), 1);
	F.Net.answer(TEXT("/getFactory"), SatisDash::Test::Reply(200, FrmBridgeOneBuilding));
	F.tick();
	TestEqual(TEXT("one worker task at a time: the players parse"), F.Worker.Launched, 1);
	F.Bridge.RequestPass(true);
	F.tick();
	TestEqual(TEXT("getPlayer passes the large body"), F.Net.pending(TEXT("/getPlayer")), 1);
	F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, FrmBridgeOnePlayer));
	F.tick();
	TestEqual(TEXT("but no second factory read while one is parsing"), F.Net.pending(TEXT("/getFactory")), 0);
	TestEqual(TEXT("and still one task"), F.Worker.Launched, 1);
	for (int32 I = 0; I < 6; ++I)
	{
		F.Worker.release();
		F.tick();
	}
	TestTrue(TEXT("the factory part is ready"), F.Bridge.TakeFactory(F.T, 30).Get(FString()).Contains(TEXT("\"B_1\"")));
	F.tick(2);
	TestEqual(TEXT("then the next factory read may start"), F.Net.pending(TEXT("/getFactory")), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmBridgeStopTest, "SatisDash.Core.FrmBridge.Stop", SatisDash::Test::Flags)

bool FFrmBridgeStopTest::RunTest(const FString& Parameters)
{
	FFrmBridgeFixture F;
	F.Bridge.RequestPass(true);
	F.tick();
	F.Bridge.Stop();
	F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, FrmBridgeOnePlayer));
	F.tick(3);
	TestFalse(TEXT("a reply after Stop is never used"), F.Bridge.TakePlayers(F.T, 10).IsSet());
	TestEqual(TEXT("and the queued factory read was dropped"), F.Net.sent(TEXT("/getFactory")), 0);

	FSnapshot Snapshot;
	Snapshot.AgentVersion = TEXT("mod/0.1.0");
	Snapshot.ObservedAt = FDateTime(2026, 10, 23, 12, 0, 0);
	Snapshot.FactoryJson = TEXT("{\"buildings\":[]}");
	Snapshot.PlayersJson = PlayersToJson(PlayersUnavailable());
	F.Bridge.EncodeSnapshot(Snapshot);
	TestTrue(TEXT("encoding is pending"), F.Bridge.EncodePending());
	F.tick(2);
	const TOptional<FEncodedBody> Body = F.Bridge.TakeEncoded();
	TestTrue(TEXT("encoded on the worker"), Body.IsSet() && Body->Bytes.Num() > 0);
	TestFalse(TEXT("no longer pending"), F.Bridge.EncodePending());
	const FString Json = SnapshotToJson(Snapshot);
	TestTrue(TEXT("the parts are spliced into the snapshot"), Json.Contains(TEXT(",\"players\":{\"available\":false,\"players\":[]}")) && Json.Contains(TEXT(",\"factory\":{\"buildings\":[]}}")));
	TestTrue(TEXT("and it is one valid object"), SatisDash::Json::ParseObject(Json).IsValid());
	return true;
}

namespace
{
	/** Players and factory parts ready and an encoded body waiting, none taken yet. */
	void FrmBridgeMakeParts(FFrmBridgeFixture& F)
	{
		F.Bridge.RequestPass(true);
		F.tick();
		F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, FrmBridgeOnePlayer));
		F.tick(2);
		F.Net.answer(TEXT("/getFactory"), SatisDash::Test::Reply(200, FrmBridgeOneBuilding));
		F.tick(3);
		FSnapshot Snapshot;
		Snapshot.AgentVersion = TEXT("mod/0.1.0");
		Snapshot.ObservedAt = FDateTime(2026, 10, 23, 12, 0, 0);
		F.Bridge.EncodeSnapshot(Snapshot);
		F.tick(2);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmBridgeStopReadyTest, "SatisDash.Core.FrmBridge.StopReady", SatisDash::Test::Flags)

bool FFrmBridgeStopReadyTest::RunTest(const FString& Parameters)
{
	{
		FFrmBridgeFixture Control;
		FrmBridgeMakeParts(Control);
		TestTrue(TEXT("control: players ready"), Control.Bridge.TakePlayers(Control.T, 1e9).IsSet());
		TestTrue(TEXT("control: factory ready"), Control.Bridge.TakeFactory(Control.T, 1e9).IsSet());
		TestTrue(TEXT("control: encoded ready"), Control.Bridge.TakeEncoded().IsSet());
	}
	FFrmBridgeFixture F;
	FrmBridgeMakeParts(F);
	F.Bridge.Stop();
	TestFalse(TEXT("a ready players part is gone after Stop"), F.Bridge.TakePlayers(F.T, 1e9).IsSet());
	TestFalse(TEXT("a ready factory part is gone after Stop"), F.Bridge.TakeFactory(F.T, 1e9).IsSet());
	TestFalse(TEXT("an encoded body is gone after Stop"), F.Bridge.TakeEncoded().IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmBridgeStopRunningTest, "SatisDash.Core.FrmBridge.StopRunning", SatisDash::Test::Flags)

bool FFrmBridgeStopRunningTest::RunTest(const FString& Parameters)
{
	{
		// A players job held on the worker when Stop comes.
		FFrmBridgeFixture F;
		F.Worker.bHold = true;
		F.Bridge.RequestPass(true);
		F.tick();
		F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, FrmBridgeOnePlayer));
		F.tick();
		TestEqual(TEXT("the players parse is held"), F.Worker.Launched, 1);
		const int32 SentBefore = F.Net.sent(TEXT("/getFactory"));
		F.Bridge.Stop();
		F.Worker.release();
		F.tick(3);
		TestFalse(TEXT("a players job running at Stop is never harvested"), F.Bridge.TakePlayers(F.T, 1e9).IsSet());
		TestEqual(TEXT("and nothing more is launched"), F.Worker.Launched, 1);
		TestEqual(TEXT("and no new read is sent"), F.Net.sent(TEXT("/getFactory")), SentBefore);
	}
	{
		// A factory slice held on the worker when Stop comes.
		FFrmBridgeFixture F;
		F.Worker.bHold = true;
		F.Bridge.RequestPass(true);
		F.tick();
		F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, FrmBridgeOnePlayer));
		F.tick();
		F.Worker.release();
		F.tick();
		F.Net.answer(TEXT("/getFactory"), SatisDash::Test::Reply(200, FrmBridgeOneBuilding));
		F.tick();
		TestEqual(TEXT("the factory slice is held"), F.Worker.Launched, 2);
		F.Bridge.Stop();
		F.Worker.release();
		F.tick(3);
		TestFalse(TEXT("a factory job running at Stop is never harvested"), F.Bridge.TakeFactory(F.T, 1e9).IsSet());
		TestEqual(TEXT("and no further slice is launched"), F.Worker.Launched, 2);
	}
	{
		// An encode job held on the worker when Stop comes.
		FFrmBridgeFixture F;
		F.Worker.bHold = true;
		FSnapshot Snapshot;
		Snapshot.AgentVersion = TEXT("mod/0.1.0");
		Snapshot.ObservedAt = FDateTime(2026, 10, 23, 12, 0, 0);
		F.Bridge.EncodeSnapshot(Snapshot);
		F.tick();
		TestEqual(TEXT("the encode is held"), F.Worker.Launched, 1);
		F.Bridge.Stop();
		F.Worker.release();
		F.tick(3);
		TestFalse(TEXT("an encode running at Stop is never harvested"), F.Bridge.TakeEncoded().IsSet());
		TestFalse(TEXT("and it is no longer pending"), F.Bridge.EncodePending());
	}
	return true;
}
