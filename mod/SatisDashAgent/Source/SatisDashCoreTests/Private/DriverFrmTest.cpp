#include "DriverFakes.h"
#include "SatisDash/Driver.h"

using namespace SatisDash;
using SatisDash::Test::FDriverFixture;

namespace
{
	const TCHAR* DriverFrmSnapshots = TEXT("/agent/v1/snapshots");

	/** Answers every open snapshot post with 200, and returns the bodies it answered. */
	TArray<FString> DriverFrmDrain(FDriverFixture& F)
	{
		TArray<FString> Bodies;
		while (F.Net.pending(DriverFrmSnapshots) > 0)
		{
			Bodies.Add(F.Net.lastBody(DriverFrmSnapshots));
			F.Net.answer(DriverFrmSnapshots, SatisDash::Test::SnapshotOk(false));
		}
		return Bodies;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverFrmPartsTest, "SatisDash.Core.Driver.FrmParts", SatisDash::Test::Flags)

bool FDriverFrmPartsTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	TestEqual(TEXT("no FRM read before enrolling"), F.Net.sent(TEXT("/getPlayer")), 0);
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	F.run(Driver, 1);
	TestEqual(TEXT("the first pass asks for players"), F.Net.pending(TEXT("/getPlayer")), 1);
	F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, TEXT("[{\"Name\":\"Player One\",\"Online\":true}]")));
	F.run(Driver, 1);
	TestEqual(TEXT("then the factory, one FRM request at a time"), F.Net.pending(TEXT("/getFactory")), 1);
	F.Net.answer(TEXT("/getFactory"), SatisDash::Test::Reply(200, TEXT("[{\"ID\":\"B_1\",\"Name\":\"n\",\"ClassName\":\"c\",\"IsProducing\":true,\"IsPaused\":false}]")));
	DriverFrmDrain(F);
	F.run(Driver, 5);
	TArray<FString> Bodies = DriverFrmDrain(F);
	F.run(Driver, 1);
	Bodies.Append(DriverFrmDrain(F));
	bool bPlayers = false;
	bool bFactory = false;
	for (const FString& Body : Bodies)
	{
		bPlayers = bPlayers || Body.Contains(TEXT("\"players\":{\"available\":true,\"players\":[{\"name\":\"Player One\",\"online\":true}]}"));
		bFactory = bFactory || Body.Contains(TEXT("\"factory\":{\"buildings\":[{\"id\":\"B_1\""));
	}
	TestTrue(TEXT("the next snapshot carries players"), bPlayers);
	TestTrue(TEXT("and one carries the factory"), bFactory);
	F.run(Driver, 10);
	TestEqual(TEXT("factory once per 30 s: no second read yet"), F.Net.sent(TEXT("/getFactory")), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverFrmDownTest, "SatisDash.Core.Driver.FrmDown", SatisDash::Test::Flags)

bool FDriverFrmDownTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	F.run(Driver, 1);
	F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(0, FString()));
	F.run(Driver, 1);
	F.Net.answer(TEXT("/getFactory"), SatisDash::Test::Reply(0, FString()));
	DriverFrmDrain(F);
	F.run(Driver, 5);
	const TArray<FString> Bodies = DriverFrmDrain(F);
	bool bUnavailable = false;
	for (const FString& Body : Bodies)
	{
		bUnavailable = bUnavailable || Body.Contains(TEXT("\"players\":{\"available\":false,\"players\":[]}"));
		TestFalse(TEXT("no factory part"), Body.Contains(TEXT("\"factory\"")));
		TestTrue(TEXT("status keeps flowing"), Body.Contains(TEXT("\"status\"")));
	}
	TestTrue(TEXT("players unavailable"), bUnavailable);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDriverFrmStopsTest, "SatisDash.Core.Driver.FrmStops", SatisDash::Test::Flags)

bool FDriverFrmStopsTest::RunTest(const FString& Parameters)
{
	FDriverFixture F;
	FDriver Driver{ F.ConfigPath, F.CredentialPath, F.Net, F.Game, F.Worker, F.clock() };
	F.enrol(Driver, SatisDash::Test::FakeSecret());
	F.run(Driver, 1);
	const int32 Asked = F.Net.sent(TEXT("/getPlayer"));
	F.Net.answer(DriverFrmSnapshots, SatisDash::Test::Reply(401, FString()));
	F.run(Driver, 1);
	const FAgent& Agent = Driver.GetAgent();
	TestTrue(TEXT("halted by the 401"), Agent.GetState() != EAgentState::Running);
	F.Net.answer(TEXT("/getPlayer"), SatisDash::Test::Reply(200, TEXT("[{\"Name\":\"Player One\",\"Online\":true}]")));
	F.run(Driver, 30);
	TestEqual(TEXT("no FRM read while not running"), F.Net.sent(TEXT("/getPlayer")), Asked);
	TestEqual(TEXT("nothing pushed after the halt"), F.Net.pending(DriverFrmSnapshots), 0);
	return true;
}
