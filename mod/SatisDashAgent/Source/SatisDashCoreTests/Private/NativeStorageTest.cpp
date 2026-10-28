#include "SatisDash/Native/Storage/NativeStorage.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	FStorageItemReading NativeStorageStack(const TCHAR* ClassName, const TCHAR* Name, double Amount, TOptional<int32> StackMax)
	{
		FStorageItemReading S;
		S.ClassName = ClassName;
		S.Name = Name;
		S.Amount = Amount;
		S.StackMax = StackMax;
		return S;
	}

	/** A synthetic Mk1 box: 24 slots, Wire (stack 500) before Plastic (stack 200) as the slots hold them. */
	FContainerReading NativeStorageBox(const TCHAR* Id)
	{
		FContainerReading R;
		R.Id = Id;
		R.Name = TEXT("Test Storage Box");
		R.ClassName = TEXT("Build_TestBox_C");
		R.bHasLocation = true;
		R.XCm = 10000;
		R.YCm = 25000;
		R.ZCm = -2000;
		R.YawDeg = 90;
		R.Slots = 24;
		R.SlotsUsed = 2;
		R.Items.Add(NativeStorageStack(TEXT("Desc_TestWire_C"), TEXT("Test Wire"), 300, 500));
		R.Items.Add(NativeStorageStack(TEXT("Desc_TestPlastic_C"), TEXT("Test Plastic"), 40, 200));
		return R;
	}

	FContainerReading NativeStorageBuffer(const TCHAR* Id)
	{
		FContainerReading R;
		R.Id = Id;
		R.Name = TEXT("Test Fluid Buffer");
		R.ClassName = TEXT("Build_TestBuffer_C");
		R.FluidCapacityM3 = 400.0;
		R.Items.Add(NativeStorageStack(TEXT("Desc_TestWater_C"), TEXT("Test Water"), 123.5, TOptional<int32>()));
		return R;
	}

	int32 NativeStorageCount(const FString& Text, const TCHAR* Needle)
	{
		int32 Found = 0;
		int32 From = 0;
		const int32 Length = FString(Needle).Len();
		for (int32 At = Text.Find(Needle, ESearchCase::CaseSensitive, ESearchDir::FromStart, From); At != INDEX_NONE;
			At = Text.Find(Needle, ESearchCase::CaseSensitive, ESearchDir::FromStart, From))
		{
			++Found;
			From = At + Length;
		}
		return Found;
	}

	FString NativeStorageOne(const FContainerReading& Reading)
	{
		int32 Dropped = 0;
		int32 Over = 0;
		int32 Cut = 0;
		int32 Clamped = 0;
		return NativeStorageFragment({ Reading }, Dropped, Over, Cut, Clamped);
	}

	/** The three golden readings, out of id order: a box, an empty industrial container without a position, a buffer. */
	TArray<FContainerReading> NativeStorageGoldenReadings()
	{
		FContainerReading Empty;
		Empty.Id = TEXT("Test_Industrial_C_2");
		Empty.Name = TEXT("Test Industrial Container");
		Empty.ClassName = TEXT("Build_TestIndustrial_C");
		Empty.Slots = 48;

		FContainerReading Buffer = NativeStorageBuffer(TEXT("Test_Buffer_C_3"));
		Buffer.bHasLocation = true;
		Buffer.XCm = -500;
		Buffer.YCm = 50;
		Buffer.ZCm = 1200;
		Buffer.YawDeg = 270;
		return { Buffer, NativeStorageBox(TEXT("Test_Box_C_1")), Empty };
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeStorageMapTest, "SatisDash.Core.NativeStorage.Map", SatisDash::Test::Flags)

