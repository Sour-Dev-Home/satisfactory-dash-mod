#include "DriverFakes.h"
#include "SatisDash/Driver.h"
#include "SatisDash/JsonUtil.h"

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

	// Nobody known: an empty list is still a read list, not "unavailable".
	F.Game.Players = TArray<FPlayerReading>();
	Bodies = DriverNativeRun(F, Driver, 10);
	TestTrue(TEXT("an empty list is available"), DriverNativeAny(Bodies, TEXT("\"players\":{\"available\":true,\"players\":[]}")));

	// The game state can't be read: the part is left out, and nothing says it is unavailable.
	F.Game.Players.Reset();
	Bodies = DriverNativeRun(F, Driver, 10);
	TestFalse(TEXT("an unreadable list leaves the part out"), DriverNativeAny(Bodies, TEXT("\"players\":")));
	TestFalse(TEXT("and does not claim unavailability"), DriverNativeAny(Bodies, TEXT("\"available\":false")));

	// No game running: the list is not read at all (gated like power), and the part is left out.
	F.Game.Players = TArray<FPlayerReading>{ { TEXT("player-a"), true } };
	FStatusReading Idle = F.Game.Status.GetValue();
	Idle.bIsGameRunning = false;
	F.Game.Status = Idle;
	const int32 ReadsBefore = F.Game.PlayerReads;
	Bodies = DriverNativeRun(F, Driver, 10);
	TestEqual(TEXT("no game running: the game's list is not read"), F.Game.PlayerReads, ReadsBefore);
	TestFalse(TEXT("no game running: no players part"), DriverNativeAny(Bodies, TEXT("\"players\":")));

	// The evidence runs read one line per Running entry, naming each reader's source. Since R7 every value is native.
	int32 NativeLines = 0;
	for (const FString& Line : Log.Take())
	{
		NativeLines += Line.EndsWith(TEXT("Readers: players=native resourceNodes=native factory=native trains=native rails=native conveyors=native mapMarkers=native droneRoutes=native powerLines=native extractors=native generators=native stationRates=native storage=native")) ? 1 : 0;
	}
	TestEqual(TEXT("one Readers line"), NativeLines, 1);
	return true;
}

namespace
{
	const TCHAR* DriverNativeNodesPost = TEXT("/agent/v1/world/resourceNodes");
	const TCHAR* DriverNativeAccepted = TEXT("{\"accepted\":true,\"unchanged\":false}");

	/** Every world layer's post path. The sender posts one at a time, so a test that watches one layer answers the others. */
	const TCHAR* const DriverNativeWorldPosts[] = {
		TEXT("/agent/v1/world/resourceNodes"), TEXT("/agent/v1/world/trainRoutes"), TEXT("/agent/v1/world/rails"), TEXT("/agent/v1/world/conveyors"),
		TEXT("/agent/v1/world/mapMarkers"), TEXT("/agent/v1/world/droneRoutes"), TEXT("/agent/v1/world/powerLines"),
	};

	/** Answers every open world post except Keep's (null: none is kept, all are answered). */
	void DriverNativeAnswerOthers(FDriverFixture& F, const TCHAR* Keep)
	{
		for (const TCHAR* Path : DriverNativeWorldPosts)
		{
			if (Keep == nullptr || FCString::Strcmp(Path, Keep) != 0)
			{
				F.Net.answer(Path, SatisDash::Test::Reply(200, DriverNativeAccepted));
			}
		}
	}

