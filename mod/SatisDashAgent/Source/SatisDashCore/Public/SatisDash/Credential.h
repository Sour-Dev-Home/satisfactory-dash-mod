#pragma once

#include "CoreMinimal.h"

namespace SatisDash
{
	struct FCredential
	{
		FString AgentSecret;
		FString ServerId;
		/** The backend that issued it (normalised like the config's). The secret is only ever sent there. */
		FString BackendUrl;
	};

	/**
	 * 1-256 base64url characters. The contract's secret is 43 (EnrollResponseSchema); the wider bound keeps a future
	 * length change working, and the alphabet keeps CR, LF and spaces out of the Authorization header.
	 */
	SATISDASHCORE_API bool IsAgentSecret(const FString& Secret);
	/** ServerIdSchema: ^[a-z0-9-]{1,32}$. */
	SATISDASHCORE_API bool IsServerId(const FString& Id);
	SATISDASHCORE_API TOptional<FCredential> ParseCredential(const FString& Text);
	SATISDASHCORE_API FString CredentialToJson(const FCredential& Credential);

	/** Saved/SatisDash/credential.json (owner decision, spec "Shape"), and only there (S11). */
	class SATISDASHCORE_API FCredentialStore
	{
	public:
		explicit FCredentialStore(FString InPath);
		/**
		 * The saved credential, or none when there is none or it is broken. A valid Path.tmp with no Path (a crash
		 * between a replace's delete and its move) is moved into place first.
		 */
		TOptional<FCredential> Load() const;
		bool Save(const FCredential& Credential) const;
		/** Removes the file and any temp file. True when neither is left. */
		bool Delete() const;
		/** Whether the file is there, valid or not (for the "unreadable" log line). */
		bool Exists() const;

	private:
		FString Path;
	};
}
