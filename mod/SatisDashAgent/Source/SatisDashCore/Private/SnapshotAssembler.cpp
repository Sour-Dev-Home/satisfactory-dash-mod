#include "SatisDash/SnapshotAssembler.h"

#include "SatisDash/Log.h"
#include "SatisDash/Records.h"

SatisDash::FSnapshotAssembler::FSnapshotAssembler(IWorker& InWorker)
	: Worker{InWorker}
{
}

void SatisDash::FSnapshotAssembler::NoteFailed(const TCHAR* Part, double Now)
{
	if (FailLog.ShouldLog(Part, Now))
	{
		UE_LOG(LogSatisDash, Warning, TEXT("Couldn't read %s; leaving it out of the snapshot"), Part);
	}
}

void SatisDash::FSnapshotAssembler::NoteMapLiveOverCap(double Now)
{
	if (FailLog.ShouldLog(TEXT("mapLiveOverCap"), Now))
	{
		UE_LOG(LogSatisDash, Warning, TEXT("mapLive is over its %lld-byte cap; leaving it out of the snapshot"), MapLiveMaxBytes);
	}
}

void SatisDash::FSnapshotAssembler::SetFactoryPart(FString Json, double ReadAt)
{
	FactoryReady = FReady{ MoveTemp(Json), ReadAt };
}

void SatisDash::FSnapshotAssembler::SetMapLivePart(FString Json, double ReadAt)
{
	MapLiveReady = FReady{ MoveTemp(Json), ReadAt };
}

TOptional<FString> SatisDash::FSnapshotAssembler::TakeMapLive(double Now, double MaxAgeSeconds)
{
	return TakeFresh(MapLiveReady, Now, MaxAgeSeconds);
}

bool SatisDash::FSnapshotAssembler::LargeBusy() const
{
	if (Factory.IsValid())
	{
		return true;
	}
	for (const FWorldSlot& Slot : World)
	{
		if (Slot.Job.IsValid())
		{
			return true;
		}
	}
	return false;
}

SatisDash::FSnapshotAssembler::FWorldSlot* SatisDash::FSnapshotAssembler::NextWorldJob()
{
	for (FWorldSlot& Slot : World)
	{
		if (Slot.Job.IsValid() && !Slot.Job->bRan)
		{
			return &Slot;
		}
	}
	return nullptr;
}

bool SatisDash::FSnapshotAssembler::ClaimWorld(EWorldLayer Layer)
{
	FWorldSlot& Slot = World[int32(Layer)];
	if (Slot.bRequested || Slot.Job.IsValid() || Slot.Outcome.IsSet())
	{
		return false;
	}
	Slot.bRequested = true;
	return true;
}

void SatisDash::FSnapshotAssembler::ReleaseWorld(EWorldLayer Layer)
{
	World[int32(Layer)].bRequested = false;
}

void SatisDash::FSnapshotAssembler::SetWorldRead(EWorldLayer Layer, TUniquePtr<FWorldRead> Read)
{
	World[int32(Layer)].Job = MakeShared<FWorldJob>(MoveTemp(Read));
}

void SatisDash::FSnapshotAssembler::FailWorld(EWorldLayer Layer)
{
	FWorldSlot& Each = World[int32(Layer)];
	FWorldOutcome Failed;
	Failed.Kind = FWorldOutcome::EKind::ReadFailed;
	Each.Outcome = MoveTemp(Failed);
	Each.bRequested = false;
}

bool SatisDash::FSnapshotAssembler::SubmitNativeWorld(EWorldLayer Layer, const FString& ObservedAt, TArray<FResourceNodeReading> Readings, TOptional<FBlake3Hash> LastData)
{
	FWorldSlot& Slot = World[int32(Layer)];
	if (Slot.bRequested || Slot.Job.IsValid() || Slot.Outcome.IsSet())
	{
		return false;
	}
	Slot.bRequested = true;
	Slot.Job = MakeShared<FWorldJob>(MakeUnique<FWorldRead>(Layer, MoveTemp(Readings), ObservedAt, LastData));
	return true;
}

bool SatisDash::FSnapshotAssembler::SubmitNativeWorld(EWorldLayer Layer, const FString& ObservedAt, FNativeWorldBuilder Builder, TOptional<FBlake3Hash> LastData)
{
	FWorldSlot& Slot = World[int32(Layer)];
	if (Slot.bRequested || Slot.Job.IsValid() || Slot.Outcome.IsSet())
	{
		return false;
	}
	Slot.bRequested = true;
	Slot.Job = MakeShared<FWorldJob>(MakeUnique<FWorldRead>(Layer, MoveTemp(Builder), ObservedAt, LastData));
	return true;
}

