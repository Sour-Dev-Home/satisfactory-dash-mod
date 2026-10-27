#include "SatisDash/FrmWorld.h"

#include "SatisDash/FrmRails.h"
#include "SatisDash/FrmResourceNodes.h"
#include "SatisDash/Log.h"

namespace
{
	constexpr SatisDash::FWorldLayerSpec FrmWorldLayers[] = {
		{ TEXT("rails"), &SatisDash::FrmGetTrainRails, 120.0, 600.0, 480.0 },
		{ TEXT("resourceNodes"), &SatisDash::FrmGetResourceNode, 480.0, 1800.0, 0.0 },
		// R3: native-only (no FRM endpoint, never requested from FRM): first read 8 minutes after Running, then every 10 minutes (ADR 0055). The first-read
		// rule is the resource-node layer's (8 minutes, jittered +/-10% by WorldJitterFraction in JitteredSeconds), so the first post
		// falls inside tick-cost-native's measured span (warmup 3 minutes, window to about 13) and in native-parity's 30-minute window.
		{ TEXT("trainRoutes"), nullptr, 480.0, 600.0, 0.0 },
		// R6: native-only like trainRoutes (the bridge never sent conveyors): the same 8 minute first read and 10 minute interval.
		{ TEXT("conveyors"), nullptr, 480.0, 600.0, 0.0 },
	};

	TUniquePtr<SatisDash::FFrmArrayParse> FrmWorldMakeParse(SatisDash::EWorldLayer Layer, TArray<uint8> Body, const FString& ObservedAt)
	{
		if (Layer == SatisDash::EWorldLayer::Rails)
		{
			return MakeUnique<SatisDash::FRailsParse>(MoveTemp(Body), ObservedAt);
		}
		if (Layer == SatisDash::EWorldLayer::ResourceNodes)
		{
			return MakeUnique<SatisDash::FResourceNodesParse>(MoveTemp(Body), ObservedAt);
		}
		return nullptr; // trainRoutes is native-only: it has no FRM body to parse
	}
}

const SatisDash::FWorldLayerSpec& SatisDash::WorldLayer(EWorldLayer Layer)
{
	return FrmWorldLayers[int32(Layer)];
}

SatisDash::FWorldRead::FWorldRead(EWorldLayer InLayer, TArray<uint8> InBody, FString InObservedAt, TOptional<FBlake3Hash> InLastRaw, TOptional<FBlake3Hash> InLastData, int64 InMaxBodyBytes)
	: Layer{InLayer}
	, Body{MoveTemp(InBody)}
	, ObservedAt{MoveTemp(InObservedAt)}
	, LastRaw{InLastRaw}
	, LastData{InLastData}
	, MaxBodyBytes{InMaxBodyBytes}
{
}

// R4's constructor is a thin wrapper: the readings ride in the builder and are freed with it after the first Step.
SatisDash::FWorldRead::FWorldRead(EWorldLayer InLayer, TArray<FResourceNodeReading> InReadings, FString InObservedAt, TOptional<FBlake3Hash> InLastData, int64 InMaxBodyBytes)
	: FWorldRead{InLayer, FNativeWorldBuilder([Readings = MoveTemp(InReadings)](const FString& Observed) { return BuildNativeResourceNodes(Observed, Readings); }),
		MoveTemp(InObservedAt), InLastData, InMaxBodyBytes}
{
}

SatisDash::FWorldRead::FWorldRead(EWorldLayer InLayer, FNativeWorldBuilder InBuilder, FString InObservedAt, TOptional<FBlake3Hash> InLastData, int64 InMaxBodyBytes)
	: Layer{InLayer}
	, Builder{MoveTemp(InBuilder)}
	, ObservedAt{MoveTemp(InObservedAt)}
	, LastData{InLastData}
	, MaxBodyBytes{InMaxBodyBytes}
	, Stage{EStage::Native}
{
}

