#include "DriverFakes.h"
#include "SatisDash/Driver.h"

using namespace SatisDash;
using SatisDash::Test::FDriverFixture;

namespace
{
	const TCHAR* DriverNativeSnapshots = TEXT("/agent/v1/snapshots");

	/** Runs the driver for Seconds, answering every open snapshot post with 200; returns the bodies it answered. */
	TArray<FString> DriverNativeRun(FDriverFixture& F, FDriver& Driver, double Seconds)
	{
		TArray<FString> Bodies;
		for (double Done = 0; Done < Seconds; Done += 0.5)
		{
			F.run(Driver, 0.5);
			while (F.Net.pending(DriverNativeSnapshots) > 0)
			{
				Bodies.Add(F.Net.lastBody(DriverNativeSnapshots));
				F.Net.answer(DriverNativeSnapshots, SatisDash::Test::SnapshotOk(false));
			}
		}
		return Bodies;
	}

	bool DriverNativeAny(const TArray<FString>& Bodies, const TCHAR* Needle)
	{
		for (const FString& Body : Bodies)
		{
			if (Body.Contains(Needle))
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverNativePlayersTest, "SatisDash.Core.Driver.NativePlayers", SatisDash::Test::Flags)

bool FDriverNativePlayersTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	FDriverFixture F;
	F.Game.Players = TArray<FPlayerReading>{ { TEXT("player-a"), true }, { TEXT("player-b"), false } };
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	// No key: native is the default since the switch PR (ADR-0058; native-parity runs 40244496806, 40253178598).
	F.enrol(Driver, SatisDash::Test::FakeSecret());

	TArray<FString> Bodies = DriverNativeRun(F, Driver, 20);
	TestTrue(TEXT("snapshots carry the game's players, connected first"),
		DriverNativeAny(Bodies, TEXT("\"players\":{\"available\":true,\"players\":[{\"name\":\"player-a\",\"online\":true},{\"name\":\"player-b\",\"online\":false}]}")));
	TestTrue(TEXT("the game was read on the status cadence"), F.Game.PlayerReads >= 3);
	TestEqual(TEXT("FRM is never asked for players in native mode"), F.Net.sent(TEXT("/getPlayer")), 0);
	TestEqual(TEXT("nor for the factory, which is native by default too"), F.Net.sent(TEXT("/getFactory")), 0);

	// Nobody known: an empty list is still a read list, not "FRM absent".
	F.Game.Players = TArray<FPlayerReading>();
	Bodies = DriverNativeRun(F, Driver, 10);
	TestTrue(TEXT("an empty list is available"), DriverNativeAny(Bodies, TEXT("\"players\":{\"available\":true,\"players\":[]}")));

	// The game state can't be read: the part is left out, and nothing says FRM is absent.
	F.Game.Players.Reset();
	Bodies = DriverNativeRun(F, Driver, 10);
	TestFalse(TEXT("an unreadable list leaves the part out"), DriverNativeAny(Bodies, TEXT("\"players\":")));
	TestFalse(TEXT("and does not claim unavailability"), DriverNativeAny(Bodies, TEXT("\"available\":false")));
	TestEqual(TEXT("still no FRM read for players"), F.Net.sent(TEXT("/getPlayer")), 0);

	// No game running: the list is not read at all (gated like power), and the part is left out.
	F.Game.Players = TArray<FPlayerReading>{ { TEXT("player-a"), true } };
	FStatusReading Idle = F.Game.Status.GetValue();
	Idle.bIsGameRunning = false;
	F.Game.Status = Idle;
	const int32 ReadsBefore = F.Game.PlayerReads;
	Bodies = DriverNativeRun(F, Driver, 10);
	TestEqual(TEXT("no game running: the game's list is not read"), F.Game.PlayerReads, ReadsBefore);
	TestFalse(TEXT("no game running: no players part"), DriverNativeAny(Bodies, TEXT("\"players\":")));

	// The override key still selects the bridge until it is removed: the first pass asks FRM.
	FDriverFixture G;
	G.Game.Players = TArray<FPlayerReading>{ { TEXT("player-a"), true } };
	FDriver Bridge{ G.ConfigPath, G.CredentialPath, G.Net, G.Game, G.Worker, G.clock() };
	G.enrol(Bridge, SatisDash::Test::FakeSecret(), TEXT(",\"playersSource\":\"bridge\""));
	G.run(Bridge, 2);
	TestEqual(TEXT("bridge key: FRM is asked"), G.Net.pending(TEXT("/getPlayer")), 1);
	TestEqual(TEXT("bridge key: the game's list is not read"), G.Game.PlayerReads, 0);

	// The evidence runs tell a native reader from the bridge by one line per Running entry, naming each reader's source.
	int32 NativeLines = 0;
	int32 BridgeLines = 0;
	for (const FString& Line : Log.Take())
	{
		NativeLines += Line.EndsWith(TEXT("Readers: players=native resourceNodes=native factory=native trains=native rails=native conveyors=native mapMarkers=bridge droneRoutes=bridge powerLines=bridge")) ? 1 : 0;
		BridgeLines += Line.EndsWith(TEXT("Readers: players=bridge resourceNodes=native factory=native trains=native rails=native conveyors=native mapMarkers=bridge droneRoutes=bridge powerLines=bridge")) ? 1 : 0;
	}
	TestEqual(TEXT("one Readers line for the default (native players) driver"), NativeLines, 1);
	TestEqual(TEXT("one Readers line for the bridge-key driver"), BridgeLines, 1);
	return true;
}

namespace
{
	const TCHAR* DriverNativeNodesPost = TEXT("/agent/v1/world/resourceNodes");
	const TCHAR* DriverNativeAccepted = TEXT("{\"accepted\":true,\"unchanged\":false}");

