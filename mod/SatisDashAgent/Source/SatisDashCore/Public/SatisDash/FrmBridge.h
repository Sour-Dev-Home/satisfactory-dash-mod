#pragma once

#include "CoreMinimal.h"
#include "SatisDash/FrmFactory.h"
#include "SatisDash/FrmMapLive.h"
#include "SatisDash/FrmPlayers.h"
#include "SatisDash/FrmQueue.h"
#include "SatisDash/Ports.h"
#include "SatisDash/RateLimit.h"
#include "SatisDash/Requests.h"
#include "SatisDash/Snapshot.h"

namespace SatisDash
{
	/**
	 * The FRM bridge's live lane (spec "Design"): asks FRM for players, the factory and mapLive through FFrmQueue, parses the
	 * bodies on the worker, and keeps the newest finished part of each for the driver's next snapshot. It also
	 * serialises and gzips, on the worker, a snapshot that carries the factory part.
	 *
	 * Threading: the worker runs one job at a time, in this order: a snapshot to encode, a players body, the next slice of
	 * the factory body. A job is shared with its task (TSharedPtr), so dropping it here never frees what the task still
	 * reads. What a task wrote is read only once IWorker::IsIdle().
	 *
	 * Memory (it lives for the server's uptime): at most one factory body (16 MB cap) and its output, one players body (4
	 * MB cap), one ready part of each, one mapLive pair (two arrays of at most 500 records each, under 256 KB joined or dropped, one body of at most 4 MB being parsed), one snapshot being encoded and its body; the failure log at most two keys.
	 */
	class SATISDASHCORE_API FFrmBridge
	{
	public:
		FFrmBridge(ITransport& InTransport, IWorker& InWorker);
		FFrmBridge(const FFrmBridge&) = delete;
		FFrmBridge& operator=(const FFrmBridge&) = delete;

		/** A sample pass that read the game: queues getPlayer (the status cadence), and getFactory when bFactoryDue. */
		void RequestPass(bool bFactoryDue);
		/** Every driver tick: takes finished work, launches the next job and starts the next FRM read. */
		void Tick(double Now, int32 Port, const FString& Token);
		/** The newest players part finished within MaxAgeSeconds, once. */
		TOptional<FString> TakePlayers(double Now, double MaxAgeSeconds);
		/** The newest factory part finished within MaxAgeSeconds, once. */
		TOptional<FString> TakeFactory(double Now, double MaxAgeSeconds);
		/** The newest mapLive part ({"trains":[...],"stations":[...]}) finished within MaxAgeSeconds, once. */
		TOptional<FString> TakeMapLive(double Now, double MaxAgeSeconds);
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
			FFactoryParse Parse;
			FFactoryParse::EStep Last = FFactoryParse::EStep::More;
			bool bRan = false;
			double ReadAt = 0;
		};
		struct FEncodeJob { FSnapshot Snapshot; FEncodedBody Body; bool bRan = false; };
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
		TSharedPtr<FEncodeJob> Encode;
		TOptional<FReady> PlayersReady;
		TOptional<FReady> FactoryReady;
		FMapSide MapTrains;
		FMapSide MapStations;
		TOptional<FReady> MapLiveReady;
		TOptional<FEncodedBody> Encoded;
		bool bPlayersAbsent = false;
		bool bFactoryFailed = false;
		double LastNow = 0;
		/** Bounded: two keys ("players", "factory"), each at most once per 600 s. */
		FRateLimitedLog FailLog;
	};
}
