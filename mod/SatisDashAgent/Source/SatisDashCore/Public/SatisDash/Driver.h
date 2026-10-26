#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Agent.h"
#include "SatisDash/Commands.h"
#include "SatisDash/FrmBridge.h"
#include "SatisDash/Ports.h"
#include "SatisDash/Pusher.h"
#include "SatisDash/RateLimit.h"
#include "SatisDash/StepBudget.h"
#include "SatisDash/WorldSender.h"

namespace SatisDash
{
	inline constexpr double ConfigCheckSeconds = 60.0;
	/** sampler.ts DUE_SLACK_MS: a pass is due this much early, so the ticker's half-second steps don't stretch it. */
	inline constexpr double DueSlackSeconds = 0.25;
	inline constexpr double MinPollSpacingSeconds = 1.0; // commandRunner.ts MIN_POLL_SPACING_MS

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
	 * call back into it, so it must outlive every request it sent. ITransport has no cancel: whoever owns the transport
	 * must destroy the driver together with dropping its handlers, never drop them and keep ticking (an unanswered Start
	 * wedges enrolment or pushing).
	 *
	 * Memory (it lives for the server's whole uptime): the snapshot queue holds at most MaxQueuedSnapshots (120) bodies
	 * and MaxQueuedBytes (2 MiB) of them, the part log at most four keys (status, auto-pause, power, players); FRM reads and parsing as FrmBridge.h bounds them (mapLive's pair included); the world sender as WorldSender.h bounds it; at most one
	 * enrolment and one push in flight. Commands: the ledger at most 1000 IDs, at most 20 commands waiting, one poll or
	 * one result in flight, never both. A tick with nothing due allocates nothing.
	 */
	class SATISDASHCORE_API FDriver
	{
	public:
		FDriver(FString ConfigPath, FString CredentialPath, ITransport& InTransport, IGameReader& InGame, IWorker& InWorker, FDriverClock InClock);
		FDriver(const FDriver&) = delete;
		FDriver& operator=(const FDriver&) = delete;

		void Boot();
		/** Call often (the game: every 0.5 s); each call does only what is due. */
		void Tick();

		const FAgent& GetAgent() const { return Agent; }
		int32 Queued() const { return Pusher.Num(); }
		/** True while a snapshot is being serialised and gzipped on the worker (a seam for the driver tests). */
		bool EncodePending() const { return Bridge.EncodePending(); }

	private:
		void TickSample(double Now);
		void TickPush(double Now);
		void NoteFailedPart(const TCHAR* Part, double Now);
		/** R2a: steps an open native factory read once, every tick, and hands a finished one to the bridge. */
		void TickFactoryRead(double Now);
		/** R3: the same for the trains read behind the mapLive part. */
		void TickTrainsRead(double Now);
		/** commandRunner.ts: poll, run each command, report its result, one thing at a time (DriverCommands.cpp). */
		void TickCommands(double Now);
		void StopCommands();

		FDriverClock Clock;
		ITransport& Transport;
		IGameReader& Game;
		FAgent Agent;
		/** Bounded: at most MaxQueuedSnapshots (120) snapshots and MaxQueuedBytes (2 MiB), the oldest dropped first (Pusher.h). */
		FPusher Pusher;
		/** Bounded: see FrmBridge.h. */
		FFrmBridge Bridge;
		/** Bounded: see WorldSender.h. */
		FWorldSender WorldLayers;
		/** Bounded: one key per snapshot part (status, auto-pause, power, players), each logged at most once per 600 s. */
		FRateLimitedLog PartLog;
		/** Times each step of the native factory read (the game's reader logs its counters). */
		FStepBudget FactoryBudget;
		/** A native factory read is open: StepFactoryRead is called every tick until it returns. */
		bool bFactoryReadOpen = false;
		/** Times each step of the native trains read for the mapLive part, and of the sender's train-routes read (separate runs, separate totals). */
		FStepBudget TrainsBudget;
		FStepBudget RoutesBudget;
		/** A native trains read for mapLive is open: StepTrainsRead is called every tick until it returns. */
		bool bTrainsReadOpen = false;
		/** Bounded: one key ("busy"). */
		FRateLimitedLog FactoryLog{ 60.0 };
		/** Bounded: two keys ("busy", "held"), each at most once per 60 s. */
		FRateLimitedLog TrainsLog{ 60.0 };
		double NextConfigAt = 0;
		double NextStatusAt = 0;
		TOptional<double> LastPowerAt;
		TOptional<double> LastFactoryAt;
		bool bRunning = false;
		/** The last status pass said a game is running (the native node scan waits for it, as the native players do). */
		bool bGameRunning = false;
		bool bSendInFlight = false;
		/** Leaving Running asks for a Clear; it happens once no send is in flight (Pusher.h). */
		bool bClearPending = false;
		/**
		 * Which backend and credential a reply belongs to: bumped whenever they change (leaving or entering Running, a
		 * halt, a Clear). A reply sent under an older generation still finishes its Start, and changes nothing else.
		 */
		uint32 Generation = 0;

		/** A result waiting to be posted (commandRunner.ts report()). */
		struct FReport
		{
			FString Id;
			FCommandResult Result;
			FDateTime ExpiresAt;
			int32 Attempts = 0;
			double NextAt = 0;
		};
		void SendPoll(double Now);
		void FinishPoll(const FHttpReply& Reply, uint32 SentIn);
		void RunNext(double Now);
		void SendReport();
		void FinishReport(const FHttpReply& Reply, uint32 SentIn);

		/** Bounded: at most FCommandLedger::MaxEntries (1000) IDs, expired ones purged on the next command after expiry (<= 600 s). */
		FCommandLedger Ledger;
		/** Bounded: one poll's commands, at most MaxCommandsPerPoll (20), from a body of at most 64 KB. */
		TArray<FAgentCommand> Waiting;
		/** At most one result waiting or in flight. */
		TOptional<FReport> Report;
		bool bPollInFlight = false;
		bool bReportInFlight = false;
		double NextPollAt = 0;
		double PollStartedAt = 0;
		int32 PollFailures = 0;
		/** Bounded: one key ("skipped"). */
		FRateLimitedLog SkipLog{60.0};
	};
}
