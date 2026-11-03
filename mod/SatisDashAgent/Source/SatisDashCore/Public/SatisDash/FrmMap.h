#pragma once

#include "CoreMinimal.h"
#include "SatisDash/JsonTokens.h"
#include "SatisDash/Records.h"

namespace SatisDash
{
	/**
	 * Reads the location every FRM map record carries (rawSchemas.ts RawFrmLocationSchema): the object whose ObjectStart was
	 * just returned (Started). Finite numbers x, y and z are required (z is read and dropped), rotation is optional but must
	 * be a finite number if present, every other field is skipped. False when it is not that, or on a reader failure. The
	 * last of a repeated key wins, as JSON.parse's does. The coordinate helpers (JsRound, ToWholeMetres, InWholeMetreBounds)
	 * are in Records.h.
	 */
	SATISDASHCORE_API bool ReadFrmLocation(FTokenReader& Reader, EJsonNotation Started, double& OutX, double& OutY);
}
