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
	FDriverFixture F;
	F.Game.Players = TArray<FPlayerReading>{ { TEXT("player-a"), true }, { TEXT("player-b"), false } };
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret(), TEXT(",\"playersSource\":\"native\""));

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

	// The default config (no key) is the bridge, unchanged: the first pass asks FRM.
	FDriverFixture G;
	G.Game.Players = TArray<FPlayerReading>{ { TEXT("player-a"), true } };
	FDriver Bridge{ G.ConfigPath, G.CredentialPath, G.Net, G.Game, G.Worker, G.clock() };
	G.enrol(Bridge, SatisDash::Test::FakeSecret());
	G.run(Bridge, 2);
	TestEqual(TEXT("default: FRM is asked"), G.Net.pending(TEXT("/getPlayer")), 1);
	TestEqual(TEXT("default: the game's list is not read"), G.Game.PlayerReads, 0);
	return true;
}
