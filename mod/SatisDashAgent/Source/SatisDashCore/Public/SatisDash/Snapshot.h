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

	/** FG.DSAutoPause as the game's server options report it (GetServerOptions: the options and the pending ones). */
	struct FAutoPauseReading
	{
		/** The applied value. */
		bool bEnabled = false;
		/** Set when FG.DSAutoPause is also among the pending options: a change the server applies later. */
		TOptional<bool> PendingValue;
	};

	/** What the contract's settings.autoPause carries: the pending value when one waits (agent.ts), else the applied one. */
	inline bool EffectiveAutoPause(const FAutoPauseReading& Reading)
	{
		return Reading.PendingValue.IsSet() ? Reading.PendingValue.GetValue() : Reading.bEnabled;
	}

	/** One pass's readings (spec "Data flow"). A part is unset when it wasn't due or its reader failed. */
	struct FSnapshot
	{
		FString AgentVersion;
		/** Must be UTC (FDateTime::UtcNow()), not local time: the wire format and expiresAt are both UTC. */
		FDateTime ObservedAt;
		/** S4-A (R7), top level beside reachable: this pass's status read succeeded. Unset writes no key. */
		TOptional<bool> bGameReachable;
		/** UTC time of the last successful status read; unset until the first one (no key, never null). */
		TOptional<FDateTime> LastGameReadAt;
		/** The sender's version (AgentVersion()), cut to 64 characters; empty writes no key. */
		FString ModVersion;
		TOptional<FAutoPauseReading> AutoPause;
		TOptional<FStatusReading> Status;
		TOptional<TArray<FCircuitReading>> Power;
		/** The players part (AgentPlayersSchema) as JSON, made by FFrmBridge; spliced in as is. */
		TOptional<FString> PlayersJson;
		/** The factory part (AgentFactorySchema) as JSON, made off the game thread by FFactoryParse. */
		TOptional<FString> FactoryJson;
		/** The mapLive part ({"trains":[...],"stations":[...]}, FRM bridge 3B) as JSON, joined by FFrmBridge; spliced in as is. */
		TOptional<FString> MapLiveJson;
	};

	/** At most 200 UTF-16 code units (the contract's string length, as JavaScript counts it), never splitting a pair. */
	SATISDASHCORE_API FString ClampString(const FString& Value);
	/** At most MaxChars UTF-16 code units, never splitting a pair; ClampString is the 200-character cut. */
	SATISDASHCORE_API FString ClampStringTo(const FString& Value, int32 MaxChars);
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
