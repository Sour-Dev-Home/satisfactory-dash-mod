#pragma once

#include "CoreMinimal.h"

namespace SatisDash
{
	inline constexpr int32 MaxPlayers = 256;

	/** ADR-0029: a name and whether the player is online. Nothing else is read. */
	struct FPlayerReading
	{
		FString Name;
		bool bOnline = false;
	};

	/** AgentPlayersSchema. */
	struct FPlayersPart
	{
		bool bAvailable = false;
		TArray<FPlayerReading> Players;
	};

	/** The part when no source has players: {available: false, players: []} (the dash's players mapper treats this shape as "no source", playersMapper.ts). */
	SATISDASHCORE_API FPlayersPart PlayersUnavailable();
	SATISDASHCORE_API FString PlayersToJson(const FPlayersPart& Part);
}
