#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Ports.h"

class UWorld;
class ULevel;
class UFGServerSubsystem;
class AFGTrain;
class AFGTrainStationIdentifier;

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
		virtual bool FactoryReadAbandoned() const override { return bFactoryAbandoned; }
		virtual void EndFactoryRead() override { ResetFactoryRead(); }
		virtual bool BeginTrainsRead() override;
		virtual TOptional<FTrainsSnapshot> StepTrainsRead(FStepBudget& Budget) override;
		virtual bool TrainsReadAbandoned() const override { return bTrainsAbandoned; }
		virtual void EndTrainsRead() override { ResetTrainsRead(); }
		virtual TOptional<FAutoPauseReading> ApplyAutoPause(bool bEnabled) override;

	private:
		/** Ends the trains read and frees what it holds. */
		void ResetTrainsRead();
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
		/** The read that just returned was abandoned (no subsystem, or a world swap), not finished. */
		bool bFactoryAbandoned = false;
		int32 FactoryCursor = 0;
		int32 FactoryExpected = 0;
		int32 FactoryBoosted = 0;
		double FactoryProductivityMin = 0;
		double FactoryProductivityMax = 0;
		TArray<FFactoryReading> FactoryFound;

		/**
		 * The trains read (R3): weak pointers to the trains and station identifiers (dozens), copied when the read begins, and two
		 * cursors into them. Nothing else is kept across ticks: every other pointer is used inside one step and re-checked. The
		 * world it began in: a read that ends in another is "cannot read". One read is open at a time (the driver's mapLive pass
		 * and the world sender's train-routes pass take turns).
		 */
		TWeakObjectPtr<UWorld> TrainsWorld;
		TArray<TWeakObjectPtr<AFGTrain>> TrainsPending;
		TArray<TWeakObjectPtr<AFGTrainStationIdentifier>> StationsPending;
		int32 TrainsCursor = 0;
		int32 StationsCursor = 0;
		bool bTrainsOpen = false;
		/** The read that just returned was abandoned (no subsystem, or a world swap), not finished. */
		bool bTrainsAbandoned = false;
		FTrainsSnapshot TrainsFound;
	};
}
