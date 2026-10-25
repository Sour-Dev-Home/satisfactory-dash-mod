#include "SatisDash/FrmRecords.h"

SatisDash::FFrmArrayParse::FFrmArrayParse(TArray<uint8> InBody, int32 InMaxKept, FString InPrefix, FString InSuffix)
	: Body{MoveTemp(InBody)}
	, Reader{Body}
	, MaxKept{InMaxKept}
	, Prefix{MoveTemp(InPrefix)}
	, Suffix{MoveTemp(InSuffix)}
{
}

bool SatisDash::FFrmArrayParse::Is(const FString& Key, const TCHAR* Name)
{
	return Key.Equals(Name, ESearchCase::CaseSensitive);
}

void SatisDash::FFrmArrayParse::BeginRecord()
{
	if (KeptCount > 0)
	{
		Out.AppendChar(TEXT(','));
	}
}

SatisDash::FFrmArrayParse::EStep SatisDash::FFrmArrayParse::Fail()
{
	State = EStep::Invalid;
	Out.Empty();
	Body.Empty(); // the reader is not used again
	return State;
}

SatisDash::FFrmArrayParse::EStep SatisDash::FFrmArrayParse::Step(int32 MaxPerSlice)
{
	if (State != EStep::More)
	{
		return State;
	}
	EJsonNotation N;
	if (!bStarted)
	{
		bStarted = true;
		if (!Reader.Next(N) || N != EJsonNotation::ArrayStart)
		{
			return Fail();
		}
		Out = Prefix;
		Out.AppendChar(TEXT('['));
	}
	for (int32 Read = 0; Read < MaxPerSlice; ++Read)
	{
		if (!Reader.Next(N))
		{
			return Fail();
		}
		if (N == EJsonNotation::ArrayEnd)
		{
			if (!Reader.AtCleanEnd())
			{
				return Fail();
			}
			Out.AppendChar(TEXT(']'));
			ArrayEnd = Out.Len();
			Out += Suffix;
			Bytes = FTCHARToUTF8_Convert::ConvertedLength(*Out, Out.Len());
			Body.Empty();
			State = EStep::Done;
			return State;
		}
		if (N != EJsonNotation::ObjectStart)
		{
			return Fail();
		}
		switch (ReadRecord(KeptCount < MaxKept))
		{
		case ERecord::Invalid: return Fail();
		case ERecord::Dropped: ++DroppedCount; break;
		case ERecord::Over: ++OverCount; break;
		case ERecord::Kept: ++KeptCount; break;
		}
	}
	return EStep::More;
}

FString SatisDash::FFrmArrayParse::TakeJson()
{
	return State == EStep::Done ? MoveTemp(Out) : FString();
}
