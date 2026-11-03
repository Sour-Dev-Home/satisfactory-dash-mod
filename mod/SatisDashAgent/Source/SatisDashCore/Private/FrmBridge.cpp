#include "SatisDash/FrmBridge.h"

SatisDash::FFrmBridge::FFrmBridge(ITransport& InTransport, IWorker& InWorker, FSnapshotAssembler& InAssembler)
	: Worker{InWorker}
	, Assembler{InAssembler}
	, FrmLanes{InTransport}
{
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
				Factory = MakeShared<FFrmFactoryJob>(MoveTemp(Reply.Body));
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
			Assembler.SetMapLivePart(MoveTemp(Joined.GetValue()), FMath::Min(MapTrains.ReadAt, MapStations.ReadAt));
		}
		else
		{
			Assembler.NoteMapLiveOverCap(Now);
		}
	}
	else
	{
		Assembler.NoteFailed(TEXT("mapLive"), Now);
	}
	MapTrains = FMapSide();
	MapStations = FMapSide();
}

bool SatisDash::FFrmBridge::LargeBusy() const
{
	return Factory.IsValid() || Assembler.LargeBusy();
}

bool SatisDash::FFrmBridge::RequestWorld(EWorldLayer Layer, const FString& ObservedAt, TOptional<FBlake3Hash> LastRaw, TOptional<FBlake3Hash> LastData)
{
	if (FrmWorldEndpoint(Layer) == nullptr)
	{
		return false; // a native-only layer (trainRoutes): there is nothing to ask FRM, and a null endpoint never reaches the queue
	}
	if (!Assembler.ClaimWorld(Layer))
	{
		return false;
	}
	if (!FrmLanes.Enqueue(FFrmRead{ FrmWorldEndpoint(Layer), [this, Layer, ObservedAt, LastRaw, LastData](FHttpReply& Reply)
	{
		if (Reply.Code >= 200 && Reply.Code < 300)
		{
			Assembler.SetWorldRead(Layer, MakeUnique<FFrmWorldRead>(Layer, MoveTemp(Reply.Body), ObservedAt, LastRaw, LastData));
		}
		else
		{
			Assembler.FailWorld(Layer);
		}
	} }))
	{
		// The endpoint is still in flight from a stopped run: FRM was not asked, so the layer is not requested. The caller asks again.
		Assembler.ReleaseWorld(Layer);
		return false;
	}
	return true;
}

void SatisDash::FFrmBridge::Harvest(double Now)
{
	if (Players.IsValid() && Players->bRan)
	{
		if (Players->Json.IsSet())
		{
			PlayersReady = FReady{ MoveTemp(Players->Json.GetValue()), Players->ReadAt };
		}
		else
		{
			Assembler.NoteFailed(TEXT("players"), Now);
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
			Assembler.SetFactoryPart(Factory->Parse.TakeJson(), Factory->ReadAt);
			Factory.Reset();
		}
		else if (Factory->Last == FFactoryParse::EStep::Invalid)
		{
			Assembler.NoteFailed(TEXT("factory"), Now);
			Factory.Reset();
		}
	}
}

void SatisDash::FFrmBridge::LaunchNext()
{
	// The worker's order is one list across both roles: encode, players, mapLive's two bodies, the native mapLive/extractors/
	// generators/storage builds, the factory (FRM body, then the native build), then the world layers.
	if (Assembler.LaunchEncode())
	{
		return;
	}
	if (Players.IsValid() && !Players->bRan)
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
	else if (Assembler.LaunchNativeParts())
	{
		// launched: the first native build that was waiting
	}
	else if (Factory.IsValid() && !Factory->bRan)
	{
		Worker.Launch([Job = Factory]
		{
			Job->Last = Job->Parse.Step();
			Job->bRan = true;
		});
	}
	else if (Assembler.LaunchFactory())
	{
		// launched: the native factory build
	}
	else
	{
		Assembler.LaunchWorld();
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
		Assembler.NoteFailed(TEXT("factory"), Now);
	}
	if (Worker.IsIdle())
	{
		Assembler.Harvest(Now);
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

void SatisDash::FFrmBridge::Stop()
{
	FrmLanes.Cancel();
	Players.Reset();
	Factory.Reset();
	PlayersReady.Reset();
	MapTrains = FMapSide();
	MapStations = FMapSide();
	bPlayersAbsent = false;
	bFactoryFailed = false;
	Assembler.Stop();
}
