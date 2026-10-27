#pragma once

#include "CoreMinimal.h"
#include "FGMapMarker.h"
#include "SatisDash/Ports.h"

class UWorld;
class ULevel;
class UFGServerSubsystem;
class AFGTrain;
class AFGBuildableRailroadTrack;
class AFGBuildableConveyorBase;
class AFGBuildablePipeline;
class AFGBuildableWire;
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
		virtual bool IsTrainsReadOpen() const override { return bTrainsOpen; }
		virtual bool BeginMapMarkersRead() override;
		virtual TOptional<FMapMarkersSnapshot> StepMapMarkersRead(FStepBudget& Budget) override;
		virtual bool MapMarkersReadAbandoned() const override { return bMarkersAbandoned; }
		virtual void EndMapMarkersRead() override { ResetMapMarkersRead(); }
		virtual bool IsMapMarkersReadOpen() const override { return bMarkersOpen; }
		virtual bool BeginDroneRoutesRead() override;
		virtual TOptional<FDroneRoutesSnapshot> StepDroneRoutesRead(FStepBudget& Budget) override;
		virtual bool DroneRoutesReadAbandoned() const override { return bDroneAbandoned; }
		virtual void EndDroneRoutesRead() override { ResetDroneRoutesRead(); }
		virtual bool IsDroneRoutesReadOpen() const override { return bDroneOpen; }
		virtual bool BeginLogisticsRead(bool bRails, bool bConveyors, bool bPowerLines) override;
		virtual TOptional<FLogisticsSnapshot> StepLogisticsRead(FStepBudget& Budget) override;
		virtual bool LogisticsReadAbandoned() const override { return bLogisticsAbandoned; }
		virtual void EndLogisticsRead() override { ResetLogisticsRead(); }
		virtual bool IsLogisticsReadOpen() const override { return bLogisticsOpen; }
		virtual TOptional<FAutoPauseReading> ApplyAutoPause(bool bEnabled) override;

	private:
		/** Ends the logistics pass and frees what it holds (also called when a step returns a finished or abandoned value). */
		void ResetLogisticsRead();
		/** Ends the trains read and frees what it holds. */
		void ResetTrainsRead();
		/** Ends the map-markers read and frees its copy. */
		void ResetMapMarkersRead();
		/** Ends the drone-routes read and frees what it holds. */
		void ResetDroneRoutesRead();
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
		/** The diagnostic for the longest scan step: its actor count and the level index (its "phase") at its start and end. */
		int32 ScanLongestItems = 0;
		int32 ScanLongestStartLevel = 0;
		int32 ScanLongestEndLevel = 0;
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

		/**
		 * The map-markers read (R8): the manager's markers copied once when the read begins (at most 250 small structs, the only
		 * game struct held across ticks; the strings are freed on end), a cursor into the copy, and the world it began in.
		 * The copy also holds fields the reader never reads (LastEditedBy, CreatedByPlayerID); it is freed at Reset and End.
		 * It is taken in the first step, not in Begin, so the step budget times it.
		 */
		TWeakObjectPtr<UWorld> MarkersWorld;
		TArray<FMapMarker> MarkersCopy;
		bool bMarkersCopied = false;
		int32 MarkersCursor = 0;
		bool bMarkersOpen = false;
		/** The read that just returned was abandoned (no manager, or a world swap), not finished. */
		bool bMarkersAbandoned = false;
		FMapMarkersSnapshot MarkersFound;

		/**
		 * The drone-routes read (R9): an index into the subsystem's station list, never a pointer into it (the list is re-fetched
		 * for every station), and the world it began in. Nothing else is held across ticks.
		 */
		TWeakObjectPtr<UWorld> DroneWorld;
		int32 DroneCursor = 0;
		bool bDroneOpen = false;
		/** The read that just returned was abandoned (no subsystem, or a world swap), not finished. */
		bool bDroneAbandoned = false;
		FDroneRoutesSnapshot DroneFound;

		/**
		 * The logistics pass (R5): an index into the buildable list (never a pointer into it) and, once collected, one weak pointer
		 * per track, belt, lift and pipeline. The world it began in: a pass that ends in another is abandoned. One pass is open at a time.
		 */
		enum class ELogisticsPhase : uint8 { Collect, ReadRails, ReadConveyors, ReadPowerLines };
		TWeakObjectPtr<UWorld> LogisticsWorld;
		TArray<TWeakObjectPtr<AFGBuildableRailroadTrack>> LogisticsTracks;
		/** R6: one weak pointer per belt, lift and pipeline (the ReadConveyors cursor runs over belts, then lifts, then pipes). */
		TArray<TWeakObjectPtr<AFGBuildableConveyorBase>> LogisticsBelts;
		TArray<TWeakObjectPtr<AFGBuildableConveyorBase>> LogisticsLifts;
		TArray<TWeakObjectPtr<AFGBuildablePipeline>> LogisticsPipes;
		/** R10: one weak pointer per power wire. */
		TArray<TWeakObjectPtr<AFGBuildableWire>> LogisticsWires;
		FLogisticsSnapshot LogisticsFound;
		ELogisticsPhase LogisticsPhase = ELogisticsPhase::Collect;
		int32 LogisticsCursor = 0;
		bool bLogisticsRails = false;
		bool bLogisticsConveyors = false;
		bool bLogisticsPowerLines = false;
		bool bLogisticsOpen = false;
		/** The pass that just returned was abandoned (no subsystem, a world swap, or zero buildables), not finished. */
		bool bLogisticsAbandoned = false;
	};
}
