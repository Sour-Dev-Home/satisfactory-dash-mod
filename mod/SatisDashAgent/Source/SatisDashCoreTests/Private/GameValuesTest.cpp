#include "SatisDash/GameValues.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	FPowerCircuitSample circuit(int32 Id, int32 Group, double Produced)
	{
		FPowerCircuitSample S;
		S.CircuitId = Id;
		S.GroupId = Group;
		S.ProducedMW = Produced;
		S.ConsumedMW = Produced / 2;
		S.CapacityMW = Produced * 2;
		S.MaxConsumptionMW = Produced;
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameValuesGroupsTest, "SatisDash.Core.GameValues.Groups", SatisDash::Test::Flags)

bool FGameValuesGroupsTest::RunTest(const FString& Parameters)
{
	// Ruling 7: circuits 3 and 0 are joined (group 0) and read the same figures; circuit 5 is alone (group 2).
	const TArray<FCircuitReading> Out = GroupCircuits({ circuit(3, 0, 100), circuit(5, 2, 7), circuit(0, 0, 100) });
	TestEqual(TEXT("one row per group"), Out.Num(), 2);
	TestEqual(TEXT("ordered by group id"), Out[0].CircuitGroupId, 0);
	TestEqual(TEXT("then"), Out[1].CircuitGroupId, 2);
	TestEqual(TEXT("figures copied"), Out[1].ProductionMW, 7.0);
	TestEqual(TEXT("consumption"), Out[1].ConsumptionMW, 3.5);
	TestEqual(TEXT("capacity"), Out[1].CapacityMW, 14.0);
	TestEqual(TEXT("max consumption"), Out[1].MaxConsumptionMW, 7.0);

	// The lowest circuit id speaks for a group, whatever order the scan found them in.
	FPowerCircuitSample Low = circuit(1, 4, 10);
	FPowerCircuitSample High = circuit(9, 4, 99);
	TestEqual(TEXT("lowest id wins"), GroupCircuits({ High, Low })[0].ProductionMW, 10.0);

	TestEqual(TEXT("a circuit with no group yet is left out"), GroupCircuits({ circuit(1, -1, 5) }).Num(), 0);
	TestEqual(TEXT("no circuits, no rows"), GroupCircuits({}).Num(), 0);

	TArray<FPowerCircuitSample> Many;
	for (int32 I = 0; I < MaxCircuits + 5; ++I)
	{
		Many.Add(circuit(I, I, 1));
	}
	TestEqual(TEXT("capped at the contract's 1000"), GroupCircuits(Many).Num(), MaxCircuits);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameValuesBatteryTest, "SatisDash.Core.GameValues.Battery", SatisDash::Test::Flags)

bool FGameValuesBatteryTest::RunTest(const FString& Parameters)
{
	FPowerCircuitSample S = circuit(0, 0, 50);
	S.bFuseTriggered = true;
	S.bHasBatteries = true;
	S.BatteryStoreMWh = 25;
	S.BatteryCapacityMWh = 100;
	S.BatteryInputMW = -12.5;
	const FCircuitReading R = GroupCircuits({ S })[0];
	TestTrue(TEXT("fuse"), R.bFuseTriggered);
	TestEqual(TEXT("capacity"), R.BatteryCapacityMWh, 100.0);
	TestEqual(TEXT("percent 0-100"), R.BatteryPercent, 25.0);
	TestEqual(TEXT("differential keeps its sign (discharging)"), R.BatteryDifferentialMW, -12.5);

	S.BatteryCapacityMWh = 0; // the game's own percent getter would divide by this (ruling 7)
	TestEqual(TEXT("no capacity: 0 percent"), GroupCircuits({ S })[0].BatteryPercent, 0.0);

	FPowerCircuitSample None = circuit(0, 0, 50);
	None.BatteryStoreMWh = 5; // stale figures with no batteries are ignored
	None.BatteryCapacityMWh = 10;
	const FCircuitReading N = GroupCircuits({ None })[0];
	TestEqual(TEXT("no batteries: capacity 0"), N.BatteryCapacityMWh, 0.0);
	TestEqual(TEXT("no batteries: percent 0"), N.BatteryPercent, 0.0);
	TestEqual(TEXT("no batteries: differential 0"), N.BatteryDifferentialMW, 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameValuesAutoPauseTest, "SatisDash.Core.GameValues.AutoPauseOption", SatisDash::Test::Flags)

bool FGameValuesAutoPauseTest::RunTest(const FString& Parameters)
{
	const FString True = TEXT("True");
	const FString False = TEXT("False");
	const FString Lower = TEXT("true");
	const FString Junk = TEXT("1");
	TestTrue(TEXT("True"), ParseAutoPauseOption(&True) == TOptional<bool>(true));
	TestTrue(TEXT("False"), ParseAutoPauseOption(&False) == TOptional<bool>(false));
	// The game writes exactly True and False (S7's log); anything else is unread, never guessed.
	TestFalse(TEXT("lower case is unread"), ParseAutoPauseOption(&Lower).IsSet());
	TestFalse(TEXT("junk is unread"), ParseAutoPauseOption(&Junk).IsSet());
	TestFalse(TEXT("missing is unread"), ParseAutoPauseOption(nullptr).IsSet());
	SatisDash::Test::SameText(*this, TEXT("write true"), AutoPauseOptionValue(true), TEXT("True"));
	SatisDash::Test::SameText(*this, TEXT("write false"), AutoPauseOptionValue(false), TEXT("False"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameValuesTickHealthTest, "SatisDash.Core.GameValues.TickHealth", SatisDash::Test::Flags)

bool FGameValuesTickHealthTest::RunTest(const FString& Parameters)
{
	// HealthCheck's values as S6 recorded them ("healthy", "slow"); the backend maps the snapshot's healthy/slow.
	TestTrue(TEXT("healthy"), IsTickHealthy(TEXT("healthy")));
	TestFalse(TEXT("slow"), IsTickHealthy(TEXT("slow")));
	TestFalse(TEXT("case-sensitive"), IsTickHealthy(TEXT("Healthy")));
	TestFalse(TEXT("empty"), IsTickHealthy(FString()));
	return true;
}
