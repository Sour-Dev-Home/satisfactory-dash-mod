#include "SatisDash/Agent.h"

#include "Misc/FileHelper.h"
#include "SatisDash/Log.h"
#include "SatisDash/Pusher.h"

SatisDash::FAgent::FAgent(FString InConfigPath, FString InCredentialPath, TFunction<double()> InRandom01)
	: ConfigPath{MoveTemp(InConfigPath)}
	, Credentials{MoveTemp(InCredentialPath)}
	, random01{MoveTemp(InRandom01)}
	, Pairing{random01}
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
	if (bEnrollInFlight || bPairingInFlight)
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
		// R7 (owner decision D2): one line per load of a file that still holds keys of the removed FRM role. Names only, never a value.
		const FString Ignored = IgnoredKeysLine(Out);
		if (!Ignored.IsEmpty())
		{
			UE_LOG(LogSatisDash, Display, TEXT("%s"), *Ignored);
		}
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
		Pairing.Stop();
		Enter(EAgentState::ConfigBroken, FString::Printf(
			TEXT("SatisDashAgent.cfg can't be used (bad %s); the mod is idle until it is fixed"), *Loaded.BadField), true);
		return;
	}
	const FString PreviousCode = Config.EnrollmentCode;
	const FString PreviousUrl = Config.BackendUrl;
	Config = Loaded.Config;
	if (IsBoundHere())
	{
		Pairing.Stop();
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
		Pairing.Stop();
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
		Pairing.Stop();
		// Kept in memory and on disk: switching the config back to its backend resumes it.
		Enter(EAgentState::Idle, TEXT("The saved credential belongs to a different backend than SatisDashAgent.cfg names; add a new enrolment code from that backend's dashboard (nothing is sent until then)"), true);
		return;
	}
	if (State == EAgentState::Halted)
	{
		return; // revoked: no pairing until a restart; a code in the cfg wins earlier in this function
	}
	if (State == EAgentState::Pairing && !PreviousUrl.Equals(Config.BackendUrl, ESearchCase::CaseSensitive))
	{
		Pairing.Stop(); // an open code belongs to the old backend
	}
	Enter(EAgentState::Pairing, TEXT("Not enrolled: pairing with the dashboard"), false);
	Pairing.Open(LastNow);
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
	LastNow = FMath::Max(LastNow, Now);
	bEnrollInFlight = true;
	return BuildEnrollRequest(Config);
}

void SatisDash::FAgent::AdoptCredential(const FEnrollResponse& Enrolled)
{
	// Issued by the backend the request went to (Config is unchanged since the Start; CheckConfig waits for the
	// answer), whatever the file says by now.
	FCredential Issued = Enrolled.Credential;
	Issued.BackendUrl = Config.BackendUrl;
	if (!Credentials.Save(Issued))
	{
		UE_LOG(LogSatisDash, Warning, TEXT("Credential received, but the credential file couldn't be written; it will be lost when the server restarts"));
	}
	Credential = Issued;
	Cadence = Enrolled.Cadence;
}

void SatisDash::FAgent::FinishEnroll(const FHttpReply& Reply, double Now)
{
	if (!bEnrollInFlight)
	{
		return;
	}
	bEnrollInFlight = false;
	LastNow = FMath::Max(LastNow, Now);
	const EReplyKind Kind = ClassifyReply(Reply.Code, false);
	if (Kind == EReplyKind::Ok)
	{
		if (const TOptional<FEnrollResponse> Enrolled = ParseEnrollResponse(Reply))
		{
			AdoptCredential(*Enrolled);
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
	Pairing.Stop();
	Enter(EAgentState::Halted, TEXT("The backend rejected the credential (401): this server was revoked in the dashboard. Stopped; restart the server to pair it again, or add a code to SatisDashAgent.cfg"), true);
}

TOptional<SatisDash::FRequestSpec> SatisDash::FAgent::StartPairing(double Now, TFunctionRef<FString()> ServerName)
{
	LastNow = FMath::Max(LastNow, Now);
	if (State != EAgentState::Pairing || bPairingInFlight)
	{
		return {};
	}
	TOptional<FRequestSpec> Spec = Pairing.Next(Config, ServerName, Now);
	bPairingInFlight = Spec.IsSet();
	return Spec;
}

void SatisDash::FAgent::FinishPairing(const FHttpReply& Reply, double Now)
{
	LastNow = FMath::Max(LastNow, Now);
	bPairingInFlight = false;
	// FPairing always sees the reply (it keeps its own one-in-flight and drop-after-Stop state); the agent then drops
	// the result when it left Pairing meanwhile (a code arrived: Review Focus 4).
	const TOptional<FPairingPoll> Poll = Pairing.Finish(Reply, Now);
	if (State != EAgentState::Pairing || !Poll.IsSet() || Poll->Status != EPairingPoll::Approved || !Poll->Enrolled.IsSet())
	{
		return;
	}
	AdoptCredential(*Poll->Enrolled);
	// No code in the file to blank. A cfg edited while the request was in flight is read at the next CheckConfig.
	FString Line = TEXT("Paired with the dashboard; reporting to the backend");
	if (!Poll->ApprovedBy.IsEmpty())
	{
		Line += FString::Printf(TEXT(" (approved by \"%s\")"), *Poll->ApprovedBy);
	}
	Enter(EAgentState::Running, Line, false);
}

void SatisDash::FAgent::OnPlayerCount(int32 Players, double Now)
{
	LastNow = FMath::Max(LastNow, Now);
	if (State == EAgentState::Pairing)
	{
		Pairing.OnPlayers(Players, Now);
	}
}

TOptional<FString> SatisDash::FAgent::TakeChatLine()
{
	return State == EAgentState::Pairing ? Pairing.TakeChatLine() : TOptional<FString>();
}
