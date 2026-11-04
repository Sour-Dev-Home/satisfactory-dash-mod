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

	/**
	 * R7: the keys the removed FRM role read, in the order the warning names them. Present in an old file with any value (a bad
	 * one included): never read, never a reason to refuse the file, and not written back.
	 */
	const TCHAR* const RemovedKeys[] = {
		TEXT("frmPort"), TEXT("frmToken"),
		TEXT("playersSource"), TEXT("resourceNodesSource"), TEXT("factorySource"), TEXT("trainsSource"), TEXT("railsSource"),
		TEXT("conveyorsSource"), TEXT("mapMarkersSource"), TEXT("droneRoutesSource"), TEXT("powerLinesSource"),
		TEXT("extractorsSource"), TEXT("generatorsSource"), TEXT("stationRatesSource"), TEXT("storageSource"),
	};
}

FString SatisDash::IgnoredKeysLine(const FConfigResult& Result)
{
	if (Result.IgnoredKeys.Num() == 0)
	{
		return FString();
	}
	FString Names;
	for (const FString& Key : Result.IgnoredKeys)
	{
		Names += Names.IsEmpty() ? Key : TEXT(", ") + Key;
	}
	return FString::Printf(TEXT("Config: ignored removed keys %s (FRM was removed in R7)"), *Names);
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

bool SatisDash::IsAllowedRequestUrl(const FString& Url, bool& OutPlainHttp)
{
	OutPlainHttp = false;
	int32 QueryAt = INDEX_NONE;
	const FString Base = Url.FindChar(TEXT('?'), QueryAt) ? Url.Left(QueryAt) : Url;
	for (const TCHAR C : Url.RightChop(Base.Len()))
	{
		if (C <= TEXT(' ') || C == 127 || C == TEXT('\\') || C == TEXT('#'))
		{
			return false;
		}
	}
	if (!IsAllowedBackendUrl(Base))
	{
		return false;
	}
	OutPlainHttp = !Base.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase);
	return true;
}

FString SatisDash::NormalizeBackendUrl(const FString& Url)
{
	FString Out = Url.TrimStartAndEnd();
	while (Out.EndsWith(TEXT("/")))
	{
		Out.LeftChopInline(1);
	}
	return Out;
}

bool SatisDash::IsEnrollmentCode(const FString& Code)
{
	// XXXX-XXXX-XXXX: 12 base32 characters, 60 bits (dash #524, ADR-0046). No allocation.
	if (Code.Len() != 14 || Code[4] != TEXT('-') || Code[9] != TEXT('-'))
	{
		return false;
	}
	for (int32 I = 0; I < 14; ++I)
	{
		if (I != 4 && I != 9 && !IsBase32(Code[I]))
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
	const FString Url = UrlValue.IsValid() ? NormalizeBackendUrl(UrlValue->AsString()) : FString();
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
	// R7: a removed key is only noted by name. Its value is never read, so no value can be refused or logged.
	TArray<FString> IgnoredKeys;
	for (const TCHAR* const Key : RemovedKeys)
	{
		if (Object->HasField(Key))
		{
			IgnoredKeys.Add(Key);
		}
	}
	bool bStepDiagnostics = false;
	if (Object->HasField(TEXT("stepDiagnostics")))
	{
		const TSharedPtr<FJsonValue> DiagnosticsValue = SatisDash::Json::Field(Object, TEXT("stepDiagnostics"), EJson::Boolean);
		if (!DiagnosticsValue.IsValid())
		{
			Result.BadField = TEXT("stepDiagnostics");
			return Result;
		}
		bStepDiagnostics = DiagnosticsValue->AsBool();
	}
	Result.Status = EConfigStatus::Valid;
	Result.Config.BackendUrl = Url;
	Result.Config.EnrollmentCode = Code;
	Result.Config.bStepDiagnostics = bStepDiagnostics;
	Result.IgnoredKeys = MoveTemp(IgnoredKeys);
	return Result;
}

FString SatisDash::ConfigToJson(const FAgentConfig& Config)
{
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("backendUrl"), Config.BackendUrl);
	Object->SetStringField(TEXT("enrollmentCode"), Config.EnrollmentCode);
	if (Config.bStepDiagnostics)
	{
		Object->SetBoolField(TEXT("stepDiagnostics"), true);
	}
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
