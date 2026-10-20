#pragma once

#include "CoreMinimal.h"

namespace SatisDash
{
	/**
	 * Writes Text as UTF-8 without a BOM to Path + ".tmp", then moves it over Path, so a crash leaves the old file or
	 * the new one, never half of one (spec S11). Creates the folder. On Linux the file is made owner-only (0600)
	 * before the move.
	 */
	SATISDASHCORE_API bool WriteFileAtomic(const FString& Path, const FString& Text);
}
