#include "SatisDash/JsonUtil.h"

#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

bool SatisDash::Json::WithinDepth(const FString& Text, int32 Max)
{
	int32 Depth = 0;
	bool bInString = false;
	bool bEscaped = false;
	for (const TCHAR C : Text)
	{
		if (bInString)
		{
			if (bEscaped)
			{
				bEscaped = false;
			}
			else if (C == TEXT('\\'))
			{
				bEscaped = true;
			}
			else if (C == TEXT('"'))
			{
				bInString = false;
			}
		}
		else if (C == TEXT('"'))
		{
			bInString = true;
		}
		else if (C == TEXT('[') || C == TEXT('{'))
		{
			if (++Depth > Max)
			{
				return false;
			}
		}
		else if ((C == TEXT(']') || C == TEXT('}')) && Depth > 0)
		{
			// Never below zero: a stray close is malformed anyway, and must not buy room for deeper nesting after it.
			--Depth;
		}
	}
	return true;
}

TSharedPtr<FJsonObject> SatisDash::Json::ParseObject(const FString& Text)
{
	// Every parse (response bodies, the config and credential files) comes through here. Too deep is refused exactly
	// like malformed JSON: the same null, so the same refusal path and logs, and nothing that echoes the text.
	if (!WithinDepth(Text))
	{
		return nullptr;
	}
	TSharedPtr<FJsonObject> Object;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
	{
		return nullptr;
	}
	return Object;
}

TSharedPtr<FJsonValue> SatisDash::Json::Field(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, EJson Type)
{
	if (!Object.IsValid())
	{
		return nullptr;
	}
	const TSharedPtr<FJsonValue> Value = Object->TryGetField(Name);
	return Value.IsValid() && Value->Type == Type ? Value : nullptr;
}

FString SatisDash::Json::Write(const TSharedRef<FJsonObject>& Object)
{
	FString Out;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Object, Writer);
	return Out;
}

FString SatisDash::Json::WritePretty(const TSharedRef<FJsonObject>& Object)
{
	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Object, Writer);
	return Out;
}
