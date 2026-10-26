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
	TestTrue(TEXT("the bridge still runs for what is not native yet"), F.Net.sent(TEXT("/getFactory")) > 0);

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
		NativeLines += Line.EndsWith(TEXT("Readers: players=native resourceNodes=native factory=bridge trains=bridge")) ? 1 : 0;
		BridgeLines += Line.EndsWith(TEXT("Readers: players=bridge resourceNodes=native factory=bridge trains=bridge")) ? 1 : 0;
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
	F.enrol(Driver, SatisDash::Test::FakeSecret(), TEXT(",\"resourceNodesSource\":\"native\""));

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
		ReadersLines += Line.EndsWith(TEXT("Readers: players=native resourceNodes=native factory=bridge trains=bridge")) ? 1 : 0;
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
	TestTrue(TEXT("default: FRM is asked for the factory"), Default.Factory > 0);
	TestTrue(TEXT("default: FRM is asked for trains"), Default.Trains > 0);
	TestTrue(TEXT("default: FRM is asked for stations"), Default.Stations > 0);
	TestEqual(TEXT("default: players are native"), Default.Players, 0);

	const FAsked FactoryNative = DriverNativeAsk(TEXT(",\"factorySource\":\"native\""));
	TestEqual(TEXT("factory native: no getFactory"), FactoryNative.Factory, 0);
	TestTrue(TEXT("factory native: trains still asked"), FactoryNative.Trains > 0);
	TestTrue(TEXT("factory native: stations still asked"), FactoryNative.Stations > 0);

	const FAsked TrainsNative = DriverNativeAsk(TEXT(",\"trainsSource\":\"native\""));
	TestTrue(TEXT("trains native: factory still asked"), TrainsNative.Factory > 0);
	TestEqual(TEXT("trains native: no getTrains"), TrainsNative.Trains, 0);
	TestEqual(TEXT("trains native: no getTrainStation"), TrainsNative.Stations, 0);

	const FAsked Both = DriverNativeAsk(TEXT(",\"factorySource\":\"native\",\"trainsSource\":\"native\""));
	TestEqual(TEXT("both native: no getFactory"), Both.Factory, 0);
	TestEqual(TEXT("both native: no getTrains"), Both.Trains, 0);
	TestEqual(TEXT("both native: no getTrainStation"), Both.Stations, 0);

	// The players key follows its own source, whatever the others say.
	const FAsked PlayersBridge = DriverNativeAsk(TEXT(",\"playersSource\":\"bridge\",\"factorySource\":\"native\",\"trainsSource\":\"native\""));
	TestTrue(TEXT("players bridge: getPlayer asked"), PlayersBridge.Players > 0);
	TestEqual(TEXT("players bridge: factory still native"), PlayersBridge.Factory, 0);

	int32 AllNative = 0;
	for (const FString& Line : Log.Take())
	{
		AllNative += Line.EndsWith(TEXT("Readers: players=native resourceNodes=native factory=native trains=native")) ? 1 : 0;
	}
	TestEqual(TEXT("one Readers line shows all four native"), AllNative, 1);
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

	// A read still open at the next due time is not restarted.
	F.Game.FactorySteps = 1000000;
	TestTrue(TEXT("the next cadence begins another read"), F.runUntil(Driver, [&F] { return F.Game.FactoryBegins >= 2; }, 120));
	DriverNativeRun(F, Driver, 150);
	TestEqual(TEXT("an open read is not restarted"), F.Game.FactoryBegins, 2);
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
