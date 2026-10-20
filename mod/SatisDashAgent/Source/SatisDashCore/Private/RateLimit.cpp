#include "SatisDash/RateLimit.h"

SatisDash::FRateLimitedLog::FRateLimitedLog(double InIntervalSeconds)
	: IntervalSeconds{InIntervalSeconds}
{
}

bool SatisDash::FRateLimitedLog::ShouldLog(const FString& Key, double Now)
{
	if (const double* Last = LastAt.Find(Key); Last != nullptr && Now - *Last < IntervalSeconds)
	{
		return false;
	}
	LastAt.Add(Key, Now);
	return true;
}
