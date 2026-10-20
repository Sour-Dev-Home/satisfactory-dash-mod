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
		/** FG.DSAutoPause; unset when it can't be read. */
		virtual TOptional<bool> ReadAutoPause() = 0;
		/** One reading per power circuit group; unset when there is no circuit subsystem. */
		virtual TOptional<TArray<FCircuitReading>> ReadPower() = 0;
		/** Sets FG.DSAutoPause and reads it back: true only when the game now reports bEnabled. */
		virtual bool ApplyAutoPause(bool bEnabled) = 0;
	};
}
