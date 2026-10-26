#include "SatisDash/Native/ResourceNodes/NativeResourceNodes.h"

#include "SatisDash/FrmMap.h"
#include "SatisDash/FrmRecords.h"
#include "SatisDash/FrmResourceNodes.h"
#include "SatisDash/Snapshot.h"

namespace
{
	const TCHAR* NativeNodesPurityName(SatisDash::ENodePurity Purity)
	{
		switch (Purity)
		{
		case SatisDash::ENodePurity::Impure: return TEXT("impure");
		case SatisDash::ENodePurity::Pure: return TEXT("pure");
		default: return TEXT("normal");
		}
	}

	const TCHAR* NativeNodesKindName(SatisDash::ENodeKind Kind)
	{
		switch (Kind)
		{
		case SatisDash::ENodeKind::FrackingSatellite: return TEXT("frackingSatellite");
		case SatisDash::ENodeKind::Geyser: return TEXT("geyser");
		default: return TEXT("node");
		}
	}

	struct FNativeNodesKept
	{
		double Mx;
		double My;
		const SatisDash::FResourceNodeReading* Reading;
	};

	bool NativeNodesLess(const FNativeNodesKept& A, const FNativeNodesKept& B)
	{
		if (A.Mx != B.Mx)
		{
			return A.Mx < B.Mx;
		}
		if (A.My != B.My)
		{
			return A.My < B.My;
		}
		const int32 ByResource = A.Reading->Resource.Compare(B.Reading->Resource, ESearchCase::CaseSensitive);
		if (ByResource != 0)
		{
			return ByResource < 0;
		}
		if (A.Reading->Purity != B.Reading->Purity)
		{
			return uint8(A.Reading->Purity) < uint8(B.Reading->Purity);
		}
		if (A.Reading->Kind != B.Reading->Kind)
		{
			return uint8(A.Reading->Kind) < uint8(B.Reading->Kind);
		}
		return !A.Reading->bExploited && B.Reading->bExploited;
	}
}

SatisDash::FNativeWorldBody SatisDash::BuildNativeResourceNodes(const FString& ObservedAt, const TArray<FResourceNodeReading>& Readings)
{
	FNativeWorldBody Out;
	TArray<FNativeNodesKept> Kept;
	Kept.Reserve(Readings.Num());
	for (const FResourceNodeReading& Reading : Readings)
	{
		const double Mx = ToWholeMetres(Reading.XCm);
		const double My = ToWholeMetres(Reading.YCm);
		if (!InWholeMetreBounds(Mx) || !InWholeMetreBounds(My))
		{
			++Out.Dropped;
			continue;
		}
		Kept.Add({ Mx, My, &Reading });
	}
	Kept.Sort(NativeNodesLess);
	Out.Json = WorldBodyPrefix(ObservedAt);
	Out.DataFrom = Out.Json.Len();
	Out.Json.AppendChar(TEXT('['));
	for (const FNativeNodesKept& Node : Kept)
	{
		if (Out.Items == MaxResourceNodes)
		{
			++Out.Over;
			continue;
		}
		if (Out.Items > 0)
		{
			Out.Json.AppendChar(TEXT(','));
		}
		AppendResourceNodeRecord(Out.Json, Node.Reading->Resource, NativeNodesPurityName(Node.Reading->Purity), NativeNodesKindName(Node.Reading->Kind), Node.Mx, Node.My, Node.Reading->bExploited);
		++Out.Items;
	}
	Out.Json.AppendChar(TEXT(']'));
	Out.DataEnd = Out.Json.Len();
	Out.Json += TEXT("}");
	Out.Bytes = FTCHARToUTF8_Convert::ConvertedLength(*Out.Json, Out.Json.Len());
	return Out;
}
