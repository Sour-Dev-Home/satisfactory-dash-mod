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

	/**
	 * getPlayer's 2xx body, as the Node agent reads it (rawSchemas.ts RawFrmPlayerSchema, conform.ts conformPlayers): an
	 * array of objects, each with a string Name and a boolean Online; every other field is skipped unread. Unset when the
	 * body is invalid (the Node agent's invalid_response: the part is left out). The first 256 are kept, names cut to 200.
	 */
	SATISDASHCORE_API TOptional<FPlayersPart> ParsePlayers(const TArray<uint8>& Body);
	/** FRM unreachable, timed out, or an HTTP error (playersMapper.ts isFrmAbsent): {available: false, players: []}. */
	SATISDASHCORE_API FPlayersPart PlayersUnavailable();
	SATISDASHCORE_API FString PlayersToJson(const FPlayersPart& Part);
}
