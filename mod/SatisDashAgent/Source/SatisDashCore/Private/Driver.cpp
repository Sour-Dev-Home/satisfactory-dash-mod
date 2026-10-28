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
	, FactoryBudget{[this] { return Clock.Now(); }}
	, TrainsBudget{[this] { return Clock.Now(); }}
	, RoutesBudget{[this] { return Clock.Now(); }}
	, LogisticsBudget{[this] { return Clock.Now(); }}
	, MarkersBudget{[this] { return Clock.Now(); }}
	, DronesBudget{[this] { return Clock.Now(); }}
	, BuildablesBudget{[this] { return Clock.Now(); }}
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
		bFactoryReadOpen = false; // an open game read is dropped: its readings are never submitted
		Game.EndFactoryRead();
		bTrainsReadOpen = false; // the same for the trains read (the sender's own is dropped by its Stop)
		bTrainsBeginPending = false;
		Game.EndTrainsRead();
		Game.EndLogisticsRead(); // the sender's pass is dropped by its Stop; the reader frees what it buffered
		Game.EndMapMarkersRead();
		Game.EndDroneRoutesRead();
		bBuildablesReadOpen = false; // an open Buildables pass is dropped: its readings are never submitted
		Game.EndBuildablesRead();
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
		LastGameReadAt.Reset();
		// Which source each native-capable reader uses, so the evidence can tell a native reader from the bridge. Names only.
		const FAgentConfig& Sources = Agent.GetConfig();
		UE_LOG(LogSatisDash, Display, TEXT("Readers: players=%s resourceNodes=%s factory=%s trains=%s rails=%s conveyors=%s mapMarkers=%s droneRoutes=%s powerLines=%s extractors=%s generators=%s stationRates=%s storage=%s"),
			Sources.GetPlayersSource() == ESource::Native ? TEXT("native") : TEXT("bridge"),
			Sources.GetResourceNodesSource() == ESource::Native ? TEXT("native") : TEXT("bridge"),
			Sources.GetFactorySource() == ESource::Native ? TEXT("native") : TEXT("bridge"),
			Sources.GetTrainsSource() == ESource::Native ? TEXT("native") : TEXT("bridge"),
			Sources.GetRailsSource() == ESource::Native ? TEXT("native") : TEXT("bridge"),
			Sources.GetConveyorsSource() == ESource::Native ? TEXT("native") : TEXT("bridge"),
			Sources.GetMapMarkersSource() == ESource::Native ? TEXT("native") : TEXT("bridge"),
			Sources.GetDroneRoutesSource() == ESource::Native ? TEXT("native") : TEXT("bridge"),
			Sources.GetPowerLinesSource() == ESource::Native ? TEXT("native") : TEXT("bridge"),
			Sources.GetExtractorsSource() == ESource::Native ? TEXT("native") : TEXT("bridge"),
			Sources.GetGeneratorsSource() == ESource::Native ? TEXT("native") : TEXT("bridge"),
			Sources.GetStationRatesSource() == ESource::Native ? TEXT("native") : TEXT("bridge"),
			Sources.GetStorageSource() == ESource::Native ? TEXT("native") : TEXT("bridge"));
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
	if (Config.GetExtractorsSource() != ESource::Native)
	{
		Bridge.DropExtractors(); // the source flipped to bridge: no held fragment or pending job is spliced after it
	}
	Bridge.Tick(Now, Config.GetFrmPort(), Config.FrmToken);
	if (TOptional<FEncodedBody> Encoded = Bridge.TakeEncoded())
	{
		Pusher.Enqueue(MoveTemp(Encoded.GetValue()));
	}
	// World layers (spec "Timing"): posted one at a time, and only while no snapshot is queued, in flight or being encoded.
	if (const FCredential* Credential = Agent.GetCredential())
	{
		WorldLayers.Tick({ Now, Clock.UtcNow(), &Config, Credential, !bSendInFlight && Pusher.Num() == 0 && !Bridge.EncodePending(), &Game, bGameRunning, &RoutesBudget, &LogisticsBudget, &MarkersBudget, &DronesBudget });
	}
	if (WorldLayers.TakeAuthRejected())
	{
		++Generation;
		Agent.OnAuthRejected(); // a 401 on any authenticated call while Running halts the agent (Agent.h)
		return;
	}
	TickFactoryRead(Now);
	TickBuildablesRead(Now);
	TickTrainsRead(Now);
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

