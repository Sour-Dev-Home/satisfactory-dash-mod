#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Responses.h"

namespace SatisDash
{
	/** CommandResultRequestSchema: ok, or one of four codes; never free text. */
	struct FCommandResult
	{
		bool bOk = false;
		FString Code;
	};

	SATISDASHCORE_API FCommandResult CommandOk();
	/** One of unsupported, upstream_unreachable, upstream_auth_rejected, upstream_error; anything else becomes upstream_error. */
	SATISDASHCORE_API FCommandResult CommandFailed(const TCHAR* Code);
	SATISDASHCORE_API FString CommandResultToJson(const FCommandResult& Result);

	/** agent/src/ledger.ts: which command IDs were seen, so none runs twice (S6). In memory, bounded, purged at expiry. */
	class SATISDASHCORE_API FCommandLedger
	{
	public:
		static constexpr int32 MaxEntries = 1000;
		enum class EVerdict : uint8 { New, Running, Done };

		/** Records Id when new. Done fills OutDone with the result Finish recorded. IDs compare case-sensitively. */
		EVerdict Begin(const FString& Id, const FDateTime& ExpiresAt, const FDateTime& Now, FCommandResult& OutDone);
		void Finish(const FString& Id, const FCommandResult& Result);
		int32 Num() const { return Entries.Num(); }

	private:
		struct FEntry
		{
			FString Id;
			FDateTime ExpiresAt;
			TOptional<FCommandResult> Result;
		};
		TArray<FEntry> Entries; // oldest first
	};

	inline constexpr int32 MaxCommandLifetimeSeconds = 600;
	inline constexpr int32 ResultAttempts = 5;

	enum class ECommandAction : uint8
	{
		/** Expired, too far out, or already running: nothing to do. */
		Skip,
		/** Post Result: an unsupported command, or a repeat of a finished one. */
		Report,
		/** Run set_auto_pause natively with bEnabled, then Finish the ledger entry and report. */
		SetAutoPause,
	};

	struct FCommandPlan
	{
		ECommandAction Action = ECommandAction::Skip;
		bool bEnabled = false;
		FCommandResult Result;
	};

	/** commandRunner.ts's handle(): only set_auto_pause with exactly {"enabled": <bool>} runs (S6). */
	SATISDASHCORE_API FCommandPlan PlanCommand(const FAgentCommand& Command, FCommandLedger& Ledger, const FDateTime& Now);
	/** commandRunner.ts's report(): up to 5 attempts, only on a transient failure, and never past the command's expiry. */
	SATISDASHCORE_API bool ShouldRetryResult(int32 AttemptsMade, EReplyKind Kind, const FDateTime& ExpiresAt, const FDateTime& Now);
}
