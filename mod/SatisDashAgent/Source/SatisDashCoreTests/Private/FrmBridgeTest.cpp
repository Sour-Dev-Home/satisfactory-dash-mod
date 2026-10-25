#include "DriverFakes.h"
#include "SatisDash/FrmBridge.h"
#include "SatisDash/FrmWorld.h"

using namespace SatisDash;

namespace
{
	const TCHAR* FrmBridgeOnePlayer = TEXT("[{\"Name\":\"Player One\",\"Online\":true}]");
	const TCHAR* FrmBridgeOneBuilding = TEXT("[{\"ID\":\"B_1\",\"Name\":\"n\",\"ClassName\":\"c\",\"IsProducing\":true,\"IsPaused\":false}]");

	const TCHAR* FrmBridgeOneTrain = TEXT("[{\"ID\":\"T1\",\"Name\":\"Train\",\"Status\":\"Self-Driving\",\"location\":{\"x\":150,\"y\":-150,\"z\":0}}]");
	const TCHAR* FrmBridgeOneStation = TEXT("[{\"ID\":\"S1\",\"Name\":\"Station\",\"location\":{\"x\":5049,\"y\":-5051,\"z\":0}}]");
	const TCHAR* FrmBridgeMapLive = TEXT("{\"trains\":[{\"id\":\"T1\",\"name\":\"Train\",\"x\":2,\"y\":-1,\"status\":\"Self-Driving\"}],\"stations\":[{\"id\":\"S1\",\"name\":\"Station\",\"x\":50,\"y\":-51}]}");

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
		/** Ticks until FRM has been asked for Path (at most MaxTicks), then answers it. False if it was never asked. */
		bool serve(const TCHAR* Path, const FHttpReply& Reply, int32 MaxTicks = 8)
		{
			for (int32 I = 0; I < MaxTicks && Net.pending(Path) == 0; ++I)
			{
				tick();
			}
			return Net.answer(Path, Reply);
		}
		/** One factory-cadence pass with FRM answering every read in turn (players and factory fine, the others as given). */
		void cycle(const FHttpReply& Trains, const FHttpReply& Stations)
		{
			Bridge.RequestPass(true);
			serve(TEXT("/getPlayer"), SatisDash::Test::Reply(200, FrmBridgeOnePlayer));
			serve(TEXT("/getFactory"), SatisDash::Test::Reply(200, FrmBridgeOneBuilding));
			serve(TEXT("/getTrains"), Trains);
			serve(TEXT("/getTrainStation"), Stations);
			tick(4);
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
	F.serve(TEXT("/getTrains"), SatisDash::Test::Reply(0, FString()));
	F.serve(TEXT("/getTrainStation"), SatisDash::Test::Reply(0, FString()));
	F.tick(2);
	TestEqual(TEXT("then the next factory read may start"), F.Net.pending(TEXT("/getFactory")), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmBridgeMapLiveTest, "SatisDash.Core.FrmBridge.MapLive", SatisDash::Test::Flags)

bool FFrmBridgeMapLiveTest::RunTest(const FString& Parameters)
{
	FFrmBridgeFixture F;
	// getTrains isn't marked passable: it waits while the factory body parses. Only getPlayer passes a large parse.
	F.Bridge.RequestPass(true);
	TestTrue(TEXT("players asked"), F.serve(TEXT("/getPlayer"), SatisDash::Test::Reply(200, FrmBridgeOnePlayer)));
	F.tick(2); // the players parse runs and is harvested
	F.Worker.bHold = true;
	TestTrue(TEXT("factory asked"), F.serve(TEXT("/getFactory"), SatisDash::Test::Reply(200, FrmBridgeOneBuilding)));
	F.tick(2);
	TestEqual(TEXT("getTrains waits while the factory body is held"), F.Net.pending(TEXT("/getTrains")), 0);
	F.Worker.bHold = false;
	F.Worker.release();
	TestTrue(TEXT("then getTrains is asked"), F.serve(TEXT("/getTrains"), SatisDash::Test::Reply(200, FrmBridgeOneTrain)));
	TestTrue(TEXT("and getTrainStation, after it: one request at a time"), F.serve(TEXT("/getTrainStation"), SatisDash::Test::Reply(200, FrmBridgeOneStation)));
	F.tick(4);
	SatisDash::Test::SameText(*this, TEXT("the part is the Node agent's mapLive"), F.Bridge.TakeMapLive(F.T, 30).Get(FString()), FrmBridgeMapLive);
	TestFalse(TEXT("taken once"), F.Bridge.TakeMapLive(F.T, 30).IsSet());

	// Either read failing, or invalid, leaves the whole part out (the Node agent reads both and one failure fails both).
	F.cycle(SatisDash::Test::Reply(200, FrmBridgeOneTrain), SatisDash::Test::Reply(500, FString()));
	TestFalse(TEXT("stations failed: no part"), F.Bridge.TakeMapLive(F.T, 30).IsSet());
	F.cycle(SatisDash::Test::Reply(200, TEXT("[{\"ID\":\"\"}]")), SatisDash::Test::Reply(200, FrmBridgeOneStation));
	TestFalse(TEXT("trains invalid: no part"), F.Bridge.TakeMapLive(F.T, 30).IsSet());
	F.cycle(SatisDash::Test::Reply(0, FString()), SatisDash::Test::Reply(0, FString()));
	TestFalse(TEXT("FRM down: no part"), F.Bridge.TakeMapLive(F.T, 30).IsSet());
	F.cycle(SatisDash::Test::Reply(200, FrmBridgeOneTrain), SatisDash::Test::Reply(200, FrmBridgeOneStation));
	SatisDash::Test::SameText(*this, TEXT("and it recovers when FRM returns"), F.Bridge.TakeMapLive(F.T, 30).Get(FString()), FrmBridgeMapLive);

	// A getTrains read left in flight by a stopped run still holds its endpoint when the run restarts: Enqueue refuses the new
	// one, that side is Failed (not Waiting forever), the pair settles without a part, and the next cadence tick starts afresh.
	F.Bridge.RequestPass(true);
	F.serve(TEXT("/getPlayer"), SatisDash::Test::Reply(200, FrmBridgeOnePlayer));
	F.serve(TEXT("/getFactory"), SatisDash::Test::Reply(200, FrmBridgeOneBuilding));
	for (int32 I = 0; I < 8 && F.Net.pending(TEXT("/getTrains")) == 0; ++I)
	{
		F.tick();
	}
	TestEqual(TEXT("getTrains is in flight"), F.Net.pending(TEXT("/getTrains")), 1);
	F.Bridge.Stop();
	F.Bridge.RequestPass(true);
	TestTrue(TEXT("the stopped run's getTrains is answered; nobody uses it"), F.Net.answer(TEXT("/getTrains"), SatisDash::Test::Reply(200, FrmBridgeOneTrain)));
	F.serve(TEXT("/getPlayer"), SatisDash::Test::Reply(200, FrmBridgeOnePlayer));
	F.serve(TEXT("/getFactory"), SatisDash::Test::Reply(200, FrmBridgeOneBuilding));
	TestEqual(TEXT("the restarted run did not ask getTrains a second time"), F.Net.pending(TEXT("/getTrains")), 0);
	F.serve(TEXT("/getTrainStation"), SatisDash::Test::Reply(200, FrmBridgeOneStation));
	F.tick(4);
	TestFalse(TEXT("the refused side failed the pair: no part"), F.Bridge.TakeMapLive(F.T, 30).IsSet());
	F.cycle(SatisDash::Test::Reply(200, FrmBridgeOneTrain), SatisDash::Test::Reply(200, FrmBridgeOneStation));
	SatisDash::Test::SameText(*this, TEXT("and the next cadence tick starts a fresh pair"), F.Bridge.TakeMapLive(F.T, 30).Get(FString()), FrmBridgeMapLive);

	// An old part is dropped: a part ages from its FRM read.
	F.cycle(SatisDash::Test::Reply(200, FrmBridgeOneTrain), SatisDash::Test::Reply(200, FrmBridgeOneStation));
	TestFalse(TEXT("older than the factory cadence: dropped"), F.Bridge.TakeMapLive(F.T + 31, 30).IsSet());
	return true;
}

namespace
{
	/** Count records with long strings, so a train and a station each come to about 400 bytes of output. */
	FString FrmBridgeBig(bool bTrains, int32 Count)
	{
		const FString Id = FString::ChrN(100, TEXT('i'));
		const FString Name = FString::ChrN(200, TEXT('n'));
		const FString Status = bTrains ? TEXT("\"Status\":\"") + FString::ChrN(50, TEXT('s')) + TEXT("\",") : FString();
		FString Json = TEXT("[");
		for (int32 I = 0; I < Count; ++I)
		{
			Json += FString::Printf(TEXT("%s{\"ID\":\"%s%d\",\"Name\":\"%s\",%s\"location\":{\"x\":%d,\"y\":0,\"z\":0}}"), I > 0 ? TEXT(",") : TEXT(""), *Id, I, *Name, *Status, I * 100);
		}
		return Json + TEXT("]");
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmBridgeMapLiveCapTest, "SatisDash.Core.FrmBridge.MapLiveCap", SatisDash::Test::Flags)

bool FFrmBridgeMapLiveCapTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	FFrmBridgeFixture F;
	// 500 trains and 500 stations, each array under 256,000 bytes, together over it: the whole part is left out.
	for (int32 Round = 0; Round < 2; ++Round)
	{
		F.cycle(SatisDash::Test::Reply(200, FrmBridgeBig(true, 500)), SatisDash::Test::Reply(200, FrmBridgeBig(false, 500)));
		TestFalse(TEXT("over 256,000 bytes: no part"), F.Bridge.TakeMapLive(F.T, 30).IsSet());
	}
	int32 CapLines = 0;
	for (const FString& Line : Log.Take())
	{
		CapLines += Line.Contains(TEXT("mapLive is over its")) ? 1 : 0;
	}
	TestEqual(TEXT("one warning, not one a cycle"), CapLines, 1);
	// Just under: a smaller pair goes through.
	F.cycle(SatisDash::Test::Reply(200, FrmBridgeBig(true, 100)), SatisDash::Test::Reply(200, FrmBridgeBig(false, 100)));
	TestTrue(TEXT("100 of each is under the cap"), F.Bridge.TakeMapLive(F.T, 30).IsSet());
	return SatisDash::Test::NoErrorLines(*this, Log);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmBridgeReadAgeTest, "SatisDash.Core.FrmBridge.ReadAge", SatisDash::Test::Flags)

bool FFrmBridgeReadAgeTest::RunTest(const FString& Parameters)
{
	FFrmBridgeFixture F;
	F.Worker.bHold = true;
	F.Bridge.RequestPass(false);
	F.tick();
	F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, FrmBridgeOnePlayer));
	F.tick();      // launches the parse; the fake worker holds it
	F.tick(40);    // 20 s go by while the worker is busy
	F.Worker.release();
	F.tick();      // harvested now
	TestFalse(TEXT("a part is as old as the FRM read, not as old as its harvest"), F.Bridge.TakePlayers(F.T, 10).IsSet());
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

namespace
{
	const TCHAR* FrmBridgeRailsBody = TEXT("[{\"ID\":\"R1\",\"SplineData\":[{\"x\":0,\"y\":0},{\"x\":300,\"y\":0}]}]");
	const TCHAR* FrmBridgeNodesBody = TEXT("[{\"Name\":\"Iron Ore\",\"Purity\":\"Normal\",\"NodeType\":\"Node\",\"Exploited\":false,\"location\":{\"x\":100,\"y\":200,\"z\":0}}]");
	const TCHAR* FrmBridgeWorldAt = TEXT("2026-10-24T12:00:00.000Z");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmBridgeWorldTest, "SatisDash.Core.FrmBridge.World", SatisDash::Test::Flags)

bool FFrmBridgeWorldTest::RunTest(const FString& Parameters)
{
	using EKind = FWorldOutcome::EKind;
	FFrmBridgeFixture F;
	TestTrue(TEXT("rails asked for"), F.Bridge.RequestWorld(EWorldLayer::Rails, FrmBridgeWorldAt, TOptional<FBlake3Hash>(), TOptional<FBlake3Hash>()));
	TestFalse(TEXT("one request per layer at a time"), F.Bridge.RequestWorld(EWorldLayer::Rails, FrmBridgeWorldAt, TOptional<FBlake3Hash>(), TOptional<FBlake3Hash>()));
	TestTrue(TEXT("resource nodes asked for"), F.Bridge.RequestWorld(EWorldLayer::ResourceNodes, FrmBridgeWorldAt, TOptional<FBlake3Hash>(), TOptional<FBlake3Hash>()));
	TestTrue(TEXT("getTrainRails is read first (queued first)"), F.serve(TEXT("/getTrainRails"), SatisDash::Test::Reply(200, FrmBridgeRailsBody)));

	// The rails job is held on the worker: it holds a large body, so getPlayer passes it but the nodes read waits.
	F.Worker.bHold = true;
	F.Bridge.RequestPass(false);
	F.tick(2);
	TestEqual(TEXT("getPlayer passes a large world parse"), F.Net.pending(TEXT("/getPlayer")), 1);
	F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, FrmBridgeOnePlayer));
	F.tick(2);
	TestEqual(TEXT("the second large read waits for the first parse"), F.Net.pending(TEXT("/getResourceNode")), 0);
	TestFalse(TEXT("and nothing is ready yet"), F.Bridge.TakeWorld(EWorldLayer::Rails).IsSet());

	F.Worker.bHold = false;
	F.Worker.release();
	F.tick(8);
	TOptional<FWorldOutcome> Rails = F.Bridge.TakeWorld(EWorldLayer::Rails);
	TestTrue(TEXT("the rails outcome is Ready"), Rails.IsSet() && Rails->Kind == EKind::Ready && Rails->Items == 1);
	TestFalse(TEXT("taken once"), F.Bridge.TakeWorld(EWorldLayer::Rails).IsSet());
	TestTrue(TEXT("then the nodes read starts"), F.serve(TEXT("/getResourceNode"), SatisDash::Test::Reply(200, FrmBridgeNodesBody)));
	F.tick(8);
	const TOptional<FWorldOutcome> Nodes = F.Bridge.TakeWorld(EWorldLayer::ResourceNodes);
	TestTrue(TEXT("the nodes outcome is Ready"), Nodes.IsSet() && Nodes->Kind == EKind::Ready && Nodes->Items == 1);

	// The same raw body again, with the hash the first outcome gave: one worker task, no parse.
	const int32 Launched = F.Worker.Launched;
	F.Bridge.RequestWorld(EWorldLayer::Rails, FrmBridgeWorldAt, Rails->RawHash, Rails->DataHash);
	F.serve(TEXT("/getTrainRails"), SatisDash::Test::Reply(200, FrmBridgeRailsBody));
	F.tick(6);
	const TOptional<FWorldOutcome> Again = F.Bridge.TakeWorld(EWorldLayer::Rails);
	TestTrue(TEXT("SameRaw"), Again.IsSet() && Again->Kind == EKind::SameRaw);
	TestEqual(TEXT("with a single task on the worker"), F.Worker.Launched, Launched + 1);

	// A failed read is an outcome too, so the sender can schedule the next cycle.
	F.Bridge.RequestWorld(EWorldLayer::Rails, FrmBridgeWorldAt, TOptional<FBlake3Hash>(), TOptional<FBlake3Hash>());
	F.serve(TEXT("/getTrainRails"), SatisDash::Test::Reply(503, FString()));
	F.tick(2);
	const TOptional<FWorldOutcome> Failed = F.Bridge.TakeWorld(EWorldLayer::Rails);
	TestTrue(TEXT("ReadFailed"), Failed.IsSet() && Failed->Kind == EKind::ReadFailed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmBridgeWorldStopTest, "SatisDash.Core.FrmBridge.WorldStop", SatisDash::Test::Flags)

