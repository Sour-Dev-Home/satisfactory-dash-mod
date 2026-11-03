#include "SatisDash/FrmPlayers.h"

#include "SatisDash/JsonTokens.h"
#include "SatisDash/Snapshot.h"

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
