#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Config.h"
#include "SatisDash/Credential.h"
#include "SatisDash/FrmBridge.h"
#include "SatisDash/FrmWorld.h"
#include "SatisDash/Ports.h"
#include "SatisDash/RateLimit.h"
#include "SatisDash/Responses.h"

namespace SatisDash
{
	/** worldReader.ts WORLD_JITTER_FRACTION. */
	inline constexpr double WorldJitterFraction = 0.1;

	/** worldReader.ts jitteredIntervalMs: Base x [0.9, 1.1]. Random01 is clamped to [0, 1]; NaN is the middle. */
	SATISDASHCORE_API double JitteredSeconds(double BaseSeconds, double Random01);

	/**
	 * S4-A Q6: the cast-count check of the power wires. True (worth one Warning) when no wire was read, or when the wires read plus
	 * the dropped ones differ from the subsystem's advisory count by more than max(5, 5% of it). An unknown count (negative)
	 * only checks for zero. It never changes the output.
	 */
	SATISDASHCORE_API bool WireCountSuspect(int32 Wires, int32 Dropped, int32 Expected);

	/** What one tick of the sender needs from the driver. The credential is used at once and never kept (Agent.h). */
	struct FWorldContext
	{
		/** Monotonic seconds. */
		double Now;
		/** UTC, for observedAt. */
		FDateTime Utc;
		const FAgentConfig* Config;
		const FCredential* Credential;
		/** No snapshot is queued or in flight: world posts go only then (spec "World-layer sends ... behind snapshots"). */
		bool bSnapshotsIdle;
		/** The native readers, when a layer's source is native. */
		IGameReader* NativeReaders = nullptr;
		/** A game is running (the last status pass said so): a native scan starts only then, as the native players are read. */
		bool bGameRunning = true;
		/** R3: times the steps of the train-routes read (the sender resets it when a read begins). Null: that layer cannot read. */
		FStepBudget* TrainsBudget = nullptr;
		/** R5: times the shared logistics pass (the sender resets it when a pass begins). Null: a native logistics layer cannot read. */
		FStepBudget* LogisticsBudget = nullptr;
		/** S4-A R8: times the map-markers read (the sender resets it when a read begins). Null: that layer cannot read. */
		FStepBudget* MarkersBudget = nullptr;
		/** S4-A R9: the same for the drone-routes read. */
		FStepBudget* DronesBudget = nullptr;
	};

	/**
	 * The Node agent's WorldReader (agent/src/worldReader.ts) on the game thread's tick: each layer is read on its own slow,
	 * jittered schedule, posted to POST /agent/v1/world/:layer only when its mapped data changed since the last ACCEPTED send,
	 * retried with backoff when the backend is unwell, and given up on until the next cycle when it refuses. The FRM read,
	 * hashing, parsing and gzip are FFrmBridge's (on the worker); this class never touches a body's bytes.
	 *
	 * Memory (it lives for the server's uptime): per layer two hashes (32 bytes each) and at most one encoded body (at most
	 * 2,000,000 bytes before gzip) held from Ready until accepted, refused or stopped.
	 */
	class SATISDASHCORE_API FWorldSender
	{
	public:
		FWorldSender(ITransport& InTransport, FFrmBridge& InBridge, TFunction<double()> InRandom01);
		FWorldSender(const FWorldSender&) = delete;
		FWorldSender& operator=(const FWorldSender&) = delete;

		/** The agent started Running: forget every hash and arm each layer's boot offset (x jitter). */
		void Start(double Now);
		/** The agent left Running: no read, no post, no held body; a reply still on its way changes nothing. */
		void Stop();
		void Tick(const FWorldContext& Context);
		/** True once after a world post got 401: the driver halts the agent as it does for a snapshot 401. */
		bool TakeAuthRejected();
		bool Active() const { return bActive; }
		/** Layers holding an encoded body (waiting for a post or in flight). */
		int32 Held() const;

	private:
		/** Scanning: a native scan is walking the game's actors, a slice per tick (the game thread's part of a native read). */
		enum class EPhase : uint8 { Waiting, Scanning, Reading, Pending, Posting };
		struct FLayer
		{
			EPhase Phase = EPhase::Waiting;
			double NextAt = 0;
			double NextAttemptAt = 0;
			int32 Attempts = 0;
			/** The last settled raw body and the last accepted data (see the plan's Global Constraints). */
			TOptional<FBlake3Hash> RawHash;
			TOptional<FBlake3Hash> DataHash;
			/** A warning was written in this layer's current streak of that condition (cleared when it next settles without it). */
			bool bReadLogged = false;
			bool bCapLogged = false;
			bool bDroppedLogged = false;
			bool bScanLogged = false;
			/** Native first read (ruling M7): the longer first wait has been applied. */
			bool bFirstNativeDone = false;
			FEncodedBody Body;
			FBlake3Hash PendingRaw;
			FBlake3Hash PendingData;
		};

		void TickLayer(EWorldLayer Layer, const FWorldContext& Context);
		/** R3: one tick of the train-routes layer's native read (the game thread's part), then its hand-over to the worker. */
		void StepTrainRoutesScan(const FWorldContext& Context);
		/** S4-A: one tick of a small layer's native read (map markers, drone routes), then its hand-over to the worker. */
		void StepSmallLayerScan(EWorldLayer Layer, const FWorldContext& Context);
		/** R5: one tick of the shared logistics pass (the game thread's part), then its hand-over to the worker. */
		void StepLogisticsPass(const FWorldContext& Context);
		void Settle(EWorldLayer Layer, FWorldOutcome Outcome, const FWorldContext& Context);
		void Post(EWorldLayer Layer, const FWorldContext& Context);
		void Finish(EWorldLayer Layer, uint32 SentIn, const FHttpReply& Reply);
		void ScheduleNext(EWorldLayer Layer, double Now);
		bool ShouldLog(EWorldLayer Layer, const TCHAR* Event, double Now);

		ITransport& Transport;
		FFrmBridge& Bridge;
		TFunction<double()> random01Source;
		FLayer Layers[WorldLayerCount];
		bool bActive = false;
		bool bPostInFlight = false;
		bool bAuthRejected = false;
		double LastNow = 0;
		double StartedAt = 0;
		/** A logistics pass is open, and which layers ride in it. */
		bool bPassOpen = false;
		bool bPassRails = false;
		bool bPassConveyors = false;
		bool bPassPowerLines = false;
		/** The one-off tier census lines have been written since Start. */
		bool bConveyorCensusDone = false;
		uint32 Generation = 0;
		/** Bounded: each layer's few events, each at most once per 600 s. */
		FRateLimitedLog Log;
	};
}