	/** Answers snapshots, and every FRM read (for the layers and parts still on the bridge) with a refusal, each tick. */
	void DriverNativeRoutine(FDriverFixture& F)
	{
		while (F.Net.pending(DriverNativeSnapshots) > 0)
		{
			F.Net.answer(DriverNativeSnapshots, SatisDash::Test::SnapshotOk(false));
		}
		for (const TCHAR* Path : { TEXT("/getPlayer"), TEXT("/getFactory"), TEXT("/getTrains"), TEXT("/getTrainStation"), TEXT("/getTrainRails"), TEXT("/getResourceNode") })
		{
			F.Net.answer(Path, SatisDash::Test::Reply(0, FString()));
		}
	}

	/** check-api's function scan can't read parentheses in a parameter list, so the predicate gets a name. */
	using FDriverNativeReached = TFunction<bool()>;

	bool DriverNativeRunUntil(FDriverFixture& F, FDriver& Driver, const FDriverNativeReached& reached, double Seconds)
	{
		for (double Spent = 0; Spent < Seconds; Spent += 0.5)
		{
			DriverNativeRoutine(F);
			if (reached())
			{
				return true;
			}
			F.run(Driver, 0.5);
		}
		DriverNativeRoutine(F);
		return reached();
	}

	struct FAsked { int32 Factory; int32 Trains; int32 Stations; int32 Players; };

	/** One fresh driver per config; FRM refuses every read, so each pair settles and can be asked again. */
	FAsked DriverNativeAsk(const TCHAR* Keys)
	{
		FDriverFixture F;
		F.Game.Players = TArray<FPlayerReading>{ { TEXT("player-a"), true } };
		FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
		F.enrol(Driver, SatisDash::Test::FakeSecret(), Keys);
		DriverNativeRunUntil(F, Driver, []() { return false; }, 20);
		return FAsked{ F.Net.sent(TEXT("/getFactory")), F.Net.sent(TEXT("/getTrains")), F.Net.sent(TEXT("/getTrainStation")), F.Net.sent(TEXT("/getPlayer")) };
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverNativeNodesTest, "SatisDash.Core.Driver.NativeNodes", SatisDash::Test::Flags)

bool FDriverNativeNodesTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	FDriverFixture F;
	FResourceNodeReading Iron;
	Iron.Resource = TEXT("Iron Ore");
	Iron.Purity = ENodePurity::Normal;
	Iron.Kind = ENodeKind::Node;
	Iron.XCm = -12345;
	Iron.YCm = 67890;
	Iron.bExploited = false;
	F.Game.Nodes.Add(Iron);
	F.Game.bHasWorld = false; // no world yet: the scan can't start
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret(), TEXT(",\"resourceNodesSource\":\"native\",\"trainsSource\":\"bridge\",\"railsSource\":\"bridge\",\"conveyorsSource\":\"bridge\"")); // trains, rails and conveyors on the bridge keep their layers off, so only nodes post

	// Ruling F18: no game running (an idle server) means no scan starts, past the first read's due time (528 s) and the log
	// stays quiet; it starts once a game runs.
	FStatusReading IdleStatus = F.Game.Status.GetValue();
	IdleStatus.bIsGameRunning = false;
	F.Game.Status = IdleStatus;
	DriverNativeRunUntil(F, Driver, []() { return false; }, 600);
	TestEqual(TEXT("an idle server starts no scan"), F.Game.ScanBegins, 0);
	int32 ScanLines = 0;
	int32 ReadersLines = 0;
	for (const FString& Line : Log.Take())
	{
		ScanLines += Line.Contains(TEXT("Resource node scan")) || Line.Contains(TEXT("found no nodes")) ? 1 : 0;
		ReadersLines += Line.EndsWith(TEXT("Readers: players=native resourceNodes=native factory=native trains=bridge rails=bridge conveyors=bridge mapMarkers=bridge droneRoutes=bridge powerLines=bridge")) ? 1 : 0;
	}
	TestEqual(TEXT("and logs nothing about a scan"), ScanLines, 0);
	TestEqual(TEXT("one Readers line for the native-nodes driver"), ReadersLines, 1);
	IdleStatus.bIsGameRunning = true;
	F.Game.Status = IdleStatus;

	// The first read was due at 480 s x jitter (the fake clock's Random01 is 1.0, so 528 s). With no world it retries each 30 s.
	TestTrue(TEXT("the scan is attempted once a game runs"), DriverNativeRunUntil(F, Driver, [&F] { return F.Game.ScanBegins >= 1; }, 60));
	TestTrue(TEXT("and retried while there is no world"), DriverNativeRunUntil(F, Driver, [&F] { return F.Game.ScanBegins >= 3; }, 90));
	TestEqual(TEXT("nothing posted without a world"), F.Net.sent(DriverNativeNodesPost), 0);

