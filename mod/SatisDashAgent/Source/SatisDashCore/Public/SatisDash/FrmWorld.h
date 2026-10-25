#pragma once

#include "CoreMinimal.h"
#include "Hash/Blake3.h"
#include "SatisDash/Frm.h"
#include "SatisDash/FrmRecords.h"
#include "SatisDash/Requests.h"

namespace SatisDash
{
	/** The world layers sent so far (spec "What the Node agent sends"). Plan 3C adds Conveyors as a third row. */
	enum class EWorldLayer : uint8 { Rails, ResourceNodes };
	inline constexpr int32 WorldLayerCount = 2;

	struct FWorldLayerSpec
	{
		/** The layer in the URL (POST /agent/v1/world/<Name>) and in log lines. */
		const TCHAR* Name;
		const FFrmEndpoint* Endpoint;
		/** After the agent starts Running: the first read (spec "Timing": rails 2 min, resource nodes 8 min), times a jitter of +/-10%. */
		double FirstSeconds;
		/** After a cycle ends: the next read (worldReader.ts RAILS_INTERVAL_MS and RESOURCE_NODES_INTERVAL_MS), times the same jitter. */
		double IntervalSeconds;
	};
	SATISDASHCORE_API const FWorldLayerSpec& WorldLayer(EWorldLayer Layer);

	/** map.ts MAP_WORLD_LAYER_MAX_BYTES: the serialised { observedAt, data }, UTF-8, before gzip (worldReader.ts measures it so). */
	inline constexpr int64 WorldLayerMaxBytes = 2000000;
	/** The raw body is hashed this much a worker task (spec "Threading": slices). */
	inline constexpr int64 WorldHashSliceBytes = 1 << 20;

	struct FWorldOutcome
	{
		/**
		 * ReadFailed: FRM unreachable, timed out, an HTTP error, or the body over its cap. Invalid: the body is not what the
		 * Node agent's schema accepts. SameRaw: the raw body equals the last settled one (not parsed). SameData: parsed, the
		 * mapped data equals the last accepted. OverCap: the body is past WorldLayerMaxBytes. Ready: Body is to be posted.
		 */
		enum class EKind : uint8 { ReadFailed, Invalid, SameRaw, SameData, OverCap, Ready };
		EKind Kind = EKind::ReadFailed;
		FBlake3Hash RawHash;
		FBlake3Hash DataHash;
		int32 Items = 0;
		/** Valid items the mapper dropped (a bad coordinate, too few or too many points). */
		int32 Dropped = 0;
		/** Valid items past the item cap. */
		int32 Over = 0;
		/** The whole body's UTF-8 bytes, before gzip (set for Ready and OverCap). */
		int64 Bytes = 0;
		FEncodedBody Body;
	};

	/**
	 * One world body being hashed, parsed and encoded on the worker, a slice per Step (see Plan 3B Task 7). Touched by one
	 * thread at a time. Not copyable: the hasher is not.
	 */
	class SATISDASHCORE_API FWorldRead
	{
	public:
		FWorldRead(EWorldLayer InLayer, TArray<uint8> InBody, FString InObservedAt, TOptional<FBlake3Hash> InLastRaw, TOptional<FBlake3Hash> InLastData, int64 InMaxBodyBytes = WorldLayerMaxBytes);
		FWorldRead(const FWorldRead&) = delete;
		FWorldRead& operator=(const FWorldRead&) = delete;

		/** One slice of work; true when the outcome is ready. */
		bool Step();
		FWorldOutcome TakeOutcome() { return MoveTemp(Outcome); }

	private:
		enum class EStage : uint8 { Hashing, Parsing, Done };
		void Finish();

		EWorldLayer Layer;
		TArray<uint8> Body;
		FString ObservedAt;
		TOptional<FBlake3Hash> LastRaw;
		TOptional<FBlake3Hash> LastData;
		int64 MaxBodyBytes;
		EStage Stage = EStage::Hashing;
		int64 Hashed = 0;
		FBlake3 Hasher;
		TUniquePtr<FFrmArrayParse> Owned;
		FWorldOutcome Outcome;
	};
}
