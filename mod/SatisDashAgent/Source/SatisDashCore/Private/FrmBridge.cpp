#include "SatisDash/FrmBridge.h"

#include "SatisDash/Log.h"

SatisDash::FFrmBridge::FFrmBridge(ITransport& InTransport, IWorker& InWorker)
	: Worker{InWorker}
	, FrmLanes{InTransport}
{
}

void SatisDash::FFrmBridge::NoteFailed(const TCHAR* Part, double Now)
{
	if (FailLog.ShouldLog(Part, Now))
	{
		UE_LOG(LogSatisDash, Warning, TEXT("Couldn't read %s from FRM; leaving it out of the snapshot"), Part);
	}
}

void SatisDash::FFrmBridge::RequestPass(bool bFactoryDue, bool bPlayers, bool bFactoryFromFrm, bool bTrainsFromFrm)
{
	if (bPlayers)
	{
		FrmLanes.Enqueue(FFrmRead{ &FrmGetPlayer, [this](FHttpReply& Reply)
		{
			if (Reply.Code >= 200 && Reply.Code < 300)
			{
				Players = MakeShared<FPlayersJob>();
				Players->Body = MoveTemp(Reply.Body);
				Players->ReadAt = LastNow;
			}
			else
			{
				bPlayersAbsent = true; // playersMapper.ts: unreachable, timed out or an HTTP error is "FRM absent"
			}
		} });
	}
	if (bFactoryDue && bFactoryFromFrm)
	{
		FrmLanes.Enqueue(FFrmRead{ &FrmGetFactory, [this](FHttpReply& Reply)
		{
			if (Reply.Code >= 200 && Reply.Code < 300)
			{
				Factory = MakeShared<FFactoryJob>(MoveTemp(Reply.Body));
				Factory->ReadAt = LastNow;
			}
			else
			{
				bFactoryFailed = true;
			}
		} });
	}
	if (bFactoryDue && bTrainsFromFrm)
	{
		StartMapCycle(); // the pair starts whole or not at all: with trains native it is never entered
	}
}

void SatisDash::FFrmBridge::StartMapCycle()
{
	using EState = FMapSide::EState;
	if (MapTrains.State != EState::Idle || MapStations.State != EState::Idle)
	{
		return; // the last pair has not settled: this cadence tick skips mapLive
	}
	MapTrains.State = EState::Waiting;
	MapStations.State = EState::Waiting;
	// Enqueue refuses a read whose endpoint is still queued or in flight (e.g. one left by a run that was stopped and quickly
	// restarted): that side is Failed, so the pair settles and the next cadence tick starts a fresh one.
	if (!FrmLanes.Enqueue(FFrmRead{ &FrmGetTrains, [this](FHttpReply& Reply)
	{
		if (Reply.Code >= 200 && Reply.Code < 300)
		{
			MapTrains.Job = MakeShared<FMapJob>();
			MapTrains.Job->Owned =MakeUnique<FTrainsParse>(MoveTemp(Reply.Body));
			MapTrains.ReadAt = LastNow;
			MapTrains.State = FMapSide::EState::Parsing;
		}
		else
		{
			MapTrains.State = FMapSide::EState::Failed;
		}
	} }))
	{
		MapTrains.State = EState::Failed;
	}
	if (!FrmLanes.Enqueue(FFrmRead{ &FrmGetTrainStation, [this](FHttpReply& Reply)
	{
		if (Reply.Code >= 200 && Reply.Code < 300)
		{
			MapStations.Job = MakeShared<FMapJob>();
			MapStations.Job->Owned =MakeUnique<FStationsParse>(MoveTemp(Reply.Body));
			MapStations.ReadAt = LastNow;
			MapStations.State = FMapSide::EState::Parsing;
		}
		else
		{
			MapStations.State = FMapSide::EState::Failed;
		}
	} }))
	{
		MapStations.State = EState::Failed;
	}
}

void SatisDash::FFrmBridge::HarvestMapSide(FMapSide& Side)
{
	if (!Side.Job.IsValid() || !Side.Job->bRan)
	{
		return;
	}
	Side.Job->bRan = false;
	if (Side.Job->Last == FFrmArrayParse::EStep::Done)
	{
		FFrmArrayParse& Array = *Side.Job->Owned;
		Side.Bytes = Array.Utf8Bytes();
		Side.Json = Array.TakeJson();
		Side.State = FMapSide::EState::Ok;
		Side.Job.Reset();
	}
	else if (Side.Job->Last == FFrmArrayParse::EStep::Invalid)
	{
		Side.State = FMapSide::EState::Failed;
		Side.Job.Reset();
	}
}

