#include "SatisDash/FrmWorld.h"

#include "SatisDash/FrmRails.h"
#include "SatisDash/FrmResourceNodes.h"

namespace
{
	constexpr SatisDash::FWorldLayerSpec FrmWorldLayers[] = {
		{ TEXT("rails"), &SatisDash::FrmGetTrainRails, 120.0, 600.0 },
		{ TEXT("resourceNodes"), &SatisDash::FrmGetResourceNode, 480.0, 1800.0 },
	};

	TUniquePtr<SatisDash::FFrmArrayParse> FrmWorldMakeParse(SatisDash::EWorldLayer Layer, TArray<uint8> Body, const FString& ObservedAt)
	{
		if (Layer == SatisDash::EWorldLayer::Rails)
		{
			return MakeUnique<SatisDash::FRailsParse>(MoveTemp(Body), ObservedAt);
		}
		return MakeUnique<SatisDash::FResourceNodesParse>(MoveTemp(Body), ObservedAt);
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

bool SatisDash::FWorldRead::Step()
{
	switch (Stage)
	{
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
	Outcome.Items = Array.Kept();
	Outcome.Dropped = Array.Dropped();
	Outcome.Over = Array.Over();
	Outcome.Bytes = Array.Utf8Bytes();

	// worldReader.ts: the hash of the projected data comes first; equal to the last accepted one means nothing to send.
	const FString& Json = Array.PeekJson();
	FBlake3 DataHasher;
	DataHasher.Update(*Json + Array.DataFrom(), uint64(Array.DataEnd() - Array.DataFrom()) * sizeof(TCHAR));
	Outcome.DataHash = DataHasher.Finalize();
	if (LastData.IsSet() && LastData.GetValue() == Outcome.DataHash)
	{
		Outcome.Kind = FWorldOutcome::EKind::SameData;
		Owned.Reset();
		return;
	}
	if (Outcome.Bytes > MaxBodyBytes)
	{
		Outcome.Kind = FWorldOutcome::EKind::OverCap;
		Owned.Reset();
		return;
	}
	Outcome.Body = EncodeBody(Json); // gzipped over 1 KB, converted straight to UTF-8
	Outcome.Kind = FWorldOutcome::EKind::Ready;
	Owned.Reset();
}