bool SatisDash::FSnapshotAssembler::SubmitNativeMapLive(FTrainsSnapshot Snapshot, double ReadAt)
{
	if (NativeMapLive.IsValid())
	{
		return false;
	}
	NativeMapLive = MakeShared<FNativeMapLiveJob>();
	NativeMapLive->Readings = MoveTemp(Snapshot);
	NativeMapLive->ReadAt = ReadAt;
	return true;
}

bool SatisDash::FSnapshotAssembler::SubmitNativeFactory(TArray<FFactoryReading> Readings, double ReadAt)
{
	if (Factory.IsValid())
	{
		return false;
	}
	Factory = MakeShared<FFactoryJob>(MoveTemp(Readings), ReadAt);
	return true;
}

bool SatisDash::FSnapshotAssembler::SubmitNativeExtractors(TArray<FExtractorReading> Readings, double ReadAt)
{
	if (ExtractorsTask.IsValid())
	{
		return false;
	}
	ExtractorsTask = MakeShared<FExtractorsJob>(MoveTemp(Readings), ReadAt);
	return true;
}

void SatisDash::FSnapshotAssembler::DropExtractors()
{
	ExtractorsTask.Reset();
	ExtractorsReady.Reset();
}

bool SatisDash::FSnapshotAssembler::SubmitNativeGenerators(TArray<FGeneratorReading> Readings, double ReadAt)
{
	if (GeneratorsTask.IsValid())
	{
		return false;
	}
	GeneratorsTask = MakeShared<FGeneratorsJob>(MoveTemp(Readings), ReadAt);
	return true;
}

TSharedPtr<const FString> SatisDash::FSnapshotAssembler::TakeGenerators(double Now, double FactorySeconds)
{
	TSharedPtr<const FString> Out;
	if (GeneratorsReady.IsSet())
	{
		if (Now - GeneratorsReady->At <= 1.5 * FactorySeconds)
		{
			Out = GeneratorsReady->Json; // once: the contract's read time is the snapshot's observedAt, so a read goes out in one power part
		}
		GeneratorsReady.Reset(); // a stale fragment is dropped, never sent under a fresh observedAt
	}
	return Out;
}

void SatisDash::FSnapshotAssembler::DropGenerators()
{
	GeneratorsTask.Reset();
	GeneratorsReady.Reset();
}

bool SatisDash::FSnapshotAssembler::SubmitNativeStorage(TArray<FContainerReading> Readings, double ReadAt)
{
	if (StorageTask.IsValid())
	{
		return false;
	}
	StorageTask = MakeShared<FStorageJob>(MoveTemp(Readings), ReadAt);
	return true;
}

TOptional<FString> SatisDash::FSnapshotAssembler::TakeStorage(double Now, double FactorySeconds)
{
	TOptional<FString> Out;
	if (StorageReady.IsSet())
	{
		if (Now - StorageReady->At <= 1.5 * FactorySeconds)
		{
			Out = MoveTemp(StorageReady->Json); // once: a data part is sent per read, then the memory is free
		}
		StorageReady.Reset(); // a stale fragment is dropped, never sent under a fresh observedAt
	}
	return Out;
}

void SatisDash::FSnapshotAssembler::DropStorage()
{
	StorageTask.Reset();
	StorageReady.Reset();
}

TOptional<SatisDash::FWorldOutcome> SatisDash::FSnapshotAssembler::TakeWorld(EWorldLayer Layer)
{
	FWorldSlot& Slot = World[int32(Layer)];
	TOptional<FWorldOutcome> Out = MoveTemp(Slot.Outcome);
	Slot.Outcome.Reset();
	return Out;
}

