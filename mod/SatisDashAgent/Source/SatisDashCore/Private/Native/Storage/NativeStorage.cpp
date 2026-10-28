#include "SatisDash/Native/Storage/NativeStorage.h"
#include "../Factory/NativeFactoryShared.h"
#include "SatisDash/Snapshot.h"

namespace
{
	using namespace SatisDash;
	using namespace SatisDash::NativeShared;

	/** An item as it will be written: the text already cut, the amounts already made valid. */
	struct FStorageItemOut
	{
		FString ClassName, Name;
		double Amount = 0;
		TOptional<int32> StackMax;
	};

	/** A container written to text, with what the order and the report need. */
	struct FStorageWritten
	{
		FString Id;
		FString Text;
		bool bItemsCut = false;
		bool bSlotsClamped = false;
	};

	/** One item per class (the class as written), sorted by it; the name and stackMax are the first stack's, the amounts are summed. */
	TArray<FStorageItemOut> NativeStorageMerge(const TArray<FStorageItemReading>& Stacks)
	{
		TArray<FStorageItemOut> Each;
		Each.Reserve(Stacks.Num());
		for (const FStorageItemReading& Stack : Stacks)
		{
			FStorageItemOut Item;
			Item.ClassName = ClampString(Stack.ClassName);
			Item.Name = ClampString(Stack.Name);
			Item.Amount = NonNegative(Stack.Amount);
			if (Stack.StackMax.IsSet())
			{
				Item.StackMax = FMath::Max(0, Stack.StackMax.GetValue());
			}
			Each.Add(MoveTemp(Item));
		}
		// Stable: of two stacks of one class, the one the reader listed first stays first.
		Each.StableSort([](const FStorageItemOut& A, const FStorageItemOut& B) { return CompareText(A.ClassName, B.ClassName) < 0; });

		TArray<FStorageItemOut> Merged;
		for (const FStorageItemOut& Item : Each)
		{
			if (Merged.Num() > 0 && Merged[Merged.Num() - 1].ClassName.Equals(Item.ClassName, ESearchCase::CaseSensitive))
			{
				FStorageItemOut& Same = Merged[Merged.Num() - 1];
				Same.Amount = NonNegative(Same.Amount + Item.Amount);
				continue;
			}
			Merged.Add(Item);
		}
		return Merged;
	}

	FStorageWritten NativeStorageWrite(const FContainerReading& R)
	{
		FStorageWritten Out;
		Out.Id = ClampString(R.Id);
		const TSharedRef<FFactoryJsonWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out.Text);
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("id"), Out.Id);
		Writer->WriteValue(TEXT("name"), ClampString(R.Name));
		Writer->WriteValue(TEXT("className"), ClampString(R.ClassName));
		FFactoryBuildingModel Place;
		if (R.bHasLocation && SetLocation(Place, R.XCm, R.YCm, R.ZCm, R.YawDeg))
		{
			Writer->WriteObjectStart(TEXT("location"));
			Writer->WriteValue(TEXT("xM"), Place.XM);
			Writer->WriteValue(TEXT("yM"), Place.YM);
			Writer->WriteValue(TEXT("zM"), Place.ZM);
			Writer->WriteValue(TEXT("rotationDeg"), Place.YawDeg);
			Writer->WriteObjectEnd();
		}
		const int32 Slots = FMath::Max(0, R.Slots);
		Writer->WriteValue(TEXT("slots"), Slots);
		const int32 SlotsUsed = FMath::Clamp(R.SlotsUsed, 0, Slots);
		Out.bSlotsClamped = SlotsUsed != R.SlotsUsed;
		Writer->WriteValue(TEXT("slotsUsed"), SlotsUsed);
		if (R.FluidCapacityM3.IsSet())
		{
			Writer->WriteValue(TEXT("fluidCapacityM3"), NonNegative(R.FluidCapacityM3.GetValue()));
		}
		Writer->WriteArrayStart(TEXT("items"));
		const TArray<FStorageItemOut> Items = NativeStorageMerge(R.Items);
		Out.bItemsCut = Items.Num() > StorageMaxItems;
		for (int32 Index = 0; Index < Items.Num() && Index < StorageMaxItems; ++Index)
		{
			const FStorageItemOut& Item = Items[Index];
			Writer->WriteObjectStart();
			Writer->WriteValue(TEXT("className"), Item.ClassName);
			Writer->WriteValue(TEXT("name"), Item.Name);
			Writer->WriteValue(TEXT("amount"), Item.Amount);
			if (Item.StackMax.IsSet())
			{
				Writer->WriteValue(TEXT("stackMax"), Item.StackMax.GetValue());
			}
			else
			{
				Writer->WriteNull(TEXT("stackMax"));
			}
			Writer->WriteObjectEnd();
		}
		Writer->WriteArrayEnd();
		Writer->WriteObjectEnd();
		Writer->Close();
		return Out;
	}

	/** Total over what is written: the written id, then the written text. */
	bool NativeStorageLess(const FStorageWritten& A, const FStorageWritten& B)
	{
		const int32 Order = CompareText(A.Id, B.Id);
		return Order != 0 ? Order < 0 : CompareText(A.Text, B.Text) < 0;
	}
}

FString SatisDash::NativeStorageFragment(TArray<FContainerReading> Readings, int32& OutDropped, int32& OutOver, int32& OutItemsCut, int32& OutSlotsClamped)
{
	OutSlotsClamped = 0;
	OutDropped = 0;
	OutOver = 0;
	OutItemsCut = 0;

	// A capacity the bridge's schema cannot take is a reading that cannot be trusted: drop it. The rest is written first and
	// ordered and deduplicated on what was written, so the clamped id is the one that decides.
	TArray<FStorageWritten> Written;
	Written.Reserve(Readings.Num());
	for (const FContainerReading& Reading : Readings)
	{
		if (Reading.FluidCapacityM3.IsSet() && (!Finite(Reading.FluidCapacityM3.GetValue()) || Reading.FluidCapacityM3.GetValue() < 0))
		{
			++OutDropped;
			continue;
		}
		Written.Add(NativeStorageWrite(Reading));
	}
	Written.Sort(NativeStorageLess);

	FString Out = TEXT("{\"containers\":[");
	int32 Count = 0;
	const FString* LastId = nullptr;
	for (const FStorageWritten& Container : Written)
	{
		if (LastId && LastId->Equals(Container.Id, ESearchCase::CaseSensitive))
		{
			continue; // a later duplicate of an id already seen
		}
		LastId = &Container.Id;
		if (Count >= StorageMaxContainers)
		{
			++OutOver;
			continue;
		}
		if (Count > 0)
		{
			Out += TEXT(",");
		}
		Out += Container.Text;
		OutItemsCut += Container.bItemsCut ? 1 : 0;
		OutSlotsClamped += Container.bSlotsClamped ? 1 : 0;
		++Count;
	}
	Out += TEXT("]}");
	return Out;
}
