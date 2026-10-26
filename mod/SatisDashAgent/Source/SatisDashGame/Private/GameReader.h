#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Ports.h"

class UWorld;
class ULevel;
class UFGServerSubsystem;

namespace SatisDash
{
	/** The game's own readers, in-process, as S6 (status), S7 (auto-pause), S3 (power) and ADR-0058 R1 (players) proved them. Game thread only. */
	class FGameReader : public IGameReader
	{
	public:
		/** The newest game world: the entry map at boot, then each loaded save. */
		void SetWorld(UWorld* InWorld) { World = InWorld; }

		virtual TOptional<FStatusReading> ReadStatus() override;
		virtual TOptional<FAutoPauseReading> ReadAutoPause() override;
		virtual TOptional<TArray<FCircuitReading>> ReadPower() override;
		virtual TOptional<TArray<FPlayerReading>> ReadPlayers() override;
		virtual bool BeginResourceNodeScan() override;
		virtual TOptional<TArray<FResourceNodeReading>> StepResourceNodeScan(int32 MaxActors) override;
		virtual bool BeginFactoryRead() override;
		virtual TOptional<TArray<FFactoryReading>> StepFactoryRead(FStepBudget& Budget) override;
		virtual TOptional<FAutoPauseReading> ApplyAutoPause(bool bEnabled) override;

	private:
		/** Ends the factory read and frees what it holds. */
		void ResetFactoryRead();
		UFGServerSubsystem* Server() const;
		TWeakObjectPtr<UWorld> World;

		/** The node scan (R4): the levels to look through, where it is, what it found, and its cost (counts and times only). */
		TArray<TWeakObjectPtr<ULevel>> ScanLevels;
		/** The world the scan began in: a scan that ends in a different world read a mix, and is "cannot read". */
		TWeakObjectPtr<UWorld> ScanWorld;
		int32 ScanLevel = 0;
		int32 ScanIndex = 0;
		int32 ScanActors = 0;
		int32 ScanSteps = 0;
		double ScanLongestStep = 0;
		double ScanTotal = 0;
		TArray<FResourceNodeReading> ScanFound;

		/**
		 * The factory read (R2a): an index into the buildable list, never a pointer into it (the list can change between ticks;
		 * GetAllBuildablesRef is re-fetched for every element). The world it began in: a read that ends in another is "cannot read".
		 */
		TWeakObjectPtr<UWorld> FactoryWorld;
		int32 FactoryCursor = 0;
		int32 FactoryExpected = 0;
		int32 FactoryBoosted = 0;
		double FactoryProductivityMin = 0;
		double FactoryProductivityMax = 0;
		TArray<FFactoryReading> FactoryFound;
	};
}
