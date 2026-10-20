#include "SatisDash/Driver.h"

#include "SatisDash/Log.h"
#include "SatisDash/Snapshot.h"
#include "SatisDash/Version.h"

SatisDash::FDriver::FDriver(FString ConfigPath, FString CredentialPath, ITransport& InTransport, IGameReader& InGame, FDriverClock InClock)
	: Clock{MoveTemp(InClock)}
	, Transport{InTransport}
	, Game{InGame}
	, Agent{MoveTemp(ConfigPath), MoveTemp(CredentialPath), Clock.Random01}
	, Pusher{Clock.Random01}
{
}

void SatisDash::FDriver::Boot()
{
	Agent.Boot();
	NextConfigAt = Clock.Now() + ConfigCheckSeconds;
}

void SatisDash::FDriver::Tick()
{
	const double Now = Clock.Now();
	if (Now >= NextConfigAt)
	{
		NextConfigAt = Now + ConfigCheckSeconds;
		Agent.CheckConfig();
	}
	if (TOptional<FRequestSpec> EnrollRequest = Agent.StartEnroll(Now))
	{
		Transport.Send(*EnrollRequest, [this](const FHttpReply& Reply) { Agent.FinishEnroll(Reply, Clock.Now()); });
	}

	// S10: "running" means a credential for the backend the config names now, not just the state's name (2A final, N2).
	const bool bNowRunning = Agent.GetCredential() != nullptr;
	if (bRunning && !bNowRunning)
	{
		bClearPending = true;
		StopCommands();
	}
	if (!bRunning && bNowRunning)
	{
		NextStatusAt = Now;
		LastPowerAt.Reset();
	}
	bRunning = bNowRunning;
	if (bClearPending && !bSendInFlight)
	{
		Pusher.Clear();
		bClearPending = false;
	}
	if (!bRunning || bClearPending)
	{
		return;
	}
	TickSample(Now);
	TickPush(Now);
	TickCommands(Now);
}

void SatisDash::FDriver::NoteFailedPart(const TCHAR* Part, double Now)
{
	if (PartLog.ShouldLog(Part, Now))
	{
		UE_LOG(LogSatisDash, Warning, TEXT("Couldn't read %s from the game; leaving it out of the snapshot"), Part);
	}
}

void SatisDash::FDriver::TickSample(double Now)
{
	if (Now < NextStatusAt)
	{
		return;
	}
	const FCadence& Cadence = Agent.GetCadence();
	NextStatusAt = Now + Cadence.StatusSeconds - DueSlackSeconds;
	FSnapshot Snapshot;
	Snapshot.AgentVersion = AgentVersion();
	Snapshot.ObservedAt = Clock.UtcNow();
	Snapshot.Status = Game.ReadStatus();
	if (!Snapshot.Status.IsSet())
	{
		NoteFailedPart(TEXT("status"), Now);
	}
	else
	{
		// Auto-pause is read with status (spec "Data flow").
		Snapshot.AutoPause = Game.ReadAutoPause();
		if (!Snapshot.AutoPause.IsSet())
		{
			NoteFailedPart(TEXT("auto-pause"), Now);
		}
		const bool bPowerDue = !LastPowerAt.IsSet() || Now - LastPowerAt.GetValue() >= Cadence.PowerSeconds - DueSlackSeconds;
		if (Snapshot.Status->bIsGameRunning && bPowerDue)
		{
			LastPowerAt = Now;
			Snapshot.Power = Game.ReadPower();
			if (!Snapshot.Power.IsSet())
			{
				NoteFailedPart(TEXT("power"), Now);
			}
		}
	}
	Pusher.Enqueue(SnapshotToJson(Snapshot));
}

void SatisDash::FDriver::TickPush(double Now)
{
	if (bSendInFlight)
	{
		return;
	}
	// Agent.h: fetched again for every authenticated request, and copied into the request, never kept.
	const FCredential* Credential = Agent.GetCredential();
	if (Credential == nullptr)
	{
		return;
	}
	const TOptional<FString> SnapshotJson = Pusher.StartSend(Now);
	if (!SnapshotJson.IsSet())
	{
		return;
	}
	bSendInFlight = true;
	Transport.Send(BuildSnapshotRequest(Agent.GetConfig(), *Credential, *SnapshotJson), [this](const FHttpReply& Reply)
	{
		bSendInFlight = false;
		const FPusher::FOutcome Outcome = Pusher.FinishSend(Reply, Clock.Now());
		if (Outcome.bHalt)
		{
			Agent.OnAuthRejected();
			return;
		}
		if (Outcome.Response.IsSet())
		{
			Agent.SetCadence(Outcome.Response->Cadence);
		}
	});
}

void SatisDash::FDriver::TickCommands(double Now)
{
	// Task 5.
}

void SatisDash::FDriver::StopCommands()
{
	// Task 5.
}
