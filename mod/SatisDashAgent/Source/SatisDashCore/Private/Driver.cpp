#include "SatisDash/Driver.h"

#include "SatisDash/Log.h"
#include "SatisDash/Native/Players/NativePlayers.h"
#include "SatisDash/Snapshot.h"
#include "SatisDash/Version.h"

SatisDash::FDriver::FDriver(FString ConfigPath, FString CredentialPath, ITransport& InTransport, IGameReader& InGame, IWorker& InWorker, FDriverClock InClock)
	: Clock{MoveTemp(InClock)}
	, Transport{InTransport}
	, Game{InGame}
	, Agent{MoveTemp(ConfigPath), MoveTemp(CredentialPath), Clock.Random01}
	, Pusher{Clock.Random01}
	, Bridge{InTransport, InWorker}
	, WorldLayers{InTransport, Bridge, Clock.Random01}
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
		// FinishEnroll is the whole of the answer and always runs (one Finish per Start). An enrolment can't go stale:
		// it only starts while not Running, and CheckConfig waits for its answer, so the backend can't change under it.
		Transport.Send(*EnrollRequest, [this](const FHttpReply& Reply) { Agent.FinishEnroll(Reply, Clock.Now()); });
	}

	// S10: "running" means a credential for the backend the config names now, not just the state's name (2A final, N2).
	const bool bNowRunning = Agent.GetCredential() != nullptr;
	if (bRunning && !bNowRunning)
	{
		++Generation;
		bClearPending = true;
		StopCommands();
		Bridge.Stop();
		WorldLayers.Stop();
	}
	if (!bRunning && bNowRunning)
	{
		++Generation;
		NextStatusAt = Now;
		LastPowerAt.Reset();
		LastFactoryAt.Reset();
		NextPollAt = Now;
		PollFailures = 0;
		WorldLayers.Start(Now);
		bGameRunning = false;
		// Which source each native-capable reader uses, so the evidence can tell a native reader from the bridge. Names only.
		const FAgentConfig& Sources = Agent.GetConfig();
		UE_LOG(LogSatisDash, Display, TEXT("Readers: players=%s resourceNodes=%s"),
			Sources.GetPlayersSource() == ESource::Native ? TEXT("native") : TEXT("bridge"),
			Sources.GetResourceNodesSource() == ESource::Native ? TEXT("native") : TEXT("bridge"));
	}
	bRunning = bNowRunning;
	if (bClearPending && !bSendInFlight)
	{
		++Generation;
		Pusher.Clear();
		bClearPending = false;
	}
	if (!bRunning || bClearPending)
	{
		return;
	}
	const FAgentConfig& Config = Agent.GetConfig();
	Bridge.Tick(Now, Config.GetFrmPort(), Config.FrmToken);
	if (TOptional<FEncodedBody> Encoded = Bridge.TakeEncoded())
	{
		Pusher.Enqueue(MoveTemp(Encoded.GetValue()));
	}
	// World layers (spec "Timing"): posted one at a time, and only while no snapshot is queued, in flight or being encoded.
	if (const FCredential* Credential = Agent.GetCredential())
	{
		WorldLayers.Tick({ Now, Clock.UtcNow(), &Config, Credential, !bSendInFlight && Pusher.Num() == 0 && !Bridge.EncodePending(), &Game, bGameRunning });
	}
	if (WorldLayers.TakeAuthRejected())
	{
		++Generation;
		Agent.OnAuthRejected(); // a 401 on any authenticated call while Running halts the agent (Agent.h)
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
	// A factory snapshot is still being encoded: this pass waits, so snapshots stay in order (Pusher.h).
	if (Bridge.EncodePending())
	{
		return;
	}
	const FCadence& Cadence = Agent.GetCadence();
	const FAgentConfig& Config = Agent.GetConfig();
	NextStatusAt = Now + Cadence.StatusSeconds - DueSlackSeconds;
	FSnapshot Snapshot;
	Snapshot.AgentVersion = AgentVersion();
	Snapshot.ObservedAt = Clock.UtcNow();
	Snapshot.Status = Game.ReadStatus();
	bGameRunning = Snapshot.Status.IsSet() && Snapshot.Status->bIsGameRunning;
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
		// FRM bridge spec: players every pass, the factory on its cadence; both arrive in a later pass's snapshot.
		const bool bFactoryDue = !LastFactoryAt.IsSet() || Now - LastFactoryAt.GetValue() >= Cadence.FactorySeconds - DueSlackSeconds;
		if (bFactoryDue)
		{
			LastFactoryAt = Now;
		}
		const bool bNativePlayers = Config.GetPlayersSource() == ESource::Native;
		Bridge.RequestPass(bFactoryDue, !bNativePlayers);
		if (bNativePlayers)
		{
			// R1 (ADR-0058): the game's own lists, small enough to serialise here. Read only while a game is running, as
			// power is; an unreadable list leaves the part out.
			if (Snapshot.Status->bIsGameRunning)
			{
				if (const TOptional<TArray<FPlayerReading>> Readings = Game.ReadPlayers())
				{
					Snapshot.PlayersJson = PlayersToJson(NativePlayersPart(Readings.GetValue()));
				}
				else
				{
					NoteFailedPart(TEXT("players"), Now);
				}
			}
		}
		else
		{
			Snapshot.PlayersJson = Bridge.TakePlayers(Now, 2.0 * Cadence.StatusSeconds);
		}
		Snapshot.FactoryJson = Bridge.TakeFactory(Now, Cadence.FactorySeconds);
		Snapshot.MapLiveJson = Bridge.TakeMapLive(Now, Cadence.FactorySeconds);
	}
	if (Snapshot.FactoryJson.IsSet() || Snapshot.MapLiveJson.IsSet())
	{
		Bridge.EncodeSnapshot(MoveTemp(Snapshot)); // up to megabytes of JSON, or up to 256 KB of mapLive: serialised and gzipped on the worker
	}
	else
	{
		Pusher.Enqueue(SnapshotToJson(Snapshot));
	}
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
	TOptional<FEncodedBody> Body = Pusher.StartSend(Now);
	if (!Body.IsSet())
	{
		return;
	}
	bSendInFlight = true;
	const uint32 SentIn = Generation;
	Transport.Send(BuildSnapshotRequest(Agent.GetConfig(), *Credential, MoveTemp(Body.GetValue())), [this, SentIn](const FHttpReply& Reply)
	{
		bSendInFlight = false;
		const FPusher::FOutcome Outcome = Pusher.FinishSend(Reply, Clock.Now());
		if (SentIn != Generation)
		{
			// Sent with an older credential, or to an older backend (task 4 review): its 401 or cadence is about that
			// one, never the one held now. Its queue is cleared on the next tick (bClearPending). A revoked old
			// credential kept this way costs one more 401 if the config ever switches back to its backend.
			return;
		}
		if (Outcome.bHalt)
		{
			++Generation;
			Agent.OnAuthRejected();
			return;
		}
		if (Outcome.Response.IsSet())
		{
			Agent.SetCadence(Outcome.Response->Cadence);
			if (Outcome.Response->bCommandsPending)
			{
				NextPollAt = 0; // commandRunner.ts notifyPending: end the wait before the next poll
			}
		}
	});
}
