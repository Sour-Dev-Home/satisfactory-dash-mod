#pragma once

#include "CoreMinimal.h"

namespace SatisDash
{
	/** The spec's default backend ("Shape", Config). */
	inline const TCHAR* DefaultBackendUrl = TEXT("https://api.satis-manager.com");

	struct FAgentConfig
	{
		/** Checked by IsAllowedBackendUrl, with no trailing slash. */
		FString BackendUrl;
		/** Empty, or a valid code in upper case. */
		FString EnrollmentCode;
	};

	enum class EConfigStatus : uint8
	{
		/** There was no file; one was written with the default URL and no code. Config holds those defaults. */
		Missing,
		/** The file can't be used. BadField names the first problem: "json", "backendUrl" or "enrollmentCode". */
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
