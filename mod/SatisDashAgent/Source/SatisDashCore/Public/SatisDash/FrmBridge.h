#pragma once

#include "CoreMinimal.h"
#include "SatisDash/FrmFactory.h"
#include "SatisDash/FrmMapLive.h"
#include "SatisDash/FrmPlayers.h"
#include "SatisDash/FrmQueue.h"
#include "SatisDash/FrmWorld.h"
#include "SatisDash/Ports.h"
#include "SatisDash/RateLimit.h"
#include "SatisDash/Requests.h"
#include "SatisDash/Snapshot.h"

namespace SatisDash
{
	/**
	 * The FRM bridge's live lane (spec "Design"): asks FRM for players, the factory, mapLive and the world layers through
	 * FFrmQueue, parses the bodies on the worker, and keeps the newest finished part of each for the driver's next snapshot
	 * (a world layer's outcome is taken by FWorldSender). It also serialises and gzips, on the worker, a snapshot that
	 * carries the factory or mapLive part.
	 *
	 * Threading: the worker runs one job at a time, in this order: a snapshot to encode, a players body, the next slice of
	 * the trains body, of the stations body, of the factory body, then of a world layer (hash, parse, finish). A job is
	 * shared with its task (TSharedPtr), so dropping it here never frees what the task still reads. What a task wrote is
	 * read only once IWorker::IsIdle().
	 *
	 * Memory (it lives for the server's uptime): at most one factory body (16 MB cap) and its output, one players body (4
	 * MB cap), one ready part of each, one mapLive pair (two arrays of at most 500 records each, under 256 KB joined or
	 * dropped; the trains and stations bodies, each at most 4 MB, can both be held while they parse), one world body (4 MB
	 * cap) being hashed and parsed, and its output (at most 2,000,000 bytes, then gzipped), and for each layer its
	 * outcome waiting to be taken; one snapshot being encoded and its body; the failure log at most four keys (players,
	 * factory, mapLive, mapLiveOverCap). The sender owns each layer's last two hashes (32 bytes each).
	 */
	class SATISDASHCORE_API FFrmBridge
	{
	public:
		FFrmBridge(ITransport& InTransport, IWorker& InWorker);
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
		/** The newest factory part finished within MaxAgeSeconds, once. */
		TOptional<FString> TakeFactory(double Now, double MaxAgeSeconds);
		/** The newest mapLive part ({"trains":[...],"stations":[...]}) finished within MaxAgeSeconds, once. */
		TOptional<FString> TakeMapLive(double Now, double MaxAgeSeconds);
		/**
		 * Asks FRM for one world layer (world lane) and hashes, parses and encodes its body on the worker. LastRaw and
		 * LastData are the layer's last settled raw hash and last accepted data hash (FWorldSender); they are copied into the
		 * job. False while that layer is already requested, being read, or has an outcome waiting, and when the queue
		 * refuses the read (the endpoint is still in flight from a stopped run): FRM was not asked, ask again later.
		 */
		bool RequestWorld(EWorldLayer Layer, const FString& ObservedAt, TOptional<FBlake3Hash> LastRaw, TOptional<FBlake3Hash> LastData);
		/**
		 * R4: the node readings a native scan finished on the game thread, moved into a world job. The worker sorts them,
		 * builds the body and settles it as an FRM body is (data hash, cap, encode); then TakeWorld. FRM is not asked. False
		 * while that layer is requested, being read, or has an outcome waiting (the readings are then dropped).
		 */
		bool SubmitNativeWorld(EWorldLayer Layer, const FString& ObservedAt, TArray<FResourceNodeReading> Readings, TOptional<FBlake3Hash> LastData);
		/**
		 * R3: the same for any native layer: Builder (which owns the readings it was made from) runs on the worker and makes the
		 * whole body; the cap, hashes and gzip are then applied as for an FRM body. FRM is not asked.
		 */
		bool SubmitNativeWorld(EWorldLayer Layer, const FString& ObservedAt, FNativeWorldBuilder Builder, TOptional<FBlake3Hash> LastData);
		/**
		 * R3: the trains and stations a native read finished on the game thread, moved into a mapLive job. The worker joins
		 * them (NativeMapLiveJson) and sets the part ready, so TakeMapLive is the one way out for both sources; a part past its
		 * byte cap is left out with one warning. FRM is not asked. ReadAt is the part's age start. False while a mapLive job
		 * is held (the readings are then dropped).
		 */
		bool SubmitNativeMapLive(FTrainsSnapshot Snapshot, double ReadAt);
		/**
		 * R2a: the machines a native read finished on the game thread, moved into a factory job. The worker builds the part
		 * (NativeFactoryPart) and sets it ready, so TakeFactory is the one way out for both sources. FRM is not asked. ReadAt
		 * is the part's age start. False while a factory job is held (the readings are then dropped).
		 */
		bool SubmitNativeFactory(TArray<FFactoryReading> Readings, double ReadAt);
		/**
		 * S4-B: the extractors a native Buildables pass finished, moved into a job. The worker builds the fragment
		 * (NativeExtractorsFragment) and holds it (the readings are freed). TakeFactory splices it into the next factory part as
		 * ,"extractors":[...] when it is not older than 1.5 x that call's MaxAgeSeconds, and consumes it; while the job is
		 * outstanding TakeFactory waits for it. False while a fragment job is held (the readings are then dropped).
		 */
		bool SubmitNativeExtractors(TArray<FExtractorReading> Readings, double ReadAt);
		/** The layer's finished outcome, once. */
		TOptional<FWorldOutcome> TakeWorld(EWorldLayer Layer);
		/** Hands a snapshot carrying the factory or mapLive part to the worker for SnapshotToJson and EncodeBody. */
		void EncodeSnapshot(FSnapshot Snapshot);
		bool EncodePending() const { return Encode.IsValid(); }
		TOptional<FEncodedBody> TakeEncoded();
		/** Leaving Running: no queued read, no reply used, no job or part kept. A running task finishes and is dropped. */
		void Stop();

