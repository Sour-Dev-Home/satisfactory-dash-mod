#include "SatisDash/WorldSender.h"

#include "SatisDash/Log.h"
#include "SatisDash/Native/Conveyors/NativeConveyors.h"
#include "SatisDash/Native/Rails/NativeRails.h"
#include "SatisDash/Native/ResourceNodes/NativeResourceNodes.h"
#include "SatisDash/Native/TrainRoutes/NativeTrainRoutes.h"
#include "SatisDash/Pusher.h"
#include "SatisDash/Requests.h"

namespace
{
	/** The first class names that differ, and the most tier lines, one pass writes (R6). */
	constexpr int32 WorldSenderDifferClassesLogged = 5;
	constexpr int32 WorldSenderCensusMaxLines = 16;

	/**
	 * A layer comes from the game itself when its source says so and a reader is there: resource nodes by resourceNodesSource (R4),
	 * train routes by trainsSource (R3), rails by railsSource (R5, the shared logistics pass).
	 */
	bool WorldSenderIsNative(SatisDash::EWorldLayer Layer, const SatisDash::FWorldContext& Context)
	{
		if (Context.NativeReaders == nullptr || Context.Config == nullptr)
		{
			return false;
		}
		switch (Layer)
		{
		case SatisDash::EWorldLayer::ResourceNodes: return Context.Config->GetResourceNodesSource() == SatisDash::ESource::Native;
		case SatisDash::EWorldLayer::TrainRoutes: return Context.Config->GetTrainsSource() == SatisDash::ESource::Native;
		case SatisDash::EWorldLayer::Rails: return Context.Config->GetRailsSource() == SatisDash::ESource::Native;
		case SatisDash::EWorldLayer::Conveyors: return Context.Config->GetConveyorsSource() == SatisDash::ESource::Native;
		// S4-A: the getters exist (Task 2) but no scheduler branch reads these layers yet, so each stays false (a native layer
		// with no branch must not be reachable). Tasks 5, 8 and 11 turn them on:
		// MapMarkers: Context.Config->GetMapMarkersSource() == Native; DroneRoutes: GetDroneRoutesSource(); PowerLines: GetPowerLinesSource().
		case SatisDash::EWorldLayer::MapMarkers:
		case SatisDash::EWorldLayer::DroneRoutes:
		case SatisDash::EWorldLayer::PowerLines: return false;
		default: return false;
		}
	}

	/** The layers read by the shared logistics pass (rails and conveyors). */
	bool WorldSenderIsLogistics(SatisDash::EWorldLayer Layer)
	{
		return Layer == SatisDash::EWorldLayer::Rails || Layer == SatisDash::EWorldLayer::Conveyors;
	}

	const TCHAR* WorldSenderKindText(SatisDash::EConveyorKind Kind)
	{
		switch (Kind)
		{
		case SatisDash::EConveyorKind::Belt: return TEXT("belt");
		case SatisDash::EConveyorKind::Lift: return TEXT("lift");
		default: return TEXT("pipe");
		}
	}
}

double SatisDash::JitteredSeconds(double BaseSeconds, double Random01)
{
	const double R = FMath::IsNaN(Random01) ? 0.5 : FMath::Clamp(Random01, 0.0, 1.0);
	return BaseSeconds * (1.0 - WorldJitterFraction + R * 2.0 * WorldJitterFraction);
}

SatisDash::FWorldSender::FWorldSender(ITransport& InTransport, FFrmBridge& InBridge, TFunction<double()> InRandom01)
	: Transport{InTransport}
	, Bridge{InBridge}
	, random01Source{MoveTemp(InRandom01)}
{
}

void SatisDash::FWorldSender::Start(double Now)
{
	++Generation;
	bActive = true;
	bPostInFlight = false;
	bAuthRejected = false;
	bPassOpen = bPassRails = bPassConveyors = false;
	bConveyorCensusDone = false;
	LastNow = Now;
	StartedAt = Now;
	for (int32 I = 0; I < WorldLayerCount; ++I)
	{
		Layers[I] = FLayer();
		Layers[I].NextAt = Now + JitteredSeconds(WorldLayer(EWorldLayer(I)).FirstSeconds, random01Source());
	}
}

