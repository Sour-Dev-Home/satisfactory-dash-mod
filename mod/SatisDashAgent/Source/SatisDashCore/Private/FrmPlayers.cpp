#include "SatisDash/FrmPlayers.h"

#include "Policies/CondensedJsonPrintPolicy.h"
#include "SatisDash/JsonTokens.h"
#include "SatisDash/Snapshot.h"
#include "Serialization/JsonWriter.h"

TOptional<SatisDash::FPlayersPart> SatisDash::ParsePlayers(const TArray<uint8>& Body)
{
	FTokenReader Reader{ Body };
	FPlayersPart Part;
	Part.bAvailable = true;
	EJsonNotation N;
	if (!Reader.Next(N) || N != EJsonNotation::ArrayStart)
	{
		return {};
	}
	while (true)
	{
		if (!Reader.Next(N))
		{
			return {};
		}
		if (N == EJsonNotation::ArrayEnd)
		{
			break;
		}
		if (N != EJsonNotation::ObjectStart)
		{
			return {};
		}
		TOptional<FString> Name;
		TOptional<bool> bOnline;
		while (true)
		{
			if (!Reader.Next(N))
			{
				return {};
			}
			if (N == EJsonNotation::ObjectEnd)
			{
				break;
			}
			const FString& Key = Reader.Name();
			if (Key.Equals(TEXT("Name"), ESearchCase::CaseSensitive))
			{
				if (N != EJsonNotation::String)
				{
					return {};
				}
				Name = Reader.String();
			}
			else if (Key.Equals(TEXT("Online"), ESearchCase::CaseSensitive))
			{
				if (N != EJsonNotation::Boolean)
				{
					return {};
				}
				bOnline = Reader.Boolean();
			}
			else if (!Reader.SkipValue(N))
			{
				return {};
			}
		}
		if (!Name.IsSet() || !bOnline.IsSet())
		{
			return {};
		}
		if (Part.Players.Num() < MaxPlayers)
		{
			Part.Players.Add({ ClampString(Name.GetValue()), bOnline.GetValue() });
		}
	}
	if (!Reader.AtCleanEnd())
	{
		return {};
	}
	return Part;
}

SatisDash::FPlayersPart SatisDash::PlayersUnavailable()
{
	return FPlayersPart();
}

FString SatisDash::PlayersToJson(const FPlayersPart& Part)
{
	FString Out;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("available"), Part.bAvailable);
	Writer->WriteArrayStart(TEXT("players"));
	for (const FPlayerReading& Player : Part.Players)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("name"), Player.Name);
		Writer->WriteValue(TEXT("online"), Player.bOnline);
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();
	Writer->WriteObjectEnd();
	Writer->Close();
	return Out;
}
