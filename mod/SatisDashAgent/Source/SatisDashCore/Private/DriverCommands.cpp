#include "SatisDash/Driver.h"

#include "SatisDash/Log.h"

void SatisDash::FDriver::StopCommands()
{
	// The ledger stays: an ID that ran never runs again, even after a halt and a new enrolment (S6).
	Waiting.Reset();
	if (!bReportInFlight)
	{
		Report.Reset();
	}
}

void SatisDash::FDriver::TickCommands(double Now)
{
	// One thing at a time, as commandRunner.ts does: a poll, then each command and its report in turn. Nothing here
	// allocates until something is due. A poll or result still out from an older generation holds back the next one
	// until it answers or times out (25 s + 10 s for a poll, 15 s for a result): accepted (task 5 review). Never allow two
	// in flight to shorten it.
	if (bPollInFlight || bReportInFlight)
	{
		return;
	}
	if (Report.IsSet())
	{
		if (Now >= Report->NextAt)
		{
			SendReport();
		}
		return;
	}
	if (Waiting.Num() > 0)
	{
		RunNext(Now);
		return;
	}
	if (Now >= NextPollAt)
	{
		SendPoll(Now);
	}
}

void SatisDash::FDriver::SendPoll(double Now)
{
	// Agent.h: fetched again for every authenticated request; no poll without a credential for the backend named now.
	const FCredential* Credential = Agent.GetCredential();
	if (Credential == nullptr)
	{
		return;
	}
	bPollInFlight = true;
	PollStartedAt = Now;
	const uint32 SentIn = Generation;
	Transport.Send(BuildCommandsPoll(Agent.GetConfig(), *Credential), [this, SentIn](const FHttpReply& Reply) { FinishPoll(Reply, SentIn); });
}

void SatisDash::FDriver::FinishPoll(const FHttpReply& Reply, uint32 SentIn)
{
	bPollInFlight = false;
	if (SentIn != Generation || Agent.GetCredential() == nullptr)
	{
		// Polled with an older credential or backend, or halted since: its 401 is about that one, and what it brought
		// is not ours to run or to record in the ledger.
		return;
	}
	const double Now = Clock.Now();
	const EReplyKind Kind = ClassifyReply(Reply.Code, true);
	if (Kind == EReplyKind::AuthRejected)
	{
		++Generation;
		Agent.OnAuthRejected();
		return;
	}
	TOptional<TArray<FAgentCommand>> Commands;
	if (Kind == EReplyKind::Ok)
	{
		Commands = ParseCommandsResponse(Reply);
	}
	if (!Commands.IsSet())
	{
		if (PollFailures == 0)
		{
			UE_LOG(LogSatisDash, Warning, TEXT("Polling for commands failed (%d); retrying with backoff"), Reply.Code);
		}
		NextPollAt = Now + FMath::Max(ParseRetryAfterSeconds(Reply.RetryAfter).Get(0.0), BackoffSeconds(PollFailures, Clock.Random01()));
		++PollFailures;
		return;
	}
	if (PollFailures > 0)
	{
		UE_LOG(LogSatisDash, Display, TEXT("Polling for commands recovered after %d attempts"), PollFailures);
		PollFailures = 0;
	}
	Waiting = MoveTemp(Commands.GetValue());
	NextPollAt = PollStartedAt + MinPollSpacingSeconds;
}

void SatisDash::FDriver::RunNext(double Now)
{
	const FAgentCommand Command = MoveTemp(Waiting[0]);
	Waiting.RemoveAt(0);
	const FCommandPlan Plan = PlanCommand(Command, Ledger, Clock.UtcNow());
	FCommandResult Result = Plan.Result;
	switch (Plan.Action)
	{
	case ECommandAction::Skip:
		if (SkipLog.ShouldLog(TEXT("skipped"), Now))
		{
			UE_LOG(LogSatisDash, Warning, TEXT("Skipped a command that has expired, expires too far ahead or is already running"));
		}
		return;
	case ECommandAction::Report:
		break;
	case ECommandAction::SetAutoPause:
		if (Game.ApplyAutoPause(Plan.bEnabled))
		{
			Result = CommandOk();
			UE_LOG(LogSatisDash, Display, TEXT("Applied set_auto_pause from the dashboard: auto-pause is now %s"), Plan.bEnabled ? TEXT("on") : TEXT("off"));
		}
		else
		{
			// S10: the read-back didn't agree, so nothing is claimed; the ledger still records the ID.
			Result = CommandFailed(TEXT("upstream_error"));
			UE_LOG(LogSatisDash, Warning, TEXT("Couldn't apply set_auto_pause; reporting upstream_error"));
		}
		Ledger.Finish(Command.Id, Result);
		break;
	}
	FReport Next;
	Next.Id = Command.Id;
	Next.Result = Result;
	Next.ExpiresAt = Command.ExpiresAt;
	Next.NextAt = Now;
	Report = MoveTemp(Next);
}

void SatisDash::FDriver::SendReport()
{
	const FCredential* Credential = Agent.GetCredential();
	if (Credential == nullptr)
	{
		Report.Reset();
		return;
	}
	bReportInFlight = true;
	++Report->Attempts;
	const uint32 SentIn = Generation;
	Transport.Send(BuildResultRequest(Agent.GetConfig(), *Credential, Report->Id, Report->Result), [this, SentIn](const FHttpReply& Reply) { FinishReport(Reply, SentIn); });
}

void SatisDash::FDriver::FinishReport(const FHttpReply& Reply, uint32 SentIn)
{
	bReportInFlight = false;
	if (!Report.IsSet() || SentIn != Generation || Agent.GetCredential() == nullptr)
	{
		// Reported with an older credential or backend, or halted since: its 401 is about that one. The result is
		// dropped; it belongs to the backend that handed the command out, not to the one held now.
		Report.Reset();
		return;
	}
	const EReplyKind Kind = ClassifyReply(Reply.Code, true);
	if (Kind == EReplyKind::AuthRejected)
	{
		Report.Reset();
		++Generation;
		Agent.OnAuthRejected();
		return;
	}
	if (Kind != EReplyKind::Transient)
	{
		Report.Reset(); // taken, or refused for good (commandRunner.ts: a fatal answer isn't retried)
		return;
	}
	const FDateTime UtcNow = Clock.UtcNow();
	if (ShouldRetryResult(Report->Attempts, Kind, Report->ExpiresAt, UtcNow))
	{
		Report->NextAt = Clock.Now() + FMath::Max(ParseRetryAfterSeconds(Reply.RetryAfter).Get(0.0), BackoffSeconds(Report->Attempts - 1, Clock.Random01()));
		return;
	}
	if (UtcNow < Report->ExpiresAt)
	{
		UE_LOG(LogSatisDash, Warning, TEXT("Couldn't report a command's result after %d attempts"), Report->Attempts);
	}
	Report.Reset();
}
