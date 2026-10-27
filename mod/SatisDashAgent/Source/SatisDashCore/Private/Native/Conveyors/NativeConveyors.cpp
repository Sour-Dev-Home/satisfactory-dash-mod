#include "SatisDash/Native/Conveyors/NativeConveyors.h"

#include "Policies/CondensedJsonPrintPolicy.h"
#include "SatisDash/FrmFactory.h"
#include "SatisDash/FrmMap.h"
#include "SatisDash/FrmRecords.h"
#include "SatisDash/Snapshot.h"
#include "Serialization/JsonWriter.h"

namespace
{
	/** An edge after the checks, the cut and the metre rounding. */
	struct FNativeConveyorsEdge
	{
		FString Id;
		SatisDash::EConveyorKind Kind = SatisDash::EConveyorKind::Belt;
		FString From, To, Tier;
		TOptional<double> Capacity;
		int32 Mx = 0;
		int32 My = 0;
	};

	/** The total order the cut and the duplicate rule rest on: id, from, to, tier (code units), then kind and position, so any input order gives the same bytes. */
	bool NativeConveyorsEdgeLess(const FNativeConveyorsEdge& A, const FNativeConveyorsEdge& B)
	{
		int32 C = A.Id.Compare(B.Id, ESearchCase::CaseSensitive);
		if (C == 0) { C = A.From.Compare(B.From, ESearchCase::CaseSensitive); }
		if (C == 0) { C = A.To.Compare(B.To, ESearchCase::CaseSensitive); }
		if (C == 0) { C = A.Tier.Compare(B.Tier, ESearchCase::CaseSensitive); }
		if (C != 0)
		{
			return C < 0;
		}
		if (A.Kind != B.Kind)
		{
			return uint8(A.Kind) < uint8(B.Kind);
		}
		if (A.Mx != B.Mx)
		{
			return A.Mx < B.Mx;
		}
		return A.My < B.My;
	}

	const TCHAR* NativeConveyorsKindText(SatisDash::EConveyorKind Kind)
	{
		switch (Kind)
		{
		case SatisDash::EConveyorKind::Lift: return TEXT("lift");
		case SatisDash::EConveyorKind::Pipe: return TEXT("pipe");
		default: return TEXT("belt");
		}
	}

	bool NativeConveyorsStartsWithAny(const FString& Cls, std::initializer_list<const TCHAR*> Prefixes)
	{
		for (const TCHAR* Prefix : Prefixes)
		{
			if (Cls.StartsWith(Prefix, ESearchCase::CaseSensitive))
			{
				return true;
			}
		}
		return false;
	}

	/** One end: an id and its kind, or two nulls for an open end. */
	void NativeConveyorsWriteEnd(SatisDash::FFactoryJsonWriter& Writer, const TCHAR* IdKey, const TCHAR* KindKey, const FString& Id)
	{
		if (Id.IsEmpty())
		{
			Writer.WriteNull(IdKey);
			Writer.WriteNull(KindKey);
			return;
		}
		Writer.WriteValue(IdKey, Id);
		Writer.WriteValue(KindKey, FString(SatisDash::ConveyorNodeKindText(SatisDash::ClassifyConveyorEnd(Id))));
	}
}

