#pragma once

#include "CoreMinimal.h"
#include "SatisDash/PlayersPart.h"

namespace SatisDash
{
	/**
	 * getPlayer's 2xx body, as the Node agent reads it (rawSchemas.ts RawFrmPlayerSchema, conform.ts conformPlayers): an
	 * array of objects, each with a string Name and a boolean Online; every other field is skipped unread. Unset when the
	 * body is invalid (the Node agent's invalid_response: the part is left out). The first 256 are kept, names cut to 200.
	 */
	SATISDASHCORE_API TOptional<FPlayersPart> ParsePlayers(const TArray<uint8>& Body);
}