	/** Answers snapshots each tick. */
	void DriverNativeRoutine(FDriverFixture& F)
	{
		while (F.Net.pending(DriverNativeSnapshots) > 0)
		{
			F.Net.answer(DriverNativeSnapshots, SatisDash::Test::SnapshotOk(false));
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

	/**
	 * Every reader is the game's own since R7, so a test about one reader makes the fake refuse to begin the others (the real reader
	 * refuses the same way when there is no world yet): the sender logs one warning and tries again, and nothing is posted for it.
	 */
	void DriverNativeRefuseLogistics(FDriverFixture& F)
	{
		F.Game.bBeginLogisticsOk = false;
	}
	void DriverNativeRefuseSmallLayers(FDriverFixture& F)
	{
		F.Game.bBeginMarkersOk = false;
		F.Game.bBeginDronesOk = false;
	}
	void DriverNativeRefuseBuildables(FDriverFixture& F)
	{
		F.Game.bBeginBuildablesOk = false;
	}
	/** Refuses every reader but the trains (and the nodes and factory, which post nothing the trains tests look at). */
	void DriverNativeOnlyTrains(FDriverFixture& F)
	{
		DriverNativeRefuseLogistics(F);
		DriverNativeRefuseSmallLayers(F);
		DriverNativeRefuseBuildables(F);
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
	F.Game.bTrainsWorld = false; // the trains, logistics and S4 readers are refused too, so only nodes post
	DriverNativeRefuseLogistics(F);
	DriverNativeRefuseSmallLayers(F);
	DriverNativeRefuseBuildables(F);
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret());

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
		ReadersLines += Line.EndsWith(TEXT("Readers: players=native resourceNodes=native factory=native trains=native rails=native conveyors=native mapMarkers=native droneRoutes=native powerLines=native extractors=native generators=native stationRates=native storage=native")) ? 1 : 0;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverNoFrmTrafficTest, "SatisDash.Core.Driver.NoFrmTraffic", SatisDash::Test::Flags)

bool FDriverNoFrmTrafficTest::RunTest(const FString& Parameters)
{
	// R7: the mod talks only to its backend. Past the world layers' first reads, every request it made went to the backend's agent API.
	SatisDash::Test::FLogCapture Log;
	FDriverFixture F;
	F.Game.Players = TArray<FPlayerReading>{ { TEXT("player-a"), true } };
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	for (double Spent = 0; Spent < 700; Spent += 0.5)
	{
		F.run(Driver, 0.5);
		DriverNativeRoutine(F);
		DriverNativeAnswerOthers(F, nullptr);
	}
	TestTrue(TEXT("requests were made"), F.Net.Sent.Num() > 0);
	TestTrue(TEXT("a world layer was posted"), F.Net.sent(TEXT("/agent/v1/world/trainRoutes")) > 0);
	for (const FRequestSpec& Spec : F.Net.Sent)
	{
		TestTrue(TEXT("every request goes to the backend's agent API"), Spec.Url.StartsWith(TEXT("http://127.0.0.1:1/agent/v1/"), ESearchCase::CaseSensitive));
	}
	int32 AllNative = 0;
	for (const FString& Line : Log.Take())
	{
		AllNative += Line.EndsWith(TEXT("Readers: players=native resourceNodes=native factory=native trains=native rails=native conveyors=native mapMarkers=native droneRoutes=native powerLines=native extractors=native generators=native stationRates=native storage=native")) ? 1 : 0;
	}
	TestEqual(TEXT("one Readers line: all thirteen native, constant since R7"), AllNative, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverNativeTrainsTest, "SatisDash.Core.Driver.NativeTrains", SatisDash::Test::Flags)

bool FDriverNativeTrainsTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	// Every other reader is refused in each fixture below (DriverNativeOnlyTrains), so only the trains reads and their posts run.
	FDriverFixture F;
	DriverNativeOnlyTrains(F);
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
	F.enrol(Driver, SatisDash::Test::FakeSecret());

	// A read that needs 3 steps: begun on a factory-cadence sample pass, stepped once per driver tick, built on the worker after the third.
	TestTrue(TEXT("the first factory-cadence pass begins a read"), F.runUntil(Driver, [&F] { return F.Game.TrainsBegins >= 1; }, 20));
	F.run(Driver, 1.5);
	TestEqual(TEXT("three steps"), F.Game.TrainsStepCalls, 3);
	F.run(Driver, 1.0);
	TestEqual(TEXT("a finished read is not stepped again"), F.Game.TrainsStepCalls, 3);
	TArray<FString> Bodies = DriverNativeRun(F, Driver, 10);
	TestTrue(TEXT("the snapshot carries the mapLive part built from the fake's readings"),
		DriverNativeAny(Bodies, TEXT("\"mapLive\":{\"trains\":[{\"id\":\"BP_Train_C_1\",\"name\":\"Express\",\"x\":150,\"y\":-250,\"status\":\"Self-Driving\"}],\"stations\":[{\"id\":\"Build_TrainStation_C_1\",\"name\":\"Depot \",\"x\":5,\"y\":7}]}")));

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

	// A finished read with no trains and no stations is a real, empty railway: empty arrays, never "unavailable".
	F.Game.Trains.Reset();
	F.Game.Stations.Reset();
	int32 Begun = F.Game.TrainsBegins;
	TestTrue(TEXT("the next cadence begins a read"), F.runUntil(Driver, [&F, Begun] { return F.Game.TrainsBegins > Begun; }, 120));
	Bodies = DriverNativeRun(F, Driver, 12);
	TestTrue(TEXT("an empty railway is {trains:[],stations:[]}"), DriverNativeAny(Bodies, TEXT("\"mapLive\":{\"trains\":[],\"stations\":[]}")));
	TestFalse(TEXT("and never available:false"), DriverNativeAny(Bodies, TEXT("\"available\":false")));

	// Every case below runs on its own fresh driver, which begins its reads in its first 30 s: the sender's own train-routes read
	// (first due 432 to 528 s after Running) is never in these windows, so each count here is the driver's mapLive pass alone.
	// The first case is the empty one above (F stays under 432 s, the earliest the sender's read can be due).
	TestTrue(TEXT("the empty and basic cases ended before the sender's first read is due"), F.T < 1000.0 + 432.0);

	// An abandoned read (a world swap) leaves the part out, with one warning.
	// FLogCapture::Take is everything since the capture was made, so each case makes its own capture just before it starts.
	SatisDash::Test::FLogCapture HLog;
	FDriverFixture H;
	DriverNativeOnlyTrains(H);
	H.Game.bTrainsAbandon = true;
	H.Game.Trains = { Train };
	FDriver Abandoned{ H.ConfigPath, H.CredentialPath, H.Net, H.Game, H.Worker, H.clock() };
	H.enrol(Abandoned, SatisDash::Test::FakeSecret());
	TestTrue(TEXT("the abandoned read begins"), H.runUntil(Abandoned, [&H] { return H.Game.TrainsBegins >= 1; }, 20));
	TestFalse(TEXT("an abandoned read leaves the part out"), DriverNativeAny(DriverNativeRun(H, Abandoned, 12), TEXT("\"mapLive\":")));
	TestEqual(TEXT("it was stepped to its end"), H.Game.TrainsStepCalls, 3);
	int32 Warnings = 0;
	for (const FString& Line : HLog.Take())
	{
		Warnings += Line.Contains(TEXT("Couldn't read mapLive from the game")) ? 1 : 0;
	}
	TestEqual(TEXT("one rate-limited warning"), Warnings, 1);

	// A mapLive past its byte cap is left out, with one warning, whatever the cap's arithmetic: 1,000 records of long names.
	SatisDash::Test::FLogCapture CapLog;
	FDriverFixture I;
	DriverNativeOnlyTrains(I);
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
	I.enrol(Capped, SatisDash::Test::FakeSecret());
	TestTrue(TEXT("the over-cap read begins"), I.runUntil(Capped, [&I] { return I.Game.TrainsBegins >= 1; }, 20));
	TestFalse(TEXT("an over-cap mapLive is left out"), DriverNativeAny(DriverNativeRun(I, Capped, 12), TEXT("\"mapLive\":")));
	TestEqual(TEXT("it was stepped to its end"), I.Game.TrainsStepCalls, 3);
	int32 CapWarnings = 0;
	for (const FString& Line : CapLog.Take())
	{
		CapWarnings += Line.Contains(TEXT("mapLive is over its")) ? 1 : 0;
	}
	TestEqual(TEXT("one over-cap warning"), CapWarnings, 1);

	// Contention (one cursor, two consumers). The sender's train-routes read holds the cursor at a factory due time: the driver
	// waits for the next free tick, writes no "Couldn't read mapLive" warning and loses no round.
	SatisDash::Test::FLogCapture KLog;
	FDriverFixture K;
	DriverNativeOnlyTrains(K);
	K.Game.Trains = { Train };
	K.Game.Stations = { Station };
	K.Game.bTrainsOpen = true; // as if the routes read were open
	FDriver Waiting{ K.ConfigPath, K.CredentialPath, K.Net, K.Game, K.Worker, K.clock() };
	K.enrol(Waiting, SatisDash::Test::FakeSecret());
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
	DriverNativeOnlyTrains(L);
	L.Game.Trains = { Train };
	L.Game.Stations = { Station };
	L.Game.bTrainsOpen = true;
	FDriver Starved{ L.ConfigPath, L.CredentialPath, L.Net, L.Game, L.Worker, L.clock() };
	L.enrol(Starved, SatisDash::Test::FakeSecret());
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

	// No world or subsystem: the part is left out.
	FDriverFixture G;
	DriverNativeOnlyTrains(G);
	G.Game.bTrainsWorld = false;
	FDriver Quiet{ G.ConfigPath, G.CredentialPath, G.Net, G.Game, G.Worker, G.clock() };
	G.enrol(Quiet, SatisDash::Test::FakeSecret());
	const TArray<FString> QuietBodies = DriverNativeRun(G, Quiet, 40);
	TestTrue(TEXT("the read was tried"), G.Game.TrainsBegins >= 1);
	TestFalse(TEXT("no mapLive part"), DriverNativeAny(QuietBodies, TEXT("\"mapLive\":")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverNativeFactoryTest, "SatisDash.Core.Driver.NativeFactory", SatisDash::Test::Flags)

bool FDriverNativeFactoryTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	FDriverFixture F;
	DriverNativeRefuseBuildables(F); // the S4-B readers are native too: the game refuses their pass, so the factory part carries no fragments
	FFactoryReading Machine;
	Machine.Id = TEXT("Build_ConstructorMk1_C_77");
	Machine.Name = TEXT("Constructor");
	Machine.ClassName = TEXT("Build_ConstructorMk1_C");
	Machine.bProducing = true;
	F.Game.Factory.Add(Machine);
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret());

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

	// A finished read with no machines is an empty factory, sent as such (a stale part must not stay on the backend).
	F.Game.Factory.Reset();
	Begun = F.Game.FactoryBegins;
	TestTrue(TEXT("the next read begins"), F.runUntil(Driver, [&F, Begun] { return F.Game.FactoryBegins > Begun; }, 120));
	TestTrue(TEXT("an empty factory carries buildings:[]"), DriverNativeAny(DriverNativeRun(F, Driver, 12), TEXT("\"factory\":{\"buildings\":[]}")));

	// An abandoned read (a world swap) is not an empty factory: the part is left out.
	F.Game.bFactoryAbandon = true;
	F.Game.Factory = TArray<FFactoryReading>{ Machine };
	Begun = F.Game.FactoryBegins;
	TestTrue(TEXT("the next read begins again"), F.runUntil(Driver, [&F, Begun] { return F.Game.FactoryBegins > Begun; }, 120));
	TestFalse(TEXT("an abandoned read leaves the part out"), DriverNativeAny(DriverNativeRun(F, Driver, 12), TEXT("\"factory\":")));
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

	// No world or subsystem: the part is left out and "available:false" is not written.
	FDriverFixture G;
	DriverNativeRefuseBuildables(G);
	G.Game.bFactoryWorld = false;
	FDriver Quiet{ G.ConfigPath, G.CredentialPath, G.Net, G.Game, G.Worker, G.clock() };
	G.enrol(Quiet, SatisDash::Test::FakeSecret());
	const TArray<FString> QuietBodies = DriverNativeRun(G, Quiet, 40);
	TestTrue(TEXT("the read was tried"), G.Game.FactoryBegins >= 1);
	TestFalse(TEXT("no factory part"), DriverNativeAny(QuietBodies, TEXT("\"factory\":")));
	TestFalse(TEXT("no available:false"), DriverNativeAny(QuietBodies, TEXT("\"available\":false,\"buildings\"")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverNativeLogisticsTest, "SatisDash.Core.Driver.NativeLogistics", SatisDash::Test::Flags)

bool FDriverNativeLogisticsTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	const TCHAR* RailsPost = TEXT("/agent/v1/world/rails");
	FDriverFixture F;
	DriverNativeRefuseSmallLayers(F);
	DriverNativeRefuseBuildables(F);
	FRailReading Track;
	Track.Id = TEXT("Build_RailroadTrack_C_1");
	Track.Points = { { 0.0, 0.0 }, { 300.0, 0.0 } };
	F.Game.LogisticsRails = { Track };
	F.Game.LogisticsCounts.Buildables = 3;
	F.Game.LogisticsCounts.Tracks = 1;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret());

	// The driver's jitter draw is 1.0, so the native first read is 480 s x 1.1 = 528 s after Running.
	TestTrue(TEXT("the pass begins through the whole driver"), DriverNativeRunUntil(F, Driver, [&F] { return F.Game.LogisticsBegins > 0; }, 560));
	TestTrue(TEXT("not before the native first read (528 s after Running)"), F.T - 1000.0 >= 528.0);
	TestTrue(TEXT("with the rails flag"), F.Game.bLastBeginRails);
	TestTrue(TEXT("and the layer is posted when it ends"), DriverNativeRunUntil(F, Driver, [&F, RailsPost]
	{
		DriverNativeAnswerOthers(F, RailsPost); // the other native layers fall due with this one: one world post at a time
		return F.Net.pending(RailsPost) > 0;
	}, 30));
	TestEqual(TEXT("three steps"), F.Game.LogisticsStepCalls, 3);
	TestTrue(TEXT("the body carries the track"), F.Net.lastBody(RailsPost).Contains(TEXT("\"id\":\"Build_RailroadTrack_C_1\"")));
	TestTrue(TEXT("the snapshot flow is unchanged"), F.Net.sent(DriverNativeSnapshots) > 0);

	int32 Readers = 0;
	for (const FString& Line : Log.Take())
	{
		Readers += Line.EndsWith(TEXT("Readers: players=native resourceNodes=native factory=native trains=native rails=native conveyors=native mapMarkers=native droneRoutes=native powerLines=native extractors=native generators=native stationRates=native storage=native")) ? 1 : 0;
	}
	TestEqual(TEXT("the Readers line shows rails=native"), Readers, 1);

	// Leaving Running (a 401 halts the agent) ends an open pass.
	FDriverFixture H;
	DriverNativeRefuseSmallLayers(H);
	DriverNativeRefuseBuildables(H);
	H.Game.LogisticsStepsToFinish = 1000000;
	FDriver Halted{ H.ConfigPath, H.CredentialPath, H.Net, H.Game, H.Worker, H.clock() };
	H.enrol(Halted, SatisDash::Test::FakeSecret());
	TestTrue(TEXT("a pass is open"), DriverNativeRunUntil(H, Halted, [&H] { return H.Game.LogisticsStepCalls >= 1; }, 560));
	TestEqual(TEXT("not ended yet"), H.Game.LogisticsEnds, 0);
	TestTrue(TEXT("a snapshot is waiting for an answer"), H.runUntil(Halted, [&H] { return H.Net.pending(DriverNativeSnapshots) > 0; }, 30));
	H.Net.answer(DriverNativeSnapshots, SatisDash::Test::Reply(401, TEXT("{}")));
	H.run(Halted, 1.0);
	TestTrue(TEXT("leaving Running ends the pass"), H.Game.LogisticsEnds >= 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverNativePowerLinesTest, "SatisDash.Core.Driver.NativeLogisticsPowerLines", SatisDash::Test::Flags)

bool FDriverNativePowerLinesTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	const TCHAR* PowerLinesPost = TEXT("/agent/v1/world/powerLines");
	FDriverFixture F;
	DriverNativeRefuseSmallLayers(F);
	DriverNativeRefuseBuildables(F);
	FPowerLineReading Wire;
	Wire.Id = TEXT("Build_PowerLine_C_1");
	Wire.X1Cm = 1000.0;
	F.Game.LogisticsPowerLines = { Wire };
	F.Game.LogisticsCounts.Buildables = 3;
	F.Game.LogisticsCounts.Wires = 1;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret());

	// The driver's jitter draw is 1.0, so the first read is 480 s x 1.1 = 528 s after Running.
	TestTrue(TEXT("the pass begins through the whole driver"), DriverNativeRunUntil(F, Driver, [&F] { return F.Game.LogisticsBegins > 0; }, 560));
	TestTrue(TEXT("not before 528 s after Running"), F.T - 1000.0 >= 528.0);
	TestTrue(TEXT("with the powerLines flag, in the one pass with rails and conveyors"), F.Game.bLastBeginPowerLines && F.Game.bLastBeginRails && F.Game.bLastBeginConveyors);
	TestTrue(TEXT("and the layer is posted when it ends"), DriverNativeRunUntil(F, Driver, [&F, PowerLinesPost]
	{
		// The other native layers (nodes, trains, rails, conveyors) fall due with this one and post first: one world post at a time.
		DriverNativeAnswerOthers(F, PowerLinesPost);
		return F.Net.pending(PowerLinesPost) > 0;
	}, 60));
	TestEqual(TEXT("three steps"), F.Game.LogisticsStepCalls, 3);
	TestTrue(TEXT("the body carries the wire"), F.Net.lastBody(PowerLinesPost).Contains(TEXT("\"id\":\"Build_PowerLine_C_1\"")));
	TestTrue(TEXT("the snapshot flow is unchanged"), F.Net.sent(DriverNativeSnapshots) > 0);

	int32 Readers = 0;
	for (const FString& Line : Log.Take())
	{
		Readers += Line.EndsWith(TEXT("rails=native conveyors=native mapMarkers=native droneRoutes=native powerLines=native extractors=native generators=native stationRates=native storage=native")) ? 1 : 0;
	}
	TestEqual(TEXT("the Readers line shows powerLines=native"), Readers, 1);

	// Leaving Running (a 401 halts the agent) ends an open pass.
	FDriverFixture H;
	DriverNativeRefuseSmallLayers(H);
	DriverNativeRefuseBuildables(H);
	H.Game.LogisticsStepsToFinish = 1000000;
	FDriver Halted{ H.ConfigPath, H.CredentialPath, H.Net, H.Game, H.Worker, H.clock() };
	H.enrol(Halted, SatisDash::Test::FakeSecret());
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
	const TCHAR* MarkersPost = TEXT("/agent/v1/world/mapMarkers");
	FDriverFixture F;
	F.Game.bBeginDronesOk = false;
	DriverNativeRefuseBuildables(F);
	FMapMarkerReading Marker;
	Marker.Id = TEXT("AAAA0000");
	Marker.Name = TEXT("Base");
	Marker.Icon = 2;
	F.Game.Markers = { Marker };
	F.Game.MarkersCounts.Read = 1;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret());

