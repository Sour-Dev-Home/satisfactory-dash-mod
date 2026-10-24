#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Requests.h"
#include "SatisDash/Responses.h"
#include "SatisDash/Snapshot.h"

namespace SatisDash
{
	using FReplyHandler = TFunction<void(const FHttpReply&)>;

	/** How a request reaches the backend: the mod's own libcurl in the game (SatisDashGame), a fake in the tests. */
	class ITransport
	{
	public:
		virtual ~ITransport() = default;
		/**
		 * Sends Spec and calls OnReply exactly once, later, on the game thread, and never from inside Send. A timeout, a
		 * refused connection, a TLS failure, a redirect or a body over MaxResponseBytes arrives as a Code 0 reply.
		 */
		virtual void Send(const FRequestSpec& Spec, FReplyHandler OnReply) = 0;
	};

	/** The game's state, read natively on the game thread (spec "Data flow"): a few lookups each, no I/O. */
	class IGameReader
	{
	public:
		virtual ~IGameReader() = default;
		/** Unset when the server's state can't be read (no game world, or no server subsystem in it yet). */
		virtual TOptional<FStatusReading> ReadStatus() = 0;
		/** FG.DSAutoPause, applied and pending; unset when it can't be read. */
		virtual TOptional<FAutoPauseReading> ReadAutoPause() = 0;
		/** One reading per power circuit group; unset when there is no circuit subsystem. */
		virtual TOptional<TArray<FCircuitReading>> ReadPower() = 0;
		/** Sets FG.DSAutoPause and returns the read-back (unset when it can't be read); the caller judges it. */
		virtual TOptional<FAutoPauseReading> ApplyAutoPause(bool bEnabled) = 0;
	};

	/**
	 * Runs pure work off the game thread (FRM bridge spec "Threading"): UE::Tasks in the game (SatisDashGame's
	 * FTaskWorker), inline in the tests. One task at a time: Launch is only called while IsIdle().
	 */
	using FWorkFn = TUniqueFunction<void()>;

	class IWorker
	{
	public:
		virtual ~IWorker() = default;
		virtual void Launch(FWorkFn Work) = 0;
		/** True when nothing launched is still running; what it wrote is then safe to read on the game thread. */
		virtual bool IsIdle() const = 0;
	};
}
