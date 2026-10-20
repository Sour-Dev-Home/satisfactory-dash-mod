#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Config.h"
#include "SatisDash/Credential.h"
#include "SatisDash/Requests.h"
#include "SatisDash/Responses.h"

namespace SatisDash
{
	enum class EAgentState : uint8 { Idle, ConfigBroken, Enrolling, CodeRejected, Running, Halted };

	/** Config, credential and enrolment (spec "Shape": Config, CredentialStore, Enroller, Halt; error table rows 1-7). */
	class SATISDASHCORE_API FAgent
	{
	public:
		FAgent(FString InConfigPath, FString InCredentialPath, TFunction<double()> InRandom01);

		/** Reads the credential and the config (writing a default config when missing) and settles the state. */
		void Boot();
		/** Each minute: re-reads the config when its modification time or its text changed. */
		void CheckConfig();

		EAgentState GetState() const { return State; }
		const FAgentConfig& GetConfig() const { return Config; }
		const FCadence& GetCadence() const { return Cadence; }
		void SetCadence(const FCadence& InCadence) { Cadence = InCadence; }
		/**
		 * Only while Running, and only for the backend the config names now: S10's "no snapshot without a valid
		 * credential". The pointer is invalidated by any later call on the agent: call this again before every
		 * authenticated request and never keep it across a send.
		 */
		const FCredential* GetCredential() const;

		/**
		 * Enrolling, nothing in flight and the backoff passed: the enrol request to send. Now (here and in FinishEnroll)
		 * must be monotonic seconds (FPlatformTime::Seconds()); a clock that goes backwards stalls enrolment. Every
		 * StartEnroll that returns a request must be followed by exactly one FinishEnroll, including on timeout or cancel
		 * (pass a Code 0 reply); otherwise enrolment and CheckConfig wedge. The transport must enforce
		 * MaxResponseBytes while reading, before buffering the whole body: ReadJsonBody only rejects afterwards.
		 */
		TOptional<FRequestSpec> StartEnroll(double Now);
		void FinishEnroll(const FHttpReply& Reply, double Now);
		/** A 401 on any authenticated call (push, poll or result) while Running: deletes the credential and halts. */
		void OnAuthRejected();

	private:
		/** A credential is held and it was issued by the backend the config names now. */
		bool IsBoundHere() const;
		void Settle(const FConfigResult& Loaded);
		void Enter(EAgentState Next, const FString& Line, bool bWarning);
		/** Reads the file once; false when nothing changed and bOnlyIfChanged. Remembers exactly the text and time it parsed. */
		bool ReadConfigFile(bool bOnlyIfChanged, FConfigResult& Out);
		void RememberConfigFile();
		/** Blanks the code in what the file holds now, and only if that still parses; otherwise leaves the file alone. */
		void BlankCodeInFile();

		FString ConfigPath;
		FCredentialStore Credentials;
		TFunction<double()> random01;
		EAgentState State = EAgentState::Idle;
		bool bBooted = false;
		FAgentConfig Config;
		TOptional<FCredential> Credential;
		FCadence Cadence;
		FDateTime ConfigTime;
		FString ConfigText;
		bool bEnrollInFlight = false;
		int32 EnrollFailures = 0;
		double NextEnrollAt = 0;
	};
}
