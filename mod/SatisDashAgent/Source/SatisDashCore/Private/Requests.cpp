#include "SatisDash/Requests.h"

#include "Misc/Compression.h"
#include "SatisDash/JsonUtil.h"
#include "SatisDash/Version.h"

namespace
{
	SatisDash::FRequestSpec Start(const TCHAR* Verb, const FString& Url)
	{
		SatisDash::FRequestSpec Spec;
		Spec.Verb = Verb;
		Spec.Url = Url;
		Spec.TimeoutSeconds = SatisDash::RequestTimeoutSeconds;
		Spec.Headers.Add({ TEXT("Accept"), TEXT("application/json") });
		// The Node agent sends its own version the same way. Nothing else about the machine (contract: agentVersion).
		Spec.Headers.Add({ TEXT("User-Agent"), TEXT("satisfactory-dash-") + SatisDash::AgentVersion() });
		return Spec;
	}

	void Authorize(SatisDash::FRequestSpec& Spec, const SatisDash::FCredential& Credential)
	{
		Spec.Headers.Add({ TEXT("Authorization"), TEXT("Bearer ") + Credential.AgentSecret });
	}

	void SetJsonBody(SatisDash::FRequestSpec& Spec, const FString& Json)
	{
		const FTCHARToUTF8 Utf8(*Json);
		TArray<uint8> Raw = TArray<uint8>(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		Spec.Headers.Add({ TEXT("Content-Type"), TEXT("application/json") });
		if (Raw.Num() > SatisDash::GzipOverBytes)
		{
			int32 Size = FCompression::CompressMemoryBound(NAME_Gzip, Raw.Num());
			TArray<uint8> Packed;
			Packed.SetNumUninitialized(Size);
			if (FCompression::CompressMemory(NAME_Gzip, Packed.GetData(), Size, Raw.GetData(), Raw.Num()))
			{
				Packed.SetNum(Size);
				Spec.Headers.Add({ TEXT("Content-Encoding"), TEXT("gzip") });
				Spec.Body = MoveTemp(Packed);
				return;
			}
		}
		Spec.Body = MoveTemp(Raw); // compression failing is not a reason to lose the snapshot
	}
}

FString SatisDash::PercentEncode(const FString& Value)
{
	const FTCHARToUTF8 Utf8(*Value);
	FString Out;
	for (int32 I = 0; I < Utf8.Length(); ++I)
	{
		const uint8 B = static_cast<uint8>(Utf8.Get()[I]);
		const bool bUnreserved = (B >= 'A' && B <= 'Z') || (B >= 'a' && B <= 'z') || (B >= '0' && B <= '9') ||
			B == '-' || B == '.' || B == '_' || B == '~';
		Out += bUnreserved ? FString::Chr(TCHAR(B)) : FString::Printf(TEXT("%%%02X"), B);
	}
	return Out;
}

SatisDash::FRequestSpec SatisDash::BuildEnrollRequest(const FAgentConfig& Config)
{
	FRequestSpec Spec = Start(TEXT("POST"), Config.BackendUrl + TEXT("/agent/v1/enroll"));
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("code"), Config.EnrollmentCode);
	Body->SetStringField(TEXT("agentVersion"), AgentVersion());
	SetJsonBody(Spec, SatisDash::Json::Write(Body));
	return Spec;
}

SatisDash::FRequestSpec SatisDash::BuildSnapshotRequest(const FAgentConfig& Config, const FCredential& Credential, const FString& SnapshotJson)
{
	FRequestSpec Spec = Start(TEXT("POST"), Config.BackendUrl + TEXT("/agent/v1/snapshots"));
	Authorize(Spec, Credential);
	SetJsonBody(Spec, SnapshotJson);
	return Spec;
}

SatisDash::FRequestSpec SatisDash::BuildCommandsPoll(const FAgentConfig& Config, const FCredential& Credential)
{
	FRequestSpec Spec = Start(TEXT("GET"), Config.BackendUrl + FString::Printf(TEXT("/agent/v1/commands?waitSeconds=%d"), PollWaitSeconds));
	Spec.TimeoutSeconds = PollWaitSeconds + LongPollGraceSeconds;
	Authorize(Spec, Credential);
	return Spec;
}

SatisDash::FRequestSpec SatisDash::BuildResultRequest(const FAgentConfig& Config, const FCredential& Credential, const FString& CommandId, const FCommandResult& Result)
{
	FRequestSpec Spec = Start(TEXT("POST"), Config.BackendUrl + TEXT("/agent/v1/commands/") + PercentEncode(CommandId) + TEXT("/result"));
	Authorize(Spec, Credential);
	SetJsonBody(Spec, CommandResultToJson(Result));
	return Spec;
}
