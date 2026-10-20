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
	return State == EAgentState::Running && IsBoundHere() ? &Credential.GetValue() : nullptr;
}

bool SatisDash::FAgent::IsBoundHere() const
{
	// Exact and case-sensitive on the normalised strings: the secret goes only to the backend that issued it.
	return Credential.IsSet() && Credential->BackendUrl.Equals(Config.BackendUrl, ESearchCase::CaseSensitive);
}

void SatisDash::FAgent::Boot()
{
	Credential = Credentials.Load();
	if (!Credential.IsSet() && Credentials.Exists())
	{
		UE_LOG(LogSatisDash, Warning, TEXT("The credential file can't be read; treating this server as not enrolled"));
	}
	FConfigResult Loaded;
	ReadConfigFile(false, Loaded);
	Settle(Loaded);
	bBooted = true;
}

void SatisDash::FAgent::CheckConfig()
{
	if (bEnrollInFlight)
	{
		return; // decide after the answer: the next check sees the change
	}
	FConfigResult Loaded;
	if (ReadConfigFile(true, Loaded))
	{
		Settle(Loaded);
	}
}

bool SatisDash::FAgent::ReadConfigFile(bool bOnlyIfChanged, FConfigResult& Out)
{
	if (!IFileManager::Get().FileExists(*ConfigPath))
	{
		// Missing: LoadConfigFile writes the defaults; remember what it wrote.
		Out = LoadConfigFile(ConfigPath);
		RememberConfigFile();
		return true;
	}
	// One read per check: the time first, then the text, and the parse uses that same text. A save that lands in
	// between leaves an older time with newer text, so the next check sees a difference and reads again.
	const FDateTime Time = ConfigModTime(ConfigPath);
	FString Text;
	const bool bRead = FFileHelper::LoadFileToString(Text, *ConfigPath);
	if (bOnlyIfChanged && Time == ConfigTime && Text.Equals(ConfigText, ESearchCase::CaseSensitive))
	{
		return false;
	}
	ConfigTime = Time;
	ConfigText = Text;
	if (bRead)
	{
		Out = ParseConfig(Text);
	}
	else
	{
		Out = FConfigResult();
		Out.BadField = TEXT("json");
	}
	return true;
}

void SatisDash::FAgent::RememberConfigFile()
{
	ConfigTime = ConfigModTime(ConfigPath);
	ConfigText.Reset();
	FFileHelper::LoadFileToString(ConfigText, *ConfigPath);
}

void SatisDash::FAgent::BlankCodeInFile()
{
	// Never from memory: the file may have been edited since it was read, and a broken one is never overwritten.
	Config.EnrollmentCode.Reset();
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *ConfigPath))
	{
		return;
	}
	const FConfigResult Now = ParseConfig(Text);
	if (Now.Status != EConfigStatus::Valid)
	{
		return; // left alone; the next CheckConfig sees it and settles
	}
	if (BlankConfigCode(ConfigPath, Now.Config))
	{
		Config = Now.Config;
		Config.EnrollmentCode.Reset();
		RememberConfigFile();
	}
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
	if (IsBoundHere())
	{
		if (!Config.EnrollmentCode.IsEmpty())
		{
			// One code never enrols twice (spec error table): the credential wins and the code is blanked.
			UE_LOG(LogSatisDash, Display, TEXT("An enrolment code is in SatisDashAgent.cfg, but this server is already enrolled; blanking it (revoke the server in the dashboard to enrol again)"));
			BlankCodeInFile();
		}
		// BlankCodeInFile adopts what the file says now, which may name another backend (2A final review, N2).
		if (IsBoundHere())
		{
			Enter(EAgentState::Running, TEXT("Enrolled; reporting to the backend"), false);
			return;
		}
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
	if (Credential.IsSet())
	{
		// Kept in memory and on disk: switching the config back to its backend resumes it.
		Enter(EAgentState::Idle, TEXT("The saved credential belongs to a different backend than SatisDashAgent.cfg names; add a new enrolment code from that backend's dashboard (nothing is sent until then)"), true);
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
			// Issued by the backend the request went to (Config is unchanged since StartEnroll; CheckConfig waits for
			// the answer), whatever the file says by now.
			FCredential Issued = Enrolled->Credential;
			Issued.BackendUrl = Config.BackendUrl;
			if (!Credentials.Save(Issued))
			{
				UE_LOG(LogSatisDash, Warning, TEXT("Enrolled, but the credential file couldn't be written; enrolment will be lost when the server restarts"));
			}
			Credential = Issued;
			Cadence = Enrolled->Cadence;
			BlankCodeInFile(); // may adopt a URL edited while the request was in flight
			EnrollFailures = 0;
			if (!IsBoundHere())
			{
				Enter(EAgentState::Idle, TEXT("The enrolment succeeded at the backend the request went to, but SatisDashAgent.cfg now names a different one; the credential is kept for the original backend and nothing is sent to the new one"), true);
				return;
			}
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
