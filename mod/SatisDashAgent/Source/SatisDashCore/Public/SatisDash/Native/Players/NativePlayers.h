#pragma once

#include "CoreMinimal.h"
#include "SatisDash/FrmPlayers.h"

namespace SatisDash
{
	/**
	 * R1 (ADR-0058): what the game's player lists gave, as the contract's players part (AgentPlayersSchema, ADR-0029: a name
	 * and online, nothing else). At most MaxPlayers are kept, connected players first so a long list of remembered players
	 * never pushes out someone who is online; the rest keep the order they were given in; names are cut to 200. A part built
	 * here is always available: an unreadable game state is the caller's "leave the part out", never "available: false".
	 */
	SATISDASHCORE_API FPlayersPart NativePlayersPart(const TArray<FPlayerReading>& Readings);
}
