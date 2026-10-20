#include "SatisDash/Commands.h"

#include "Dom/JsonValue.h"
#include "SatisDash/JsonUtil.h"

SatisDash::FCommandResult SatisDash::CommandOk()
{
	FCommandResult Result;
	Result.bOk = true;
	return Result;
}

SatisDash::FCommandResult SatisDash::CommandFailed(const TCHAR* Code)
{
	static const TCHAR* Known[] = { TEXT("unsupported"), TEXT("upstream_unreachable"), TEXT("upstream_auth_rejected"), TEXT("upstream_error") };
	FCommandResult Result;
	Result.Code = TEXT("upstream_error");
	for (const TCHAR* K : Known)
	{
		if (FCString::Strcmp(Code, K) == 0)
		{
			Result.Code = K;
		}
	}
	return Result;
}

FString SatisDash::CommandResultToJson(const FCommandResult& Result)
{
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetBoolField(TEXT("ok"), Result.bOk);
	if (!Result.bOk)
	{
		Object->SetStringField(TEXT("code"), Result.Code);
	}
	return SatisDash::Json::Write(Object);
}

SatisDash::FCommandLedger::EVerdict SatisDash::FCommandLedger::Begin(const FString& Id, const FDateTime& ExpiresAt, const FDateTime& Now, FCommandResult& OutDone)
{
	Entries.RemoveAll([&Now](const FEntry& Entry) { return Entry.ExpiresAt <= Now; });
	for (const FEntry& Entry : Entries)
	{
		if (Entry.Id.Equals(Id, ESearchCase::CaseSensitive))
		{
			if (!Entry.Result.IsSet())
			{
				return EVerdict::Running;
			}
			OutDone = Entry.Result.GetValue();
			return EVerdict::Done;
		}
	}
	Entries.Add({ Id, ExpiresAt, {} });
	if (Entries.Num() > MaxEntries)
	{
		Entries.RemoveAt(0, Entries.Num() - MaxEntries);
	}
	return EVerdict::New;
}

void SatisDash::FCommandLedger::Finish(const FString& Id, const FCommandResult& Result)
{
	for (FEntry& Entry : Entries)
	{
		if (Entry.Id.Equals(Id, ESearchCase::CaseSensitive))
		{
			Entry.Result = Result;
		}
	}
}

namespace
{
	/** SetAutoPauseParamsSchema is strict: exactly one field, "enabled", a boolean. */
	TOptional<bool> AutoPauseEnabled(const TSharedPtr<FJsonObject>& Params)
	{
		if (!Params.IsValid() || Params->Values.Num() != 1)
		{
			return {};
		}
		const TSharedPtr<FJsonValue> Enabled = SatisDash::Json::Field(Params, TEXT("enabled"), EJson::Boolean);
		return Enabled.IsValid() ? TOptional<bool>(Enabled->AsBool()) : TOptional<bool>();
	}
}

SatisDash::FCommandPlan SatisDash::PlanCommand(const FAgentCommand& Command, FCommandLedger& Ledger, const FDateTime& Now)
{
	FCommandPlan Plan;
	const FTimespan Left = Command.ExpiresAt - Now;
	if (Left <= FTimespan::Zero() || Left.GetTotalSeconds() > MaxCommandLifetimeSeconds)
	{
		return Plan;
	}
	FCommandResult Done;
	switch (Ledger.Begin(Command.Id, Command.ExpiresAt, Now, Done))
	{
	case FCommandLedger::EVerdict::Running:
		return Plan;
	case FCommandLedger::EVerdict::Done:
		Plan.Action = ECommandAction::Report;
		Plan.Result = Done;
		return Plan;
	case FCommandLedger::EVerdict::New:
		break;
	}
	const TOptional<bool> Enabled = Command.Type.Equals(TEXT("set_auto_pause"), ESearchCase::CaseSensitive)
		? AutoPauseEnabled(Command.Params) : TOptional<bool>();
	if (!Enabled.IsSet())
	{
		Plan.Action = ECommandAction::Report;
		Plan.Result = CommandFailed(TEXT("unsupported"));
		Ledger.Finish(Command.Id, Plan.Result);
		return Plan;
	}
	Plan.Action = ECommandAction::SetAutoPause;
	Plan.bEnabled = Enabled.GetValue();
	return Plan;
}

bool SatisDash::ShouldRetryResult(int32 AttemptsMade, EReplyKind Kind, const FDateTime& ExpiresAt, const FDateTime& Now)
{
	return AttemptsMade < ResultAttempts && Kind == EReplyKind::Transient && Now < ExpiresAt;
}
