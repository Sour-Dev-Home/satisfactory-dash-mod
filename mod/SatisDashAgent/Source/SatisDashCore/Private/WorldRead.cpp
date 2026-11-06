#include "SatisDash/WorldRead.h"

namespace
{
	constexpr SatisDash::FWorldLayerSpec WorldLayerTable[] = {
		{ TEXT("rails"), 120.0, 600.0, 480.0 },
		{ TEXT("resourceNodes"), 480.0, 1800.0, 0.0 },
		// R3: native-only (no other source ever provided it): first read 8 minutes after Running, then every 10 minutes (ADR 0055). The first-read
		// rule is the resource-node layer's (8 minutes, jittered +/-10% by WorldJitterFraction in JitteredSeconds), so the first post
		// falls inside tick-cost-native's measured span (warmup 3 minutes, window to about 13) and in native-parity's 30-minute window.
		{ TEXT("trainRoutes"), 480.0, 600.0, 0.0 },
		// R6: native-only like trainRoutes (the bridge never sent conveyors): the same 8 minute first read and 10 minute interval.
		{ TEXT("conveyors"), 480.0, 600.0, 0.0 },
		// S4-A: native-only like conveyors: the same 8 minute first read and 10 minute interval (worldReader.ts).
		{ TEXT("mapMarkers"), 480.0, 600.0, 0.0 },
		{ TEXT("droneRoutes"), 480.0, 600.0, 0.0 },
		{ TEXT("powerLines"), 480.0, 600.0, 0.0 },
	};
}

const SatisDash::FWorldLayerSpec& SatisDash::WorldLayer(EWorldLayer Layer)
{
	return WorldLayerTable[int32(Layer)];
}

// R4's constructor is a thin wrapper: the readings ride in the builder and are freed with it after the first Step.
SatisDash::FWorldRead::FWorldRead(EWorldLayer InLayer, TArray<FResourceNodeReading> InReadings, FString InObservedAt, TOptional<FBlake3Hash> InLastData, int64 InMaxBodyBytes)
	: FWorldRead{InLayer, FNativeWorldBuilder([Readings = MoveTemp(InReadings)](const FString& Observed) { return BuildNativeResourceNodes(Observed, Readings); }),
		MoveTemp(InObservedAt), InLastData, InMaxBodyBytes}
{
}

SatisDash::FWorldRead::FWorldRead(EWorldLayer InLayer, FNativeWorldBuilder InBuilder, FString InObservedAt, TOptional<FBlake3Hash> InLastData, int64 InMaxBodyBytes)
	: Layer{InLayer}
	, ObservedAt{MoveTemp(InObservedAt)}
	, Builder{MoveTemp(InBuilder)}
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
	default:
		return true;
	}
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
