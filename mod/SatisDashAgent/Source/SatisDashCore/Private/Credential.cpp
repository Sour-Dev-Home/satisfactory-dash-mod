#include "SatisDash/Credential.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
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
	if (!Secret.IsValid() || !ServerId.IsValid() || !IsAgentSecret(Secret->AsString()) || !IsServerId(ServerId->AsString()))
	{
		return {};
	}
	FCredential Credential;
	Credential.AgentSecret = Secret->AsString();
	Credential.ServerId = ServerId->AsString();
	return Credential;
}

FString SatisDash::CredentialToJson(const FCredential& Credential)
{
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("agentSecret"), Credential.AgentSecret);
	Object->SetStringField(TEXT("serverId"), Credential.ServerId);
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
		Files.Move(*Path, *Temp, true);
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
