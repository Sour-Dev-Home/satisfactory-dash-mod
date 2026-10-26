#pragma once

#include "CoreMinimal.h"

namespace SatisDash
{
	/** The spec's default backend ("Shape", Config). */
	inline const TCHAR* DefaultBackendUrl = TEXT("https://api.satis-manager.com");

	/** FRM's own default HTTP port (FicsitRemoteMonitoring.cpp:258) and the Node agent's (store.ts:29). */
	inline constexpr int32 DefaultFrmPort = 8080;

	/**
	 * Where a reader gets its data (ADR-0058 Decision 4): FRM through the bridge, or the game itself. One source per reader.
	 */
	enum class ESource : uint8 { Bridge, Native };
	/** What a host gets with no key set. The switch PR for a reader flips its constant here and changes nothing else. */
	inline constexpr ESource DefaultPlayersSource = ESource::Native;
	inline constexpr ESource DefaultResourceNodesSource = ESource::Bridge;

	struct FAgentConfig
	{
		/** Checked by IsAllowedBackendUrl, with no trailing slash. */
		FString BackendUrl;
		/** Empty, or a valid code in upper case. */
		FString EnrollmentCode;
		/** FRM's port on 127.0.0.1 (FRM bridge spec "Configuration"); unset = DefaultFrmPort, and not written back. */
		TOptional<int32> FrmPort;
		/** Sent only as X-FRM-Authorization; empty = none. The host's own FRM secret: never logged. */
		FString FrmToken;
		/**
		 * Hidden stopgap overrides, documented in docs/native-readers.md and deleted with the bridge (ADR-0058, R7): the
		 * keys "playersSource" and "resourceNodesSource", each exactly "bridge" or "native". Unset = the default above,
		 * and not written back.
		 */
		TOptional<ESource> PlayersSource;
		TOptional<ESource> ResourceNodesSource;

		int32 GetFrmPort() const { return FrmPort.Get(DefaultFrmPort); }
		ESource GetPlayersSource() const { return PlayersSource.Get(DefaultPlayersSource); }
		ESource GetResourceNodesSource() const { return ResourceNodesSource.Get(DefaultResourceNodesSource); }
	};

	enum class EConfigStatus : uint8
	{
		/** There was no file; one was written with the default URL and no code. Config holds those defaults. */
		Missing,
		/** The file can't be used. BadField names the first problem: "json", "backendUrl", "enrollmentCode", "frmPort", "frmToken", "playersSource" or "resourceNodesSource". */
		Broken,
		Valid,
	};

	struct FConfigResult
	{
		EConfigStatus Status = EConfigStatus::Broken;
		FAgentConfig Config;
		FString BadField;
	};

	/** S1: https:// with a host, or http:// only to 127.0.0.1 or localhost (the harness). No user info, query or fragment. */
	SATISDASHCORE_API bool IsAllowedBackendUrl(const FString& Url);
	/**
	 * The transport's check on a request URL (Task 1e): the part before its query passes IsAllowedBackendUrl, and the query
	 * has no fragment, whitespace or control character. OutPlainHttp says the scheme is http (so the host is loopback).
	 */
	SATISDASHCORE_API bool IsAllowedRequestUrl(const FString& Url, bool& OutPlainHttp);
	/** The form ParseConfig stores: surrounding whitespace trimmed, trailing slashes removed. The credential uses it too. */
	SATISDASHCORE_API FString NormalizeBackendUrl(const FString& Url);
	/** The contract's code: three groups of four base32 characters joined by dashes, ^[A-Z2-7]{4}-[A-Z2-7]{4}-[A-Z2-7]{4}$ (EnrollmentCodeSchema). */
	SATISDASHCORE_API bool IsEnrollmentCode(const FString& Code);
	/** A whole number from 1 to 65535. */
	SATISDASHCORE_API bool IsFrmPort(double Value);
	/** No control character (below 0x20, or 0x7F), so it can't end a header line or start another; at most 512 characters. */
	SATISDASHCORE_API bool IsHeaderSafe(const FString& Value);
	/** Trims both values, upper-cases the code and drops trailing slashes from the URL, then checks them. */
	SATISDASHCORE_API FConfigResult ParseConfig(const FString& Text);
	SATISDASHCORE_API FString ConfigToJson(const FAgentConfig& Config);
	/** Reads the file. A missing one is written with the defaults; a broken one is never overwritten. */
	SATISDASHCORE_API FConfigResult LoadConfigFile(const FString& Path);
	/** Rewrites the file with the code blanked, keeping its URL. */
	SATISDASHCORE_API bool BlankConfigCode(const FString& Path, const FAgentConfig& Config);
	/** The file's modification time; FDateTime::MinValue() when it doesn't exist. */
	SATISDASHCORE_API FDateTime ConfigModTime(const FString& Path);
}