void SatisDash::FWorldSender::Stop()
{
	++Generation;
	bActive = false;
	bPostInFlight = false;
	bPassOpen = bPassRails = bPassConveyors = false; // the driver ends the reader's pass
	for (FLayer& Layer : Layers)
	{
		Layer = FLayer();
	}
}

bool SatisDash::FWorldSender::TakeAuthRejected()
{
	const bool bWas = bAuthRejected;
	bAuthRejected = false;
	return bWas;
}

int32 SatisDash::FWorldSender::Held() const
{
	int32 Count = 0;
	for (const FLayer& Layer : Layers)
	{
		Count += Layer.Body.Bytes.Num() > 0 ? 1 : 0;
	}
	return Count;
}

bool SatisDash::FWorldSender::ShouldLog(EWorldLayer Layer, const TCHAR* Event, double Now)
{
	return Log.ShouldLog(FString::Printf(TEXT("%s:%s"), WorldLayer(Layer).Name, Event), Now);
}

void SatisDash::FWorldSender::ScheduleNext(EWorldLayer Layer, double Now)
{
	FLayer& L = Layers[int32(Layer)];
	L.Phase = EPhase::Waiting;
	L.NextAt = Now + JitteredSeconds(WorldLayer(Layer).IntervalSeconds, random01Source());
}

void SatisDash::FWorldSender::Tick(const FWorldContext& Context)
{
	if (!bActive)
	{
		return;
	}
	LastNow = Context.Now;
	if (bPassOpen)
	{
		StepLogisticsPass(Context);
	}
	for (int32 I = 0; I < WorldLayerCount; ++I)
	{
		TickLayer(EWorldLayer(I), Context);
		if (!bActive)
		{
			return; // a 401 stopped it
		}
	}
}

