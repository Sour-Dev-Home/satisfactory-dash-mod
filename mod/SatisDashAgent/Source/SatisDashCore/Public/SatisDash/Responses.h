#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "SatisDash/Credential.h"

namespace SatisDash
{
	/** One HTTP call's outcome, as Plan 2B's transport fills it. Code 0 = no answer (timeout, refused, TLS failure). */
	struct FHttpReply
	{
		int32 Code = 0;
		FString ContentType;
		FString RetryAfter;
		TArray<uint8> Body;
	};

	enum class EReplyKind : uint8 { Ok, Transient, AuthRejected, Fatal };

	/**
	 * The Node agent's rules (agent/src/backendClient.ts): 2xx is Ok; any 3xx is Fatal (a redirect is refused, S3);
	 * 401 on an authenticated call is AuthRejected; 429, 5xx and no answer are Transient; anything else is Fatal.
	 */
	SATISDASHCORE_API EReplyKind ClassifyReply(int32 Code, bool bAuthenticated);

	inline constexpr int32 MaxResponseBytes = 64 * 1024;
	/** The largest cap any one request may ask for: FRM's large endpoints (FRM bridge spec "Memory"). */
	inline constexpr int64 MaxRequestResponseBytes = 16 * 1024 * 1024;
	inline constexpr int32 MinCadenceSeconds = 1;
	inline constexpr int32 MaxCadenceSeconds = 3600;
	inline constexpr int32 MaxCommandIdLength = 100;
	inline constexpr int32 MaxCommandsPerPoll = 20;

	/** CadenceSchema. The defaults are today's backend values (spec "Data flow"; factory from the schema). */
	struct FCadence
	{
		int32 StatusSeconds = 5;
		int32 PowerSeconds = 5;
		int32 FactorySeconds = 30;
	};

	struct FEnrollResponse
	{
		FCredential Credential;
		FCadence Cadence;
	};

	struct FSnapshotResponse
	{
		FCadence Cadence;
		bool bCommandsPending = false;
	};

	/** WorldIngestResponseSchema (map.ts): what POST /agent/v1/world/:layer answers on 200 or 202. */
	struct FWorldResponse
	{
		bool bAccepted = false;
		bool bUnchanged = false;
	};

	struct FAgentCommand
	{
		FString Id;
		FString Type;
		TSharedPtr<FJsonObject> Params;
		FDateTime ExpiresAt;
	};

	/** S4: at most 64 KB, Content-Type application/json, UTF-8, a JSON object. Null otherwise. */
	SATISDASHCORE_API TSharedPtr<FJsonObject> ReadJsonBody(const FHttpReply& Reply);
	/** S5: each value a finite number, rounded and clamped to 1-3600. Unset when one is missing or not a number. */
	SATISDASHCORE_API TOptional<FCadence> ParseCadence(const TSharedPtr<FJsonObject>& Object);
	/** 201 only (EnrollResponseSchema), with a well-formed secret and server id. */
	SATISDASHCORE_API TOptional<FEnrollResponse> ParseEnrollResponse(const FHttpReply& Reply);
	SATISDASHCORE_API TOptional<FSnapshotResponse> ParseSnapshotResponse(const FHttpReply& Reply);
	/** A 2xx JSON object (S4's limits) with boolean accepted and unchanged. Unset otherwise: the sender treats that as transient, as backendClient.ts does. */
	SATISDASHCORE_API TOptional<FWorldResponse> ParseWorldResponse(const FHttpReply& Reply);
	/** Every command must be well formed, or the whole response is refused (S4); at most 20 are kept. */
	SATISDASHCORE_API TOptional<TArray<FAgentCommand>> ParseCommandsResponse(const FHttpReply& Reply);
}