	// The driver's jitter draw is 1.0, so the first read is 480 s x 1.1 = 528 s after Running.
	TestTrue(TEXT("the read begins through the whole driver"), DriverNativeRunUntil(F, Driver, [&F] { return F.Game.MarkersBegins > 0; }, 560));
	TestTrue(TEXT("not before 528 s after Running"), F.T - 1000.0 >= 528.0);
	TestTrue(TEXT("and the layer is posted when it ends"), DriverNativeRunUntil(F, Driver, [&F, MarkersPost]
	{
		// The other native layers (nodes, trains, rails, conveyors) fall due with this one and post first: one world post at a time.
		DriverNativeAnswerOthers(F, MarkersPost);
		return F.Net.pending(MarkersPost) > 0;
	}, 60));
	TestEqual(TEXT("three steps"), F.Game.MarkersStepCalls, 3);
	TestTrue(TEXT("the body carries the marker"), F.Net.lastBody(MarkersPost).Contains(TEXT("\"id\":\"AAAA0000\"")));
	TestTrue(TEXT("the snapshot flow is unchanged"), F.Net.sent(DriverNativeSnapshots) > 0);

	int32 Readers = 0;
	for (const FString& Line : Log.Take())
	{
		Readers += Line.EndsWith(TEXT("mapMarkers=native droneRoutes=native powerLines=native extractors=native generators=native stationRates=native storage=native")) ? 1 : 0;
	}
	TestEqual(TEXT("the Readers line shows mapMarkers=native"), Readers, 1);