void SatisDash::FWorldSender::TickLayer(EWorldLayer Layer, const FWorldContext& Context)
{
	FLayer& L = Layers[int32(Layer)];
	const TCHAR* Name = WorldLayer(Layer).Name;
	switch (L.Phase)
	{
	case EPhase::Waiting:
		if (Context.Now >= L.NextAt)
		{
			if (WorldLayer(Layer).Endpoint == nullptr && !WorldSenderIsNative(Layer, Context))
			{
				return; // a native-only layer whose source is the bridge is never scheduled: there is no FRM read to fall back to
			}
			if (WorldSenderIsNative(Layer, Context))
			{
				if (WorldLayer(Layer).NativeFirstSeconds > 0.0 && !L.bFirstNativeDone)
				{
					// Ruling M7: a native layer's first read is later than the bridge's, so no pass falls in the host's start-up.
					L.bFirstNativeDone = true;
					L.NextAt = StartedAt + JitteredSeconds(WorldLayer(Layer).NativeFirstSeconds, random01Source());
					if (Context.Now < L.NextAt)
					{
						return;
					}
				}
				if (!Context.bGameRunning)
				{
					return; // an idle server (no game) has no world to scan: nothing starts and nothing is logged; it starts once a game runs
				}
				if (Layer == EWorldLayer::TrainRoutes && Context.NativeReaders->IsTrainsReadOpen())
				{
					return; // the driver's mapLive read holds the one cursor (a few ticks): look again next tick, not a failed scan
				}
				if (WorldSenderIsLogistics(Layer))
				{
					if (bPassOpen || Context.NativeReaders->IsLogisticsReadOpen())
					{
						return; // a pass is under way (ours, or the reader's): look again next tick, not a failed scan
					}
					const bool bRails = WorldSenderIsNative(EWorldLayer::Rails, Context) && Layers[int32(EWorldLayer::Rails)].Phase == EPhase::Waiting;
					const bool bConveyors = WorldSenderIsNative(EWorldLayer::Conveyors, Context) && Layers[int32(EWorldLayer::Conveyors)].Phase == EPhase::Waiting;
					if (Context.LogisticsBudget != nullptr && Context.NativeReaders->BeginLogisticsRead(bRails, bConveyors))
					{
						Context.LogisticsBudget->Reset();
						bPassOpen = true;
						bPassRails = bRails;
						bPassConveyors = bConveyors;
						if (bRails)
						{
							Layers[int32(EWorldLayer::Rails)].Phase = EPhase::Scanning;
							Layers[int32(EWorldLayer::Rails)].bFirstNativeDone = true; // pulled in early by the other layer: no second "first" wait after it
						}
						if (bConveyors)
						{
							Layers[int32(EWorldLayer::Conveyors)].Phase = EPhase::Scanning;
						}
					}
					else
					{
						L.NextAt = Context.Now + NodeScanRetrySeconds; // no world yet; FRM is not asked
					}
					return;
				}
				const bool bBegun = Layer == EWorldLayer::TrainRoutes
					? Context.TrainsBudget != nullptr && Context.NativeReaders->BeginTrainsRead()
					: Context.NativeReaders->BeginResourceNodeScan();
				if (bBegun)
				{
					if (Layer == EWorldLayer::TrainRoutes)
					{
						Context.TrainsBudget->Reset();
					}
					L.Phase = EPhase::Scanning;
				}
				else
				{
					L.NextAt = Context.Now + NodeScanRetrySeconds; // no world yet (or the reader is busy); FRM is not asked
				}
			}
			else if (Bridge.RequestWorld(Layer, Context.Utc.ToIso8601(), L.RawHash, L.DataHash))
			{
				L.Phase = EPhase::Reading;
			}
		}
		return;
	case EPhase::Scanning:
	{
		if (WorldSenderIsLogistics(Layer))
		{
			return; // the shared pass is stepped once a tick, at the top of Tick
		}
		if (!WorldSenderIsNative(Layer, Context))
		{
			if (Layer == EWorldLayer::TrainRoutes && Context.NativeReaders != nullptr)
			{
				Context.NativeReaders->EndTrainsRead(); // frees what the reader buffered
			}
			ScheduleNext(Layer, Context.Now); // the source changed under a scan: drop it
			return;
		}
		if (Layer == EWorldLayer::TrainRoutes)
		{
			StepTrainRoutesScan(Context);
			return;
		}
		// The game thread's whole part: a slice of the actor scan. The sort and the JSON text are built on the worker.
		TOptional<TArray<FResourceNodeReading>> Found = Context.NativeReaders->StepResourceNodeScan(NodeScanActorsPerStep);
		if (!Found.IsSet())
		{
			return;
		}
		if (Found.GetValue().Num() == 0)
		{
			// No levels, no actors or no nodes: the world could not be read. An empty layer would replace the backend's good
			// copy, so nothing is posted; try again soon. Counts only in the log.
			if (!L.bScanLogged && ShouldLog(Layer, TEXT("scan"), Context.Now))
			{
				UE_LOG(LogSatisDash, Warning, TEXT("World layer %s: the game scan found no nodes; nothing sent, will scan again in %.0f s"), Name, NodeScanRetrySeconds);
			}
			L.bScanLogged = true;
			L.Phase = EPhase::Waiting;
			L.NextAt = Context.Now + NodeScanRetrySeconds;
			return;
		}
		L.bScanLogged = false;
		if (Bridge.SubmitNativeWorld(Layer, Context.Utc.ToIso8601(), MoveTemp(Found.GetValue()), L.DataHash))
		{
			L.Phase = EPhase::Reading;
		}
		else
		{
			ScheduleNext(Layer, Context.Now);
		}
		return;
	}
	case EPhase::Reading:
		if (TOptional<FWorldOutcome> Outcome = Bridge.TakeWorld(Layer))
		{
			Settle(Layer, MoveTemp(Outcome.GetValue()), Context);
		}
		return;
	case EPhase::Pending:
		if (Context.Now >= L.NextAttemptAt && !bPostInFlight && Context.bSnapshotsIdle && Context.Config != nullptr && Context.Credential != nullptr)
		{
			Post(Layer, Context);
		}
		return;
	case EPhase::Posting:
		return;
	}
}

