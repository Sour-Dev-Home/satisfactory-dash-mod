#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "SatisDash/Ports.h"
#include "SatisDash/RateLimit.h"
#include "SatisDash/Reply.h"

namespace SatisDash
{
	/** ITransport over UE's HTTP module (libcurl on Windows, ruling 1). Replies arrive on the game thread (ruling 6). */
	class FHttpTransport : public ITransport
	{
	public:
		virtual ~FHttpTransport() override;
		virtual void Send(const FRequestSpec& Spec, FReplyHandler OnReply) override;
		/** Cancels every request still out. Their handlers never run. For module shutdown. */
		void CancelAll();

	private:
		struct FPending
		{
			FString Url;
			FReplyHandler OnReply;
			TSharedPtr<FCappedBody> Body;
			FHttpRequestPtr Request;
			bool bAnswered = false;
		};
		void Finish(const TSharedRef<FPending>& Pending, const FRawReply& Raw);

		TArray<TSharedRef<FPending>> InFlight;
		/** Replaced by CancelAll: a callback holding the old one knows the transport no longer wants it. */
		TSharedRef<bool> Alive = MakeShared<bool>(true);
		FRateLimitedLog RefusalLog;
	};
}