	// Leaving Running (a 401 halts the agent) ends an open read.
	FDriverFixture H;
	DriverNativeRefuseBuildables(H);
	H.Game.MarkersStepsToFinish = 1000000;
	H.Game.DronesStepsToFinish = 1000000;
	FDriver Halted{ H.ConfigPath, H.CredentialPath, H.Net, H.Game, H.Worker, H.clock() };
	H.enrol(Halted, SatisDash::Test::FakeSecret());
	TestTrue(TEXT("a read is open"), DriverNativeRunUntil(H, Halted, [&H] { return H.Game.MarkersStepCalls >= 1; }, 560));
	TestEqual(TEXT("not ended yet"), H.Game.MarkersEnds, 0);
	TestTrue(TEXT("the drone-routes read is open beside it"), DriverNativeRunUntil(H, Halted, [&H] { return H.Game.DronesStepCalls >= 1; }, 60));
	TestEqual(TEXT("and not ended yet"), H.Game.DronesEnds, 0);
	TestTrue(TEXT("a snapshot is waiting for an answer"), H.runUntil(Halted, [&H] { return H.Net.pending(DriverNativeSnapshots) > 0; }, 30));
	H.Net.answer(DriverNativeSnapshots, SatisDash::Test::Reply(401, TEXT("{}")));
	H.run(Halted, 1.0);
	TestTrue(TEXT("leaving Running ends the read"), H.Game.MarkersEnds >= 1);
	TestTrue(TEXT("and the drone-routes read"), H.Game.DronesEnds >= 1);
	int32 BothNative = 0;
	for (const FString& Line : Log.Take())
	{
		BothNative += Line.EndsWith(TEXT("mapMarkers=native droneRoutes=native powerLines=native extractors=native generators=native stationRates=native storage=native")) ? 1 : 0;
	}
	TestEqual(TEXT("the Readers line shows droneRoutes=native"), BothNative, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverNativeBuildablesTest, "SatisDash.Core.Driver.NativeBuildables", SatisDash::Test::Flags)

bool FDriverNativeBuildablesTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	FDriverFixture F;
	FExtractorReading Miner;
	Miner.Id = TEXT("Build_MinerMk1_C_1");
	Miner.Name = TEXT("Miner");
	Miner.ClassName = TEXT("Build_MinerMk1_C");
	F.Game.Extractors.Add(Miner);
	F.Game.BuildablesCounts.Elements = 4;
	F.Game.BuildablesCounts.Extractors = 1;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	TestEqual(TEXT("nothing begins before the first due tick"), F.Game.BuildablesBegins, 0);