void SatisDash::FWorldSender::StepTrainRoutesScan(const FWorldContext& Context)
{
	constexpr EWorldLayer Layer = EWorldLayer::TrainRoutes;
	FLayer& L = Layers[int32(Layer)];
	if (Context.TrainsBudget == nullptr)
	{
		Context.NativeReaders->EndTrainsRead();
		ScheduleNext(Layer, Context.Now);
		return;
	}
	// The game thread's whole part: a budgeted slice of the trains and stations. The sort and the JSON text are built on the worker.
	TOptional<FTrainsSnapshot> Found = Context.NativeReaders->StepTrainsRead(*Context.TrainsBudget);
	if (!Found.IsSet())
	{
		return;
	}
	if (Context.NativeReaders->TrainsReadAbandoned())
	{
		// No railroad subsystem, or a world swapped mid-read: this is not an empty railway. Nothing is posted (the backend keeps its
		// good copy), FRM is never asked, and the read is tried again soon. Counts only in the log.
		if (!L.bScanLogged && ShouldLog(Layer, TEXT("scan"), Context.Now))
		{
			UE_LOG(LogSatisDash, Warning, TEXT("World layer %s: the game's train routes read was abandoned; nothing sent, will read again in %.0f s"), WorldLayer(Layer).Name, NodeScanRetrySeconds);
		}
		L.bScanLogged = true;
		L.Phase = EPhase::Waiting;
		L.NextAt = Context.Now + NodeScanRetrySeconds;
		return;
	}
	L.bScanLogged = false;
	// A finished read with no trains and no stations is a real, empty railway: an empty layer is posted (the contract accepts it).
	// Counts and times only; never an id or a name.
	UE_LOG(LogSatisDash, Display, TEXT("Train routes read (game thread only): %d stations, %d trains in %d steps, longest step %.2f ms, total %.2f ms"),
		Found->Stations.Num(), Found->Trains.Num(), Context.TrainsBudget->Steps(), Context.TrainsBudget->LongestMs(), Context.TrainsBudget->TotalMs());
	if (Bridge.SubmitNativeWorld(Layer, Context.Utc.ToIso8601(),
		FNativeWorldBuilder([Snapshot = MoveTemp(Found.GetValue())](const FString& Observed) { return BuildNativeTrainRoutes(Observed, Snapshot.Stations, Snapshot.Trains); }), L.DataHash))
	{
		L.Phase = EPhase::Reading;
	}
	else
	{
		ScheduleNext(Layer, Context.Now);
	}
}

