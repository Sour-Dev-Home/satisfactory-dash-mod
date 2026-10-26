#pragma once

#include "CoreMinimal.h"
#include "SatisDash/FrmPlayers.h"
#include "SatisDash/Native/ResourceNodes/NativeResourceNodes.h"
#include "SatisDash/Requests.h"
#include "SatisDash/Responses.h"
#include "SatisDash/Snapshot.h"

namespace SatisDash
{
	using FReplyHandler = TFunction<void(FHttpReply&)>; // non-const: a handler may take the body (MoveTemp) instead of copying up to 16 MB

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
		/**
		 * R1: every player the game knows in this session, connected ones online and the rest offline (names only, ADR-0029).
		 * Unset when the game state can't be read. Only called when the config's playersSource is "native".
		 */
		virtual TOptional<TArray<FPlayerReading>> ReadPlayers() { return {}; }
		/**
		 * R4: starts a pass over the world's actors looking for resource nodes. False when there is no world. A pass takes many
		 * ticks: call StepResourceNodeScan until it returns a value. Only called when resourceNodesSource is "native".
		 */
		virtual bool BeginResourceNodeScan() { return false; }
		/**
		 * Looks at up to MaxActors more actors, on the game thread, copying each node found into a plain reading. Unset until
		 * the pass has seen every actor, then every node found (once; empty when the world held no levels, actors or nodes,
		 * which the caller treats as "cannot read"). Sorting and building the body are not done here.
		 */
		virtual TOptional<TArray<FResourceNodeReading>> StepResourceNodeScan(int32 MaxActors) { return {}; }
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