	// Begun once on the cadence tick, then stepped once per driver tick; a finished read is not stepped again.
	TestTrue(TEXT("the first factory-cadence tick begins a pass"), F.runUntil(Driver, [&F] { return F.Game.BuildablesBegins >= 1; }, 20));
	TestEqual(TEXT("one begin"), F.Game.BuildablesBegins, 1);
	TestTrue(TEXT("with the extractors, generators and storage flags"), F.Game.LastBuildablesFlags.bExtractors && F.Game.LastBuildablesFlags.bGenerators && F.Game.LastBuildablesFlags.bStorage);
	F.run(Driver, 0.5);
	TestEqual(TEXT("one step per tick"), F.Game.BuildablesStepCalls, 1);
	F.run(Driver, 0.5);
	TestEqual(TEXT("second step"), F.Game.BuildablesStepCalls, 2);
	F.run(Driver, 0.5);
	TestEqual(TEXT("third step finishes it"), F.Game.BuildablesStepCalls, 3);
	F.run(Driver, 1.0);
	TestEqual(TEXT("a finished pass is not stepped again"), F.Game.BuildablesStepCalls, 3);
	const TArray<FString> Bodies = DriverNativeRun(F, Driver, 10);
	TestTrue(TEXT("the factory part carries the extractors fragment"), DriverNativeAny(Bodies, TEXT("\"extractors\":[{\"id\":\"Build_MinerMk1_C_1\"")));
	TestEqual(TEXT("still one begin inside the cadence"), F.Game.BuildablesBegins, 1);
	TestTrue(TEXT("the next cadence tick begins another"), F.runUntil(Driver, [&F] { return F.Game.BuildablesBegins >= 2; }, 60));