void SatisDash::FSnapshotAssembler::Harvest(double Now)
{
	if (Encode.IsValid() && Encode->bRan)
	{
		Encoded = MoveTemp(Encode->Body);
		const FSnapshotSizeReport Report = Encode->Report;
		Encode.Reset();
		if ((Report.bDroppedStorage || Report.bDroppedGenerators || Report.bDroppedExtractors) && FailLog.ShouldLog(TEXT("snapshotOverCap"), Now))
		{
			// Sizes in bytes only (the body is player data).
			UE_LOG(LogSatisDash, Warning, TEXT("The snapshot body was %lld bytes, over the %lld-byte cap: left out %s%s%s, now %lld bytes"),
				Report.BytesBefore, SnapshotMaxBytes, Report.bDroppedStorage ? TEXT("storage ") : TEXT(""),
				Report.bDroppedGenerators ? TEXT("generators ") : TEXT(""), Report.bDroppedExtractors ? TEXT("extractors") : TEXT(""), Report.BytesAfter);
		}
	}
	if (NativeMapLive.IsValid() && NativeMapLive->bRan)
	{
		if (NativeMapLive->bOk)
		{
			MapLiveReady = FReady{ MoveTemp(NativeMapLive->Json), NativeMapLive->ReadAt };
		}
		else
		{
			NoteMapLiveOverCap(Now);
		}
		NativeMapLive.Reset();
	}
	if (ExtractorsTask.IsValid() && ExtractorsTask->bRan)
	{
		ExtractorsReady = FReady{ MoveTemp(ExtractorsTask->Fragment), ExtractorsTask->ReadAt };
		ExtractorsTask.Reset();
	}
	if (GeneratorsTask.IsValid() && GeneratorsTask->bRan)
	{
		GeneratorsReady = FSharedReady{ MakeShared<FString>(MoveTemp(GeneratorsTask->Fragment)), GeneratorsTask->ReadAt }; // replaces the older fragment
		GeneratorsTask.Reset();
	}
	if (StorageTask.IsValid() && StorageTask->bRan)
	{
		// Counts and bytes only; a container's id and name are player text.
		UE_LOG(LogSatisDash, Display, TEXT("Buildables storage fragment built (worker): %lld bytes"), StorageTask->Bytes);
		if ((StorageTask->Dropped > 0 || StorageTask->Over > 0 || StorageTask->ItemsCut > 0 || StorageTask->SlotsClamped > 0) && FailLog.ShouldLog(TEXT("storageCuts"), Now))
		{
			UE_LOG(LogSatisDash, Warning, TEXT("Storage fragment: %d containers dropped, %d past the list cap, %d items cut, %d slot counts clamped"),
				StorageTask->Dropped, StorageTask->Over, StorageTask->ItemsCut, StorageTask->SlotsClamped);
		}
		StorageReady = FReady{ MoveTemp(StorageTask->Fragment), StorageTask->ReadAt }; // a newer fragment replaces an untaken one
		StorageTask.Reset();
	}
	if (Factory.IsValid() && Factory->bRan)
	{
		Factory->bRan = false;
		FactoryReady = FReady{ MoveTemp(Factory->NativeJson), Factory->ReadAt };
		Factory.Reset();
	}
	for (FWorldSlot& Slot : World)
	{
		if (!Slot.Job.IsValid() || !Slot.Job->bRan)
		{
			continue;
		}
		Slot.Job->bRan = false;
		if (Slot.Job->bFinished)
		{
			FWorldRead& Reading = *Slot.Job->Owned;
			Slot.Outcome = Reading.TakeOutcome();
			Slot.Job.Reset();
			Slot.bRequested = false;
		}
	}
}

void SatisDash::FSnapshotAssembler::Tick(double Now)
{
	if (Worker.IsIdle())
	{
		Harvest(Now);
		if (!LaunchEncode() && !LaunchNativeParts() && !LaunchFactory())
		{
			LaunchWorld();
		}
	}
}

bool SatisDash::FSnapshotAssembler::LaunchEncode()
{
	if (Encode.IsValid() && !Encode->bRan)
	{
		Worker.Launch([Job = Encode]
		{
			FString Json = SnapshotToJson(Job->Snapshot, Job->Report);
			Job->Snapshot.FactoryJson.Reset(); // the factory text is spliced into Json: don't hold both through the encode
			Job->Snapshot.PlayersJson.Reset();
			Job->Snapshot.MapLiveJson.Reset();
			Job->Snapshot.GeneratorsJson.Reset();
			Job->Snapshot.StorageJson.Reset();
			Job->Body = EncodeBody(Json);
			Job->bRan = true;
		});
		return true;
	}
	return false;
}

bool SatisDash::FSnapshotAssembler::LaunchNativeParts()
{
	if (NativeMapLive.IsValid() && !NativeMapLive->bRan)
	{
		Worker.Launch([Job = NativeMapLive]
		{
			// The whole part in one task: sorted, capped and written by the record writers the FRM parse uses (a few hundred records).
			Job->bOk = NativeMapLiveJson(Job->Readings.Trains, Job->Readings.Stations, Job->Json);
			Job->Readings = FTrainsSnapshot();
			Job->bRan = true;
		});
		return true;
	}
	if (ExtractorsTask.IsValid() && !ExtractorsTask->bRan)
	{
		Worker.Launch([Job = ExtractorsTask]
		{
			// The whole fragment in one task: sorted, capped and written by the one record writer; the readings are freed after.
			int32 Dropped = 0, Over = 0;
			Job->Fragment = NativeExtractorsFragment(MoveTemp(Job->Readings), Dropped, Over);
			Job->Readings = TArray<FExtractorReading>();
			Job->bRan = true;
		});
		return true;
	}
	if (GeneratorsTask.IsValid() && !GeneratorsTask->bRan)
	{
		Worker.Launch([Job = GeneratorsTask]
		{
			// The whole fragment in one task: sorted, capped and written by the one record writer; the readings are freed after.
			int32 Dropped = 0, Over = 0;
			Job->Fragment = NativeGeneratorsFragment(MoveTemp(Job->Readings), Dropped, Over);
			Job->Readings = TArray<FGeneratorReading>();
			Job->bRan = true;
		});
		return true;
	}
	if (StorageTask.IsValid() && !StorageTask->bRan)
	{
		Worker.Launch([Job = StorageTask]
		{
			// The whole fragment in one task: sorted, capped and written by the one record writer; the readings are freed after.
			Job->Fragment = NativeStorageFragment(MoveTemp(Job->Readings), Job->Dropped, Job->Over, Job->ItemsCut, Job->SlotsClamped);
			Job->Bytes = Job->Fragment.IsEmpty() ? 0 : int64(FTCHARToUTF8_Convert::ConvertedLength(*Job->Fragment, Job->Fragment.Len()));
			Job->Readings = TArray<FContainerReading>();
			Job->bRan = true;
		});
		return true;
	}
	return false;
}