void SatisDash::FWorldSender::StepLogisticsPass(const FWorldContext& Context)
{
	constexpr EWorldLayer RailsLayer = EWorldLayer::Rails;
	constexpr EWorldLayer ConveyorsLayer = EWorldLayer::Conveyors;
	FLayer& Rails = Layers[int32(RailsLayer)];
	FLayer& Conveyors = Layers[int32(ConveyorsLayer)];
	IGameReader* Reader = Context.NativeReaders;
	// An included layer whose source changed under the pass drops out of it.
	if (bPassRails && !WorldSenderIsNative(RailsLayer, Context))
	{
		bPassRails = false;
		if (Rails.Phase == EPhase::Scanning)
		{
			ScheduleNext(RailsLayer, Context.Now);
		}
	}
	if (bPassConveyors && !WorldSenderIsNative(ConveyorsLayer, Context))
	{
		bPassConveyors = false;
		if (Conveyors.Phase == EPhase::Scanning)
		{
			ScheduleNext(ConveyorsLayer, Context.Now);
		}
	}
	if (Reader == nullptr || Context.LogisticsBudget == nullptr || (!bPassRails && !bPassConveyors))
	{
		// Nothing native is left in the pass (or its reader is gone): the reader frees what it buffered, and the pass closes.
		if (Reader != nullptr)
		{
			Reader->EndLogisticsRead();
		}
		if (bPassRails && Rails.Phase == EPhase::Scanning)
		{
			ScheduleNext(RailsLayer, Context.Now);
		}
		if (bPassConveyors && Conveyors.Phase == EPhase::Scanning)
		{
			ScheduleNext(ConveyorsLayer, Context.Now);
		}
		bPassOpen = bPassRails = bPassConveyors = false;
		return;
	}
	// The game thread's whole part: one budgeted slice of the buildables. Sorting and the JSON text are built on the worker.
	TOptional<FLogisticsSnapshot> Found = Reader->StepLogisticsRead(*Context.LogisticsBudget);
	if (!Found.IsSet())
	{
		return;
	}
	const bool bWasRails = bPassRails;
	const bool bWasConveyors = bPassConveyors;
	bPassOpen = bPassRails = bPassConveyors = false;
	if (Reader->LogisticsReadAbandoned())
	{
		// No subsystem, a world swapped mid-read, or no buildables at all: this is not an empty world. Nothing is posted (the backend
		// keeps its good copy), FRM is never asked, and the read is tried again soon. Counts only in the log.
		for (const EWorldLayer Each : { RailsLayer, ConveyorsLayer })
		{
			if (!(Each == RailsLayer ? bWasRails : bWasConveyors))
			{
				continue;
			}
			FLayer& Abandoned = Layers[int32(Each)];
			if (!Abandoned.bScanLogged && ShouldLog(Each, TEXT("scan"), Context.Now))
			{
				UE_LOG(LogSatisDash, Warning, TEXT("World layer logistics: the game's read was abandoned; nothing sent, will read again in %.0f s"), NodeScanRetrySeconds);
			}
			Abandoned.bScanLogged = true;
			Abandoned.Phase = EPhase::Waiting;
			Abandoned.NextAt = Context.Now + NodeScanRetrySeconds;
		}
		return;
	}
	Rails.bScanLogged = false;
	Conveyors.bScanLogged = false;
	const FLogisticsCounts& C = Found->Counts;
	const FStepBudget& Budget = *Context.LogisticsBudget;
	// Counts and times only; never an id or a name.
	UE_LOG(LogSatisDash, Display, TEXT("Logistics read (game thread only): layers rails=%d conveyors=%d, %d buildables, %d tracks, %d belts, %d lifts, %d pipes, %d open ends, %d ownerless ends in %d steps, longest step %.2f ms, total %.2f ms, expected tracks %d, dropped %d, over cap %d"),
		bWasRails ? 1 : 0, bWasConveyors ? 1 : 0, C.Buildables, C.Tracks, C.Belts, C.Lifts, C.Pipes, C.OpenEnds, C.OwnerlessEnds, Budget.Steps(), Budget.LongestMs(), Budget.TotalMs(),
		C.ExpectedTracks, C.Dropped, C.Over);
	if (bWasConveyors)
	{
		// Game class names and tier display names (game data), counts and speeds; never an id.
		FString Classes;
		for (int32 I = 0; I < C.DifferClasses.Num() && I < WorldSenderDifferClassesLogged; ++I)
		{
			Classes += (I > 0 ? TEXT(", ") : TEXT("")) + C.DifferClasses[I];
		}
		UE_LOG(LogSatisDash, Display, TEXT("Conveyor kinds: %d ends checked, %d differ from the class table (first classes: %s)"), C.KindChecked, C.KindDiffer, *Classes);
		UE_LOG(LogSatisDash, Display, TEXT("Conveyor drone ports: %d edges end at a port, %d start at one"), C.EndsIntoPorts, C.EndsOutOfPorts);
		if (!bConveyorCensusDone)
		{
			bConveyorCensusDone = true;
			for (int32 I = 0; I < C.Tiers.Num() && I < WorldSenderCensusMaxLines; ++I)
			{
				const FTierCount& Tier = C.Tiers[I];
				UE_LOG(LogSatisDash, Display, TEXT("Conveyor tier census: %s | %s | speed %.3f | count %d"), WorldSenderKindText(Tier.Kind), *Tier.Name, Tier.RawRate, Tier.Count);
			}
		}
	}
	if (bWasRails)
	{
		if (Bridge.SubmitNativeWorld(RailsLayer, Context.Utc.ToIso8601(),
			FNativeWorldBuilder([Snapshot = MoveTemp(Found->Rails)](const FString& Observed) { return BuildNativeRails(Observed, Snapshot); }), Rails.DataHash))
		{
			Rails.Phase = EPhase::Reading;
		}
		else
		{
			ScheduleNext(RailsLayer, Context.Now);
		}
	}
	if (bWasConveyors)
	{
		if (Bridge.SubmitNativeWorld(ConveyorsLayer, Context.Utc.ToIso8601(),
			FNativeWorldBuilder([Snapshot = MoveTemp(Found->Conveyors)](const FString& Observed) { return BuildNativeConveyors(Observed, Snapshot); }), Conveyors.DataHash))
		{
			Conveyors.Phase = EPhase::Reading;
		}
		else
		{
			ScheduleNext(ConveyorsLayer, Context.Now);
		}
	}
}

