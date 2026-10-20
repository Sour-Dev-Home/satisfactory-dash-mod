#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Responses.h"

namespace SatisDash
{
	inline constexpr int32 MaxQueuedSnapshots = 120;
	inline constexpr double MaxRetryAfterSeconds = 300.0;

	/** backoff.ts: the ceiling is min(60, 2^attempt) s; the delay is uniform in [ceiling / 2, ceiling]. */
	SATISDASHCORE_API double BackoffSeconds(int32 Attempt, double Random01);
	/** backendClient.ts: Retry-After in seconds, capped at 300 s; an HTTP date, a negative or junk is ignored. */
	SATISDASHCORE_API TOptional<double> ParseRetryAfterSeconds(const FString& Header);

	/** pusher.ts and queue.ts: an in-memory queue, oldest first, sent one at a time with backoff (spec "Shape"). */
	class SATISDASHCORE_API FPusher
	{
	public:
		struct FOutcome
		{
			/** 401: the agent must halt (FAgent::OnAuthRejected). */
			bool bHalt = false;
			/** The backend's answer to a snapshot it took. */
			TOptional<FSnapshotResponse> Response;
		};

		explicit FPusher(TFunction<double()> InRandom01);
		/** Adds a snapshot, dropping the oldest past 120. Logs once when dropping starts. */
		void Enqueue(FString SnapshotJson);
		/**
		 * The snapshot to send now: none while one is in flight, while a retry waits, or when the queue is empty.
		 * Now (here and in FinishSend) must be monotonic seconds (FPlatformTime::Seconds()); a clock that goes backwards
		 * stalls pushing. Every StartSend that returns a snapshot must be followed by exactly one FinishSend, including
		 * on timeout or cancel (pass a Code 0 reply), or the pusher waits for the answer forever. Call GetCredential()
		 * again before each authenticated request, and send any 401 to FAgent::OnAuthRejected.
		 */
		TOptional<FString> StartSend(double Now);
		/** The reply to the send StartSend began. */
		FOutcome FinishSend(const FHttpReply& Reply, double Now);
		/** Empties the queue. Not while a send is in flight: call it after that send's FinishSend. */
		void Clear();
		int32 Num() const { return Queue.Num(); }
		int32 Failures() const { return FailureCount; }

	private:
		struct FItem
		{
			uint64 Seq = 0;
			FString Json;
		};
		void PopIfFront(const TOptional<uint64>& Seq);

		TFunction<double()> random01;
		TArray<FItem> Queue;
		uint64 NextSeq = 0;
		TOptional<uint64> InFlight;
		double NextAttemptAt = 0;
		int32 FailureCount = 0;
		int32 DroppedInStreak = 0;
		int32 Refused = 0;
	};
}
