#include "SatisDash/Pairing.h"

#include "SatisDash/Log.h"
#include "SatisDash/Pusher.h"

bool SatisDash::ShouldPostCode(int32 Players, bool bNewCode, bool bJoined)
{
	return Players == 1 && (bNewCode || bJoined);
}

FString SatisDash::PairingCodeLine(const FAgentConfig& Config, const FString& UserCode, int32 ExpiresInSeconds)
{
	const int32 Minutes = FMath::Max(1, ExpiresInSeconds / 60);
	const TCHAR* Where = Config.BackendUrl == DefaultBackendUrl ? TEXT("at https://satis-manager.com/pair") : TEXT("on your dashboard's Pair page");
	return FString::Printf(TEXT("Pairing code %s: sign in %s and enter it (expires in %d %s)"), *UserCode, Where, Minutes, Minutes == 1 ? TEXT("minute") : TEXT("minutes"));
}

SatisDash::FPairing::FPairing(TFunction<double()> InRandom01)
	: random01(MoveTemp(InRandom01))
{
}

bool SatisDash::FPairing::HasOpenCode(double Now) const
{
	return !Secret.IsEmpty() && Now < CodeExpiresAt;
}

void SatisDash::FPairing::ClearCode()
{
	Secret.Reset();
	UserCode.Reset();
	CodeExpiresAt = 0;
}

void SatisDash::FPairing::Open(double Now)
{
	if (!bStopped && Now - WindowOpenedAt < PairingWindowSeconds)
	{
		return;
	}
	WindowOpenedAt = Now;
	WindowEndsAt = Now + PairingWindowSeconds;
	bStopped = false;
	NextAt = Now;
	if (!bInFlight)
	{
		ClearCode();
	}
	Failures = 0;
	bClosedLogged = false;
	bRefusedLogged = false;
}

void SatisDash::FPairing::Stop()
{
	bStopped = true;
	bDropReply = bInFlight;
	ClearCode();
	PendingChat.Reset();
}

bool SatisDash::FPairing::IsWindowOpen(double Now) const
{
	return !bStopped && Now < WindowEndsAt;
}

void SatisDash::FPairing::WaitBackoff(double Now)
{
	NextAt = Now + BackoffSeconds(Failures++, random01());
}

void SatisDash::FPairing::WaitRetryAfter(const FHttpReply& Reply, double Now)
{
	const int32 Wait = FMath::Max(1, FMath::RoundToInt(ParseRetryAfterSeconds(Reply.RetryAfter).Get(60.0)));
	NextAt = Now + Wait;
	UE_LOG(LogSatisDash, Display, TEXT("Waiting %d s for a pairing code (the backend's per-address limit)"), Wait);
}

TOptional<SatisDash::FRequestSpec> SatisDash::FPairing::Next(const FAgentConfig& Config, TFunctionRef<FString()> ServerName, double Now)
{
	if (bStopped || bInFlight)
	{
		return {};
	}
	const bool bWindowOpen = Now < WindowEndsAt;
	if (!bWindowOpen && !HasOpenCode(Now))
	{
		if (!bClosedLogged)
		{
			bClosedLogged = true;
			UE_LOG(LogSatisDash, Display, TEXT("No pairing code active: join the server or restart it to get a new one"));
		}
		return {};
	}
	if (Now < NextAt)
	{
		return {};
	}
	LastConfig = Config;
	bInFlight = true;
	if (HasOpenCode(Now))
	{
		InFlightKind = ENext::Poll;
		return BuildPairingPollRequest(Config, Secret);
	}
	ClearCode();
	InFlightKind = ENext::Start;
	const FString Name = bSendName ? CleanServerName(ServerName()) : FString();
	bInFlightNamed = !Name.IsEmpty();
	return BuildPairingStartRequest(Config, Name);
}

TOptional<SatisDash::FPairingPoll> SatisDash::FPairing::Finish(const FHttpReply& Reply, double Now)
{
	if (!bInFlight)
	{
		return {};
	}
	bInFlight = false;
	if (bDropReply)
	{
		bDropReply = false;
		return {};
	}

	if (InFlightKind == ENext::Start)
	{
		if (const TOptional<FPairingStart> Started = ParsePairingStartResponse(Reply))
		{
			Secret = Started->PairingSecret;
			UserCode = Started->UserCode;
			CodeExpiresAt = Now + Started->ExpiresInSeconds;
			IntervalSeconds = Started->IntervalSeconds;
			Failures = 0;
			bSendName = true;
			bBadStartLogged = false;
			bRefusedLogged = false;
			NextAt = Now + IntervalSeconds;
			const FString Line = PairingCodeLine(LastConfig, UserCode, Started->ExpiresInSeconds);
			UE_LOG(LogSatisDash, Display, TEXT("%s"), *Line);
			if (ShouldPostCode(PlayersNow, true, false))
			{
				PendingChat = Line;
			}
		}
		else if (Reply.Code == 400)
		{
			if (bInFlightNamed)
			{
				bSendName = false;
				NextAt = Now;
			}
			else
			{
				NextAt = Now + PairingRefusedRetrySeconds;
				if (!bRefusedLogged)
				{
					bRefusedLogged = true;
					UE_LOG(LogSatisDash, Warning, TEXT("The backend refused the pairing request (400); retrying in an hour"));
				}
			}
		}
		else if (Reply.Code == 429)
		{
			WaitRetryAfter(Reply, Now);
		}
		else
		{
			if (Reply.Code == 201 && !bBadStartLogged)
			{
				bBadStartLogged = true;
				UE_LOG(LogSatisDash, Warning, TEXT("The backend's pairing answer was not in the expected form; retrying"));
			}
			WaitBackoff(Now);
		}
		return {};
	}

	const TOptional<FPairingPoll> Poll = ParsePairingPollResponse(Reply);
	if (!Poll)
	{
		if (Reply.Code == 400)
		{
			ClearCode();
			NextAt = Now;
		}
		else if (Reply.Code == 429)
		{
			WaitRetryAfter(Reply, Now);
		}
		else
		{
			WaitBackoff(Now);
		}
		return {};
	}
	Failures = 0;
	switch (Poll->Status)
	{
	case EPairingPoll::Approved:
		Stop();
		return Poll;
	case EPairingPoll::Expired:
		ClearCode();
		NextAt = Now;
		break;
	case EPairingPoll::SlowDown:
		IntervalSeconds += PairingSlowDownSeconds;
		NextAt = Now + IntervalSeconds;
		break;
	case EPairingPoll::Pending:
		NextAt = Now + IntervalSeconds;
		break;
	}
	return {};
}

void SatisDash::FPairing::OnPlayers(int32 Players, double Now)
{
	if (Players < 0)
	{
		return;
	}
	const bool bJoined = Players == 1 && LastPlayers == 0;
	LastPlayers = Players;
	PlayersNow = Players;
	if (!bJoined || bStopped)
	{
		return;
	}
	if (HasOpenCode(Now))
	{
		PendingChat = PairingCodeLine(LastConfig, UserCode, static_cast<int32>(CodeExpiresAt - Now));
	}
	else if (!IsWindowOpen(Now))
	{
		Open(Now);
	}
}

TOptional<FString> SatisDash::FPairing::TakeChatLine()
{
	TOptional<FString> Out = MoveTemp(PendingChat);
	PendingChat.Reset();
	return Out;
}
