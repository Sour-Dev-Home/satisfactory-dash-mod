#include "SatisDash/FrmWorld.h"

#include "SatisDash/FrmRails.h"
#include "SatisDash/FrmResourceNodes.h"
#include "SatisDash/Log.h"

namespace
{
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

const SatisDash::FFrmEndpoint* SatisDash::FrmWorldEndpoint(EWorldLayer Layer)
{
	switch (Layer)
	{
	case EWorldLayer::Rails:
		return &FrmGetTrainRails;
	case EWorldLayer::ResourceNodes:
		return &FrmGetResourceNode;
	default:
		return nullptr; // native-only: trainRoutes, conveyors, mapMarkers, droneRoutes, powerLines
	}
}

SatisDash::FFrmWorldRead::FFrmWorldRead(EWorldLayer InLayer, TArray<uint8> InBody, FString InObservedAt, TOptional<FBlake3Hash> InLastRaw, TOptional<FBlake3Hash> InLastData, int64 InMaxBodyBytes)
	: FWorldRead{InLayer, MoveTemp(InObservedAt), InLastData, InMaxBodyBytes}
	, Body{MoveTemp(InBody)}
	, LastRaw{InLastRaw}
{
}

bool SatisDash::FFrmWorldRead::Step()
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

void SatisDash::FFrmWorldRead::Finish()
{
	const FFrmArrayParse& Array = *Owned;
	Settle(Array.PeekJson(), Array.DataFrom(), Array.DataEnd(), Array.Kept(), Array.Dropped(), Array.Over(), Array.Utf8Bytes());
	Owned.Reset();
}
