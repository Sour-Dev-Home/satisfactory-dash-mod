#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Commands.h"
#include "SatisDash/Config.h"
#include "SatisDash/Credential.h"

namespace SatisDash
{
	/** One call to make, built in full here so Plan 2B's transport only copies it onto an IHttpRequest. */
	struct FRequestSpec
	{
		FString Verb;
		FString Url;
		TArray<TPair<FString, FString>> Headers;
		TArray<uint8> Body;
		float TimeoutSeconds = 15.f;
	};

	// backendClient.ts: REQUEST_TIMEOUT_MS, LONG_POLL_GRACE_MS, GZIP_OVER_BYTES; the contract's longest wait (25 s).
	inline constexpr float RequestTimeoutSeconds = 15.f;
	inline constexpr int32 PollWaitSeconds = 25;
	inline constexpr float LongPollGraceSeconds = 10.f;
	inline constexpr int32 GzipOverBytes = 1024;

	/** A JSON body as it goes on the wire: UTF-8, gzipped when over GzipOverBytes and compression worked. */
	struct FEncodedBody
	{
		TArray<uint8> Bytes;
		bool bGzip = false;
	};

	/** The bytes a request built from Json sends (the pusher stores these, so a queued snapshot is held compressed). */
	SATISDASHCORE_API FEncodedBody EncodeBody(const FString& Json);

	/** RFC 3986: unreserved characters kept, every other UTF-8 byte as %XX. For a command ID in a path. */
	SATISDASHCORE_API FString PercentEncode(const FString& Value);
	/** POST /agent/v1/enroll with {code, agentVersion}. No Authorization header. */
	SATISDASHCORE_API FRequestSpec BuildEnrollRequest(const FAgentConfig& Config);
	/** POST /agent/v1/snapshots. The secret goes only in the Authorization header (S7). */
	SATISDASHCORE_API FRequestSpec BuildSnapshotRequest(const FAgentConfig& Config, const FCredential& Credential, const FString& SnapshotJson);
	/** The same request from a body EncodeBody already made: byte for byte what the JSON overload sends. */
	SATISDASHCORE_API FRequestSpec BuildSnapshotRequest(const FAgentConfig& Config, const FCredential& Credential, FEncodedBody Body);
	/** GET /agent/v1/commands?waitSeconds=25, with a 35 s timeout. */
	SATISDASHCORE_API FRequestSpec BuildCommandsPoll(const FAgentConfig& Config, const FCredential& Credential);
	/** POST /agent/v1/commands/<id>/result. */
	SATISDASHCORE_API FRequestSpec BuildResultRequest(const FAgentConfig& Config, const FCredential& Credential, const FString& CommandId, const FCommandResult& Result);
}
