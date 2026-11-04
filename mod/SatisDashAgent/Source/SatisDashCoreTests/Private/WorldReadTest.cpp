#include "SatisDash/Native/ResourceNodes/NativeResourceNodes.h"
#include "SatisDash/WorldRead.h"
#include "TestUtil.h"

using namespace SatisDash;

// Moved from FrmWorldTest (SatisDash.Core.FrmWorld.Native and .LayerTable) when the FRM role was deleted (R7): the native read
// and the layer table are what stays; the assertions are the same, minus the FRM endpoint column.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReadNativeTest, "SatisDash.Core.WorldRead.Native", SatisDash::Test::Flags)

bool FWorldReadNativeTest::RunTest(const FString& Parameters)
{
	TArray<FResourceNodeReading> In;
	FResourceNodeReading R;
	R.Resource = TEXT("Iron Ore");
	R.Purity = ENodePurity::Normal;
	R.Kind = ENodeKind::Node;
	R.XCm = 100;
	R.YCm = 200;
	In.Add(R);

	const FNativeWorldBody Body = BuildNativeResourceNodes(TEXT("T"), In);
	FBlake3 Hasher;
	Hasher.Update(*Body.Json + Body.DataFrom, uint64(Body.DataEnd - Body.DataFrom) * sizeof(TCHAR));
	const FBlake3Hash Expected = Hasher.Finalize();

	// The readings go in and the worker builds the body: one Step does the build, the hash and the encode.
	FWorldRead NativeRead{ EWorldLayer::ResourceNodes, In, FString(TEXT("T")), TOptional<FBlake3Hash>() };
	TestTrue(TEXT("a native read is one step"), NativeRead.Step());
	FWorldOutcome Ready = NativeRead.TakeOutcome();
	TestTrue(TEXT("ready to post"), Ready.Kind == FWorldOutcome::EKind::Ready);
	TestEqual(TEXT("one item"), Ready.Items, 1);
	TestEqual(TEXT("the body's bytes"), Ready.Bytes, Body.Bytes);
	TestTrue(TEXT("the data hash covers exactly the array text"), Ready.DataHash == Expected);
	TestTrue(TEXT("a body to post"), Ready.Body.Bytes.Num() > 0);

	FWorldRead NativeSameRead{ EWorldLayer::ResourceNodes, In, FString(TEXT("T2")), TOptional<FBlake3Hash>(Expected) };
	TestTrue(TEXT("same data, one step"), NativeSameRead.Step());
	TestTrue(TEXT("a new observedAt over the same nodes is not a change"), NativeSameRead.TakeOutcome().Kind == FWorldOutcome::EKind::SameData);

	FWorldRead NativeOverRead{ EWorldLayer::ResourceNodes, In, FString(TEXT("T")), TOptional<FBlake3Hash>(), 10 };
	TestTrue(TEXT("over the cap, one step"), NativeOverRead.Step());
	TestTrue(TEXT("over the byte cap is skipped"), NativeOverRead.TakeOutcome().Kind == FWorldOutcome::EKind::OverCap);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReadLayerTableTest, "SatisDash.Core.WorldRead.LayerTable", SatisDash::Test::Flags)
bool FWorldReadLayerTableTest::RunTest(const FString&)
{
	struct FExpected { EWorldLayer Layer; const TCHAR* Name; double First; double Interval; double NativeFirst; };
	const FExpected Table[] = {
		{ EWorldLayer::Rails, TEXT("rails"), 120.0, 600.0, 480.0 },
		{ EWorldLayer::ResourceNodes, TEXT("resourceNodes"), 480.0, 1800.0, 0.0 },
		{ EWorldLayer::TrainRoutes, TEXT("trainRoutes"), 480.0, 600.0, 0.0 },
		{ EWorldLayer::Conveyors, TEXT("conveyors"), 480.0, 600.0, 0.0 },
		{ EWorldLayer::MapMarkers, TEXT("mapMarkers"), 480.0, 600.0, 0.0 },
		{ EWorldLayer::DroneRoutes, TEXT("droneRoutes"), 480.0, 600.0, 0.0 },
		{ EWorldLayer::PowerLines, TEXT("powerLines"), 480.0, 600.0, 0.0 },
	};
	TestEqual(TEXT("seven layers"), WorldLayerCount, 7);
	for (const FExpected& E : Table)
	{
		const FWorldLayerSpec& Spec = WorldLayer(E.Layer);
		TestEqual(*FString::Printf(TEXT("%s name"), E.Name), FString(Spec.Name), FString(E.Name));
		TestEqual(*FString::Printf(TEXT("%s first"), E.Name), Spec.FirstSeconds, E.First);
		TestEqual(*FString::Printf(TEXT("%s interval"), E.Name), Spec.IntervalSeconds, E.Interval);
		TestEqual(*FString::Printf(TEXT("%s native first"), E.Name), Spec.NativeFirstSeconds, E.NativeFirst);
	}
	return true;
}