	// A scan that finds no nodes is "cannot read", never an empty layer (it would replace the backend's good copy): nothing
	// is posted and the scan is retried after NodeScanRetrySeconds.
	F.Game.bHasWorld = true;
	F.Game.Nodes.Reset();
	const int32 EmptyBegins = F.Game.ScanBegins;
	TestTrue(TEXT("an empty scan finishes"), DriverNativeRunUntil(F, Driver, [&F, EmptyBegins] { return F.Game.ScanBegins > EmptyBegins && F.Game.ScanSteps >= F.Game.StepsToFinish; }, 120));
	TestTrue(TEXT("and is retried soon, not after a whole cycle"), DriverNativeRunUntil(F, Driver, [&F, EmptyBegins] { return F.Game.ScanBegins > EmptyBegins + 1; }, 60));
	TestEqual(TEXT("an empty scan posts nothing"), F.Net.sent(DriverNativeNodesPost), 0);

	F.Game.Nodes.Add(Iron);
	TestTrue(TEXT("the layer is posted once the scan finds nodes"), DriverNativeRunUntil(F, Driver, [&F] { return F.Net.pending(DriverNativeNodesPost) > 0; }, 120));
	const FString Body = F.Net.lastBody(DriverNativeNodesPost);
	TestTrue(TEXT("the data is the mapped nodes"), Body.Contains(TEXT("\"data\":[{\"type\":\"Iron Ore\",\"purity\":\"normal\",\"nodeType\":\"node\",\"x\":-123,\"y\":679,\"exploited\":false}]")));
	TestEqual(TEXT("FRM is never asked for resource nodes in native mode"), F.Net.sent(TEXT("/getResourceNode")), 0);
	F.Net.answer(DriverNativeNodesPost, SatisDash::Test::Reply(200, DriverNativeAccepted));

	// The next cycle (1,800 s x jitter later) scans again, finds the same nodes and posts nothing.
	const int32 Begins = F.Game.ScanBegins;
	TestTrue(TEXT("the next cycle scans again"), DriverNativeRunUntil(F, Driver, [&F, Begins] { return F.Game.ScanBegins > Begins; }, 2100));
	DriverNativeRunUntil(F, Driver, []() { return false; }, 30);
	TestEqual(TEXT("an unchanged world is not posted again"), F.Net.sent(DriverNativeNodesPost), 1);

	// A changed world is.
	FResourceNodeReading Oil = Iron;
	Oil.Resource = TEXT("Crude Oil");
	Oil.Kind = ENodeKind::FrackingSatellite;
	F.Game.Nodes.Add(Oil);
	TestTrue(TEXT("a changed layer posts"), DriverNativeRunUntil(F, Driver, [&F] { return F.Net.pending(DriverNativeNodesPost) > 0; }, 2100));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverNativeBridgeFlagsTest, "SatisDash.Core.Driver.NativeBridgeFlags", SatisDash::Test::Flags)

bool FDriverNativeBridgeFlagsTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	const FAsked Default = DriverNativeAsk(TEXT(""));
	TestEqual(TEXT("default: FRM is not asked for the factory"), Default.Factory, 0);
	TestEqual(TEXT("default: FRM is not asked for trains"), Default.Trains, 0);
	TestEqual(TEXT("default: FRM is not asked for stations"), Default.Stations, 0);
	TestEqual(TEXT("default: players are native"), Default.Players, 0);

	const FAsked FactoryBridge = DriverNativeAsk(TEXT(",\"factorySource\":\"bridge\""));
	TestTrue(TEXT("factory bridge: getFactory asked"), FactoryBridge.Factory > 0);
	TestEqual(TEXT("factory bridge: trains still native"), FactoryBridge.Trains, 0);
	TestEqual(TEXT("factory bridge: stations still native"), FactoryBridge.Stations, 0);

	const FAsked TrainsBridge = DriverNativeAsk(TEXT(",\"trainsSource\":\"bridge\""));
	TestEqual(TEXT("trains bridge: factory still native"), TrainsBridge.Factory, 0);
	TestTrue(TEXT("trains bridge: getTrains asked"), TrainsBridge.Trains > 0);
	TestTrue(TEXT("trains bridge: getTrainStation asked"), TrainsBridge.Stations > 0);

	const FAsked Both = DriverNativeAsk(TEXT(",\"factorySource\":\"bridge\",\"trainsSource\":\"bridge\""));
	TestTrue(TEXT("both bridge: getFactory asked"), Both.Factory > 0);
	TestTrue(TEXT("both bridge: getTrains asked"), Both.Trains > 0);
	TestTrue(TEXT("both bridge: getTrainStation asked"), Both.Stations > 0);

	// The players key follows its own source, whatever the others say.
	const FAsked PlayersBridge = DriverNativeAsk(TEXT(",\"playersSource\":\"bridge\""));
	TestTrue(TEXT("players bridge: getPlayer asked"), PlayersBridge.Players > 0);
	TestEqual(TEXT("players bridge: factory still native"), PlayersBridge.Factory, 0);

	int32 AllNative = 0;
	for (const FString& Line : Log.Take())
	{
		AllNative += Line.EndsWith(TEXT("Readers: players=native resourceNodes=native factory=native trains=native rails=native conveyors=native mapMarkers=bridge droneRoutes=bridge powerLines=bridge")) ? 1 : 0;
	}
	TestEqual(TEXT("one Readers line shows the default driver's: six native, the three S4 layers bridge"), AllNative, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverNativeTrainsTest, "SatisDash.Core.Driver.NativeTrains", SatisDash::Test::Flags)