void SatisDash::FWorldSender::Settle(EWorldLayer Layer, FWorldOutcome Outcome, const FWorldContext& Context)
{
	using EKind = FWorldOutcome::EKind;
	FLayer& L = Layers[int32(Layer)];
	const TCHAR* Name = WorldLayer(Layer).Name;
	// Each warning is written once per streak (the flags, as FFrmQueue's bTokenLogged), with the rate limit as a backstop.
	// A streak ends when the layer next settles without the condition. Only a parsed body says anything about drops, so a
	// failed read or a SameRaw (nothing parsed) leaves the dropped flag alone.
	const bool bParsed = Outcome.Kind == EKind::SameData || Outcome.Kind == EKind::Ready;
	if (Outcome.Dropped > 0 || Outcome.Over > 0)
	{
		if (!L.bDroppedLogged && ShouldLog(Layer, TEXT("dropped"), Context.Now))
		{
			UE_LOG(LogSatisDash, Warning, TEXT("World layer %s: the layer had %d items the map can't hold and %d past the cap; left them out"), Name, Outcome.Dropped, Outcome.Over);
		}
		L.bDroppedLogged = true;
	}
	else if (bParsed)
	{
		L.bDroppedLogged = false;
	}
	const bool bReadProblem = Outcome.Kind == EKind::ReadFailed || Outcome.Kind == EKind::Invalid;
	if (!bReadProblem)
	{
		L.bReadLogged = false;
	}
	if (Outcome.Kind != EKind::OverCap)
	{
		L.bCapLogged = false;
	}
	switch (Outcome.Kind)
	{
	case EKind::ReadFailed:
	case EKind::Invalid:
		if (!L.bReadLogged && ShouldLog(Layer, TEXT("read"), Context.Now))
		{
			UE_LOG(LogSatisDash, Warning, TEXT("Couldn't read world layer %s from FRM; skipping this cycle"), Name);
		}
		L.bReadLogged = true;
		ScheduleNext(Layer, Context.Now);
		return;
	case EKind::SameRaw:
		ScheduleNext(Layer, Context.Now);
		return;
	case EKind::SameData:
		L.RawHash = Outcome.RawHash; // nothing to send, and this body needs no second look
		ScheduleNext(Layer, Context.Now);
		return;
	case EKind::OverCap:
		if (!L.bCapLogged && ShouldLog(Layer, TEXT("cap"), Context.Now))
		{
			UE_LOG(LogSatisDash, Warning, TEXT("World layer %s is %lld bytes, over the %lld-byte cap; skipped it (the backend keeps its last copy)"), Name, Outcome.Bytes, WorldLayerMaxBytes);
		}
		L.bCapLogged = true;
		L.RawHash = Outcome.RawHash;
		ScheduleNext(Layer, Context.Now);
		return;
	case EKind::Ready:
		L.Body = MoveTemp(Outcome.Body);
		L.PendingRaw = Outcome.RawHash;
		L.PendingData = Outcome.DataHash;
		L.Attempts = 0;
		L.NextAttemptAt = Context.Now;
		L.Phase = EPhase::Pending;
		return;
	}
}