	int32 ReadLines = 0;
	for (const FString& Line : Log.Take())
	{
		ReadLines += Line.Contains(TEXT("Buildables read (game thread only): 4 elements, 1 extractors, 0 generators, 0 containers in 3 steps, longest step ")) ? 1 : 0;
		TestFalse(TEXT("the log line names no id or name"), Line.Contains(TEXT("Build_MinerMk1_C_1")) && Line.Contains(TEXT("Buildables read")));
	}
	TestTrue(TEXT("a finished pass logs counts and times"), ReadLines >= 1);

	// A second due tick while the pass is open is skipped, not restarted, and noted (rate-limited).
	FDriverFixture G;
	G.Game.BuildablesStepsToFinish = 1000000;
	FDriver Slow{ G.ConfigPath, G.CredentialPath, G.Net, G.Game, G.Worker, G.clock() };
	G.enrol(Slow, SatisDash::Test::FakeSecret());
	TestTrue(TEXT("a pass is open"), G.runUntil(Slow, [&G] { return G.Game.BuildablesStepCalls >= 1; }, 20));
	DriverNativeRun(G, Slow, 150);
	TestEqual(TEXT("an open pass is not restarted"), G.Game.BuildablesBegins, 1);
	int32 Notes = 0;
	for (const FString& Line : Log.Take())
	{
		Notes += Line.Contains(TEXT("Buildables read still running")) ? 1 : 0;
	}
	TestTrue(TEXT("skipped ticks are noted, rate-limited"), Notes >= 1 && Notes <= 3);

	// An abandoned pass posts nothing and warns once.
	FDriverFixture A;
	A.Game.bBuildablesAbandoned = true;
	FDriver Gone{ A.ConfigPath, A.CredentialPath, A.Net, A.Game, A.Worker, A.clock() };
	A.enrol(Gone, SatisDash::Test::FakeSecret());
	const TArray<FString> GoneBodies = DriverNativeRun(A, Gone, 80);
	TestTrue(TEXT("the pass was tried"), A.Game.BuildablesBegins >= 1);
	TestFalse(TEXT("an abandoned pass posts no extractors"), DriverNativeAny(GoneBodies, TEXT("\"extractors\":")));
	int32 Warned = 0;
	for (const FString& Line : Log.Take())
	{
		Warned += Line.Contains(TEXT("Couldn't read buildables")) ? 1 : 0;
	}
	TestEqual(TEXT("one warning"), Warned, 1);

	// A pass over zero buildables (a save still loading) is reported abandoned by the game reader: nothing is posted, so the
	// dashboard keeps the last extractors, and the factory part itself still goes out unchanged.
	FDriverFixture Z;
	Z.Game.bBuildablesAbandoned = true;
	Z.Game.BuildablesCounts.Elements = 0;
	Z.Game.Extractors.Add(Miner);
	FDriver Empty{ Z.ConfigPath, Z.CredentialPath, Z.Net, Z.Game, Z.Worker, Z.clock() };
	Z.enrol(Empty, SatisDash::Test::FakeSecret());
	const TArray<FString> EmptyBodies = DriverNativeRun(Z, Empty, 80);
	TestTrue(TEXT("the empty pass was tried"), Z.Game.BuildablesBegins >= 1);
	TestFalse(TEXT("an empty (abandoned) pass posts no extractors"), DriverNativeAny(EmptyBodies, TEXT("\"extractors\":")));
	TestTrue(TEXT("the factory part is unchanged"), DriverNativeAny(EmptyBodies, TEXT("\"factory\":{\"buildings\":[]}")));
	Log.Take();