SatisDash::EConveyorNodeKind SatisDash::ClassifyConveyorEnd(const FString& Id)
{
	// The class part: one trailing _<digits> removed (/_\d+$/).
	FString Cls = Id;
	int32 Digits = 0;
	while (Digits < Cls.Len() && Cls[Cls.Len() - 1 - Digits] >= TEXT('0') && Cls[Cls.Len() - 1 - Digits] <= TEXT('9'))
	{
		++Digits;
	}
	if (Digits > 0 && Digits < Cls.Len() && Cls[Cls.Len() - 1 - Digits] == TEXT('_'))
	{
		Cls.LeftInline(Cls.Len() - 1 - Digits);
	}

	if (Cls.StartsWith(TEXT("Build_ConveyorAttachmentSplitter"), ESearchCase::CaseSensitive))
	{
		return EConveyorNodeKind::Splitter;
	}
	if (Cls.StartsWith(TEXT("Build_ConveyorAttachmentMerger"), ESearchCase::CaseSensitive))
	{
		return EConveyorNodeKind::Merger;
	}
	if (Cls.StartsWith(TEXT("Build_ConveyorBelt"), ESearchCase::IgnoreCase) || Cls.StartsWith(TEXT("Build_ConveyorLift"), ESearchCase::IgnoreCase)
		|| Cls.Equals(TEXT("Build_Pipeline_C"), ESearchCase::IgnoreCase) || Cls.Equals(TEXT("Build_PipelineMK2_C"), ESearchCase::IgnoreCase))
	{
		return EConveyorNodeKind::Conveyor;
	}
	if (NativeConveyorsStartsWithAny(Cls, { TEXT("Build_PipelineJunction"), TEXT("Build_PipelinePump") }))
	{
		return EConveyorNodeKind::Junction;
	}
	if (NativeConveyorsStartsWithAny(Cls, { TEXT("Build_Miner"), TEXT("Build_WaterPump"), TEXT("Build_OilPump"), TEXT("Build_Frack") }))
	{
		return EConveyorNodeKind::Extractor;
	}
	if (NativeConveyorsStartsWithAny(Cls, { TEXT("Build_Generator"), TEXT("Build_ResourceSink"), TEXT("Build_SpaceElevator") }))
	{
		return EConveyorNodeKind::Consumer;
	}
	if (NativeConveyorsStartsWithAny(Cls, { TEXT("Build_StorageContainer"), TEXT("Build_CentralStorage"), TEXT("Build_IndustrialTank"), TEXT("Build_PipeStorage"), TEXT("Build_FluidBuffer") }))
	{
		return EConveyorNodeKind::Storage;
	}
	if (NativeConveyorsStartsWithAny(Cls, { TEXT("Build_TrainDockingStation"), TEXT("Build_TrainStation"), TEXT("Build_DroneStation"), TEXT("Build_TruckStation") }))
	{
		return EConveyorNodeKind::Station;
	}
	if (NativeConveyorsStartsWithAny(Cls, { TEXT("Build_Constructor"), TEXT("Build_Assembler"), TEXT("Build_Manufacturer"), TEXT("Build_Smelter"), TEXT("Build_Foundry"),
		TEXT("Build_Packager"), TEXT("Build_OilRefinery"), TEXT("Build_Blender"), TEXT("Build_HadronCollider"), TEXT("Build_Converter"), TEXT("Build_QuantumEncoder") }))
	{
		return EConveyorNodeKind::Machine;
	}
	return EConveyorNodeKind::Other;
}

const TCHAR* SatisDash::ConveyorNodeKindText(EConveyorNodeKind Kind)
{
	switch (Kind)
	{
	case EConveyorNodeKind::Machine: return TEXT("machine");
	case EConveyorNodeKind::Splitter: return TEXT("splitter");
	case EConveyorNodeKind::Merger: return TEXT("merger");
	case EConveyorNodeKind::Storage: return TEXT("storage");
	case EConveyorNodeKind::Station: return TEXT("station");
	case EConveyorNodeKind::Extractor: return TEXT("extractor");
	case EConveyorNodeKind::Consumer: return TEXT("consumer");
	case EConveyorNodeKind::Junction: return TEXT("junction");
	case EConveyorNodeKind::Conveyor: return TEXT("conveyor");
	default: return TEXT("other");
	}
}

void SatisDash::NoteConveyorTier(TArray<FTierCount>& Tiers, EConveyorKind Kind, const FString& Name, double RawRate)
{
	for (FTierCount& Entry : Tiers)
	{
		if (Entry.Kind == Kind && Entry.Name.Equals(Name, ESearchCase::CaseSensitive))
		{
			++Entry.Count;
			return;
		}
	}
	if (Tiers.Num() < ConveyorMaxTiers)
	{
		FTierCount Entry;
		Entry.Kind = Kind;
		Entry.Name = Name;
		Entry.RawRate = RawRate;
		Entry.Count = 1;
		Tiers.Add(MoveTemp(Entry));
	}
}

TOptional<double> SatisDash::ConveyorCapacityPerMinute(EConveyorKind Kind, double RawRate, const FString& Tier, ECapacityMode Mode)
{
	double Value = 0.0;
	if (Kind == EConveyorKind::Pipe)
	{
		Value = RawRate * 60.0;
	}
	else if (Mode == ECapacityMode::SpeedHalf)
	{
		Value = RawRate / 2.0;
	}
	else
	{
		int32 At = Tier.Len() - 3;
		while (At >= 0 && !(Tier[At] == TEXT('M') && Tier[At + 1] == TEXT('k') && Tier[At + 2] == TEXT('.')))
		{
			--At; // the last "Mk." (Tier[At + 2] is in range: At <= Len - 3)
		}
		if (At < 0)
		{
			return TOptional<double>();
		}
		int32 Mk = 0;
		int32 Count = 0;
		for (int32 I = At + 3; I < Tier.Len() && Tier[I] >= TEXT('0') && Tier[I] <= TEXT('9') && Count < 6; ++I, ++Count)
		{
			Mk = Mk * 10 + (Tier[I] - TEXT('0'));
		}
		switch (Count > 0 ? Mk : 0)
		{
		case 1: Value = 60.0; break;
		case 2: Value = 120.0; break;
		case 3: Value = 270.0; break;
		case 4: Value = 480.0; break;
		default: return TOptional<double>();
		}
	}
	if (!FMath::IsFinite(Value) || Value < 0.0)
	{
		return TOptional<double>();
	}
	// To 4 decimals by a true division (the volatile keeps /fp:fast from multiplying by the reciprocal).
	volatile double Scale = 10000.0;
	const double Scaled = FMath::RoundToDouble(Value * Scale);
	if (!FMath::IsFinite(Scaled))
	{
		return TOptional<double>();
	}
	const double Rounded = Scaled / Scale;
	return Rounded == 0.0 ? 0.0 : Rounded; // never -0
}