void SatisDash::FFrmBridge::FinishMapLive(double Now)
{
	using EState = FMapSide::EState;
	const bool bTrainsSettled = MapTrains.State == EState::Ok || MapTrains.State == EState::Failed;
	const bool bStationsSettled = MapStations.State == EState::Ok || MapStations.State == EState::Failed;
	if (!bTrainsSettled || !bStationsSettled)
	{
		return;
	}
	if (MapTrains.State == EState::Ok && MapStations.State == EState::Ok)
	{
		TOptional<FString> Joined = JoinMapLive(MapTrains.Json, MapTrains.Bytes, MapStations.Json, MapStations.Bytes);
		if (Joined.IsSet())
		{
			MapLiveReady = FReady{ MoveTemp(Joined.GetValue()), FMath::Min(MapTrains.ReadAt, MapStations.ReadAt) };
		}
		else if (FailLog.ShouldLog(TEXT("mapLiveOverCap"), Now))
		{
			UE_LOG(LogSatisDash, Warning, TEXT("mapLive is over its %lld-byte cap; leaving it out of the snapshot"), MapLiveMaxBytes);
		}
	}
	else
	{
		NoteFailed(TEXT("mapLive"), Now);
	}
	MapTrains = FMapSide();
	MapStations = FMapSide();
}

TOptional<FString> SatisDash::FFrmBridge::TakeMapLive(double Now, double MaxAgeSeconds)
{
	return TakeFresh(MapLiveReady, Now, MaxAgeSeconds);
}

bool SatisDash::FFrmBridge::LargeBusy() const
{
	return Factory.IsValid() || World[0].Job.IsValid() || World[1].Job.IsValid();
}

SatisDash::FFrmBridge::FWorldSlot* SatisDash::FFrmBridge::NextWorldJob()
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

bool SatisDash::FFrmBridge::RequestWorld(EWorldLayer Layer, const FString& ObservedAt, TOptional<FBlake3Hash> LastRaw, TOptional<FBlake3Hash> LastData)
{
	FWorldSlot& Slot = World[int32(Layer)];
	if (Slot.bRequested || Slot.Job.IsValid() || Slot.Outcome.IsSet())
	{
		return false;
	}
	Slot.bRequested = true;
	if (!FrmLanes.Enqueue(FFrmRead{ WorldLayer(Layer).Endpoint, [this, Layer, ObservedAt, LastRaw, LastData](FHttpReply& Reply)
	{
		FWorldSlot& Each = World[int32(Layer)];
		if (Reply.Code >= 200 && Reply.Code < 300)
		{
			Each.Job = MakeShared<FWorldJob>(Layer, MoveTemp(Reply.Body), ObservedAt, LastRaw, LastData);
		}
		else
		{
			FWorldOutcome Failed;
			Failed.Kind = FWorldOutcome::EKind::ReadFailed;
			Each.Outcome = MoveTemp(Failed);
			Each.bRequested = false;
		}
	} }))
	{
		// The endpoint is still in flight from a stopped run: FRM was not asked, so the layer is not requested. The caller asks again.
		Slot.bRequested = false;
		return false;
	}
	return true;
}

bool SatisDash::FFrmBridge::SubmitNativeWorld(EWorldLayer Layer, const FString& ObservedAt, TArray<FResourceNodeReading> Readings, TOptional<FBlake3Hash> LastData)
{
	FWorldSlot& Slot = World[int32(Layer)];
	if (Slot.bRequested || Slot.Job.IsValid() || Slot.Outcome.IsSet())
	{
		return false;
	}
	Slot.bRequested = true;
	Slot.Job = MakeShared<FWorldJob>(Layer, MoveTemp(Readings), ObservedAt, LastData);
	return true;
}

TOptional<SatisDash::FWorldOutcome> SatisDash::FFrmBridge::TakeWorld(EWorldLayer Layer)
{
	FWorldSlot& Slot = World[int32(Layer)];
	TOptional<FWorldOutcome> Out = MoveTemp(Slot.Outcome);
	Slot.Outcome.Reset();
	return Out;
}

void SatisDash::FFrmBridge::Harvest(double Now)
{
	if (Encode.IsValid() && Encode->bRan)
	{
		Encoded = MoveTemp(Encode->Body);
		Encode.Reset();
	}
	if (Players.IsValid() && Players->bRan)
	{
		if (Players->Json.IsSet())
		{
			PlayersReady = FReady{ MoveTemp(Players->Json.GetValue()), Players->ReadAt };
		}
		else
		{
			NoteFailed(TEXT("players"), Now);
		}
		Players.Reset();
	}
	HarvestMapSide(MapTrains);
	HarvestMapSide(MapStations);
	if (Factory.IsValid() && Factory->bRan)
	{
		Factory->bRan = false;
		if (Factory->Last == FFactoryParse::EStep::Done)
		{
			FactoryReady = FReady{ Factory->Parse.TakeJson(), Factory->ReadAt };
			Factory.Reset();
		}
		else if (Factory->Last == FFactoryParse::EStep::Invalid)
		{
			NoteFailed(TEXT("factory"), Now);
			Factory.Reset();
		}
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
			Slot.Outcome = Slot.Job->Work.TakeOutcome();
			Slot.Job.Reset();
			Slot.bRequested = false;
		}
	}
}

