#include "SatisDash/Native/Players/NativePlayers.h"

#include "SatisDash/Snapshot.h"

SatisDash::FPlayersPart SatisDash::NativePlayersPart(const TArray<FPlayerReading>& Readings)
{
	FPlayersPart Part;
	Part.bAvailable = true;
	for (const bool bOnlinePass : { true, false })
	{
		for (const FPlayerReading& Reading : Readings)
		{
			if (Reading.bOnline != bOnlinePass)
			{
				continue;
			}
			if (Part.Players.Num() == MaxPlayers)
			{
				return Part;
			}
			Part.Players.Add({ ClampString(Reading.Name), Reading.bOnline });
		}
	}
	return Part;
}
