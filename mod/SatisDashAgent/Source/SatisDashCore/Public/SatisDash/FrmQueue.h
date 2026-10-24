#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Frm.h"
#include "SatisDash/Ports.h"

namespace SatisDash
{
	/** One read to make. OnReply runs once, later, on the game thread, unless Cancel came first. */
	struct FFrmRead
	{
		const FFrmEndpoint* Endpoint = nullptr;
		FReplyHandler OnReply;
	};

	/**
	 * Every FRM read (FRM bridge spec "Load spreading"): at most one request in flight, because FRM answers on one
	 * thread and a second request would only wait there while holding another body here. The live lane goes first. While
	 * a large body is parsing (bLargeBusy), only a read marked bMayPassLarge (getPlayer) may start. One read of each
	 * endpoint at a time. Bounded: at most one queued read per endpoint.
	 */
	class SATISDASHCORE_API FFrmQueue
	{
	public:
		explicit FFrmQueue(ITransport& InTransport);
		FFrmQueue(const FFrmQueue&) = delete;
		FFrmQueue& operator=(const FFrmQueue&) = delete;

		/** False when a read of that endpoint is already queued or in flight. */
		bool Enqueue(FFrmRead Read);
		/** Starts the next allowed read if none is in flight. */
		void Pump(int32 Port, const FString& Token, bool bLargeBusy);
		/** Drops every queued read. A read in flight stays counted until its reply, which is then dropped. */
		void Cancel();
		bool InFlight() const { return bInFlight; }
		int32 Queued() const { return Live.Num() + World.Num(); }

	private:
		bool Has(const FFrmEndpoint* Endpoint) const;
		void NoteCode(int32 Code);

		ITransport& Transport;
		TArray<FFrmRead> Live;
		TArray<FFrmRead> World;
		const FFrmEndpoint* Current = nullptr;
		bool bInFlight = false;
		uint32 Generation = 0;
		/** The "set frmToken" line was written in this streak of 401/403 answers. */
		bool bTokenLogged = false;
	};
}
