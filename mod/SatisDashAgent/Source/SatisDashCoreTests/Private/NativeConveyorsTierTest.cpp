#include "SatisDash/Native/Conveyors/NativeConveyors.h"
#include "TestUtil.h"

using namespace SatisDash;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeConveyorsTierTest, "SatisDash.Core.NativeConveyors.Tier", SatisDash::Test::Flags)

bool FNativeConveyorsTierTest::RunTest(const FString& Parameters)
{
	// Expected values are worked by hand from the census rule: one entry per exact (kind, name) pair, the first raw rate kept, at most 16 entries.
	TArray<FTierCount> Tiers;
	NoteConveyorTier(Tiers, EConveyorKind::Belt, TEXT("Conveyor Belt Mk.1"), 120.0);
	NoteConveyorTier(Tiers, EConveyorKind::Belt, TEXT("Conveyor Belt Mk.1"), 999.0);
	NoteConveyorTier(Tiers, EConveyorKind::Belt, TEXT("Conveyor Belt Mk.2"), 240.0);
	NoteConveyorTier(Tiers, EConveyorKind::Lift, TEXT("Conveyor Belt Mk.1"), 120.0); // same name, other kind: its own entry
	NoteConveyorTier(Tiers, EConveyorKind::Belt, TEXT("conveyor belt mk.1"), 120.0); // case matters
	if (!TestEqual(TEXT("four pairs"), Tiers.Num(), 4))
	{
		return false;
	}
	TestEqual(TEXT("first pair counted twice"), Tiers[0].Count, 2);
	TestEqual(TEXT("first raw rate kept"), Tiers[0].RawRate, 120.0);
	TestEqual(TEXT("second pair once"), Tiers[1].Count, 1);
	TestTrue(TEXT("third is the lift"), Tiers[2].Kind == EConveyorKind::Lift && Tiers[2].Count == 1);
	TestEqual(TEXT("fourth is the lower-case name"), Tiers[3].Name, FString(TEXT("conveyor belt mk.1")));

	for (int32 I = 4; I < ConveyorMaxTiers + 3; ++I)
	{
		NoteConveyorTier(Tiers, EConveyorKind::Pipe, FString::Printf(TEXT("Pipeline %d"), I), 5.0);
	}
	TestEqual(TEXT("capped at 16 entries"), Tiers.Num(), 16);
	NoteConveyorTier(Tiers, EConveyorKind::Belt, TEXT("Conveyor Belt Mk.1"), 120.0);
	TestEqual(TEXT("a known pair still counts past the cap"), Tiers[0].Count, 3);
	NoteConveyorTier(Tiers, EConveyorKind::Belt, TEXT("Conveyor Belt Mk.9"), 1.0);
	TestEqual(TEXT("a new pair past the cap is ignored"), Tiers.Num(), 16);
	return true;
}