bool SatisDash::FWorldRead::Step()
{
	switch (Stage)
	{
	case EStage::Native:
	{
		// On the worker: the sort and the JSON text happen here, never on the game thread. The readings are freed first.
		const FNativeWorldBody Built = Invoke(Builder, ObservedAt);
		Builder = FNativeWorldBuilder(); // frees the readings the builder captured
		Settle(Built.Json, Built.DataFrom, Built.DataEnd, Built.Items, Built.Dropped, Built.Over, Built.Bytes);
		Outcome.RawHash = Outcome.DataHash;
		Stage = EStage::Done;
		return true;
	}
	case EStage::Hashing:
	{
		const int64 Take = FMath::Min<int64>(WorldHashSliceBytes, int64(Body.Num()) - Hashed);
		if (Take > 0)
		{
			Hasher.Update(Body.GetData() + Hashed, uint64(Take));
			Hashed += Take;
		}
		if (Hashed < Body.Num())
		{
			return false;
		}
		Outcome.RawHash = Hasher.Finalize();
		if (LastRaw.IsSet() && LastRaw.GetValue() == Outcome.RawHash)
		{
			// The same bytes as the last settled read: nothing to parse and nothing to send.
			Outcome.Kind = FWorldOutcome::EKind::SameRaw;
			Body.Empty();
			Stage = EStage::Done;
			return true;
		}
		Owned = FrmWorldMakeParse(Layer, MoveTemp(Body), ObservedAt);
		if (!Owned.IsValid())
		{
			// Not reachable (RequestWorld refuses a layer with no endpoint): an FRM body for a native-only layer is invalid, never parsed as nodes.
			UE_LOG(LogSatisDash, Warning, TEXT("World layer %s has no FRM body format; skipped"), WorldLayer(Layer).Name);
			Outcome.Kind = FWorldOutcome::EKind::Invalid;
			Stage = EStage::Done;
			return true;
		}
		Stage = EStage::Parsing;
		return false;
	}
	case EStage::Parsing:
	{
		FFrmArrayParse& Array = *Owned;
		switch (Array.Step())
		{
		case FFrmArrayParse::EStep::More:
			return false;
		case FFrmArrayParse::EStep::Invalid:
			Outcome.Kind = FWorldOutcome::EKind::Invalid;
			Owned.Reset();
			Stage = EStage::Done;
			return true;
		case FFrmArrayParse::EStep::Done:
			Finish();
			Stage = EStage::Done;
			return true;
		}
		return false;
	}
	default:
		return true;
	}
}

void SatisDash::FWorldRead::Finish()
{
	const FFrmArrayParse& Array = *Owned;
	Settle(Array.PeekJson(), Array.DataFrom(), Array.DataEnd(), Array.Kept(), Array.Dropped(), Array.Over(), Array.Utf8Bytes());
	Owned.Reset();
}

void SatisDash::FWorldRead::Settle(const FString& Json, int32 DataFrom, int32 DataEnd, int32 Items, int32 Dropped, int32 Over, int64 Bytes)
{
	Outcome.Items = Items;
	Outcome.Dropped = Dropped;
	Outcome.Over = Over;
	Outcome.Bytes = Bytes;

	// worldReader.ts: the hash of the projected data comes first; equal to the last accepted one means nothing to send.
	FBlake3 DataHasher;
	DataHasher.Update(*Json + DataFrom, uint64(DataEnd - DataFrom) * sizeof(TCHAR));
	Outcome.DataHash = DataHasher.Finalize();
	if (LastData.IsSet() && LastData.GetValue() == Outcome.DataHash)
	{
		Outcome.Kind = FWorldOutcome::EKind::SameData;
		return;
	}
	if (Outcome.Bytes > MaxBodyBytes)
	{
		Outcome.Kind = FWorldOutcome::EKind::OverCap;
		return;
	}
	Outcome.Body = EncodeBody(Json); // gzipped over 1 KB, converted straight to UTF-8
	Outcome.Kind = FWorldOutcome::EKind::Ready;
}