bool SatisDash::FSnapshotAssembler::LaunchFactory()
{
	if (Factory.IsValid() && !Factory->bRan)
	{
		Worker.Launch([Job = Factory]
		{
			// The whole part in one task: sorted, capped and written by the one record writer (one read is at most 20,000 records).
			int32 Dropped = 0;
			Job->NativeJson = NativeFactoryPart(MoveTemp(Job->Readings), Dropped);
			Job->bRan = true;
		});
		return true;
	}
	return false;
}

bool SatisDash::FSnapshotAssembler::LaunchWorld()
{
	if (FWorldSlot* Slot = NextWorldJob())
	{
		Worker.Launch([Job = Slot->Job]
		{
			FWorldRead& Reading = *Job->Owned;
			Job->bFinished = Reading.Step();
			Job->bRan = true;
		});
		return true;
	}
	return false;
}

TOptional<FString> SatisDash::FSnapshotAssembler::TakeFresh(TOptional<FReady>& Ready, double Now, double MaxAgeSeconds)
{
	TOptional<FString> Out;
	if (Ready.IsSet() && Now - Ready->At <= MaxAgeSeconds)
	{
		Out = MoveTemp(Ready->Json);
	}
	Ready.Reset();
	return Out;
}

TOptional<FString> SatisDash::FSnapshotAssembler::TakeFactory(double Now, double MaxAgeSeconds)
{
	// Hold the part while the fragment's worker job is outstanding, but only inside the fragment's own 1.5 x age gate. Accepted: a
	// part that is ready before the game-thread pass finishes carries the previous cycle's fragment (one cadence of lag, within the gate).
	if (ExtractorsTask.IsValid() && Now - ExtractorsTask->ReadAt <= 1.5 * MaxAgeSeconds)
	{
		return {};
	}
	TOptional<FString> Part = TakeFresh(FactoryReady, Now, MaxAgeSeconds);
	if (Part.IsSet() && ExtractorsReady.IsSet())
	{
		// A held fragment is consumed once, by the first factory part that is taken after it; a stale one is dropped, not sent.
		if (Now - ExtractorsReady->At <= 1.5 * MaxAgeSeconds)
		{
			if (Part->EndsWith(TEXT("}")))
			{
				Part->LeftChopInline(1);
				Part->Append(TEXT(",\"extractors\":"));
				Part->Append(ExtractorsReady->Json);
				Part->AppendChar(TEXT('}'));
			}
			else if (FailLog.ShouldLog(TEXT("extractorsDropped"), Now))
			{
				UE_LOG(LogSatisDash, Warning, TEXT("The factory part does not end in a closing brace; leaving the extractors out of it"));
			}
		}
		ExtractorsReady.Reset();
	}
	return Part;
}

void SatisDash::FSnapshotAssembler::EncodeSnapshot(FSnapshot Snapshot)
{
	Encode = MakeShared<FEncodeJob>();
	Encode->Snapshot = MoveTemp(Snapshot);
}

TOptional<SatisDash::FEncodedBody> SatisDash::FSnapshotAssembler::TakeEncoded()
{
	TOptional<FEncodedBody> Out = MoveTemp(Encoded);
	Encoded.Reset();
	return Out;
}

void SatisDash::FSnapshotAssembler::Stop()
{
	Factory.Reset();
	ExtractorsTask.Reset();
	ExtractorsReady.Reset();
	DropGenerators();
	DropStorage();
	Encode.Reset();
	NativeMapLive.Reset();
	FactoryReady.Reset();
	MapLiveReady.Reset();
	for (FWorldSlot& Slot : World)
	{
		Slot = FWorldSlot();
	}
	Encoded.Reset();
}