	// Leaving Running (a 401 halts the agent) ends an open pass.
	FDriverFixture H;
	H.Game.BuildablesStepsToFinish = 1000000;
	FDriver Halted{ H.ConfigPath, H.CredentialPath, H.Net, H.Game, H.Worker, H.clock() };
	H.enrol(Halted, SatisDash::Test::FakeSecret());
	TestTrue(TEXT("a pass is open"), H.runUntil(Halted, [&H] { return H.Game.BuildablesStepCalls >= 1; }, 20));
	TestEqual(TEXT("not ended yet"), H.Game.BuildablesEnds, 0);
	TestTrue(TEXT("a snapshot is waiting for an answer"), H.runUntil(Halted, [&H] { return H.Net.pending(DriverNativeSnapshots) > 0; }, 30));
	H.Net.answer(DriverNativeSnapshots, SatisDash::Test::Reply(401, TEXT("{}")));
	H.run(Halted, 1.0);
	TestTrue(TEXT("leaving Running ends the pass"), H.Game.BuildablesEnds >= 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverNativeGeneratorsTest, "SatisDash.Core.Driver.NativeGenerators", SatisDash::Test::Flags)

bool FDriverNativeGeneratorsTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	const TCHAR* InPower = TEXT("\"power\":{\"circuits\":[");
	const TCHAR* Attached = TEXT("],\"generators\":[{\"id\":\"Test_Coal_1\"");
	FGeneratorReading Coal;
	Coal.Id = TEXT("Test_Coal_1");
	Coal.Name = TEXT("Coal");
	Coal.ClassName = TEXT("Build_GeneratorCoal_C");
	Coal.CapacityMW = 75;

	// A finished read rides inside power, on the next power part only.
	FDriverFixture F;
	F.Game.Generators.Add(Coal);
	F.Game.BuildablesCounts.Elements = 4;
	F.Game.BuildablesCounts.Generators = 1;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	TestTrue(TEXT("the first factory-cadence tick begins a pass"), F.runUntil(Driver, [&F] { return F.Game.BuildablesBegins >= 1; }, 20));
	TestTrue(TEXT("with the generators flag"), F.Game.LastBuildablesFlags.bGenerators);
	const TArray<FString> Early = DriverNativeRun(F, Driver, 20);
	TestTrue(TEXT("the generators are cast on every step"), F.Game.GeneratorCasts >= 3);
	int32 WithFragment = 0, PowerParts = 0;
	for (const FString& Body : Early)
	{
		PowerParts += Body.Contains(InPower) ? 1 : 0;
		WithFragment += Body.Contains(Attached) ? 1 : 0;
		TestFalse(TEXT("no top-level generators key"), Body.Contains(TEXT("}},\"generators\":")));
	}
	TestEqual(TEXT("the fragment is inside power on exactly one power part: the next one after the read (once per read)"), WithFragment, 1);
	TestTrue(TEXT("and later power parts omit the generators key"), PowerParts >= 2);
	TestTrue(TEXT("the factory part is sent"), DriverNativeAny(Early, TEXT("\"factory\":{\"buildings\":[]")));
	TestTrue(TEXT("the body parses as JSON with generators inside power"), [&Early, Attached]
	{
		for (const FString& Body : Early)
		{
			if (Body.Contains(Attached))
			{
				const TSharedPtr<FJsonObject> Parsed = SatisDash::Json::ParseObject(Body);
				return Parsed.IsValid() && Parsed->HasTypedField<EJson::Object>(TEXT("power")) && Parsed->GetObjectField(TEXT("power"))->HasTypedField<EJson::Array>(TEXT("generators"));
			}
		}
		return false;
	}());

	// The next factory-cadence read (30 s) attaches again, on one power part; every other power part omits the key.
	const TArray<FString> Next = DriverNativeRun(F, Driver, 40);
	int32 NextWith = 0, NextPower = 0;
	for (const FString& Body : Next)
	{
		NextPower += Body.Contains(InPower) ? 1 : 0;
		NextWith += Body.Contains(Attached) ? 1 : 0;
	}
	TestTrue(TEXT("the next read is attached again"), NextWith >= 1);
	TestTrue(TEXT("but not on every power part"), NextPower > NextWith);

	// No new read: the key stays gone (nothing is held over), and the power part is still sent.
	F.Game.bBeginBuildablesOk = false;
	DriverNativeRun(F, Driver, 100);
	const TArray<FString> Late = DriverNativeRun(F, Driver, 20);
	int32 LatePower = 0;
	for (const FString& Body : Late)
	{
		LatePower += Body.Contains(InPower) ? 1 : 0;
	}
	TestTrue(TEXT("power parts are still sent"), LatePower >= 1);
	TestFalse(TEXT("but with no read since, no generators key"), DriverNativeAny(Late, TEXT("\"generators\":")));

	// One pass serves all three Buildables readers: every flag on, each reader cast on every step.
	TestTrue(TEXT("the flags are all on"), F.Game.LastBuildablesFlags.bGenerators && F.Game.LastBuildablesFlags.bExtractors && F.Game.LastBuildablesFlags.bStorage);
	TestTrue(TEXT("every reader is cast in the passes"), F.Game.ExtractorCasts >= 3 && F.Game.ContainerCasts >= 3);
	Log.Take();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverNativeStorageTest, "SatisDash.Core.Driver.NativeStorage", SatisDash::Test::Flags)

