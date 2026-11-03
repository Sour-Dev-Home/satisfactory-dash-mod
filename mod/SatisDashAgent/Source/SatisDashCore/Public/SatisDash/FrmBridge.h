#pragma once

#include "CoreMinimal.h"
#include "SatisDash/FrmFactory.h"
#include "SatisDash/FrmMapLive.h"
#include "SatisDash/FrmPlayers.h"
#include "SatisDash/FrmQueue.h"
#include "SatisDash/FrmWorld.h"
#include "SatisDash/Ports.h"
#include "SatisDash/Requests.h"
#include "SatisDash/SnapshotAssembler.h"

namespace SatisDash
{
	/**
	 * The FRM bridge's live lane (spec "Design"): asks FRM for players, the factory, mapLive and the world layers through
	 * FFrmQueue, parses the bodies on the worker, and hands each finished part to the FSnapshotAssembler (players it keeps
	 * for the driver's next snapshot itself). This is the FRM role only (R7 step 2): the assembler holds the parts and builds
	 * the native ones, and has no FRM knowledge; this class goes in step 3.
	 *
	 * Until then it is also the worker's turn: Tick harvests and launches the assembler's jobs and its own in the order they
	 * had before the split, and Stop stops the assembler too.
	 *
	 * Threading: the worker runs one job at a time, in this order: a snapshot to encode (assembler), a players body, the next
	 * slice of the trains body, of the stations body, the assembler's native mapLive, extractors, generators and storage builds,
	 * the next slice of the factory body (FRM), the assembler's factory build, then a world layer (hash, parse, finish; the
	 * assembler holds its slot). A job is shared with its task (TSharedPtr), so dropping it here never frees what the task still
	 * reads. What a task wrote is read only once IWorker::IsIdle().
	 *
	 * Memory (it lives for the server's uptime): at most one factory body (16 MB cap) and its output, one players body (4
	 * MB cap), one ready players part, one mapLive pair (the trains and stations bodies, each at most 4 MB, can both be held
	 * while they parse), one world body (4 MB cap) being hashed and parsed. The assembler's holdings are bounded in
	 * SnapshotAssembler.h. The sender owns each layer's last two hashes (32 bytes each).
	 */
	class SATISDASHCORE_API FFrmBridge
	{
	public:
		FFrmBridge(ITransport& InTransport, IWorker& InWorker, FSnapshotAssembler& InAssembler);
		FFrmBridge(const FFrmBridge&) = delete;
		FFrmBridge& operator=(const FFrmBridge&) = delete;

		/**
		 * A sample pass that read the game: queues getPlayer (the status cadence) unless bPlayers is false (the players
		 * reader is native), and on the factory cadence (bFactoryDue) getFactory unless bFactoryFromFrm is false, and the
		 * getTrains/getTrainStation pair (StartMapCycle) unless bTrainsFromFrm is false. A native part is never requested.
		 */
		void RequestPass(bool bFactoryDue, bool bPlayers = true, bool bFactoryFromFrm = true, bool bTrainsFromFrm = true);
		/** Every driver tick: takes finished work, launches the next job and starts the next FRM read. */
		void Tick(double Now, int32 Port, const FString& Token);
		/** The newest players part finished within MaxAgeSeconds, once. */
		TOptional<FString> TakePlayers(double Now, double MaxAgeSeconds);
		/**
		 * Asks FRM for one world layer (world lane) and hashes, parses and encodes its body on the worker. LastRaw and
		 * LastData are the layer's last settled raw hash and last accepted data hash (FWorldSender); they are copied into the
		 * job. False while that layer is already requested, being read, or has an outcome waiting, and when the queue
		 * refuses the read (the endpoint is still in flight from a stopped run): FRM was not asked, ask again later.
		 */
		bool RequestWorld(EWorldLayer Layer, const FString& ObservedAt, TOptional<FBlake3Hash> LastRaw, TOptional<FBlake3Hash> LastData);
		/** Leaving Running: no queued read, no reply used, no job or part kept (the assembler's too). A running task finishes and is dropped. */
		void Stop();

	private:
		struct FPlayersJob { TArray<uint8> Body; TOptional<FString> Json; bool bRan = false; double ReadAt = 0; };
		struct FFrmFactoryJob
		{
			explicit FFrmFactoryJob(TArray<uint8> InBody) : Parse{MoveTemp(InBody)} {}
			FFactoryParse Parse;
			FFactoryParse::EStep Last = FFactoryParse::EStep::More;
			bool bRan = false;
			double ReadAt = 0;
		};
		struct FReady { FString Json; double At = 0; };

		/** One of mapLive's two reads: not asked, waiting for FRM, parsing on the worker, or settled (Ok or Failed). */
		struct FMapJob
		{
			/** Named Owned, not Parse: FFrmFactoryJob::Parse is a receiver check-api trusts as the code's own type. */
			TUniquePtr<FFrmArrayParse> Owned;
			FFrmArrayParse::EStep Last = FFrmArrayParse::EStep::More;
			bool bRan = false;
		};
		struct FMapSide
		{
			enum class EState : uint8 { Idle, Waiting, Parsing, Ok, Failed };
			EState State = EState::Idle;
			TSharedPtr<FMapJob> Job;
			FString Json;
			int64 Bytes = 0;
			double ReadAt = 0;
		};
		bool LargeBusy() const;
		void StartMapCycle();
		void HarvestMapSide(FMapSide& Side);
		void FinishMapLive(double Now);

		void Harvest(double Now);
		void LaunchNext();
		static TOptional<FString> TakeFresh(TOptional<FReady>& Ready, double Now, double MaxAgeSeconds);

		IWorker& Worker;
		FSnapshotAssembler& Assembler;
		FFrmQueue FrmLanes;
		TSharedPtr<FPlayersJob> Players;
		TSharedPtr<FFrmFactoryJob> Factory;
		TOptional<FReady> PlayersReady;
		FMapSide MapTrains;
		FMapSide MapStations;
		bool bPlayersAbsent = false;
		bool bFactoryFailed = false;
		double LastNow = 0;
	};
}