void SatisDash::FWorldSender::Post(EWorldLayer Layer, const FWorldContext& Context)
{
	FLayer& L = Layers[int32(Layer)];
	// The body is copied into the request and kept for a retry (at most the layer's gzipped size, once per change).
	const FRequestSpec Spec = BuildWorldRequest(*Context.Config, *Context.Credential, WorldLayer(Layer).Name, L.Body);
	L.Phase = EPhase::Posting;
	bPostInFlight = true;
	const uint32 SentIn = Generation;
	Transport.Send(Spec, [this, Layer, SentIn](const FHttpReply& Reply) { Finish(Layer, SentIn, Reply); });
}

void SatisDash::FWorldSender::Finish(EWorldLayer Layer, uint32 SentIn, const FHttpReply& Reply)
{
	if (SentIn != Generation || !bActive)
	{
		return; // from an older run: it changes nothing
	}
	bPostInFlight = false;
	FLayer& L = Layers[int32(Layer)];
	const TCHAR* Name = WorldLayer(Layer).Name;
	const double Now = LastNow;
	const EReplyKind Kind = ClassifyReply(Reply.Code, true);
	if (Kind == EReplyKind::AuthRejected)
	{
		bAuthRejected = true;
		bActive = false;
		bPassOpen = bPassRails = bPassConveyors = false;
		for (FLayer& Each : Layers)
		{
			Each = FLayer();
		}
		return;
	}
	if (Kind == EReplyKind::Ok && ParseWorldResponse(Reply).IsSet())
	{
		L.RawHash = L.PendingRaw;
		L.DataHash = L.PendingData;
		if (L.Attempts > 0)
		{
			UE_LOG(LogSatisDash, Display, TEXT("Pushing world layer %s recovered after %d attempts"), Name, L.Attempts);
		}
		L.Body = FEncodedBody();
		L.Attempts = 0;
		ScheduleNext(Layer, Now);
		return;
	}
	if (Kind == EReplyKind::Fatal)
	{
		// worldReader.ts: not retried, and its hash is not recorded, so the layer is tried again at its next read.
		if (ShouldLog(Layer, TEXT("refused"), Now))
		{
			UE_LOG(LogSatisDash, Warning, TEXT("The backend refused world layer %s (%d); will try again next cycle"), Name, Reply.Code);
		}
		L.Body = FEncodedBody();
		L.Attempts = 0;
		ScheduleNext(Layer, Now);
		return;
	}
	// Transient, or a 2xx not in the expected form: the same body again after max(Retry-After, backoff).
	if (L.Attempts == 0 && ShouldLog(Layer, TEXT("push"), Now))
	{
		UE_LOG(LogSatisDash, Warning, TEXT("Pushing world layer %s failed (%d); retrying with backoff"), Name, Reply.Code);
	}
	const double Delay = FMath::Max(ParseRetryAfterSeconds(Reply.RetryAfter).Get(0.0), BackoffSeconds(L.Attempts, random01Source()));
	++L.Attempts;
	L.NextAttemptAt = Now + Delay;
	L.Phase = EPhase::Pending;
}
