#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Agent.h"
#include "SatisDash/Commands.h"
#include "SatisDash/Ports.h"
#include "SatisDash/Pusher.h"
#include "SatisDash/RateLimit.h"

namespace SatisDash
{
	inline constexpr double ConfigCheckSeconds = 60.0;
	/** sampler.ts DUE_SLACK_MS: a pass is due this much early, so the ticker's half-second steps don't stretch it. */
	inline constexpr double DueSlackSeconds = 0.25;

	struct FDriverClock
	{
		/** Monotonic seconds: FPlatformTime::Seconds() in the game (Agent.h, Pusher.h). */
		TFunction<double()> Now;
		/** UTC: FDateTime::UtcNow() in the game (Snapshot.h, Commands.h). */
		TFunction<FDateTime()> UtcNow;
		/** Uniform in [0, 1]: FMath::FRand() in the game. */
		TFunction<double()> Random01;
	};

	/**
	 * The Node agent's agentRunner.ts on one game-thread tick: config checks, enrolment, sampling, pushing, commands and
	 * halt. Owns every pairing rule in Agent.h and Pusher.h, so the game module only ticks it. Not copyable: replies
	 * call back into it, so it must outlive every request it sent (the transport's CancelAll ensures that).
	 *
	 * Memory (it lives for the server's whole uptime): the snapshot queue is at most MaxQueuedSnapshots (120) bodies,
	 * the part log at most three keys (status, auto-pause, power), at most one enrolment and one push in flight. A tick
	 * with nothing due allocates nothing.
	 */
	class SATISDASHCORE_API FDriver
	{
	public:
		FDriver(FString ConfigPath, FString CredentialPath, ITransport& InTransport, IGameReader& InGame, FDriverClock InClock);
		FDriver(const FDriver&) = delete;
		FDriver& operator=(const FDriver&) = delete;

		void Boot();
		/** Call often (the game: every 0.5 s); each call does only what is due. */
		void Tick();

		const FAgent& GetAgent() const { return Agent; }
		int32 Queued() const { return Pusher.Num(); }

	private:
		void TickSample(double Now);
		void TickPush(double Now);
		void NoteFailedPart(const TCHAR* Part, double Now);
		/** Task 5. */
		void TickCommands(double Now);
		void StopCommands();

		FDriverClock Clock;
		ITransport& Transport;
		IGameReader& Game;
		FAgent Agent;
		/** Bounded: at most MaxQueuedSnapshots (120) snapshots, the oldest dropped first (Pusher.h). */
		FPusher Pusher;
		/** Bounded: one key per snapshot part (status, auto-pause, power), each logged at most once per 600 s. */
		FRateLimitedLog PartLog;
		double NextConfigAt = 0;
		double NextStatusAt = 0;
		TOptional<double> LastPowerAt;
		bool bRunning = false;
		bool bSendInFlight = false;
		/** Leaving Running asks for a Clear; it happens once no send is in flight (Pusher.h). */
		bool bClearPending = false;
		// Task 5 adds the command members here.
	};
}