SatisDash::FNativeWorldBody SatisDash::BuildNativeConveyors(const FString& ObservedAt, const TArray<FConveyorReading>& Readings)
{
	FNativeWorldBody Out;

	TArray<FNativeConveyorsEdge> Edges;
	Edges.Reserve(Readings.Num());
	for (const FConveyorReading& Reading : Readings)
	{
		if (Reading.Id.IsEmpty() || Reading.Id.Len() > ConveyorMaxIdLength || Reading.FromId.Len() > ConveyorMaxIdLength
			|| Reading.ToId.Len() > ConveyorMaxIdLength || Reading.Tier.IsEmpty())
		{
			++Out.Dropped;
			continue;
		}
		const double Mx = ToWholeMetres(Reading.XCm);
		const double My = ToWholeMetres(Reading.YCm);
		if (!InWholeMetreBounds(Mx) || !InWholeMetreBounds(My))
		{
			++Out.Dropped;
			continue;
		}
		FNativeConveyorsEdge Edge;
		Edge.Id = Reading.Id;
		Edge.Kind = Reading.Kind;
		Edge.From = Reading.FromId;
		Edge.To = Reading.ToId;
		Edge.Tier = ClampStringTo(Reading.Tier, ConveyorMaxTierLength);
		Edge.Capacity = ConveyorCapacityPerMinute(Reading.Kind, Reading.RawRate, Reading.Tier, NativeConveyorCapacityMode);
		Edge.Mx = int32(Mx);
		Edge.My = int32(My);
		Edges.Add(MoveTemp(Edge));
	}
	Edges.Sort(NativeConveyorsEdgeLess);

	FString Data;
	{
		const TSharedRef<FFactoryJsonWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Data);
		Writer->WriteArrayStart();
		const FNativeConveyorsEdge* Previous = nullptr;
		for (const FNativeConveyorsEdge& Edge : Edges)
		{
			if (Previous && Previous->Id.Equals(Edge.Id, ESearchCase::CaseSensitive))
			{
				continue; // a repeated id: the one that sorted first stays
			}
			Previous = &Edge;
			if (Out.Items == ConveyorsMaxEdges)
			{
				++Out.Over;
				continue;
			}
			Writer->WriteObjectStart();
			Writer->WriteValue(TEXT("id"), Edge.Id);
			Writer->WriteValue(TEXT("kind"), FString(NativeConveyorsKindText(Edge.Kind)));
			NativeConveyorsWriteEnd(*Writer, TEXT("from"), TEXT("fromKind"), Edge.From);
			NativeConveyorsWriteEnd(*Writer, TEXT("to"), TEXT("toKind"), Edge.To);
			if (Edge.Capacity.IsSet())
			{
				Writer->WriteValue(TEXT("capacityPerMinute"), Edge.Capacity.GetValue());
			}
			else
			{
				Writer->WriteNull(TEXT("capacityPerMinute"));
			}
			Writer->WriteValue(TEXT("tier"), Edge.Tier);
			Writer->WriteObjectStart(TEXT("at"));
			Writer->WriteValue(TEXT("x"), Edge.Mx);
			Writer->WriteValue(TEXT("y"), Edge.My);
			Writer->WriteObjectEnd();
			Writer->WriteObjectEnd();
			++Out.Items;
		}
		Writer->WriteArrayEnd();
		Writer->Close();
	}

	Out.Json = WorldBodyPrefix(ObservedAt);
	Out.DataFrom = Out.Json.Len();
	Out.Json += Data;
	Out.DataEnd = Out.Json.Len();
	Out.Json += TEXT("}");
	Out.Bytes = FTCHARToUTF8_Convert::ConvertedLength(*Out.Json, Out.Json.Len());
	return Out;
}
