#pragma once

#include "CoreMinimal.h"
#include "Serialization/JsonReader.h"

namespace SatisDash
{
	/**
	 * FRM bridge spec "Memory": a body read token by token, in place, with no FJsonValue tree. UTF-8 bytes go straight
	 * to TJsonReader<UTF8CHAR> (CreateFromView, no copy). Nesting deeper than Json::MaxDepth (32) is refused as it is
	 * met. The reader's GetValueAs* check() the token type (JsonReader.h:166-208): call String, Number or Boolean only
	 * right after Next returned that notation.
	 */
	class SATISDASHCORE_API FTokenReader
	{
	public:
		/** Bytes must outlive the reader, which reads them in place. */
		explicit FTokenReader(const TArray<uint8>& Bytes);

		/** The next notation; false at the end of input or on a refusal (then Failed()). */
		bool Next(EJsonNotation& Out);
		/** After Next returned Started: skips that value. An object or array is read to its end; a scalar needs nothing. */
		bool SkipValue(EJsonNotation Started);
		/** The current value's name inside an object. */
		const FString& Name() const { return Reader->GetIdentifier(); }
		FString String() const { return Reader->GetValueAsString(); }
		double Number() const { return Reader->GetValueAsNumber(); }
		bool Boolean() const { return Reader->GetValueAsBoolean(); }
		bool Failed() const { return bFailed; }
		/** True when the input ends cleanly here (after the root value). */
		bool AtCleanEnd();

	private:
		TSharedRef<TJsonReader<UTF8CHAR>> Reader;
		int32 Level = 0;
		bool bFailed = false;
	};

	/** rawSchemas.ts nonNegative: a finite number no lower than -0.001 (float noise, then raised to 0). */
	SATISDASHCORE_API bool IsNonNegativeNoise(double Value);
}
