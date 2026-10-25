#pragma once

#include "CoreMinimal.h"
#include "SatisDash/JsonTokens.h"

namespace SatisDash
{
	/**
	 * Game units (centimetres) to the whole metres the map contract uses (map/coordinates.ts, map.ts WholeMetreSchema), and the
	 * location every FRM map record carries (rawSchemas.ts RawFrmLocationSchema).
	 */

	/** JavaScript's Math.round: a half goes toward +infinity (-1.5 is -1, 2.5 is 3, -0.5 is 0). Not FMath::RoundToDouble, which is floor(x + 0.5) and gets 0.49999999999999994 wrong. */
	SATISDASHCORE_API double JsRound(double Value);
	/** toWholeMetres: Math.round(gameUnits / 100), the division a true division (see FrmMap.cpp). */
	SATISDASHCORE_API double ToWholeMetres(double GameUnits);
	/** inWholeMetreBounds: finite and within +/-1,000,000 m. */
	SATISDASHCORE_API bool InWholeMetreBounds(double WholeMetres);
	/**
	 * Reads the object whose ObjectStart was just returned (Started): finite numbers x, y and z are required (z is read and
	 * dropped), rotation is optional but must be a finite number if present, every other field is skipped. False when it is
	 * not that, or on a reader failure. The last of a repeated key wins, as JSON.parse's does.
	 */
	SATISDASHCORE_API bool ReadFrmLocation(FTokenReader& Reader, EJsonNotation Started, double& OutX, double& OutY);
}
