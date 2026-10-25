#include "SatisDash/WorldSender.h"

#include "SatisDash/Log.h"
#include "SatisDash/Native/ResourceNodes/NativeResourceNodes.h"
#include "SatisDash/Pusher.h"
#include "SatisDash/Requests.h"

namespace
{
	/** R4: the resourceNodes layer comes from the game itself when the config says so and a reader is there. */
	bool WorldSenderIsNative(SatisDash::EWorldLayer Layer, const SatisDash::FWorldContext& Context)
	{
		return Layer == SatisDash::EWorldLayer::ResourceNodes && Context.NativeReaders != nullptr && Context.Config != nullptr
			&& Context.Config->GetResourceNodesSource() == SatisDash::ESource::Native;
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
	LastNow = Now;
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
			if (WorldSenderIsNative(Layer, Context))
			{
				if (!Context.bGameRunning)
					{
						return; // an idle server (no game) has no world to scan: nothing starts and nothing is logged; it starts once a game runs
					}
					if (Context.NativeReaders->BeginResourceNodeScan())
				{
					L.Phase = EPhase::Scanning;
				}
				else
				{
					L.NextAt = Context.Now + NodeScanRetrySeconds; // no world yet; FRM is not asked
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
		if (!WorldSenderIsNative(Layer, Context))
		{
			ScheduleNext(Layer, Context.Now); // the source changed under a scan: drop it
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
