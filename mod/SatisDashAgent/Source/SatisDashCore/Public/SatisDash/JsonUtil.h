#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace SatisDash::Json
{
	/** The text as a JSON object, or null when it isn't valid JSON or isn't an object. */
	SATISDASHCORE_API TSharedPtr<FJsonObject> ParseObject(const FString& Text);
	/** The named field when it exists and has exactly this type (TryGetStringField would turn a number into text). */
	SATISDASHCORE_API TSharedPtr<FJsonValue> Field(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, EJson Type);
	SATISDASHCORE_API FString Write(const TSharedRef<FJsonObject>& Object);
	/** Indented, for the file an owner edits by hand. */
	SATISDASHCORE_API FString WritePretty(const TSharedRef<FJsonObject>& Object);
}
