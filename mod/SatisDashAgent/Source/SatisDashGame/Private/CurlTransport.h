#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "SatisDash/Ports.h"
#include "SatisDash/RateLimit.h"
#include "SatisDash/Reply.h"

struct curl_slist;

namespace SatisDash
{
	/**
	 * ITransport over the mod's own libcurl easy handles (Plan 2B Task 1e): redirects are never followed, only https, a TLS 1.2
	 * minimum. One multi handle, pumped by a core-ticker delegate on the game thread; replies are delivered only from that
	 * tick, so they always arrive later, on the game thread, and never inside Send (Ports.h). No threads of its own.
	 * libcurl must be globally initialised before the first Send (the module does it, SatisDashGameModule.cpp).
	 */
	class FCurlTransport : public ITransport
	{
	public:
		FCurlTransport() = default;
		FCurlTransport(const FCurlTransport&) = delete;
		FCurlTransport& operator=(const FCurlTransport&) = delete;
		virtual ~FCurlTransport() override;
		virtual void Send(const FRequestSpec& Spec, FReplyHandler OnReply) override;
		/** Cancels every request still out and releases the multi handle. Their handlers never run. For module shutdown. */
		void CancelAll();

		/** One request. Public only so the file's libcurl callbacks can reach it; nothing outside the transport uses it. */
		struct FPending
		{
			FString Url;
			FReplyHandler OnReply;
			FCappedBody CappedBody;
			/** The POST body; libcurl reads it in place (CURLOPT_POSTFIELDS), so it lives as long as the transfer. */
			TArray<uint8> PostBody;
			FString RetryAfter;
			/** CURL*, null once released. */
			void* Easy = nullptr;
			curl_slist* Headers = nullptr;
			/** Set when the transfer ended (or never started); the next tick delivers Raw. */
			bool bDone = false;
			FRawReply Raw;
		};

	private:
		bool Tick();
		/** Sets up and adds Pending's easy handle; false when it couldn't (Pending is then answered on the next tick). */
		bool Start(FPending& Pending, const FRequestSpec& Spec);
		void Complete(FPending& Pending, int32 Result);
		void Release(FPending& Pending);
		void Deliver(FPending& Pending);

		TArray<TUniquePtr<FPending>> InFlight;
		/** CURLM*, created on the first Send. */
		void* Multi = nullptr;
		FTSTicker::FDelegateHandle TickHandle;
		bool bTicking = false;
		/** Bumped by CancelAll: a delivery loop that sees it change stops, so no handler runs after a cancel. */
		uint32 Generation = 0;
		FRateLimitedLog RefusalLog;
	};
}
