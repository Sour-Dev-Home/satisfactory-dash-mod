#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace SatisDash::Json
{
	/**
	 * The deepest [ and { nesting ParseObject hands to the engine's parser (security review F1). The contract's deepest
	 * legitimate shape is about 4 levels, so 32 is generous; anything deeper is hostile or broken, and is refused before
	 * the recursive parser (or the recursive teardown of its tree) can run out of stack on the game thread.
	 */
	inline constexpr int32 MaxDepth = 32;
	/** One cheap pass: false when [ and { outside strings nest deeper than Max. Escapes inside strings are honoured. */
	SATISDASHCORE_API bool WithinDepth(const FString& Text, int32 Max = MaxDepth);
	/** The text as a JSON object, or null when it isn't valid JSON, isn't an object or nests deeper than MaxDepth. */
	SATISDASHCORE_API TSharedPtr<FJsonObject> ParseObject(const FString& Text);
	/** The named field when it exists and has exactly this type (TryGetStringField would turn a number into text). */
	SATISDASHCORE_API TSharedPtr<FJsonValue> Field(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, EJson Type);
	SATISDASHCORE_API FString Write(const TSharedRef<FJsonObject>& Object);
	/** Indented, for the file an owner edits by hand. */
	SATISDASHCORE_API FString WritePretty(const TSharedRef<FJsonObject>& Object);
}
