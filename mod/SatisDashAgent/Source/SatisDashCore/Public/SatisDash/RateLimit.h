#pragma once

#include "CoreMinimal.h"

namespace SatisDash
{
	/** For lines that could repeat every pass, such as a reader failing (spec error table): once per key per interval. */
	class SATISDASHCORE_API FRateLimitedLog
	{
	public:
		explicit FRateLimitedLog(double InIntervalSeconds = 600.0);
		/** True the first time a key is seen, then at most once per interval. */
		bool ShouldLog(const FString& Key, double Now);

	private:
		double IntervalSeconds;
		TMap<FString, double> LastAt;
	};
}