	private:
		struct FPlayersJob { TArray<uint8> Body; TOptional<FString> Json; bool bRan = false; double ReadAt = 0; };
		struct FFactoryJob
		{
			explicit FFactoryJob(TArray<uint8> InBody) : Parse{MoveTemp(InBody)} {}
			/** R2a: the game's own readings; the worker builds the part from them (no FRM body, Parse stays empty). */
			FFactoryJob(TArray<FFactoryReading> InReadings, double InReadAt) : Parse{TArray<uint8>()}, Readings{MoveTemp(InReadings)}, bNative{true}, ReadAt{InReadAt} {}
			FFactoryParse Parse;
			FFactoryParse::EStep Last = FFactoryParse::EStep::More;
			bool bRan = false;
			double ReadAt = 0;
			TArray<FFactoryReading> Readings;
			bool bNative = false;
			/** The native part's text, written by the worker. */
			FString NativeJson;
		};
		/** S4-B: the extractors readings, then (worker) the fragment text; the readings are freed once it is built. */
		struct FExtractorsJob
		{
			FExtractorsJob(TArray<FExtractorReading> InReadings, double InReadAt) : Readings{MoveTemp(InReadings)}, ReadAt{InReadAt} {}
			TArray<FExtractorReading> Readings;
			double ReadAt = 0;
			FString Fragment;
			bool bRan = false;
		};
		struct FEncodeJob {FSnapshot Snapshot; FEncodedBody Body; bool bRan = false; };
		/** R3: the game's own trains and stations; the worker writes the joined part (Json, or Ok false when it is over its cap). */
		struct FNativeMapLiveJob
		{
			FTrainsSnapshot Readings;
			double ReadAt = 0;
			FString Json;
			bool bOk = false;
			bool bRan = false;
		};
		struct FReady { FString Json; double At = 0; };

		/** One of mapLive's two reads: not asked, waiting for FRM, parsing on the worker, or settled (Ok or Failed). */
		struct FMapJob
		{
			/** Named Owned, not Parse: FFactoryJob::Parse is a receiver check-api trusts as the code's own type. */
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
		struct FWorldJob
		{
			FWorldJob(EWorldLayer InLayer, TArray<uint8> InBody, const FString& InObservedAt, TOptional<FBlake3Hash> InLastRaw, TOptional<FBlake3Hash> InLastData)
				: Work{InLayer, MoveTemp(InBody), InObservedAt, InLastRaw, InLastData}
			{
			}
			FWorldJob(EWorldLayer InLayer, TArray<FResourceNodeReading> InReadings, const FString& InObservedAt, TOptional<FBlake3Hash> InLastData)
				: Work{InLayer, MoveTemp(InReadings), InObservedAt, InLastData}
			{
			}
			FWorldJob(EWorldLayer InLayer, FNativeWorldBuilder InBuilder, const FString& InObservedAt, TOptional<FBlake3Hash> InLastData)
				: Work{InLayer, MoveTemp(InBuilder), InObservedAt, InLastData}
			{
			}
			/** Named Work, not Read: check-api's receiver heuristic does not trust a name FFrmQueue also declares. */
			FWorldRead Work;
			bool bFinished = false;
			bool bRan = false;
		};
		struct FWorldSlot
		{
			bool bRequested = false;
			TSharedPtr<FWorldJob> Job;
			TOptional<FWorldOutcome> Outcome;
		};
		bool LargeBusy() const;
		FWorldSlot* NextWorldJob();
		void StartMapCycle();
		void HarvestMapSide(FMapSide& Side);
		void FinishMapLive(double Now);

		void Harvest(double Now);
		void LaunchNext();
		void NoteFailed(const TCHAR* Part, double Now);
		static TOptional<FString> TakeFresh(TOptional<FReady>& Ready, double Now, double MaxAgeSeconds);

		IWorker& Worker;
		FFrmQueue FrmLanes;
		TSharedPtr<FPlayersJob> Players;
		TSharedPtr<FFactoryJob> Factory;
		/** At most one extractors job, and one built fragment held until the next factory part takes it (bounded by the 5000-record cap). */
		TSharedPtr<FExtractorsJob> ExtractorsTask;
		TOptional<FReady> ExtractorsReady;
		TSharedPtr<FEncodeJob> Encode;
		TSharedPtr<FNativeMapLiveJob> NativeMapLive;
		TOptional<FReady> PlayersReady;
		TOptional<FReady> FactoryReady;
		FMapSide MapTrains;
		FMapSide MapStations;
		TOptional<FReady> MapLiveReady;
		FWorldSlot World[WorldLayerCount];
		TOptional<FEncodedBody> Encoded;
		bool bPlayersAbsent = false;
		bool bFactoryFailed = false;
		double LastNow = 0;
		/** Bounded: four keys ("players", "factory", "mapLive", "mapLiveOverCap"), each at most once per 600 s. */
		FRateLimitedLog FailLog;
	};
}