bool FFrmBridgeWorldStopTest::RunTest(const FString& Parameters)
{
	FFrmBridgeFixture F;
	F.Bridge.RequestWorld(EWorldLayer::Rails, FrmBridgeWorldAt, TOptional<FBlake3Hash>(), TOptional<FBlake3Hash>());
	F.tick();
	TestEqual(TEXT("the read is in flight"), F.Net.pending(TEXT("/getTrainRails")), 1);
	F.Bridge.RequestWorld(EWorldLayer::ResourceNodes, FrmBridgeWorldAt, TOptional<FBlake3Hash>(), TOptional<FBlake3Hash>());
	F.Bridge.Stop();
	// The stopped run's read is still in flight, so the queue refuses a second one: FRM is not asked, and the layer is not
	// left "requested" (the caller asks again later).
	TestFalse(TEXT("not asked while the stopped run's read is still in flight"), F.Bridge.RequestWorld(EWorldLayer::Rails, FrmBridgeWorldAt, TOptional<FBlake3Hash>(), TOptional<FBlake3Hash>()));
	F.Net.answer(TEXT("/getTrainRails"), SatisDash::Test::Reply(200, FrmBridgeRailsBody));
	F.tick(6);
	TestFalse(TEXT("a reply after Stop is never used"), F.Bridge.TakeWorld(EWorldLayer::Rails).IsSet());
	TestEqual(TEXT("and the queued read was dropped"), F.Net.sent(TEXT("/getResourceNode")), 0);
	TestEqual(TEXT("nothing was read again"), F.Net.sent(TEXT("/getTrainRails")), 1);
	TestTrue(TEXT("after Stop a layer can be asked for again"), F.Bridge.RequestWorld(EWorldLayer::Rails, FrmBridgeWorldAt, TOptional<FBlake3Hash>(), TOptional<FBlake3Hash>()));
	return true;
}