void SatisDash::FDriver::TickFactoryRead(double Now)
{
	if (!bFactoryReadOpen)
	{
		return;
	}
	const FAgentConfig& Config = Agent.GetConfig();
	if (Config.GetFactorySource() != ESource::Native)
	{
		bFactoryReadOpen = false; // the source changed under a read: drop it
		Game.EndFactoryRead();
		return;
	}
	// The game thread's whole part: one budgeted slice of the buildables. Sorting and the JSON text are built on the worker.
	TOptional<TArray<FFactoryReading>> Found = Game.StepFactoryRead(FactoryBudget);
	if (!Found.IsSet())
	{
		return;
	}
	bFactoryReadOpen = false;
	if (Game.FactoryReadAbandoned())
	{
		// No subsystem or a world swapped mid-read: the part is left out of the snapshot (one warning per 600 s) and FRM is never
		// asked. A finished read with no machines is not this: it is an empty factory and is sent as {"buildings":[]}.
		NoteFailedPart(TEXT("factory"), Now);
		return;
	}
	if (!Bridge.SubmitNativeFactory(MoveTemp(Found.GetValue()), Now) && FactoryLog.ShouldLog(TEXT("held"), Now))
	{
		UE_LOG(LogSatisDash, Display, TEXT("Factory read finished while the last one is still being built: it is dropped"));
	}
}

namespace
{
	/** Which Buildables readers the config asks for: each whose source is native. */
	SatisDash::FBuildablesFlags BuildablesFlagsFor(const SatisDash::FAgentConfig& Config)
	{
		SatisDash::FBuildablesFlags Asked;
		Asked.bExtractors = Config.GetExtractorsSource() == SatisDash::ESource::Native;
		Asked.bGenerators = Config.GetGeneratorsSource() == SatisDash::ESource::Native;
		Asked.bStorage = Config.GetStorageSource() == SatisDash::ESource::Native;
		return Asked;
	}
}

void SatisDash::FDriver::TickBuildablesRead(double Now)
{
	if (!bBuildablesReadOpen)
	{
		return;
	}
	const FBuildablesFlags Wanted = BuildablesFlagsFor(Agent.GetConfig());
	if (!Wanted.bExtractors && !Wanted.bGenerators && !Wanted.bStorage)
	{
		bBuildablesReadOpen = false; // every source changed under the pass: drop it
		Game.EndBuildablesRead();
		return;
	}
	// The game thread's whole part: one budgeted slice of the buildables. The fragment text is built on the worker.
	TOptional<FBuildablesSnapshot> Found = Game.StepBuildablesRead(BuildablesBudget);
	if (!Found.IsSet())
	{
		return;
	}
	bBuildablesReadOpen = false;
	if (Game.BuildablesReadAbandoned())
	{
		// No subsystem or a world swapped mid-pass: nothing is submitted (one warning per 600 s).
		NoteFailedPart(TEXT("buildables"), Now);
		return;
	}
	// Counts and times only; never an id or a name (both are player-chosen text).
	UE_LOG(LogSatisDash, Display, TEXT("Buildables read (game thread only): %d elements, %d extractors, %d generators, %d containers in %d steps, longest step %.2f ms, total %.2f ms"),
		Found->Counts.Elements, Found->Counts.Extractors, Found->Counts.Generators, Found->Counts.Containers, BuildablesBudget.Steps(), BuildablesBudget.LongestMs(), BuildablesBudget.TotalMs());
	LogLongestStep(TEXT("Buildables"), BuildablesBudget);
	if (Found->Counts.ExtractorsOverCap > 0)
	{
		UE_LOG(LogSatisDash, Warning, TEXT("Buildables read: %d extractors past the list cap were dropped"), Found->Counts.ExtractorsOverCap);
	}
	if (BuildablesAsked.bExtractors && !Bridge.SubmitNativeExtractors(MoveTemp(Found->Extractors), Now) && BuildablesLog.ShouldLog(TEXT("held"), Now))
	{
		UE_LOG(LogSatisDash, Display, TEXT("Buildables read finished while the last extractors fragment is still being built: it is dropped"));
	}
}