bool FNativeStorageMapTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::SameText(*this, TEXT("a box with Wire and Plastic, items sorted by class"), NativeStorageOne(NativeStorageBox(TEXT("Test_Box_C_1"))),
		TEXT("{\"containers\":[{\"id\":\"Test_Box_C_1\",\"name\":\"Test Storage Box\",\"className\":\"Build_TestBox_C\",")
		TEXT("\"location\":{\"xM\":100,\"yM\":250,\"zM\":-20,\"rotationDeg\":90},\"slots\":24,\"slotsUsed\":2,")
		TEXT("\"items\":[{\"className\":\"Desc_TestPlastic_C\",\"name\":\"Test Plastic\",\"amount\":40,\"stackMax\":200},")
		TEXT("{\"className\":\"Desc_TestWire_C\",\"name\":\"Test Wire\",\"amount\":300,\"stackMax\":500}]}]}"));

	// No position read: the key is absent. Long text is cut at 200. A stack with no stackMax read is null.
	FContainerReading Odd = NativeStorageBox(TEXT("A"));
	Odd.bHasLocation = false;
	Odd.ClassName = FString::ChrN(250, TEXT('x'));
	Odd.Items[0].StackMax.Reset();
	const FString OddOut = NativeStorageOne(Odd);
	TestFalse(TEXT("no location read: the key is absent"), OddOut.Contains(TEXT("location")));
	TestTrue(TEXT("the class name is cut at 200"), OddOut.Contains(TEXT("\"className\":\"") + FString::ChrN(200, TEXT('x')) + TEXT("\",")));
	TestTrue(TEXT("an unread stackMax is null"), OddOut.Contains(TEXT("\"amount\":300,\"stackMax\":null}")));
	TestFalse(TEXT("an item container never has the key"), OddOut.Contains(TEXT("fluidCapacityM3")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeStorageFluidTest, "SatisDash.Core.NativeStorage.Fluid", SatisDash::Test::Flags)

bool FNativeStorageFluidTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::SameText(*this, TEXT("a fluid buffer: capacity, one fluid item, stackMax null"), NativeStorageOne(NativeStorageBuffer(TEXT("Test_Buffer_C_3"))),
		TEXT("{\"containers\":[{\"id\":\"Test_Buffer_C_3\",\"name\":\"Test Fluid Buffer\",\"className\":\"Build_TestBuffer_C\",\"slots\":0,\"slotsUsed\":0,")
		TEXT("\"fluidCapacityM3\":400,\"items\":[{\"className\":\"Desc_TestWater_C\",\"name\":\"Test Water\",\"amount\":123.5,\"stackMax\":null}]}]}"));

	FContainerReading Empty = NativeStorageBuffer(TEXT("E"));
	Empty.Items.Empty();
	TestTrue(TEXT("an empty buffer has items: []"), NativeStorageOne(Empty).EndsWith(TEXT("\"fluidCapacityM3\":400,\"items\":[]}]}")));

	FContainerReading Item = NativeStorageBox(TEXT("I"));
	TestFalse(TEXT("an item container never has fluidCapacityM3"), NativeStorageOne(Item).Contains(TEXT("fluidCapacityM3")));

	// A capacity of 0 that was read is still written: it is the test for a buffer.
	FContainerReading Zero = NativeStorageBuffer(TEXT("Z"));
	Zero.FluidCapacityM3 = 0.0;
	TestTrue(TEXT("a capacity of 0 is written"), NativeStorageOne(Zero).Contains(TEXT("\"fluidCapacityM3\":0,")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeStorageRulesTest, "SatisDash.Core.NativeStorage.Rules", SatisDash::Test::Flags)

bool FNativeStorageRulesTest::RunTest(const FString& Parameters)
{
	int32 Dropped = 0;
	int32 Over = 0;
	int32 Cut = 0;
	int32 Clamped = 0;
	SatisDash::Test::SameText(*this, TEXT("empty"), NativeStorageFragment({}, Dropped, Over, Cut, Clamped), TEXT("{\"containers\":[]}"));
	TestEqual(TEXT("empty: nothing dropped, over or cut"), Dropped + Over + Cut, 0);

	// Two stacks of one class are one item: amounts summed, stackMax of the first stack.
	FContainerReading Merge = NativeStorageBox(TEXT("M"));
	Merge.Items.Empty();
	Merge.Items.Add(NativeStorageStack(TEXT("Desc_TestWire_C"), TEXT("Test Wire"), 300, 500));
	Merge.Items.Add(NativeStorageStack(TEXT("Desc_TestWire_C"), TEXT("Test Wire"), 120, 400));
	TestTrue(TEXT("merged per class"), NativeStorageOne(Merge).EndsWith(
		TEXT("\"items\":[{\"className\":\"Desc_TestWire_C\",\"name\":\"Test Wire\",\"amount\":420,\"stackMax\":500}]}]}")));

	// 65 classes keep 64 (by class order); the container is counted once.
	FContainerReading Rich = NativeStorageBox(TEXT("R"));
	Rich.Items.Empty();
	for (int32 I = 0; I < StorageMaxItems + 1; ++I)
	{
		Rich.Items.Add(NativeStorageStack(*FString::Printf(TEXT("Desc_Test_C_%02d"), I), TEXT("Test Item"), 1, 100));
	}
	const FString Cut64 = NativeStorageFragment({ Rich, NativeStorageBox(TEXT("S")) }, Dropped, Over, Cut, Clamped);
	TestEqual(TEXT("65 classes: one container cut"), Cut, 1);
	TestTrue(TEXT("the first 64 classes are kept"), Cut64.Contains(TEXT("Desc_Test_C_63")) && !Cut64.Contains(TEXT("Desc_Test_C_64")));

	// More than the cap keeps the cap and counts the rest.
	TArray<FContainerReading> Many;
	for (int32 I = 0; I < StorageMaxContainers + 1; ++I)
	{
		Many.Add(NativeStorageBox(*FString::Printf(TEXT("Test_Box_C_%05d"), I)));
	}
	const FString Capped = NativeStorageFragment(Many, Dropped, Over, Cut, Clamped);
	TestEqual(TEXT("2001 containers: one over"), Over, 1);
	TestEqual(TEXT("2001 containers: none dropped"), Dropped, 0);
	TestTrue(TEXT("the first 2000 by id are kept"), Capped.Contains(TEXT("Test_Box_C_01999")) && !Capped.Contains(TEXT("Test_Box_C_02000")));

	// The same bytes whatever order the scan found them in.
	TArray<FContainerReading> Forward = NativeStorageGoldenReadings();
	TArray<FContainerReading> Backward;
	for (int32 I = Forward.Num() - 1; I >= 0; --I)
	{
		Backward.Add(Forward[I]);
	}
	SatisDash::Test::SameText(*this, TEXT("shuffled input"), NativeStorageFragment(Backward, Dropped, Over, Cut, Clamped), NativeStorageFragment(Forward, Dropped, Over, Cut, Clamped));

	// A repeated id is written once, whichever came first: the order is by the written content.
	FContainerReading Twin = NativeStorageBox(TEXT("Same"));
	Twin.SlotsUsed = 5;
	const FString PairA = NativeStorageFragment({ NativeStorageBox(TEXT("Same")), Twin }, Dropped, Over, Cut, Clamped);
	const FString PairB = NativeStorageFragment({ Twin, NativeStorageBox(TEXT("Same")) }, Dropped, Over, Cut, Clamped);
	SatisDash::Test::SameText(*this, TEXT("a duplicate id: the same bytes in either order"), PairA, PairB);
	TestEqual(TEXT("a duplicate id is written once"), NativeStorageCount(PairA, TEXT("\"id\":")), 1);

	// Two ids that differ only past the cut are the same written id: one is written.
	FContainerReading LongA = NativeStorageBox(*(FString::ChrN(250, TEXT('x')) + TEXT("a")));
	FContainerReading LongB = NativeStorageBox(*(FString::ChrN(250, TEXT('x')) + TEXT("b")));
	const FString LongPair = NativeStorageFragment({ LongA, LongB }, Dropped, Over, Cut, Clamped);
	const FString LongPairReversed = NativeStorageFragment({ LongB, LongA }, Dropped, Over, Cut, Clamped);
	SatisDash::Test::SameText(*this, TEXT("ids equal once cut: one written, order-independent"), LongPair, LongPairReversed);
	TestEqual(TEXT("ids equal once cut: one container"), NativeStorageCount(LongPair, TEXT("\"slots\":")), 1);

	// slotsUsed never passes slots; a negative count is zero.
	FContainerReading Over2 = NativeStorageBox(TEXT("Clamp"));
	Over2.SlotsUsed = 99;
	TestTrue(TEXT("slotsUsed is clamped to slots"), NativeStorageOne(Over2).Contains(TEXT("\"slots\":24,\"slotsUsed\":24,")));
	Over2.Slots = -3;
	TestTrue(TEXT("a negative slot count is 0"), NativeStorageOne(Over2).Contains(TEXT("\"slots\":0,\"slotsUsed\":0,")));
	FContainerReading Fine = NativeStorageBox(TEXT("Fine"));
	Over2.Slots = 24;
	NativeStorageFragment({ Over2, Fine, NativeStorageBox(TEXT("Fine2")) }, Dropped, Over, Cut, Clamped);
	TestEqual(TEXT("one container had its slotsUsed clamped"), Clamped, 1);
	NativeStorageFragment({ Fine }, Dropped, Over, Cut, Clamped);
	TestEqual(TEXT("none clamped: 0"), Clamped, 0);

	// A capacity that is not a number drops the container; an amount that is not a number is 0.
	const uint64 NanBits = 0x7FF8000000000000ull;
	double Nan = 0;
	FMemory::Memcpy(&Nan, &NanBits, sizeof(Nan));
	FContainerReading Bad = NativeStorageBuffer(TEXT("Bad"));
	Bad.FluidCapacityM3 = Nan;
	FContainerReading NanAmount = NativeStorageBox(TEXT("NanAmount"));
	NanAmount.Items[0].Amount = Nan;
	const FString Kept = NativeStorageFragment({ Bad, NanAmount, NativeStorageBox(TEXT("Good")) }, Dropped, Over, Cut, Clamped);
	TestEqual(TEXT("one dropped"), Dropped, 1);
	TestTrue(TEXT("the good one stays, the bad one is gone"), Kept.Contains(TEXT("\"id\":\"Good\"")) && !Kept.Contains(TEXT("\"id\":\"Bad\"")));
	TestTrue(TEXT("a NaN amount is 0"), Kept.Contains(TEXT("\"name\":\"Test Wire\",\"amount\":0,")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeStorageGoldenTest, "SatisDash.Core.NativeStorage.Golden", SatisDash::Test::Flags)

bool FNativeStorageGoldenTest::RunTest(const FString& Parameters)
{
	int32 Dropped = 0;
	int32 Over = 0;
	int32 Cut = 0;
	int32 Clamped = 0;
	const FString Actual = NativeStorageFragment(NativeStorageGoldenReadings(), Dropped, Over, Cut, Clamped);
	FString Expected = SatisDash::Test::Fixture(TEXT("native-storage.expected.json"));
	while (Expected.Len() > 0 && (Expected[Expected.Len() - 1] == TEXT('\n') || Expected[Expected.Len() - 1] == TEXT('\r')))
	{
		Expected.LeftChopInline(1);
	}
	TestTrue(TEXT("the fixture was found"), !Expected.IsEmpty());
	SatisDash::Test::SameText(*this, TEXT("the hand-written text, byte for byte"), Actual, Expected);
	TestEqual(TEXT("nothing dropped, over or cut"), Dropped + Over + Cut, 0);
	return true;
}
