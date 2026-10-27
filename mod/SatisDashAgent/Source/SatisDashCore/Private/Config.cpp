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

	/** Reads an optional "bridge" / "native" key. False when the key is there and is anything else. */
	bool ConfigReadSource(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, TOptional<SatisDash::ESource>& Out)
	{
		if (!Object->HasField(Key))
		{
			return true;
		}
		const TSharedPtr<FJsonValue> Value = SatisDash::Json::Field(Object, Key, EJson::String);
		if (!Value.IsValid())
		{
			return false;
		}
		const FString Text = Value->AsString();
		if (Text.Equals(TEXT("bridge"), ESearchCase::CaseSensitive))
		{
			Out = SatisDash::ESource::Bridge;
			return true;
		}
		if (Text.Equals(TEXT("native"), ESearchCase::CaseSensitive))
		{
			Out = SatisDash::ESource::Native;
			return true;
		}
		return false;
	}

	const TCHAR* ConfigSourceName(SatisDash::ESource Source)
	{
		return Source == SatisDash::ESource::Native ? TEXT("native") : TEXT("bridge");
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

bool SatisDash::IsFrmPort(double Value)
{
	return FMath::IsFinite(Value) && Value >= 1.0 && Value <= 65535.0 && double(int32(Value)) == Value;
}

bool SatisDash::IsHeaderSafe(const FString& Value)
{
	if (Value.Len() > 512)
	{
		return false;
	}
	for (const TCHAR C : Value)
	{
		if (C < TEXT(' ') || C == 127)
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
	TOptional<int32> FrmPort;
	if (Object->HasField(TEXT("frmPort")))
	{
		const TSharedPtr<FJsonValue> PortValue = SatisDash::Json::Field(Object, TEXT("frmPort"), EJson::Number);
		if (!PortValue.IsValid() || !IsFrmPort(PortValue->AsNumber()))
		{
			Result.BadField = TEXT("frmPort");
			return Result;
		}
		FrmPort = int32(PortValue->AsNumber());
	}
	FString FrmToken;
	if (Object->HasField(TEXT("frmToken")))
	{
		const TSharedPtr<FJsonValue> TokenValue = SatisDash::Json::Field(Object, TEXT("frmToken"), EJson::String);
		FrmToken = TokenValue.IsValid() ? TokenValue->AsString().TrimStartAndEnd() : FString();
		if (!TokenValue.IsValid() || !IsHeaderSafe(FrmToken))
		{
			Result.BadField = TEXT("frmToken");
			return Result;
		}
	}
	TOptional<ESource> PlayersSource;
	if (!ConfigReadSource(Object, TEXT("playersSource"), PlayersSource))
	{
		Result.BadField = TEXT("playersSource");
		return Result;
	}
	TOptional<ESource> ResourceNodesSource;
	if (!ConfigReadSource(Object, TEXT("resourceNodesSource"), ResourceNodesSource))
	{
		Result.BadField = TEXT("resourceNodesSource");
		return Result;
	}
	TOptional<ESource> FactorySource;
	if (!ConfigReadSource(Object, TEXT("factorySource"), FactorySource))
	{
		Result.BadField = TEXT("factorySource");
		return Result;
	}
	TOptional<ESource> TrainsSource;
	if (!ConfigReadSource(Object, TEXT("trainsSource"), TrainsSource))
	{
		Result.BadField = TEXT("trainsSource");
		return Result;
	}
	TOptional<ESource> RailsSource;
	if (!ConfigReadSource(Object, TEXT("railsSource"), RailsSource))
	{
		Result.BadField = TEXT("railsSource");
		return Result;
	}
	TOptional<ESource> ConveyorsSource;
	if (!ConfigReadSource(Object, TEXT("conveyorsSource"), ConveyorsSource))
	{
		Result.BadField = TEXT("conveyorsSource");
		return Result;
	}
	Result.Status = EConfigStatus::Valid;
	Result.Config.BackendUrl = Url;
	Result.Config.EnrollmentCode = Code;
	Result.Config.FrmPort = FrmPort;
	Result.Config.FrmToken = FrmToken;
	Result.Config.PlayersSource = PlayersSource;
	Result.Config.ResourceNodesSource = ResourceNodesSource;
	Result.Config.FactorySource = FactorySource;
	Result.Config.TrainsSource = TrainsSource;
	Result.Config.RailsSource = RailsSource;
	Result.Config.ConveyorsSource = ConveyorsSource;
	return Result;
}

FString SatisDash::ConfigToJson(const FAgentConfig& Config)
{
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("backendUrl"), Config.BackendUrl);
	Object->SetStringField(TEXT("enrollmentCode"), Config.EnrollmentCode);
	if (Config.FrmPort.IsSet())
	{
		Object->SetNumberField(TEXT("frmPort"), Config.FrmPort.GetValue());
	}
	if (!Config.FrmToken.IsEmpty())
	{
		Object->SetStringField(TEXT("frmToken"), Config.FrmToken);
	}
	if (Config.PlayersSource.IsSet())
	{
		Object->SetStringField(TEXT("playersSource"), ConfigSourceName(Config.PlayersSource.GetValue()));
	}
	if (Config.ResourceNodesSource.IsSet())
	{
		Object->SetStringField(TEXT("resourceNodesSource"), ConfigSourceName(Config.ResourceNodesSource.GetValue()));
	}
	if (Config.FactorySource.IsSet())
	{
		Object->SetStringField(TEXT("factorySource"), ConfigSourceName(Config.FactorySource.GetValue()));
	}
	if (Config.TrainsSource.IsSet())
	{
		Object->SetStringField(TEXT("trainsSource"), ConfigSourceName(Config.TrainsSource.GetValue()));
	}
	if (Config.RailsSource.IsSet())
	{
		Object->SetStringField(TEXT("railsSource"), ConfigSourceName(Config.RailsSource.GetValue()));
	}
	if (Config.ConveyorsSource.IsSet())
	{
		Object->SetStringField(TEXT("conveyorsSource"), ConfigSourceName(Config.ConveyorsSource.GetValue()));
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
