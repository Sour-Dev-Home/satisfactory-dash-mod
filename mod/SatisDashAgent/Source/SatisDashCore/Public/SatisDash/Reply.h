#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Serialization/Archive.h"
#include "SatisDash/Responses.h"

namespace SatisDash
{
	/**
	 * The response body as UE's HTTP thread streams it in (IHttpRequest::SetResponseBodyReceiveStream). Past
	 * MaxResponseBytes it keeps nothing more and sets an error, which ends the request while it downloads (S4).
	 */
	class SATISDASHCORE_API FCappedBody final : public FArchive
	{
	public:
		FCappedBody();
		virtual void Serialize(void* Data, int64 Length) override;
		bool Overflowed() const;
		TArray<uint8> Take();

	private:
		mutable FCriticalSection Guard;
		TArray<uint8> Bytes;
		bool bOverflowed = false;
	};

	/** What UE reported for one request, before the mod's rules (filled by SatisDashGame's transport). */
	struct FRawReply
	{
		/** The completion delegate's bProcessedSuccessfully, with a response present. */
		bool bProcessed = false;
		int32 Code = 0;
		FString ContentType;
		FString RetryAfter;
		/** IHttpBase::GetEffectiveURL(): where the answer came from after any redirect curl followed. */
		FString EffectiveUrl;
		/** LexToString of the failure reason, for the log line only. */
		FString Failure;
	};

	enum class EReplyRefusal : uint8 { None, NoAnswer, Redirected, Oversized };

	/**
	 * The reply the agent sees. Only a complete answer from exactly the URL asked is passed on; anything else is a
	 * Code 0 reply (S3, S4), which the agent retries with backoff, and OutRefusal says why.
	 */
	SATISDASHCORE_API FHttpReply MakeReply(const FRawReply& Raw, const FString& RequestUrl, FCappedBody& CappedBody, EReplyRefusal& OutRefusal);
}
