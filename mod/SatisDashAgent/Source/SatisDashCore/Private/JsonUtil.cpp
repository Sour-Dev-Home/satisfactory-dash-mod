#include "SatisDash/JsonUtil.h"

#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

TSharedPtr<FJsonObject> SatisDash::Json::ParseObject(const FString& Text)
{
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
