#pragma once

#include "CoreMinimal.h"
#include "SatisDash/FrmPlayers.h"
#include "SatisDash/Native/Factory/NativeFactory.h"
#include "SatisDash/Native/Rails/NativeRails.h"
#include "SatisDash/StepBudget.h"
#include "SatisDash/Native/ResourceNodes/NativeResourceNodes.h"
#include "SatisDash/Native/Trains/NativeTrains.h"
#include "SatisDash/Requests.h"
#include "SatisDash/Responses.h"
#include "SatisDash/Snapshot.h"

namespace SatisDash
{
	/** R3: one read of the railway; the same struct feeds the mapLive part and the train-routes layer. */
	struct FTrainsSnapshot
	{
		TArray<FTrainReading> Trains;
		TArray<FStationReading> Stations;
	};

	/** Counts of one logistics pass (R5, ADR-0058): numbers only, they go to the log line and nowhere else. */
	struct FLogisticsCounts
	{
		int32 Buildables = 0, Tracks = 0, Belts = 0, Lifts = 0, Pipes = 0;
		int32 OpenEnds = 0, OwnerlessEnds = 0, Dropped = 0;
		/** Advisory GetBuildableCount of the track class, -1 when not asked. */
		int32 ExpectedTracks = -1;
	};

	/** What a finished logistics pass hands over. */
	struct FLogisticsSnapshot
	{
		/** Empty when rails were not asked for. */
		TArray<FRailReading> Rails;
		FLogisticsCounts Counts;
	};

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
		/**
		 * R2a: starts a time-sliced read of the factory's machines. False when there is no world or no buildable subsystem.
		 * Call StepFactoryRead every tick until it returns a value. Only called when factorySource is "native".
		 */
		virtual bool BeginFactoryRead() { return false; }
		/**
		 * Reads machines until the budget is spent, on the game thread. Unset until every buildable has been seen, then every
		 * machine read (once; empty for a world with no machines, which is a real, empty factory). Scaling and building the
		 * body are not done here.
		 */
		virtual TOptional<TArray<FFactoryReading>> StepFactoryRead(FStepBudget& Budget) { return {}; }
		/** True when the read StepFactoryRead just ended was abandoned (no subsystem, or the world changed): its value is then not a factory. */
		virtual bool FactoryReadAbandoned() const { return false; }
		/** The driver drops an open read (source changed, left Running): frees what the reader buffered. */
		virtual void EndFactoryRead() {}
		/**
		 * R3: starts a time-sliced read of the trains and stations. False when there is no world or no railroad subsystem, or when
		 * a read is already open (the reader keeps one cursor: the driver's mapLive pass and the world sender's train-routes pass
		 * take turns). Call StepTrainsRead every tick until it returns a value. Only called when trainsSource is "native".
		 */
		virtual bool BeginTrainsRead() { return false; }
		/**
		 * Reads trains, then stations, until the budget is spent, on the game thread. Unset until every one has been read, then
		 * the snapshot (once; empty arrays for a world with no trains or stations, which is a real, empty railway). Sorting and
		 * building the bodies are not done here.
		 */
		virtual TOptional<FTrainsSnapshot> StepTrainsRead(FStepBudget& Budget) { return {}; }
		/** True when the read StepTrainsRead just ended was abandoned (no subsystem, or the world changed): its value is then not a railway. */
		virtual bool TrainsReadAbandoned() const { return false; }
		/** The caller drops its open read (source changed, left Running): frees what the reader buffered. */
		virtual void EndTrainsRead() {}
		/**
		 * True while a trains read is open (begun, not yet finished or ended), by whichever consumer. A consumer that finds the
		 * cursor busy waits for the next tick; it is not a failed read.
		 */
		virtual bool IsTrainsReadOpen() const { return false; }
		/**
		 * R5: starts the shared, time-sliced logistics pass over the buildables, for the rails layer and/or (later) the conveyors layer.
		 * False when there is no world or subsystem, or when a pass is already open. Call StepLogisticsRead every tick until it returns
		 * a value. Bridge-sourced layers never reach this. bConveyors is accepted and ignored until the conveyors reader lands.
		 */
		virtual bool BeginLogisticsRead(bool bRails, bool bConveyors) { return false; }
		/**
		 * Reads buildables until the budget is spent, on the game thread. Unset until the pass has seen every one, then the
		 * snapshot (once). Sorting and building the bodies are not done here.
		 */
		virtual TOptional<FLogisticsSnapshot> StepLogisticsRead(FStepBudget& Budget) { return {}; }
		/** True when the pass StepLogisticsRead just ended was abandoned (no subsystem, the world changed, or zero buildables): its value is not a world. */
		virtual bool LogisticsReadAbandoned() const { return false; }
		/** The caller drops an open pass (source changed, left Running): frees what the reader buffered. */
		virtual void EndLogisticsRead() {}
		/** True while a logistics pass is open (begun, not finished or ended). */
		virtual bool IsLogisticsReadOpen() const { return false; }
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
