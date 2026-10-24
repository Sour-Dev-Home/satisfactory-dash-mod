#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Ports.h"

class UWorld;
class UFGServerSubsystem;

namespace SatisDash
{
	/** The game's own readers, in-process, as S6 (status), S7 (auto-pause) and S3 (power) proved them. Game thread only. */
	class FGameReader : public IGameReader
	{
	public:
		/** The newest game world: the entry map at boot, then each loaded save. */
		void SetWorld(UWorld* InWorld) { World = InWorld; }

		virtual TOptional<FStatusReading> ReadStatus() override;
		virtual TOptional<FAutoPauseReading> ReadAutoPause() override;
		virtual TOptional<TArray<FCircuitReading>> ReadPower() override;
		virtual TOptional<FAutoPauseReading> ApplyAutoPause(bool bEnabled) override;

	private:
		UFGServerSubsystem* Server() const;
		TWeakObjectPtr<UWorld> World;
	};
}
