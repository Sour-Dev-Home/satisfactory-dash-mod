#pragma once

#include "CoreMinimal.h"

namespace SatisDash
{
	/** ADR-0031's input bounds, from the contract (agent.ts AGENT_INPUT_MAX_*). */
	inline constexpr int32 MaxStringLength = 200;
	inline constexpr int32 MaxCircuits = 1000;

	/** One status reading (AgentStatusSchema), as the native reader (Plan 2B, C8) fills it. */
	struct FStatusReading
	{
		bool bTickHealthy = true;
		bool bIsGameRunning = false;
		bool bGamePaused = false;
		FString SessionName;
		int32 ConnectedPlayers = 0;
		int32 PlayerLimit = 0;
		double TickRate = 0;
		double TotalGameDurationSeconds = 0;
	};

	/** One power circuit group (AgentPowerCircuitSchema: no status, which the backend derives). */
	struct FCircuitReading
	{
		int32 CircuitGroupId = 0;
		double ProductionMW = 0;
		double ConsumptionMW = 0;
		double CapacityMW = 0;
		double MaxConsumptionMW = 0;
		bool bFuseTriggered = false;
		double BatteryCapacityMWh = 0;
		double BatteryPercent = 0;
		double BatteryDifferentialMW = 0;
	};

	/** One pass's readings (spec "Data flow"). A part is unset when it wasn't due or its reader failed. */
	struct FSnapshot
	{
		FString AgentVersion;
		/** Must be UTC (FDateTime::UtcNow()), not local time: the wire format and expiresAt are both UTC. */
		FDateTime ObservedAt;
		TOptional<bool> AutoPause;
		TOptional<FStatusReading> Status;
		TOptional<TArray<FCircuitReading>> Power;
	};

	/** At most 200 UTF-16 code units (the contract's string length, as JavaScript counts it), never splitting a pair. */
	SATISDASHCORE_API FString ClampString(const FString& Value);
	/** NaN and infinities become 0. */
	SATISDASHCORE_API double ClampFinite(double Value);
	/** ClampFinite, and negatives become 0. */
	SATISDASHCORE_API double ClampNonNegative(double Value);
	SATISDASHCORE_API FStatusReading ConformStatus(const FStatusReading& In);
	SATISDASHCORE_API FCircuitReading ConformCircuit(const FCircuitReading& In);
	/**
	 * The snapshot body (SnapshotRequestSchema), conformed. reachable is always true: the mod never sends false
	 * (spec "Data flow"). paused is the status reading's gamePaused, or null with no status.
	 */
	SATISDASHCORE_API FString SnapshotToJson(const FSnapshot& Snapshot);
}
