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

void SatisDash::FFrmBridge::RequestPass(bool bFactoryDue)
{
	FrmLanes.Enqueue(FFrmRead{ &FrmGetPlayer, [this](const FHttpReply& Reply)
	{
		if (Reply.Code >= 200 && Reply.Code < 300)
		{
			Players = MakeShared<FPlayersJob>();
			Players->Body = Reply.Body;
		}
		else
		{
			bPlayersAbsent = true; // playersMapper.ts: unreachable, timed out or an HTTP error is "FRM absent"
		}
	} });
	if (bFactoryDue)
	{
		FrmLanes.Enqueue(FFrmRead{ &FrmGetFactory, [this](const FHttpReply& Reply)
		{
			if (Reply.Code >= 200 && Reply.Code < 300)
			{
				Factory = MakeShared<FFactoryJob>(Reply.Body);
			}
			else
			{
				bFactoryFailed = true;
			}
		} });
	}
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
			PlayersReady = FReady{ MoveTemp(Players->Json.GetValue()), Now };
		}
		else
		{
			NoteFailed(TEXT("players"), Now);
		}
		Players.Reset();
	}
	if (Factory.IsValid() && Factory->bRan)
	{
		Factory->bRan = false;
		if (Factory->Last == FFactoryParse::EStep::Done)
		{
			FactoryReady = FReady{ Factory->Parse.TakeJson(), Now };
			Factory.Reset();
		}
		else if (Factory->Last == FFactoryParse::EStep::Invalid)
		{
			NoteFailed(TEXT("factory"), Now);
			Factory.Reset();
		}
	}
}

void SatisDash::FFrmBridge::LaunchNext()
{
	if (Encode.IsValid() && !Encode->bRan)
	{
		Worker.Launch([Job = Encode]
		{
			Job->Body = EncodeBody(SnapshotToJson(Job->Snapshot));
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
	else if (Factory.IsValid() && !Factory->bRan)
	{
		Worker.Launch([Job = Factory]
		{
			Job->Last = Job->Parse.Step();
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
	FrmLanes.Pump(Port, Token, Factory.IsValid());
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
	Encoded.Reset();
	bPlayersAbsent = false;
	bFactoryFailed = false;
}