bool FDriverNativeTrainsTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	const TCHAR* Native = TEXT(",\"trainsSource\":\"native\",\"railsSource\":\"bridge\",\"conveyorsSource\":\"bridge\"");
	FDriverFixture F;
	FTrainReading Train;
	Train.Id = TEXT("BP_Train_C_1");
	Train.Name = TEXT("Express");
	Train.State = ETrainState::SelfDriving;
	Train.XCm = 15000;
	Train.YCm = -25000;
	F.Game.Trains = { Train };
	FStationReading Station;
	Station.Id = TEXT("Build_TrainStation_C_1");
	Station.Name = TEXT("Depot ");
	Station.XCm = 500;
	Station.YCm = 700;
	F.Game.Stations = { Station };
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret(), Native);

	// A read that needs 3 steps: begun on a factory-cadence sample pass, stepped once per driver tick, built on the worker after the third.
	TestTrue(TEXT("the first factory-cadence pass begins a read"), F.runUntil(Driver, [&F] { return F.Game.TrainsBegins >= 1; }, 20));
	F.run(Driver, 1.5);
	TestEqual(TEXT("three steps"), F.Game.TrainsStepCalls, 3);
	F.run(Driver, 1.0);
	TestEqual(TEXT("a finished read is not stepped again"), F.Game.TrainsStepCalls, 3);
	TArray<FString> Bodies = DriverNativeRun(F, Driver, 10);
	TestTrue(TEXT("the snapshot carries the mapLive part built from the fake's readings"),
		DriverNativeAny(Bodies, TEXT("\"mapLive\":{\"trains\":[{\"id\":\"BP_Train_C_1\",\"name\":\"Express\",\"x\":150,\"y\":-250,\"status\":\"Self-Driving\"}],\"stations\":[{\"id\":\"Build_TrainStation_C_1\",\"name\":\"Depot \",\"x\":5,\"y\":7}]}")));
	TestEqual(TEXT("FRM is never asked for trains"), F.Net.sent(TEXT("/getTrains")), 0);
	TestEqual(TEXT("or stations"), F.Net.sent(TEXT("/getTrainStation")), 0);

	// The "Trains read" line: counts and times, no ids or names.
	int32 ReadLines = 0;
	int32 Leaks = 0;
	for (const FString& Line : Log.Take())
	{
		ReadLines += Line.Contains(TEXT("Trains read (game thread only): 1 trains, 1 stations in 3 steps, longest step ")) ? 1 : 0;
		Leaks += Line.Contains(TEXT("Depot")) || Line.Contains(TEXT("Express")) || Line.Contains(TEXT("BP_Train")) ? 1 : 0;
	}
	TestTrue(TEXT("the read is logged"), ReadLines >= 1);
	TestEqual(TEXT("with no id or name"), Leaks, 0);

	// A finished read with no trains and no stations is a real, empty railway: empty arrays, never "unavailable", never FRM.
	F.Game.Trains.Reset();
	F.Game.Stations.Reset();
	int32 Begun = F.Game.TrainsBegins;
	TestTrue(TEXT("the next cadence begins a read"), F.runUntil(Driver, [&F, Begun] { return F.Game.TrainsBegins > Begun; }, 120));
	Bodies = DriverNativeRun(F, Driver, 12);
	TestTrue(TEXT("an empty railway is {trains:[],stations:[]}"), DriverNativeAny(Bodies, TEXT("\"mapLive\":{\"trains\":[],\"stations\":[]}")));
	TestFalse(TEXT("and never available:false"), DriverNativeAny(Bodies, TEXT("\"available\":false")));
	TestEqual(TEXT("FRM is still not asked"), F.Net.sent(TEXT("/getTrains")) + F.Net.sent(TEXT("/getTrainStation")), 0);

	// An abandoned read (a world swap) leaves the part out, with a warning, and FRM is not asked.
	// Every case below runs on its own fresh driver, which begins its reads in its first 30 s: the sender's own train-routes read
	// (first due 432 to 528 s after Running) is never in these windows, so each count here is the driver's mapLive pass alone.
	// The first case is the empty one above (F stays under 432 s, the earliest the sender's read can be due).
	TestTrue(TEXT("the empty and basic cases ended before the sender's first read is due"), F.T < 1000.0 + 432.0);

	// An abandoned read (a world swap) leaves the part out, with one warning, and FRM is not asked.
	// FLogCapture::Take is everything since the capture was made, so each case makes its own capture just before it starts.
	SatisDash::Test::FLogCapture HLog;
	FDriverFixture H;
	H.Game.bTrainsAbandon = true;
	H.Game.Trains = { Train };
	FDriver Abandoned{ H.ConfigPath, H.CredentialPath, H.Net, H.Game, H.Worker, H.clock() };
	H.enrol(Abandoned, SatisDash::Test::FakeSecret(), Native);
	TestTrue(TEXT("the abandoned read begins"), H.runUntil(Abandoned, [&H] { return H.Game.TrainsBegins >= 1; }, 20));
	TestFalse(TEXT("an abandoned read leaves the part out"), DriverNativeAny(DriverNativeRun(H, Abandoned, 12), TEXT("\"mapLive\":")));
	TestEqual(TEXT("it was stepped to its end"), H.Game.TrainsStepCalls, 3);
	TestEqual(TEXT("and FRM is not asked"), H.Net.sent(TEXT("/getTrains")) + H.Net.sent(TEXT("/getTrainStation")), 0);
	int32 Warnings = 0;
	for (const FString& Line : HLog.Take())
	{
		Warnings += Line.Contains(TEXT("Couldn't read mapLive from the game")) ? 1 : 0;
	}
	TestEqual(TEXT("one rate-limited warning"), Warnings, 1);

	// A mapLive past its byte cap is left out, with one warning, whatever the cap's arithmetic: 1,000 records of long names.
	SatisDash::Test::FLogCapture CapLog;
	FDriverFixture I;
	for (int32 N = 0; N < 500; ++N)
	{
		FTrainReading Long = Train;
		Long.Id = FString::Printf(TEXT("BP_Train_C_%d"), N);
		Long.Name = FString::ChrN(400, TEXT('n'));
		I.Game.Trains.Add(Long);
		FStationReading Stop = Station;
		Stop.Id = FString::Printf(TEXT("Build_TrainStation_C_%d"), N);
		Stop.Name = FString::ChrN(400, TEXT('s'));
		I.Game.Stations.Add(Stop);
	}
	FDriver Capped{ I.ConfigPath, I.CredentialPath, I.Net, I.Game, I.Worker, I.clock() };
	I.enrol(Capped, SatisDash::Test::FakeSecret(), Native);
	TestTrue(TEXT("the over-cap read begins"), I.runUntil(Capped, [&I] { return I.Game.TrainsBegins >= 1; }, 20));
	TestFalse(TEXT("an over-cap mapLive is left out"), DriverNativeAny(DriverNativeRun(I, Capped, 12), TEXT("\"mapLive\":")));
	TestEqual(TEXT("it was stepped to its end"), I.Game.TrainsStepCalls, 3);
	int32 CapWarnings = 0;
	for (const FString& Line : CapLog.Take())
	{
		CapWarnings += Line.Contains(TEXT("mapLive is over its")) ? 1 : 0;
	}
	TestEqual(TEXT("one over-cap warning"), CapWarnings, 1);

	// A read still open when the source is switched away is dropped and freed (the reader is told).
	FDriverFixture J;
	J.Game.TrainsSteps = 1000000;
	FDriver Switched{ J.ConfigPath, J.CredentialPath, J.Net, J.Game, J.Worker, J.clock() };
	J.enrol(Switched, SatisDash::Test::FakeSecret(), Native);
	TestTrue(TEXT("a read is open"), J.runUntil(Switched, [&J] { return J.Game.TrainsBegins >= 1; }, 20));
	const int32 Ends = J.Game.TrainsEnds;
	J.writeConfig(TEXT("http://127.0.0.1:1"), TEXT("AB3D-7XQ2-K5MN"), TEXT(",\"trainsSource\":\"bridge\""));
	TestTrue(TEXT("a source change ends the open read"), J.runUntil(Switched, [&J, Ends] { return J.Game.TrainsEnds > Ends; }, 120));

	// Contention (one cursor, two consumers). The sender's train-routes read holds the cursor at a factory due time: the driver
	// waits for the next free tick, writes no "Couldn't read mapLive" warning and loses no round.
	SatisDash::Test::FLogCapture KLog;
	FDriverFixture K;
	K.Game.Trains = { Train };
	K.Game.Stations = { Station };
	K.Game.bTrainsOpen = true; // as if the routes read were open
	FDriver Waiting{ K.ConfigPath, K.CredentialPath, K.Net, K.Game, K.Worker, K.clock() };
	K.enrol(Waiting, SatisDash::Test::FakeSecret(), Native);
	TArray<FString> Held = DriverNativeRun(K, Waiting, 20);
	TestEqual(TEXT("the busy cursor is not begun over"), K.Game.TrainsStepCalls, 0);
	TestFalse(TEXT("nothing is sent while it is held"), DriverNativeAny(Held, TEXT("\"mapLive\":")));
	K.Game.bTrainsOpen = false;
	K.run(Waiting, 1.5);
	TestTrue(TEXT("the read starts within a couple of ticks of the release"), K.Game.TrainsStepCalls >= 1);
	TestTrue(TEXT("and mapLive arrives"), DriverNativeAny(DriverNativeRun(K, Waiting, 10), TEXT("\"mapLive\":{\"trains\":[{")));
	int32 FalseWarnings = 0;
	for (const FString& Line : KLog.Take())
	{
		FalseWarnings += Line.Contains(TEXT("Couldn't read mapLive from the game")) ? 1 : 0;
	}
	TestEqual(TEXT("a busy cursor is not a failed read"), FalseWarnings, 0);

	// The other direction: the cursor is held when the routes layer is due (432 to 528 s after Running: 480 s, jitter +/-10%) and released
	// later; the layer does not wait out a flat 30 s retry, it starts on the first free tick, and posts.
	SatisDash::Test::FLogCapture LLog;
	FDriverFixture L;
	L.Game.Trains = { Train };
	L.Game.Stations = { Station };
	L.Game.bTrainsOpen = true;
	FDriver Starved{ L.ConfigPath, L.CredentialPath, L.Net, L.Game, L.Worker, L.clock() };
	L.enrol(Starved, SatisDash::Test::FakeSecret(), Native);
	DriverNativeRun(L, Starved, 540); // past the latest due time, 528 s
	TestEqual(TEXT("held through the routes due time: no routes post yet"), L.Net.sent(TEXT("/agent/v1/world/trainRoutes")), 0);
	L.Game.bTrainsOpen = false;
	TestTrue(TEXT("the routes layer posts soon after the release"), DriverNativeRunUntil(L, Starved, [&L] { return L.Net.pending(TEXT("/agent/v1/world/trainRoutes")) > 0; }, 15));
	int32 StarvedWarnings = 0;
	for (const FString& Line : LLog.Take())
	{
		StarvedWarnings += Line.Contains(TEXT("Couldn't read mapLive from the game")) || Line.Contains(TEXT("train routes read was abandoned")) ? 1 : 0; // the node layer's own "found no nodes" (this fake game has none; its first scan now falls in the window) is not a trains warning
	}
	TestEqual(TEXT("no failed-read warning in either direction"), StarvedWarnings, 0);

	// No world or subsystem: the part is left out and FRM is not asked.
	FDriverFixture G;
	G.Game.bTrainsWorld = false;
	FDriver Quiet{ G.ConfigPath, G.CredentialPath, G.Net, G.Game, G.Worker, G.clock() };
	G.enrol(Quiet, SatisDash::Test::FakeSecret(), Native);
	const TArray<FString> QuietBodies = DriverNativeRun(G, Quiet, 40);
	TestTrue(TEXT("the read was tried"), G.Game.TrainsBegins >= 1);
	TestFalse(TEXT("no mapLive part"), DriverNativeAny(QuietBodies, TEXT("\"mapLive\":")));
	TestEqual(TEXT("FRM is still not asked"), G.Net.sent(TEXT("/getTrains")) + G.Net.sent(TEXT("/getTrainStation")), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverNativeFactoryTest, "SatisDash.Core.Driver.NativeFactory", SatisDash::Test::Flags)