bool FDriverNativeStorageTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::FLogCapture Log;
	const TCHAR* Part = TEXT(",\"storage\":{\"containers\":[{\"id\":\"Test_Box_1\"");
	FContainerReading Box;
	Box.Id = TEXT("Test_Box_1");
	Box.Name = TEXT("Box");
	Box.ClassName = TEXT("Build_Box_C");
	Box.Slots = 8;
	const auto Count = [](const TArray<FString>& Bodies, const TCHAR* Needle)
	{
		int32 Found = 0;
		for (const FString& Body : Bodies)
		{
			Found += Body.Contains(Needle) ? 1 : 0;
		}
		return Found;
	};

	// The part rides one snapshot per read, then is gone until the next read.
	FDriverFixture F;
	F.Game.Containers.Add(Box);
	F.Game.BuildablesCounts.Elements = 4;
	F.Game.BuildablesCounts.Containers = 1;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	TestTrue(TEXT("the first factory-cadence tick begins a pass"), F.runUntil(Driver, [&F] { return F.Game.BuildablesBegins >= 1; }, 20));
	TestTrue(TEXT("with the storage flag"), F.Game.LastBuildablesFlags.bStorage);
	const TArray<FString> First = DriverNativeRun(F, Driver, 12);
	TestEqual(TEXT("the part is on exactly one snapshot of the read"), Count(First, Part), 1);
	TestTrue(TEXT("and other snapshots of that read are sent without it"), First.Num() >= 2);
	TestTrue(TEXT("the containers are cast"), F.Game.ContainerCasts >= 3);
	TestTrue(TEXT("the body parses as JSON with a top-level storage object"), [&First, Part]
	{
		for (const FString& Body : First)
		{
			if (Body.Contains(Part))
			{
				const TSharedPtr<FJsonObject> Parsed = SatisDash::Json::ParseObject(Body);
				return Parsed.IsValid() && Parsed->HasTypedField<EJson::Object>(TEXT("storage")) && Parsed->GetObjectField(TEXT("storage"))->GetArrayField(TEXT("containers")).Num() == 1;
			}
		}
		return false;
	}());
	TestEqual(TEXT("not again before the next read"), Count(DriverNativeRun(F, Driver, 8), Part), 0);
	TestTrue(TEXT("the next read sends it again"), Count(DriverNativeRun(F, Driver, 60), Part) >= 1);

	// A failed status read: the fragment is built but not written (unreachable), and it goes out with the first reachable snapshot.
	FDriverFixture U;
	U.Game.Containers.Add(Box);
	U.Game.BuildablesCounts.Containers = 1;
	FDriver Unreachable{ U.ConfigPath, U.CredentialPath, U.Net, U.Game, U.Worker, U.clock() };
	U.enrol(Unreachable, SatisDash::Test::FakeSecret());
	TestTrue(TEXT("a pass begins"), U.runUntil(Unreachable, [&U] { return U.Game.BuildablesBegins >= 1; }, 20));
	const TOptional<FStatusReading> Healthy = U.Game.Status;
	U.Game.Status.Reset();
	TestEqual(TEXT("no snapshot without a status carries storage"), Count(DriverNativeRun(U, Unreachable, 10), TEXT("\"storage\":")), 0);
	U.Game.Status = Healthy;
	TestEqual(TEXT("the first one with a status does, once"), Count(DriverNativeRun(U, Unreachable, 6), Part), 1);

	// A fragment held through a long outage (older than 1.5 x FactorySeconds) is dropped, not sent under a fresh observedAt.
	FDriverFixture S;
	S.Game.Containers.Add(Box);
	S.Game.BuildablesCounts.Containers = 1;
	FDriver Stale{ S.ConfigPath, S.CredentialPath, S.Net, S.Game, S.Worker, S.clock() };
	S.enrol(Stale, SatisDash::Test::FakeSecret());
	TestTrue(TEXT("a pass begins before the outage"), S.runUntil(Stale, [&S] { return S.Game.BuildablesBegins >= 1; }, 20));
	const TOptional<FStatusReading> Back = S.Game.Status;
	S.Game.Status.Reset();
	DriverNativeRun(S, Stale, 60);
	S.Game.Status = Back;
	const TArray<FString> AfterOutage = DriverNativeRun(S, Stale, 12);
	const FString* FirstWithStatus = AfterOutage.FindByPredicate([](const FString& Body) { return Body.Contains(TEXT("\"status\":")); });
	TestTrue(TEXT("snapshots with a status resume"), FirstWithStatus != nullptr);
	TestFalse(TEXT("the first one carries no stale storage"), FirstWithStatus != nullptr && FirstWithStatus->Contains(TEXT("\"storage\":")));

	Log.Take();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverStepDiagnosticsTest, "SatisDash.Core.Driver.StepDiagnostics", SatisDash::Test::Flags)

bool FDriverStepDiagnosticsTest::RunTest(const FString& Parameters)
{
	// Owner 2026-11-01: the thread cycle source is asked only when the config says "stepDiagnostics": true. Off, it is never called.
	SatisDash::Test::FLogCapture Log;
	for (const bool bOn : { false, true })
	{
		FContainerReading Box;
		Box.Id = TEXT("Test_Box_1");
		Box.ClassName = TEXT("Build_Box_C");
		FDriverFixture F;
		F.Game.Containers.Add(Box);
		F.Game.BuildablesCounts.Containers = 1;
		int32 CycleCalls = 0;
		SatisDash::FDriverClock Clock = F.clock();
		Clock.ThreadCycles = [&CycleCalls]() -> TOptional<uint64> { return TOptional<uint64>(uint64(++CycleCalls) * 1000); };
		FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, MoveTemp(Clock) };
		F.enrol(Driver, SatisDash::Test::FakeSecret(), bOn ? TEXT(",\"stepDiagnostics\":true") : TEXT(""));
		TestTrue(TEXT("a Buildables pass begins"), F.runUntil(Driver, [&F] { return F.Game.BuildablesBegins >= 1; }, 20));
		DriverNativeRun(F, Driver, 40);
		TestTrue(TEXT("the pass was stepped (the budget began and ended each step)"), F.Game.ContainerCasts >= 1);
		if (bOn)
		{
			TestTrue(TEXT("stepDiagnostics on: the cycle source is asked at each step's begin and end"), CycleCalls >= 2 && CycleCalls % 2 == 0);
		}
		else
		{
			TestEqual(TEXT("stepDiagnostics off: the cycle source is never asked"), CycleCalls, 0);
		}
	}
	Log.Take();
	return true;
}
