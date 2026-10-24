#pragma once

#include "CoreMinimal.h"
#include "SatisDash/FrmFactory.h"
#include "SatisDash/FrmPlayers.h"
#include "SatisDash/FrmQueue.h"
#include "SatisDash/Ports.h"
#include "SatisDash/RateLimit.h"
#include "SatisDash/Requests.h"
#include "SatisDash/Snapshot.h"

namespace SatisDash
{
	/**
	 * The FRM bridge's live lane (spec "Design"): asks FRM for players and the factory through FFrmQueue, parses the
	 * bodies on the worker, and keeps the newest finished part of each for the driver's next snapshot. It also
	 * serialises and gzips, on the worker, a snapshot that carries the factory part.
	 *
	 * Threading: the worker runs one job at a time, in this order: a snapshot to encode, a players body, the next slice of
	 * the factory body. A job is shared with its task (TSharedPtr), so dropping it here never frees what the task still
	 * reads. What a task wrote is read only once IWorker::IsIdle().
	 *
	 * Memory (it lives for the server's uptime): at most one factory body (16 MB cap) and its output, one players body (4
	 * MB cap), one ready part of each, one snapshot being encoded and its body; the failure log at most two keys.
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
		/** Hands a snapshot carrying the factory part to the worker for SnapshotToJson and EncodeBody. */
		void EncodeSnapshot(FSnapshot Snapshot);
		bool EncodePending() const { return Encode.IsValid(); }
		TOptional<FEncodedBody> TakeEncoded();
		/** Leaving Running: no queued read, no reply used, no job or part kept. A running task finishes and is dropped. */
		void Stop();

	private:
		struct FPlayersJob { TArray<uint8> Body; TOptional<FPlayersPart> Result; bool bRan = false; };
		struct FFactoryJob
		{
			explicit FFactoryJob(TArray<uint8> InBody) : Parse{MoveTemp(InBody)} {}
			FFactoryParse Parse;
			FFactoryParse::EStep Last = FFactoryParse::EStep::More;
			bool bRan = false;
		};
		struct FEncodeJob { FSnapshot Snapshot; FEncodedBody Body; bool bRan = false; };
		struct FReady { FString Json; double At = 0; };

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
		TOptional<FEncodedBody> Encoded;
		bool bPlayersAbsent = false;
		bool bFactoryFailed = false;
		double LastNow = 0;
		/** Bounded: two keys ("players", "factory"), each at most once per 600 s. */
		FRateLimitedLog FailLog;
	};
}