void SatisDash::FFrmBridge::LaunchNext()
{
	if (Encode.IsValid() && !Encode->bRan)
	{
		Worker.Launch([Job = Encode]
		{
			FString Json = SnapshotToJson(Job->Snapshot);
			Job->Snapshot.FactoryJson.Reset(); // the factory text is spliced into Json: don't hold both through the encode
			Job->Snapshot.PlayersJson.Reset();
			Job->Snapshot.MapLiveJson.Reset();
			Job->Body = EncodeBody(Json);
			Job->bRan = true;
		});
	}
	else if (Players.IsValid() && !Players->bRan)
	{
		Worker.Launch([Job = Players]
		{
			const TOptional<FPlayersPart> Part = ParsePlayers(Job->Body);
			if (Part.IsSet())
			{
				Job->Json = PlayersToJson(Part.GetValue());
			}
			Job->Body.Empty();
			Job->bRan = true;
		});
	}
	else if (MapTrains.Job.IsValid() && !MapTrains.Job->bRan)
	{
		Worker.Launch([Job = MapTrains.Job]
		{
			FFrmArrayParse& Array = *Job->Owned;
			Job->Last = Array.Step();
			Job->bRan = true;
		});
	}
	else if (MapStations.Job.IsValid() && !MapStations.Job->bRan)
	{
		Worker.Launch([Job = MapStations.Job]
		{
			FFrmArrayParse& Array = *Job->Owned;
			Job->Last = Array.Step();
			Job->bRan = true;
		});
	}
	else if (Factory.IsValid() && !Factory->bRan)
	{
		Worker.Launch([Job = Factory]
		{
			Job->Last = Job->Parse.Step();
			Job->bRan = true;
		});
	}
	else if (FWorldSlot* Slot = NextWorldJob())
	{
		Worker.Launch([Job = Slot->Job]
		{
			Job->bFinished = Job->Work.Step();
			Job->bRan = true;
		});
	}
}

void SatisDash::FFrmBridge::Tick(double Now, int32 Port, const FString& Token)
{
	LastNow = Now;
	if (bPlayersAbsent)
	{
		bPlayersAbsent = false;
		PlayersReady = FReady{ PlayersToJson(PlayersUnavailable()), Now };
	}
	if (bFactoryFailed)
	{
		bFactoryFailed = false;
		NoteFailed(TEXT("factory"), Now);
	}
	if (Worker.IsIdle())
	{
		Harvest(Now);
		LaunchNext();
	}
	FinishMapLive(Now);
	FrmLanes.Pump(Port, Token, LargeBusy());
}

TOptional<FString> SatisDash::FFrmBridge::TakeFresh(TOptional<FReady>& Ready, double Now, double MaxAgeSeconds)
{
	TOptional<FString> Out;
	if (Ready.IsSet() && Now - Ready->At <= MaxAgeSeconds)
	{
		Out = MoveTemp(Ready->Json);
	}
	Ready.Reset();
	return Out;
}

TOptional<FString> SatisDash::FFrmBridge::TakePlayers(double Now, double MaxAgeSeconds)
{
	return TakeFresh(PlayersReady, Now, MaxAgeSeconds);
}

TOptional<FString> SatisDash::FFrmBridge::TakeFactory(double Now, double MaxAgeSeconds)
{
	return TakeFresh(FactoryReady, Now, MaxAgeSeconds);
}

void SatisDash::FFrmBridge::EncodeSnapshot(FSnapshot Snapshot)
{
	Encode = MakeShared<FEncodeJob>();
	Encode->Snapshot = MoveTemp(Snapshot);
}

TOptional<SatisDash::FEncodedBody> SatisDash::FFrmBridge::TakeEncoded()
{
	TOptional<FEncodedBody> Out = MoveTemp(Encoded);
	Encoded.Reset();
	return Out;
}

void SatisDash::FFrmBridge::Stop()
{
	FrmLanes.Cancel();
	Players.Reset();
	Factory.Reset();
	Encode.Reset();
	PlayersReady.Reset();
	FactoryReady.Reset();
	MapTrains = FMapSide();
	MapStations = FMapSide();
	MapLiveReady.Reset();
	for (FWorldSlot& Slot : World)
	{
		Slot = FWorldSlot();
	}
	Encoded.Reset();
	bPlayersAbsent = false;
	bFactoryFailed = false;
}
