#include "SatisDash/Native/Players/NativePlayers.h"
#include "TestUtil.h"

using namespace SatisDash;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativePlayersMapTest, "SatisDash.Core.NativePlayers.Map", SatisDash::Test::Flags)

bool FNativePlayersMapTest::RunTest(const FString& Parameters)
{
	TArray<FPlayerReading> In;
	In.Add({ TEXT("player-b"), false });
	In.Add({ TEXT("player-a"), true });
	const FPlayersPart Part = NativePlayersPart(In);
	TestTrue(TEXT("a read list is available, whoever is in it"), Part.bAvailable);
	TestEqual(TEXT("both kept"), Part.Players.Num(), 2);
	TestEqual(TEXT("connected players first"), Part.Players[0].Name, FString(TEXT("player-a")));
	TestTrue(TEXT("and online"), Part.Players[0].bOnline);
	TestFalse(TEXT("the remembered one is offline"), Part.Players[1].bOnline);
	TestEqual(TEXT("the JSON is the part the bridge sends"), PlayersToJson(Part),
		FString(TEXT("{\"available\":true,\"players\":[{\"name\":\"player-a\",\"online\":true},{\"name\":\"player-b\",\"online\":false}]}")));

	const FPlayersPart Empty = NativePlayersPart({});
	TestTrue(TEXT("nobody known: still available"), Empty.bAvailable);
	TestEqual(TEXT("nobody known: no players"), PlayersToJson(Empty), FString(TEXT("{\"available\":true,\"players\":[]}")));

	// 300 remembered players and one connected: the cap keeps the connected one, as MaxPlayers (256) says.
	TArray<FPlayerReading> Many;
	for (int32 I = 0; I < 300; ++I)
	{
		Many.Add({ FString::Printf(TEXT("player-old-%d"), I), false });
	}
	Many.Add({ TEXT("player-now"), true });
	const FPlayersPart Capped = NativePlayersPart(Many);
	TestEqual(TEXT("at most 256"), Capped.Players.Num(), MaxPlayers);
	TestEqual(TEXT("the connected player survives the cap"), Capped.Players[0].Name, FString(TEXT("player-now")));
	TestEqual(TEXT("the rest keep the game's order"), Capped.Players[1].Name, FString(TEXT("player-old-0")));

	// Names are cut to 200 characters, as ParsePlayers does.
	TArray<FPlayerReading> Long;
	Long.Add({ FString::ChrN(250, TEXT('n')), true });
	TestEqual(TEXT("a long name is cut to 200"), NativePlayersPart(Long).Players[0].Name.Len(), 200);
	return true;
}