void SatisDash::FDriver::TickTrainsRead(double Now)
{
	const FAgentConfig& Config = Agent.GetConfig();
	if (bTrainsBeginPending)
	{
		if (Config.GetTrainsSource() != ESource::Native)
		{
			bTrainsBeginPending = false;
		}
		else if (!Game.IsTrainsReadOpen())
		{
			bTrainsBeginPending = false;
			TrainsBudget.Reset();
			bTrainsReadOpen = Game.BeginTrainsRead();
			if (!bTrainsReadOpen)
			{
				NoteFailedPart(TEXT("mapLive"), Now); // not busy any more, so a real failure: no world or subsystem
			}
		}
		return;
	}
	if (!bTrainsReadOpen)
	{
		return;
	}
	if (Config.GetTrainsSource() != ESource::Native)
	{
		bTrainsReadOpen = false; // the source changed under a read: drop it
		Game.EndTrainsRead();
		return;
	}
	// The game thread's whole part: one budgeted slice of the trains and stations. Sorting and the JSON text are built on the worker.
	TOptional<FTrainsSnapshot> Found = Game.StepTrainsRead(TrainsBudget);
	if (!Found.IsSet())
	{
		return;
	}
	bTrainsReadOpen = false;
	if (Game.TrainsReadAbandoned())
	{
		// No railroad subsystem or a world swapped mid-read: the part is left out of the snapshot (one warning per 600 s) and FRM is
		// never asked. A finished read with no trains and no stations is not this: it is an empty railway, {"trains":[],"stations":[]}.
		NoteFailedPart(TEXT("mapLive"), Now);
		return;
	}
	// Counts and times only; never an id or a name (both are player-chosen text).
	UE_LOG(LogSatisDash, Display, TEXT("Trains read (game thread only): %d trains, %d stations in %d steps, longest step %.2f ms, total %.2f ms"),
		Found->Trains.Num(), Found->Stations.Num(), TrainsBudget.Steps(), TrainsBudget.LongestMs(), TrainsBudget.TotalMs());
	LogLongestStep(TEXT("Trains"), TrainsBudget);
	if (!Bridge.SubmitNativeMapLive(MoveTemp(Found.GetValue()), Now) && TrainsLog.ShouldLog(TEXT("held"), Now))
	{
		UE_LOG(LogSatisDash, Display, TEXT("Trains read finished while the last one is still being built: it is dropped"));
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
	Snapshot.ModVersion = AgentVersion();
	Snapshot.bGameReachable = Snapshot.Status.IsSet();
	if (Snapshot.Status.IsSet())
	{
		LastGameReadAt = Snapshot.ObservedAt; // the same UtcNow() reading as observedAt
	}
	Snapshot.LastGameReadAt = LastGameReadAt;
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
		if (bFactoryDue && Snapshot.Status->bIsGameRunning && Config.GetFactorySource() == ESource::Native)
		{
			if (bFactoryReadOpen)
			{
				// The last read has not finished: this cadence tick is skipped, not restarted. No names, counts or ids.
				if (FactoryLog.ShouldLog(TEXT("busy"), Now))
				{
					UE_LOG(LogSatisDash, Display, TEXT("Factory read still running: this cadence tick is skipped"));
				}
			}
			else
			{
				FactoryBudget.Reset();
				bFactoryReadOpen = Game.BeginFactoryRead();
				if (!bFactoryReadOpen)
				{
					NoteFailedPart(TEXT("factory"), Now); // no world or subsystem yet: the part is left out and FRM is not asked
				}
			}
		}
		const FBuildablesFlags BuildablesWanted = BuildablesFlagsFor(Config);
		if (bFactoryDue && Snapshot.Status->bIsGameRunning && (BuildablesWanted.bExtractors || BuildablesWanted.bGenerators || BuildablesWanted.bStorage))
		{
			if (bBuildablesReadOpen)
			{
				// The last pass has not finished: this cadence tick is skipped, not restarted. No names, counts or ids.
				if (BuildablesLog.ShouldLog(TEXT("busy"), Now))
				{
					UE_LOG(LogSatisDash, Display, TEXT("Buildables read still running: this cadence tick is skipped"));
				}
			}
			else
			{
				BuildablesBudget.Reset();
				BuildablesAsked = BuildablesWanted;
				bBuildablesReadOpen = Game.BeginBuildablesRead(BuildablesWanted);
				if (!bBuildablesReadOpen)
				{
					NoteFailedPart(TEXT("buildables"), Now); // no world or subsystem yet: nothing is spliced into the factory part
				}
			}
		}
		if (bFactoryDue && Snapshot.Status->bIsGameRunning && Config.GetTrainsSource() == ESource::Native)
		{
			if (bTrainsBeginPending || (!bTrainsReadOpen && Game.IsTrainsReadOpen()))
			{
				// The sender's train-routes read holds the one cursor (a few ticks): this round starts on the first free tick. Not a failed read.
				bTrainsBeginPending = true;
				if (TrainsLog.ShouldLog(TEXT("wait"), Now))
				{
					UE_LOG(LogSatisDash, Display, TEXT("Trains read waits for the train-routes read to finish"));
				}
			}
			else if (bTrainsReadOpen)
			{
				// The last read has not finished: this cadence tick is skipped, not restarted. No names, counts or ids.
				if (TrainsLog.ShouldLog(TEXT("busy"), Now))
				{
					UE_LOG(LogSatisDash, Display, TEXT("Trains read still running: this cadence tick is skipped"));
				}
			}
			else
			{
				TrainsBudget.Reset();
				bTrainsReadOpen = Game.BeginTrainsRead();
				if (!bTrainsReadOpen)
				{
					NoteFailedPart(TEXT("mapLive"), Now); // no world or subsystem yet (a busy cursor is handled above, not here): the part is left out and FRM is not asked
				}
			}
		}
		const bool bNativePlayers = Config.GetPlayersSource() == ESource::Native;
		Bridge.RequestPass(bFactoryDue, !bNativePlayers, Config.GetFactorySource() == ESource::Bridge, Config.GetTrainsSource() == ESource::Bridge);
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
