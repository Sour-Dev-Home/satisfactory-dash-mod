#include "SatisDash/JsonTokens.h"

#include "SatisDash/JsonUtil.h"

SatisDash::FTokenReader::FTokenReader(const TArray<uint8>& Bytes)
	: Reader{ TJsonReaderFactory<UTF8CHAR>::CreateFromView(TStringView<UTF8CHAR>(reinterpret_cast<const UTF8CHAR*>(Bytes.GetData()), Bytes.Num())) }
{
}

bool SatisDash::FTokenReader::Next(EJsonNotation& Out)
{
	if (bFailed)
	{
		return false;
	}
	// ReadNext: true with Error on bad input; false at a clean end, or after an error (JsonReader.h:57-152).
	if (!Reader->ReadNext(Out))
	{
		bFailed = !Reader->GetErrorMessage().IsEmpty();
		return false;
	}
	if (Out == EJsonNotation::Error)
	{
		bFailed = true;
		return false;
	}
	if (Out == EJsonNotation::ObjectStart || Out == EJsonNotation::ArrayStart)
	{
		if (++Level > SatisDash::Json::MaxDepth)
		{
			bFailed = true;
			return false;
		}
	}
	else if (Out == EJsonNotation::ObjectEnd || Out == EJsonNotation::ArrayEnd)
	{
		--Level;
	}
	return true;
}

bool SatisDash::FTokenReader::SkipValue(EJsonNotation Started)
{
	if (Started != EJsonNotation::ObjectStart && Started != EJsonNotation::ArrayStart)
	{
		return true;
	}
	// Our own walk, not TJsonReader::SkipObject: the depth bound holds inside skipped values too.
	const int32 Until = Level - 1;
	EJsonNotation N;
	while (Level > Until)
	{
		if (!Next(N))
		{
			return false;
		}
	}
	return true;
}

bool SatisDash::FTokenReader::AtCleanEnd()
{
	EJsonNotation N;
	return !Next(N) && !bFailed;
}

bool SatisDash::IsNonNegativeNoise(double Value)
{
	return FMath::IsFinite(Value) && Value >= -0.001;
}
