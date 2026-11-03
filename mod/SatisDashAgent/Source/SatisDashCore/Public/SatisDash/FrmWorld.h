#pragma once

#include "CoreMinimal.h"
#include "Hash/Blake3.h"
#include "SatisDash/Frm.h"
#include "SatisDash/FrmRecords.h"
#include "SatisDash/WorldRead.h"

namespace SatisDash
{
	/** The FRM read of the layer; null for a native-only layer (trainRoutes and the others), which is never requested from FRM. */
	SATISDASHCORE_API const FFrmEndpoint* FrmWorldEndpoint(EWorldLayer Layer);

	/**
	 * One FRM world body being hashed, parsed and encoded on the worker, a slice per Step (see Plan 3B Task 7). Touched by one
	 * thread at a time. Not copyable: the hasher is not. The layers, the outcome, the native reads and Settle are WorldRead.h's.
	 */
	class SATISDASHCORE_API FFrmWorldRead final : public FWorldRead
	{
	public:
		FFrmWorldRead(EWorldLayer InLayer, TArray<uint8> InBody, FString InObservedAt, TOptional<FBlake3Hash> InLastRaw, TOptional<FBlake3Hash> InLastData, int64 InMaxBodyBytes = WorldLayerMaxBytes);

		/** One slice of work; true when the outcome is ready. */
		virtual bool Step() override;

	private:
		enum class EStage : uint8 { Hashing, Parsing, Done };
		/** The end of an FRM read: settles the finished parse. */
		void Finish();

		TArray<uint8> Body;
		TOptional<FBlake3Hash> LastRaw;
		EStage Stage = EStage::Hashing;
		int64 Hashed = 0;
		FBlake3 Hasher;
		TUniquePtr<FFrmArrayParse> Owned;
	};
}
