#include "SatisDash/Agent.h"

#include "Misc/FileHelper.h"
#include "SatisDash/Log.h"
#include "SatisDash/Pusher.h"

SatisDash::FAgent::FAgent(FString InConfigPath, FString InCredentialPath, TFunction<double()> InRandom01)
	: ConfigPath{MoveTemp(InConfigPath)}
	, Credentials{MoveTemp(InCredentialPath)}
	, random01{MoveTemp(InRandom01)}
{
}

const SatisDash::FCredential* SatisDash::FAgent::GetCredential() const
{
	return State == EAgentState::Running && Credential.IsSet() ? &Credential.GetValue() : nullptr;
}

void SatisDash::FAgent::Boot()
{
	Credential = Credentials.Load();
	if (!Credential.IsSet() && Credentials.Exists())
	{
		UE_LOG(LogSatisDash, Warning, TEXT("The credential file can't be read; treating this server as not enrolled"));
	}
	const FConfigResult Loaded = LoadConfigFile(ConfigPath);
	RememberConfigFile();
	Settle(Loaded);
	bBooted = true;
}

void SatisDash::FAgent::CheckConfig()
{
	if (bEnrollInFlight)
	{
		return; // decide after the answer: the next check sees the change
	}
	FString Text;
	FFileHelper::LoadFileToString(Text, *ConfigPath);
	if (ConfigModTime(ConfigPath) == ConfigTime && Text.Equals(ConfigText, ESearchCase::CaseSensitive))
	{
		return;
	}
	const FConfigResult Loaded = LoadConfigFile(ConfigPath);
	RememberConfigFile();
	Settle(Loaded);
}

void SatisDash::FAgent::RememberConfigFile()
{
	ConfigTime = ConfigModTime(ConfigPath);
	ConfigText.Reset();
	FFileHelper::LoadFileToString(ConfigText, *ConfigPath);
}

void SatisDash::FAgent::Settle(const FConfigResult& Loaded)
{
	if (Loaded.Status == EConfigStatus::Broken)
	{
		Enter(EAgentState::ConfigBroken, FString::Printf(
			TEXT("SatisDashAgent.cfg can't be used (bad %s); the mod is idle until it is fixed"), *Loaded.BadField), true);
		return;
	}
	const FString PreviousCode = Config.EnrollmentCode;
	Config = Loaded.Config;
	if (Credential.IsSet())
	{
		if (!Config.EnrollmentCode.IsEmpty())
		{
			// One code never enrols twice (spec error table): the credential wins and the code is blanked.
			UE_LOG(LogSatisDash, Display, TEXT("An enrolment code is in SatisDashAgent.cfg, but this server is already enrolled; blanking it (revoke the server in the dashboard to enrol again)"));
			BlankConfigCode(ConfigPath, Config);
			Config.EnrollmentCode.Reset();
			RememberConfigFile();
		}
		Enter(EAgentState::Running, TEXT("Enrolled; reporting to the backend"), false);
		return;
	}
	if (!Config.EnrollmentCode.IsEmpty())
	{
		if (State != EAgentState::Enrolling || !Config.EnrollmentCode.Equals(PreviousCode, ESearchCase::CaseSensitive))
		{
			EnrollFailures = 0;
			NextEnrollAt = 0;
		}
		Enter(EAgentState::Enrolling, TEXT("Enrolling with the code from SatisDashAgent.cfg"), false);
		return;
	}
	Enter(EAgentState::Idle, TEXT("Not enrolled: add an enrolment code from the dashboard to SatisDashAgent.cfg"), false);
}

void SatisDash::FAgent::Enter(EAgentState Next, const FString& Line, bool bWarning)
{
	if (bBooted && State == Next)
	{
		return; // one line per state change, never one per check
	}
	State = Next;
	if (bWarning)
	{
		UE_LOG(LogSatisDash, Warning, TEXT("%s"), *Line);
	}
	else
	{
		UE_LOG(LogSatisDash, Display, TEXT("%s"), *Line);
	}
}

TOptional<SatisDash::FRequestSpec> SatisDash::FAgent::StartEnroll(double Now)
{
	if (State != EAgentState::Enrolling || bEnrollInFlight || Now < NextEnrollAt)
	{
		return {};
	}
	bEnrollInFlight = true;
	return BuildEnrollRequest(Config);
}

void SatisDash::FAgent::FinishEnroll(const FHttpReply& Reply, double Now)
{
	if (!bEnrollInFlight)
	{
		return;
	}
	bEnrollInFlight = false;
	const EReplyKind Kind = ClassifyReply(Reply.Code, false);
	if (Kind == EReplyKind::Ok)
	{
		if (const TOptional<FEnrollResponse> Enrolled = ParseEnrollResponse(Reply))
		{
			if (!Credentials.Save(Enrolled->Credential))
			{
				UE_LOG(LogSatisDash, Warning, TEXT("Enrolled, but the credential file couldn't be written; enrolment will be lost when the server restarts"));
			}
			Credential = Enrolled->Credential;
			Cadence = Enrolled->Cadence;
			BlankConfigCode(ConfigPath, Config);
			Config.EnrollmentCode.Reset();
			RememberConfigFile();
			EnrollFailures = 0;
			Enter(EAgentState::Running, TEXT("Enrolled; reporting to the backend"), false);
			return;
		}
	}
	if (Kind == EReplyKind::Fatal)
	{
		Enter(EAgentState::CodeRejected, FString::Printf(
			TEXT("The backend refused the enrolment code (%d); get a new code from the dashboard"), Reply.Code), true);
		return;
	}
	// Transient, or a 201 not in the expected form: backendClient.ts retries both.
	if (EnrollFailures == 0)
	{
		UE_LOG(LogSatisDash, Warning, TEXT("Enrolment failed (%d); retrying with backoff"), Reply.Code);
	}
	NextEnrollAt = Now + BackoffSeconds(EnrollFailures, random01());
	++EnrollFailures;
}

void SatisDash::FAgent::OnAuthRejected()
{
	if (State != EAgentState::Running)
	{
		return;
	}
	Credentials.Delete();
	Credential.Reset();
	Enter(EAgentState::Halted, TEXT("The backend rejected the credential (401): this server was revoked in the dashboard. Stopped; add a new code to SatisDashAgent.cfg to enrol again"), true);
}
