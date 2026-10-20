#include "SatisDash/Config.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "SatisDash/Files.h"
#include "SatisDash/JsonUtil.h"

namespace
{
	bool IsBase32(TCHAR C)
	{
		return (C >= TEXT('A') && C <= TEXT('Z')) || (C >= TEXT('2') && C <= TEXT('7'));
	}
}

bool SatisDash::IsAllowedBackendUrl(const FString& Url)
{
	bool bHttps = false;
	FString Rest;
	if (Url.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase))
	{
		bHttps = true;
		Rest = Url.RightChop(8);
	}
	else if (Url.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase))
	{
		Rest = Url.RightChop(7);
	}
	else
	{
		return false;
	}
	for (const TCHAR C : Url)
	{
		if (C <= TEXT(' ') || C == 127 || C == TEXT('\\') || C == TEXT('?') || C == TEXT('#'))
		{
			return false;
		}
	}
	int32 Slash = INDEX_NONE;
	const FString Authority = Rest.FindChar(TEXT('/'), Slash) ? Rest.Left(Slash) : Rest;
	if (Authority.IsEmpty() || Authority.Contains(TEXT("@")))
	{
		return false;
	}
	FString Host = Authority;
	int32 Colon = INDEX_NONE;
	if (Authority.FindLastChar(TEXT(':'), Colon))
	{
		const FString Port = Authority.RightChop(Colon + 1);
		if (Port.IsEmpty() || Port.Len() > 5)
		{
			return false;
		}
		for (const TCHAR C : Port)
		{
			if (!FChar::IsDigit(C))
			{
				return false;
			}
		}
		const int32 Number = FCString::Atoi(*Port);
		if (Number < 1 || Number > 65535)
		{
			return false;
		}
		Host = Authority.Left(Colon);
	}
	if (Host.IsEmpty())
	{
		return false;
	}
	return bHttps || Host.Equals(TEXT("127.0.0.1"), ESearchCase::CaseSensitive) || Host.Equals(TEXT("localhost"), ESearchCase::IgnoreCase);
}

bool SatisDash::IsEnrollmentCode(const FString& Code)
{
	if (Code.Len() != 9 || Code[4] != TEXT('-'))
	{
		return false;
	}
	for (int32 I = 0; I < 9; ++I)
	{
		if (I != 4 && !IsBase32(Code[I]))
		{
			return false;
		}
	}
	return true;
}

SatisDash::FConfigResult SatisDash::ParseConfig(const FString& Text)
{
	FConfigResult Result;
	const TSharedPtr<FJsonObject> Object = SatisDash::Json::ParseObject(Text);
	if (!Object.IsValid())
	{
		Result.BadField = TEXT("json");
		return Result;
	}
	const TSharedPtr<FJsonValue> UrlValue = SatisDash::Json::Field(Object, TEXT("backendUrl"), EJson::String);
	FString Url = UrlValue.IsValid() ? UrlValue->AsString().TrimStartAndEnd() : FString();
	while (Url.EndsWith(TEXT("/")))
	{
		Url.LeftChopInline(1);
	}
	if (!IsAllowedBackendUrl(Url))
	{
		Result.BadField = TEXT("backendUrl");
		return Result;
	}
	FString Code;
	if (Object->HasField(TEXT("enrollmentCode")))
	{
		const TSharedPtr<FJsonValue> CodeValue = SatisDash::Json::Field(Object, TEXT("enrollmentCode"), EJson::String);
		Code = CodeValue.IsValid() ? CodeValue->AsString().TrimStartAndEnd().ToUpper() : FString();
		if (!CodeValue.IsValid() || (!Code.IsEmpty() && !IsEnrollmentCode(Code)))
		{
			Result.BadField = TEXT("enrollmentCode");
			return Result;
		}
	}
	Result.Status = EConfigStatus::Valid;
	Result.Config.BackendUrl = Url;
	Result.Config.EnrollmentCode = Code;
	return Result;
}

FString SatisDash::ConfigToJson(const FAgentConfig& Config)
{
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("backendUrl"), Config.BackendUrl);
	Object->SetStringField(TEXT("enrollmentCode"), Config.EnrollmentCode);
	return SatisDash::Json::WritePretty(Object);
}

SatisDash::FConfigResult SatisDash::LoadConfigFile(const FString& Path)
{
	if (!IFileManager::Get().FileExists(*Path))
	{
		FConfigResult Result;
		Result.Status = EConfigStatus::Missing;
		Result.Config.BackendUrl = DefaultBackendUrl;
		WriteFileAtomic(Path, ConfigToJson(Result.Config));
		return Result;
	}
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		FConfigResult Result;
		Result.BadField = TEXT("json");
		return Result;
	}
	return ParseConfig(Text);
}

bool SatisDash::BlankConfigCode(const FString& Path, const FAgentConfig& Config)
{
	FAgentConfig Blanked = Config;
	Blanked.EnrollmentCode.Reset();
	return WriteFileAtomic(Path, ConfigToJson(Blanked));
}

FDateTime SatisDash::ConfigModTime(const FString& Path)
{
	return IFileManager::Get().GetTimeStamp(*Path);
}