bool FDriverNativeFactoryTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	const TCHAR* Native = TEXT(",\"factorySource\":\"native\"");
	FDriverFixture F;
	FFactoryReading Machine;
	Machine.Id = TEXT("Build_ConstructorMk1_C_77");
	Machine.Name = TEXT("Constructor");
	Machine.ClassName = TEXT("Build_ConstructorMk1_C");
	Machine.bProducing = true;
	F.Game.Factory.Add(Machine);
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret(), Native);

	// A read that needs 3 steps: begun on a sample pass, then stepped once per driver tick, submitted after the third.
	TestTrue(TEXT("the first factory pass begins a read"), F.runUntil(Driver, [&F] { return F.Game.FactoryBegins >= 1; }, 20));
	TestEqual(TEXT("one begin"), F.Game.FactoryBegins, 1);
	F.run(Driver, 0.5);
	TestEqual(TEXT("one step per tick"), F.Game.FactoryStepCalls, 1);
	F.run(Driver, 0.5);
	TestEqual(TEXT("second step"), F.Game.FactoryStepCalls, 2);
	F.run(Driver, 0.5);
	TestEqual(TEXT("third step finishes it"), F.Game.FactoryStepCalls, 3);
	F.run(Driver, 1.0);
	TestEqual(TEXT("a finished read is not stepped again"), F.Game.FactoryStepCalls, 3);
	const TArray<FString> Bodies = DriverNativeRun(F, Driver, 10);
	TestTrue(TEXT("the snapshot carries the fake's machine"), DriverNativeAny(Bodies, TEXT("\"factory\":{\"buildings\":[{\"id\":\"Build_ConstructorMk1_C_77\"")));
	TestEqual(TEXT("FRM is never asked for the factory"), F.Net.sent(TEXT("/getFactory")), 0);

	// A fluid's amounts are scaled in pure code: litres to m3. A solid's are not.
	const FSlotReading Water = MakeSlotReading(TEXT("Desc_Water_C"), 5000, 10000, true);
	TestEqual(TEXT("fluid amount in m3"), Water.Amount, 5.0);
	TestEqual(TEXT("fluid capacity in m3"), Water.MaxAmount, 10.0);
	const FSlotReading Plates = MakeSlotReading(TEXT("Desc_IronPlate_C"), 40, 200, false);
	TestEqual(TEXT("solid amount as is"), Plates.Amount, 40.0);
	TestEqual(TEXT("solid capacity as is"), Plates.MaxAmount, 200.0);

	// The world shrinks mid-read and a machine is listed twice (the real index guard is in GameReader.cpp and the parity run
	// covers it): the part carries each id once and only what the finished read holds.
	FFactoryReading Smelter = Machine;
	Smelter.Id = TEXT("Build_SmelterMk1_C_5");
	F.Game.Factory = TArray<FFactoryReading>{ Machine, Machine, Smelter };
	int32 Begun = F.Game.FactoryBegins;
	TestTrue(TEXT("the next cadence begins a read"), F.runUntil(Driver, [&F, Begun] { return F.Game.FactoryBegins > Begun; }, 120));
	F.run(Driver, 0.5);
	TestEqual(TEXT("one step in"), F.Game.FactoryStepCalls, 1);
	F.Game.Factory = TArray<FFactoryReading>{ Machine, Machine };
	const TArray<FString> Shrunk = DriverNativeRun(F, Driver, 12);
	int32 Copies = -1;
	for (const FString& Body : Shrunk)
	{
		if (Body.Contains(TEXT("\"factory\":{\"buildings\":[")))
		{
			FString Copy = Body;
			Copies = Copy.ReplaceInline(TEXT("\"id\":\"Build_ConstructorMk1_C_77\""), TEXT(""));
			TestFalse(TEXT("the machine that went away is not in the part"), Body.Contains(TEXT("Build_SmelterMk1_C_5")));
		}
	}
	TestEqual(TEXT("a duplicate id comes out once"), Copies, 1);

	// A finished read with no machines is an empty factory, sent as such (a stale part must not stay on the backend), never FRM.
	F.Game.Factory.Reset();
	Begun = F.Game.FactoryBegins;
	TestTrue(TEXT("the next read begins"), F.runUntil(Driver, [&F, Begun] { return F.Game.FactoryBegins > Begun; }, 120));
	TestTrue(TEXT("an empty factory carries buildings:[]"), DriverNativeAny(DriverNativeRun(F, Driver, 12), TEXT("\"factory\":{\"buildings\":[]}")));
	TestEqual(TEXT("and FRM is still not asked"), F.Net.sent(TEXT("/getFactory")), 0);

	// An abandoned read (a world swap) is not an empty factory: the part is left out.
	F.Game.bFactoryAbandon = true;
	F.Game.Factory = TArray<FFactoryReading>{ Machine };
	Begun = F.Game.FactoryBegins;
	TestTrue(TEXT("the next read begins again"), F.runUntil(Driver, [&F, Begun] { return F.Game.FactoryBegins > Begun; }, 120));
	TestFalse(TEXT("an abandoned read leaves the part out"), DriverNativeAny(DriverNativeRun(F, Driver, 12), TEXT("\"factory\":")));
	TestEqual(TEXT("and FRM is not asked"), F.Net.sent(TEXT("/getFactory")), 0);
	F.Game.bFactoryAbandon = false;

	// A read still open at the next due time is not restarted.
	F.Game.FactorySteps = 1000000;
	Begun = F.Game.FactoryBegins;
	TestTrue(TEXT("the next cadence begins another read"), F.runUntil(Driver, [&F, Begun] { return F.Game.FactoryBegins > Begun; }, 120));
	DriverNativeRun(F, Driver, 150);
	TestEqual(TEXT("an open read is not restarted"), F.Game.FactoryBegins, Begun + 1);
	int32 Notes = 0;
	for (const FString& Line : Log.Take())
	{
		Notes += Line.Contains(TEXT("Factory read")) ? 1 : 0;
	}
	TestTrue(TEXT("skipped ticks are noted, rate-limited (one per 60 s, not one per skip)"), Notes >= 1 && Notes <= 3);

	// No world or subsystem: the part is left out, FRM is not asked and "available:false" is not written.
	FDriverFixture G;
	G.Game.bFactoryWorld = false;
	FDriver Quiet{ G.ConfigPath, G.CredentialPath, G.Net, G.Game, G.Worker, G.clock() };
	G.enrol(Quiet, SatisDash::Test::FakeSecret(), Native);
	const TArray<FString> QuietBodies = DriverNativeRun(G, Quiet, 40);
	TestTrue(TEXT("the read was tried"), G.Game.FactoryBegins >= 1);
	TestFalse(TEXT("no factory part"), DriverNativeAny(QuietBodies, TEXT("\"factory\":")));
	TestFalse(TEXT("no available:false"), DriverNativeAny(QuietBodies, TEXT("\"available\":false,\"buildings\"")));
	TestEqual(TEXT("FRM is still not asked"), G.Net.sent(TEXT("/getFactory")), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverNativeLogisticsTest, "SatisDash.Core.Driver.NativeLogistics", SatisDash::Test::Flags)

bool FDriverNativeLogisticsTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	const TCHAR* Native = TEXT(",\"railsSource\":\"native\",\"conveyorsSource\":\"bridge\"");
	const TCHAR* RailsPost = TEXT("/agent/v1/world/rails");
	FDriverFixture F;
	FRailReading Track;
	Track.Id = TEXT("Build_RailroadTrack_C_1");
	Track.Points = { { 0.0, 0.0 }, { 300.0, 0.0 } };
	F.Game.LogisticsRails = { Track };
	F.Game.LogisticsCounts.Buildables = 3;
	F.Game.LogisticsCounts.Tracks = 1;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret(), Native);

	// The driver's jitter draw is 1.0, so the native first read is 480 s x 1.1 = 528 s after Running.
	TestTrue(TEXT("the pass begins through the whole driver"), DriverNativeRunUntil(F, Driver, [&F] { return F.Game.LogisticsBegins > 0; }, 560));
	TestTrue(TEXT("not before the native first read (528 s after Running)"), F.T - 1000.0 >= 528.0);
	TestTrue(TEXT("with the rails flag"), F.Game.bLastBeginRails);
	TestTrue(TEXT("and the layer is posted when it ends"), DriverNativeRunUntil(F, Driver, [&F, RailsPost] { return F.Net.pending(RailsPost) > 0; }, 30));
	TestEqual(TEXT("three steps"), F.Game.LogisticsStepCalls, 3);
	TestTrue(TEXT("the body carries the track"), F.Net.lastBody(RailsPost).Contains(TEXT("\"id\":\"Build_RailroadTrack_C_1\"")));
	TestEqual(TEXT("FRM is never asked for rails while native"), F.Net.sent(TEXT("/getTrainRails")), 0);
	TestTrue(TEXT("the snapshot flow is unchanged"), F.Net.sent(DriverNativeSnapshots) > 0);

	int32 Readers = 0;
	for (const FString& Line : Log.Take())
	{
		Readers += Line.EndsWith(TEXT("Readers: players=native resourceNodes=native factory=native trains=native rails=native conveyors=bridge mapMarkers=bridge droneRoutes=bridge powerLines=bridge")) ? 1 : 0;
	}
	TestEqual(TEXT("the Readers line shows rails=native"), Readers, 1);

	// Leaving Running (a 401 halts the agent) ends an open pass.
	FDriverFixture H;
	H.Game.LogisticsStepsToFinish = 1000000;
	FDriver Halted{ H.ConfigPath, H.CredentialPath, H.Net, H.Game, H.Worker, H.clock() };
	H.enrol(Halted, SatisDash::Test::FakeSecret(), Native);
	TestTrue(TEXT("a pass is open"), DriverNativeRunUntil(H, Halted, [&H] { return H.Game.LogisticsStepCalls >= 1; }, 560));
	TestEqual(TEXT("not ended yet"), H.Game.LogisticsEnds, 0);
	TestTrue(TEXT("a snapshot is waiting for an answer"), H.runUntil(Halted, [&H] { return H.Net.pending(DriverNativeSnapshots) > 0; }, 30));
	H.Net.answer(DriverNativeSnapshots, SatisDash::Test::Reply(401, TEXT("{}")));
	H.run(Halted, 1.0);
	TestTrue(TEXT("leaving Running ends the pass"), H.Game.LogisticsEnds >= 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverNativeMapMarkersTest, "SatisDash.Core.Driver.NativeMapMarkers", SatisDash::Test::Flags)

bool FDriverNativeMapMarkersTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	const TCHAR* Native = TEXT(",\"mapMarkersSource\":\"native\"");
	const TCHAR* MarkersPost = TEXT("/agent/v1/world/mapMarkers");
	FDriverFixture F;
	FMapMarkerReading Marker;
	Marker.Id = TEXT("AAAA0000");
	Marker.Name = TEXT("Base");
	Marker.Icon = 2;
	F.Game.Markers = { Marker };
	F.Game.MarkersCounts.Read = 1;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret(), Native);

	// The driver's jitter draw is 1.0, so the first read is 480 s x 1.1 = 528 s after Running.
	TestTrue(TEXT("the read begins through the whole driver"), DriverNativeRunUntil(F, Driver, [&F] { return F.Game.MarkersBegins > 0; }, 560));
	TestTrue(TEXT("not before 528 s after Running"), F.T - 1000.0 >= 528.0);
	TestTrue(TEXT("and the layer is posted when it ends"), DriverNativeRunUntil(F, Driver, [&F, MarkersPost]
	{
		// The other native layers (nodes, trains, rails, conveyors) fall due with this one and post first: one world post at a time.
		for (const TCHAR* Other : { TEXT("/agent/v1/world/resourceNodes"), TEXT("/agent/v1/world/trainRoutes"), TEXT("/agent/v1/world/rails"), TEXT("/agent/v1/world/conveyors") })
		{
			F.Net.answer(Other, SatisDash::Test::Reply(200, DriverNativeAccepted));
		}
		return F.Net.pending(MarkersPost) > 0;
	}, 60));
	TestEqual(TEXT("three steps"), F.Game.MarkersStepCalls, 3);
	TestTrue(TEXT("the body carries the marker"), F.Net.lastBody(MarkersPost).Contains(TEXT("\"id\":\"AAAA0000\"")));
	TestEqual(TEXT("FRM is never asked for map markers"), F.Net.sent(TEXT("/getMapMarkers")), 0);
	TestTrue(TEXT("the snapshot flow is unchanged"), F.Net.sent(DriverNativeSnapshots) > 0);

	int32 Readers = 0;
	for (const FString& Line : Log.Take())
	{
		Readers += Line.EndsWith(TEXT("mapMarkers=native droneRoutes=bridge powerLines=bridge")) ? 1 : 0;
	}
	TestEqual(TEXT("the Readers line shows mapMarkers=native"), Readers, 1);

	// Leaving Running (a 401 halts the agent) ends an open read.
	FDriverFixture H;
	H.Game.MarkersStepsToFinish = 1000000;
	FDriver Halted{ H.ConfigPath, H.CredentialPath, H.Net, H.Game, H.Worker, H.clock() };
	H.enrol(Halted, SatisDash::Test::FakeSecret(), Native);
	TestTrue(TEXT("a read is open"), DriverNativeRunUntil(H, Halted, [&H] { return H.Game.MarkersStepCalls >= 1; }, 560));
	TestEqual(TEXT("not ended yet"), H.Game.MarkersEnds, 0);
	TestTrue(TEXT("a snapshot is waiting for an answer"), H.runUntil(Halted, [&H] { return H.Net.pending(DriverNativeSnapshots) > 0; }, 30));
	H.Net.answer(DriverNativeSnapshots, SatisDash::Test::Reply(401, TEXT("{}")));
	H.run(Halted, 1.0);
	TestTrue(TEXT("leaving Running ends the read"), H.Game.MarkersEnds >= 1);
	return true;
}
