#include "SatisDash/Pusher.h"

#include "SatisDash/Log.h"

double SatisDash::BackoffSeconds(int32 Attempt, double Random01)
{
	const int32 Exponent = FMath::Clamp(Attempt, 0, 30);
	const double Ceiling = FMath::Min(60.0, FMath::Pow(2.0, double(Exponent)));
	return Ceiling / 2.0 + FMath::Clamp(Random01, 0.0, 1.0) * (Ceiling / 2.0);
}

TOptional<double> SatisDash::ParseRetryAfterSeconds(const FString& Header)
{
	const FString Value = Header.TrimStartAndEnd();
	if (Value.IsEmpty() || !Value.IsNumeric())
	{
		return {};
	}
	const double Seconds = FCString::Atod(*Value);
	if (!FMath::IsFinite(Seconds) || Seconds < 0)
	{
		return {};
	}
	return FMath::Min(Seconds, MaxRetryAfterSeconds);
}

SatisDash::FPusher::FPusher(TFunction<double()> InRandom01)
	: random01(MoveTemp(InRandom01))
{
}

void SatisDash::FPusher::Enqueue(FString SnapshotJson)
{
	Queue.Add({ NextSeq++, MoveTemp(SnapshotJson) });
	if (Queue.Num() > MaxQueuedSnapshots)
	{
		const int32 Dropped = Queue.Num() - MaxQueuedSnapshots;
		Queue.RemoveAt(0, Dropped);
		if (DroppedInStreak == 0)
		{
			UE_LOG(LogSatisDash, Warning, TEXT("The snapshot queue is full (%d); dropping the oldest until the backend answers"), MaxQueuedSnapshots);
		}
		DroppedInStreak += Dropped;
	}
}

TOptional<FString> SatisDash::FPusher::StartSend(double Now)
{
	if (InFlight.IsSet() || Queue.Num() == 0 || Now < NextAttemptAt)
	{
		return {};
	}
	InFlight = Queue[0].Seq;
	return Queue[0].Json;
}

void SatisDash::FPusher::PopIfFront(const TOptional<uint64>& Seq)
{
	// The sent snapshot may have been dropped for room while it was in flight (pusher.ts: queue.peek() === head).
	if (Seq.IsSet() && Queue.Num() > 0 && Queue[0].Seq == Seq.GetValue())
	{
		Queue.RemoveAt(0);
	}
}

SatisDash::FPusher::FOutcome SatisDash::FPusher::FinishSend(const FHttpReply& Reply, double Now)
{
	FOutcome Out;
	const TOptional<uint64> Sent = InFlight;
	InFlight.Reset();
	const EReplyKind Kind = ClassifyReply(Reply.Code, true);
	if (Kind == EReplyKind::AuthRejected)
	{
		Out.bHalt = true;
		return Out;
	}
	if (Kind == EReplyKind::Fatal)
	{
		PopIfFront(Sent);
		++Refused;
		if (Refused == 1 || Refused % 100 == 0)
		{
			UE_LOG(LogSatisDash, Warning, TEXT("The backend refused a snapshot (%d); dropped it (%d refused so far)"), Reply.Code, Refused);
		}
		return Out;
	}
	if (Kind == EReplyKind::Ok)
	{
		Out.Response = ParseSnapshotResponse(Reply);
		if (Out.Response.IsSet())
		{
			PopIfFront(Sent);
			if (FailureCount > 0)
			{
				UE_LOG(LogSatisDash, Display, TEXT("Pushing snapshots recovered after %d attempts (%d queued, %d dropped)"), FailureCount, Queue.Num(), DroppedInStreak);
				FailureCount = 0;
				DroppedInStreak = 0;
			}
			else if (DroppedInStreak > 0 && Queue.Num() <= MaxQueuedSnapshots / 2)
			{
				// A slow backend that never fails: the episode ends once the queue has drained to half (2A final review).
				UE_LOG(LogSatisDash, Display, TEXT("The snapshot queue has room again (%d dropped while the backend was slow)"), DroppedInStreak);
				DroppedInStreak = 0;
			}
			return Out;
		}
	}
	// Transient, or a 2xx not in the expected form: backendClient.ts retries both.
	if (FailureCount == 0)
	{
		UE_LOG(LogSatisDash, Warning, TEXT("Pushing snapshots failed (%d); retrying with backoff (%d queued)"), Reply.Code, Queue.Num());
	}
	const double Delay = FMath::Max(ParseRetryAfterSeconds(Reply.RetryAfter).Get(0.0), BackoffSeconds(FailureCount, random01()));
	++FailureCount;
	NextAttemptAt = Now + Delay;
	return Out;
}

void SatisDash::FPusher::Clear()
{
	Queue.Reset();
	InFlight.Reset();
	NextAttemptAt = 0;
	FailureCount = 0;
	DroppedInStreak = 0;
}
