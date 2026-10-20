#include "SatisDash/Credential.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "SatisDash/Config.h"
#include "SatisDash/Files.h"
#include "SatisDash/JsonUtil.h"

bool SatisDash::IsAgentSecret(const FString& Secret)
{
	if (Secret.IsEmpty() || Secret.Len() > 256)
	{
		return false;
	}
	for (const TCHAR C : Secret)
	{
		const bool bOk = (C >= TEXT('A') && C <= TEXT('Z')) || (C >= TEXT('a') && C <= TEXT('z')) ||
			(C >= TEXT('0') && C <= TEXT('9')) || C == TEXT('-') || C == TEXT('_');
		if (!bOk)
		{
			return false;
		}
	}
	return true;
}

bool SatisDash::IsServerId(const FString& Id)
{
	if (Id.IsEmpty() || Id.Len() > 32)
	{
		return false;
	}
	for (const TCHAR C : Id)
	{
		if (!((C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('0') && C <= TEXT('9')) || C == TEXT('-')))
		{
			return false;
		}
	}
	return true;
}

TOptional<SatisDash::FCredential> SatisDash::ParseCredential(const FString& Text)
{
	const TSharedPtr<FJsonObject> Object = SatisDash::Json::ParseObject(Text);
	const TSharedPtr<FJsonValue> Secret = SatisDash::Json::Field(Object, TEXT("agentSecret"), EJson::String);
	const TSharedPtr<FJsonValue> ServerId = SatisDash::Json::Field(Object, TEXT("serverId"), EJson::String);
	const TSharedPtr<FJsonValue> Url = SatisDash::Json::Field(Object, TEXT("backendUrl"), EJson::String);
	if (!Secret.IsValid() || !ServerId.IsValid() || !Url.IsValid() || !IsAgentSecret(Secret->AsString()) || !IsServerId(ServerId->AsString()))
	{
		return {};
	}
	const FString BackendUrl = NormalizeBackendUrl(Url->AsString());
	if (!IsAllowedBackendUrl(BackendUrl))
	{
		return {}; // no known issuer: fail closed, like any other malformed credential file
	}
	FCredential Credential;
	Credential.AgentSecret = Secret->AsString();
	Credential.ServerId = ServerId->AsString();
	Credential.BackendUrl = BackendUrl;
	return Credential;
}

FString SatisDash::CredentialToJson(const FCredential& Credential)
{
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("agentSecret"), Credential.AgentSecret);
	Object->SetStringField(TEXT("serverId"), Credential.ServerId);
	Object->SetStringField(TEXT("backendUrl"), Credential.BackendUrl);
	return SatisDash::Json::WritePretty(Object);
}

SatisDash::FCredentialStore::FCredentialStore(FString InPath)
	: Path{MoveTemp(InPath)}
{
}

TOptional<SatisDash::FCredential> SatisDash::FCredentialStore::Load() const
{
	IFileManager& Files = IFileManager::Get();
	const FString Temp = Path + TEXT(".tmp");
	FString Text;
	if (!Files.FileExists(*Path) && Files.FileExists(*Temp) && FFileHelper::LoadFileToString(Text, *Temp) && ParseCredential(Text).IsSet())
	{
		// One attempt, no retry sleeps (see WriteFileAtomic). A failed recovery leaves the valid temp file for the next Load.
		Files.Move(*Path, *Temp, true, false, false, true);
	}
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		return {};
	}
	return ParseCredential(Text);
}

bool SatisDash::FCredentialStore::Save(const FCredential& Credential) const
{
	return WriteFileAtomic(Path, CredentialToJson(Credential));
}

bool SatisDash::FCredentialStore::Delete() const
{
	IFileManager& Files = IFileManager::Get();
	const FString Temp = Path + TEXT(".tmp");
	Files.Delete(*Path, false, false, true);
	Files.Delete(*Temp, false, false, true);
	return !Files.FileExists(*Path) && !Files.FileExists(*Temp);
}

bool SatisDash::FCredentialStore::Exists() const
{
	return IFileManager::Get().FileExists(*Path);
}
