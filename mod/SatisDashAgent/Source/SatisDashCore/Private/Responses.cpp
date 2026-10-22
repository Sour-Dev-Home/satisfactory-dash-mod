#include "SatisDash/Responses.h"

#include "Dom/JsonValue.h"
#include "SatisDash/JsonUtil.h"

namespace
{
	/**
	 * Security review F4: ".", ".." and so on would be dot segments in the result URL, which libcurl removes, so the
	 * result would go to another path. A dot inside a real ID stays allowed (Node parity: encodeURIComponent keeps it).
	 */
	bool isAllDots(const FString& Id)
	{
		for (const TCHAR C : Id)
		{
			if (C != TEXT('.'))
			{
				return false;
			}
		}
		return true;
	}
}

SatisDash::EReplyKind SatisDash::ClassifyReply(int32 Code, bool bAuthenticated)
{
	if (Code >= 200 && Code < 300)
	{
		return EReplyKind::Ok;
	}
	if (Code == 401 && bAuthenticated)
	{
		return EReplyKind::AuthRejected;
	}
	if (Code == 0 || Code == 429 || Code >= 500)
	{
		return EReplyKind::Transient;
	}
	return EReplyKind::Fatal;
}

TSharedPtr<FJsonObject> SatisDash::ReadJsonBody(const FHttpReply& Reply)
{
	if (Reply.Body.Num() > MaxResponseBytes)
	{
		return nullptr;
	}
	FString Type = Reply.ContentType;
	int32 Semicolon = INDEX_NONE;
	if (Type.FindChar(TEXT(';'), Semicolon))
	{
		Type.LeftInline(Semicolon);
	}
	if (!Type.TrimStartAndEnd().Equals(TEXT("application/json"), ESearchCase::IgnoreCase))
	{
		return nullptr;
	}
	const FUTF8ToTCHAR Utf8(reinterpret_cast<const ANSICHAR*>(Reply.Body.GetData()), Reply.Body.Num());
	return SatisDash::Json::ParseObject(FString::ConstructFromPtrSize(Utf8.Get(), Utf8.Length()));
}

TOptional<SatisDash::FCadence> SatisDash::ParseCadence(const TSharedPtr<FJsonObject>& Object)
{
	auto seconds = [&Object](const TCHAR* Name) -> TOptional<int32>
	{
		const TSharedPtr<FJsonValue> Value = SatisDash::Json::Field(Object, Name, EJson::Number);
		if (!Value.IsValid() || !FMath::IsFinite(Value->AsNumber()))
		{
			return {};
		}
		// Clamp as a double first: rounding 1e300 to an int would overflow.
		return FMath::RoundToInt(FMath::Clamp(Value->AsNumber(), double(MinCadenceSeconds), double(MaxCadenceSeconds)));
	};
	const TOptional<int32> Status = seconds(TEXT("statusSeconds"));
	const TOptional<int32> Power = seconds(TEXT("powerSeconds"));
	const TOptional<int32> Factory = seconds(TEXT("factorySeconds"));
	if (!Status.IsSet() || !Power.IsSet() || !Factory.IsSet())
	{
		return {};
	}
	FCadence Cadence;
	Cadence.StatusSeconds = Status.GetValue();
	Cadence.PowerSeconds = Power.GetValue();
	Cadence.FactorySeconds = Factory.GetValue();
	return Cadence;
}

TOptional<SatisDash::FEnrollResponse> SatisDash::ParseEnrollResponse(const FHttpReply& Reply)
{
	if (Reply.Code != 201)
	{
		return {};
	}
	const TSharedPtr<FJsonObject> Object = ReadJsonBody(Reply);
	const TSharedPtr<FJsonValue> Secret = SatisDash::Json::Field(Object, TEXT("agentSecret"), EJson::String);
	const TSharedPtr<FJsonValue> ServerId = SatisDash::Json::Field(Object, TEXT("serverId"), EJson::String);
	const TSharedPtr<FJsonValue> CadenceValue = SatisDash::Json::Field(Object, TEXT("cadence"), EJson::Object);
	if (!Secret.IsValid() || !ServerId.IsValid() || !CadenceValue.IsValid() ||
		!IsAgentSecret(Secret->AsString()) || !IsServerId(ServerId->AsString()))
	{
		return {};
	}
	const TOptional<FCadence> Cadence = ParseCadence(CadenceValue->AsObject());
	if (!Cadence.IsSet())
	{
		return {};
	}
	FEnrollResponse Out;
	Out.Credential.AgentSecret = Secret->AsString();
	Out.Credential.ServerId = ServerId->AsString();
	Out.Cadence = Cadence.GetValue();
	return Out;
}

TOptional<SatisDash::FSnapshotResponse> SatisDash::ParseSnapshotResponse(const FHttpReply& Reply)
{
	if (ClassifyReply(Reply.Code, true) != EReplyKind::Ok)
	{
		return {};
	}
	const TSharedPtr<FJsonObject> Object = ReadJsonBody(Reply);
	const TSharedPtr<FJsonValue> CadenceValue = SatisDash::Json::Field(Object, TEXT("cadence"), EJson::Object);
	const TSharedPtr<FJsonValue> Pending = SatisDash::Json::Field(Object, TEXT("commandsPending"), EJson::Boolean);
	if (!CadenceValue.IsValid() || !Pending.IsValid())
	{
		return {};
	}
	const TOptional<FCadence> Cadence = ParseCadence(CadenceValue->AsObject());
	if (!Cadence.IsSet())
	{
		return {};
	}
	FSnapshotResponse Out;
	Out.Cadence = Cadence.GetValue();
	Out.bCommandsPending = Pending->AsBool();
	return Out;
}

TOptional<TArray<SatisDash::FAgentCommand>> SatisDash::ParseCommandsResponse(const FHttpReply& Reply)
{
	if (ClassifyReply(Reply.Code, true) != EReplyKind::Ok)
	{
		return {};
	}
	const TSharedPtr<FJsonValue> List = SatisDash::Json::Field(ReadJsonBody(Reply), TEXT("commands"), EJson::Array);
	if (!List.IsValid())
	{
		return {};
	}
	TArray<FAgentCommand> Commands;
	for (const TSharedPtr<FJsonValue>& Item : List->AsArray())
	{
		const TSharedPtr<FJsonObject> Object = Item.IsValid() && Item->Type == EJson::Object ? Item->AsObject() : nullptr;
		const TSharedPtr<FJsonValue> Id = SatisDash::Json::Field(Object, TEXT("id"), EJson::String);
		const TSharedPtr<FJsonValue> Type = SatisDash::Json::Field(Object, TEXT("type"), EJson::String);
		const TSharedPtr<FJsonValue> Params = SatisDash::Json::Field(Object, TEXT("params"), EJson::Object);
		const TSharedPtr<FJsonValue> Expires = SatisDash::Json::Field(Object, TEXT("expiresAt"), EJson::String);
		FAgentCommand Command;
		if (!Id.IsValid() || !Type.IsValid() || !Params.IsValid() || !Expires.IsValid() ||
			!FDateTime::ParseIso8601(*Expires->AsString(), Command.ExpiresAt))
		{
			return {};
		}
		Command.Id = Id->AsString();
		if (Command.Id.IsEmpty() || Command.Id.Len() > MaxCommandIdLength || isAllDots(Command.Id))
		{
			return {};
		}
		Command.Type = Type->AsString();
		Command.Params = Params->AsObject();
		if (Commands.Num() < MaxCommandsPerPoll)
		{
			Commands.Add(MoveTemp(Command));
		}
	}
	return Commands;
}
