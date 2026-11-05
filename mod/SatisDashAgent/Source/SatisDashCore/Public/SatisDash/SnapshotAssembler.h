#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Ports.h"
#include "SatisDash/RateLimit.h"
#include "SatisDash/Requests.h"
#include "SatisDash/Snapshot.h"
#include "SatisDash/WorldRead.h"

namespace SatisDash
{
	/**
	 * Holds the parts that go into a snapshot or a world post, and does their heavy work on the worker (R7 step 2: it is the
	 * assembly role, split out of the retired bridge class). A reader hands a finished game-thread reading to a Submit* call; the worker
	 * builds the part (or fragment) from it, and the driver takes it with the matching Take* call when it assembles the next
	 * snapshot (a world layer's outcome is taken by FWorldSender). It also serialises and gzips, on the worker, a snapshot that
	 * carries the factory or mapLive part.
	 *
	 * Threading: the worker runs one job at a time. The assembler's jobs, in the order it offers them (LaunchEncode, then
	 * LaunchNativeParts, LaunchFactory and LaunchWorld): a snapshot to
	 * encode, the mapLive, extractors, generators and storage builds, the factory build, then a world layer (build, hash, finish).
	 * A job is shared with its task (TSharedPtr), so dropping it here never frees what the task still reads. What a task wrote
	 * is read only once IWorker::IsIdle().
	 *
	 * Memory (it lives for the server's uptime): one factory job and its output, one ready part of each, one mapLive job and its
	 * part (under 256 KB, or dropped), one world body (4 MB cap) being built and hashed, and its output (at most 2,000,000
	 * bytes, then gzipped), and for each layer its outcome waiting to be taken; one snapshot being encoded and its body; the
	 * failure log at most seven keys.
	 *
	 * Nothing here knows where a part came from. A part that arrives finished (a body another role parsed) comes in through
	 * the narrow entry points after LargeBusy (SetFactoryPart, SetMapLivePart, and the world claim calls).
	 */
	class SATISDASHCORE_API FSnapshotAssembler
	{
	public:
		explicit FSnapshotAssembler(IWorker& InWorker);
		FSnapshotAssembler(const FSnapshotAssembler&) = delete;
		FSnapshotAssembler& operator=(const FSnapshotAssembler&) = delete;

		/** The newest factory part finished within MaxAgeSeconds, once (with the extractors fragment spliced in, see SubmitNativeExtractors). */
		TOptional<FString> TakeFactory(double Now, double MaxAgeSeconds);
		/** The newest mapLive part ({"trains":[...],"stations":[...]}) finished within MaxAgeSeconds, once. */
		TOptional<FString> TakeMapLive(double Now, double MaxAgeSeconds);
		/**
		 * R4: the node readings a native scan finished on the game thread, moved into a world job. The worker sorts them,
		 * builds the body and settles it (data hash, cap, encode); then TakeWorld. False while that layer is requested, being
		 * read, or has an outcome waiting (the readings are then dropped).
		 */
		bool SubmitNativeWorld(EWorldLayer Layer, const FString& ObservedAt, TArray<FResourceNodeReading> Readings, TOptional<FBlake3Hash> LastData);
		/**
		 * R3: the same for any native layer: Builder (which owns the readings it was made from) runs on the worker and makes the
		 * whole body; the cap, hashes and gzip are then applied.
		 */
		bool SubmitNativeWorld(EWorldLayer Layer, const FString& ObservedAt, FNativeWorldBuilder Builder, TOptional<FBlake3Hash> LastData);
		/**
		 * R3: the trains and stations a native read finished on the game thread, moved into a mapLive job. The worker joins
		 * them (NativeMapLiveJson) and sets the part ready, so TakeMapLive is the one way out; a part past its byte cap is left
		 * out with one warning. ReadAt is the part's age start. False while a mapLive job is held (the readings are then dropped).
		 */
		bool SubmitNativeMapLive(FTrainsSnapshot Snapshot, double ReadAt);
		/**
		 * R2a: the machines a native read finished on the game thread, moved into a factory job. The worker builds the part
		 * (NativeFactoryPart) and sets it ready, so TakeFactory is the one way out. ReadAt is the part's age start. False while
		 * a factory job is held (the readings are then dropped).
		 */
		bool SubmitNativeFactory(TArray<FFactoryReading> Readings, double ReadAt);
		/**
		 * S4-B: the extractors a native Buildables pass finished, moved into a job. The worker builds the fragment
		 * (NativeExtractorsFragment) and holds it (the readings are freed). TakeFactory splices it into the next factory part as
		 * ,"extractors":[...] when it is not older than 1.5 x that call's MaxAgeSeconds, and consumes it; while the job is
		 * outstanding TakeFactory waits for it. False while a fragment job is held (the readings are then dropped).
		 */
		bool SubmitNativeExtractors(TArray<FExtractorReading> Readings, double ReadAt);
		/** The extractors source is not native (it flipped to bridge): forget the held fragment and any job, so nothing is spliced after the flip. */
		void DropExtractors();
		/**
		 * S4-B (ruling B3): the generators a native Buildables pass finished, moved into a job. The worker builds the fragment
		 * (NativeGeneratorsFragment), replaces the held one with it, and the readings are freed. TakeGenerators then hands it to the
		 * NEXT power part only (contract #807: once per read). False while a fragment job is outstanding (the readings are then dropped).
		 */
		bool SubmitNativeGenerators(TArray<FGeneratorReading> Readings, double ReadAt);
		/**
		 * The built generators fragment for the next power part, consumed (a shared reference, never a copy; later power parts get null
		 * until the next read finishes), or null when none is ready or it is older than 1.5 x FactorySeconds (dropped: the read time is
		 * the snapshot's observedAt, so a stale list is never sent under a fresh one).
		 */
		TSharedPtr<const FString> TakeGenerators(double Now, double FactorySeconds);
		/** The generators source is not native (it flipped to bridge): forget the held fragment and any job. */
		void DropGenerators();
		/**
		 * S4-B (ruling B3): the containers a native Buildables pass finished, moved into a job. The worker builds the storage
		 * fragment (NativeStorageFragment), holds it, and the readings are freed. TakeStorage hands it to the first snapshot taken
		 * after it is built and clears it (a data part is sent once per read). False while a fragment job is outstanding.
		 */
		bool SubmitNativeStorage(TArray<FContainerReading> Readings, double ReadAt);
		/**
		 * The held storage fragment, once (consumed), or unset when none is built or it is older than 1.5 x FactorySeconds
		 * (then it is cleared, not sent: the extractors' age gate).
		 */
		TOptional<FString> TakeStorage(double Now, double FactorySeconds);
		/** The storage source is not native (it flipped to bridge): forget the held fragment and any job. */
		void DropStorage();
		/** The layer's finished outcome, once. */
		TOptional<FWorldOutcome> TakeWorld(EWorldLayer Layer);
		/** Hands a snapshot carrying the factory or mapLive part to the worker for SnapshotToJson and EncodeBody. */
		void EncodeSnapshot(FSnapshot Snapshot);
		bool EncodePending() const { return Encode.IsValid(); }
		TOptional<FEncodedBody> TakeEncoded();
		/** Leaving Running: no part, fragment, job or encoded body kept. A running task finishes and is dropped. */
		void Stop();

		/**
		 * The worker's turn: while IWorker::IsIdle(), Harvest what the last task wrote, then offer the worker its next job (the
		 * Launch* calls below, in that order, stopping at the first that launched). The driver calls this once a tick.
		 */
		void Tick(double Now);

		/** The pieces of the worker's turn, for a role that interleaves its own jobs. Each Launch* returns true if it launched one. */
		void Harvest(double Now);
		bool LaunchEncode();
		bool LaunchNativeParts();
		bool LaunchFactory();
		bool LaunchWorld();
		/** A large body is held (a factory build or a world job): no second large read starts, to bound memory (the large-read cap and queue limit). */
		bool LargeBusy() const;

		// Finished parts and world reads: the narrow entry points for a part another role produced. The assembler treats
		// them exactly as it does its own.

		/** A factory part's text finished by another role; ReadAt is its age start. Replaces an untaken one. */
		void SetFactoryPart(FString Json, double ReadAt);
		/** The same for the mapLive part. */
		void SetMapLivePart(FString Json, double ReadAt);
		/** Claims the layer's slot for a read another role makes; false while the layer is requested, being read, or has an outcome waiting. */
		bool ClaimWorld(EWorldLayer Layer);
		/** The claimed read was not made: the layer is free again. */
		void ReleaseWorld(EWorldLayer Layer);
		/** The claimed layer's read, finished by another role's body source: stepped on the worker like a native one. */
		void SetWorldRead(EWorldLayer Layer, TUniquePtr<FWorldRead> Read);
		/** The claimed layer's read failed: its outcome is ReadFailed (so the sender can schedule the next cycle), and the layer is free. */
		void FailWorld(EWorldLayer Layer);
		/** One warning, at most once per 600 s per Part, that a part could not be read and is left out of the snapshot. */
		void NoteFailed(const TCHAR* Part, double Now);
		/** One warning, at most once per 600 s, that the mapLive part is over its byte cap and is left out. */
		void NoteMapLiveOverCap(double Now);

	private:
		/** R2a: the game's own readings; the worker builds the part from them. */
		struct FFactoryJob
		{
			FFactoryJob(TArray<FFactoryReading> InReadings, double InReadAt) : Readings{MoveTemp(InReadings)}, ReadAt{InReadAt} {}
			bool bRan = false;
			double ReadAt = 0;
			TArray<FFactoryReading> Readings;
			/** The part's text, written by the worker. */
			FString NativeJson;
		};
		/** S4-B: the extractors readings, then (worker) the fragment text; the readings are freed once it is built. */
		struct FExtractorsJob
		{
			FExtractorsJob(TArray<FExtractorReading> InReadings, double InReadAt) : Readings{MoveTemp(InReadings)}, ReadAt{InReadAt} {}
			TArray<FExtractorReading> Readings;
			double ReadAt = 0;
			FString Fragment;
			bool bRan = false;
		};
		/** S4-B: the generators readings, then (worker) the fragment text; the readings are freed once it is built. */
		struct FGeneratorsJob
		{
			FGeneratorsJob(TArray<FGeneratorReading> InReadings, double InReadAt) : Readings{MoveTemp(InReadings)}, ReadAt{InReadAt} {}
			TArray<FGeneratorReading> Readings;
			double ReadAt = 0;
			FString Fragment;
			bool bRan = false;
		};
		/** S4-B: the containers readings, then (worker) the fragment text and its counts; the readings are freed once it is built. */
		struct FStorageJob
		{
			FStorageJob(TArray<FContainerReading> InReadings, double InReadAt) : Readings{MoveTemp(InReadings)}, ReadAt{InReadAt} {}
			TArray<FContainerReading> Readings;
			double ReadAt = 0;
			FString Fragment;
			int32 Dropped = 0, Over = 0, ItemsCut = 0, SlotsClamped = 0;
			/** The fragment's UTF-8 size, counted on the worker for the log line. */
			int64 Bytes = 0;
			bool bRan = false;
		};
		/** The snapshot body is made on the worker; Report says what the size rule left out. */
		struct FEncodeJob { FSnapshot Snapshot; FEncodedBody Body; FSnapshotSizeReport Report; bool bRan = false; };
		/** R3: the game's own trains and stations; the worker writes the joined part (Json, or Ok false when it is over its cap). */
		struct FNativeMapLiveJob
		{
			FTrainsSnapshot Readings;
			double ReadAt = 0;
			FString Json;
			bool bOk = false;
			bool bRan = false;
		};
		struct FReady { FString Json; double At = 0; };
		/** A held fragment shared with every snapshot that carries it (the generators fragment, handed out once). */
		struct FSharedReady { TSharedPtr<const FString> Json; double At = 0; };

		struct FWorldJob
		{
			explicit FWorldJob(TUniquePtr<FWorldRead> InRead) : Owned{MoveTemp(InRead)} {}
			/**
			 * Named Owned, not Work or Read: check-api's receiver heuristic does not trust a name that is declared with another type
			 * anywhere in the code, and the tests call Step on a FWorldRead named Work.
			 */
			TUniquePtr<FWorldRead> Owned;
			bool bFinished = false;
			bool bRan = false;
		};
		struct FWorldSlot
		{
			bool bRequested = false;
			TSharedPtr<FWorldJob> Job;
			TOptional<FWorldOutcome> Outcome;
		};
		FWorldSlot* NextWorldJob();
		static TOptional<FString> TakeFresh(TOptional<FReady>& Ready, double Now, double MaxAgeSeconds);

		IWorker& Worker;
		TSharedPtr<FFactoryJob> Factory;
		/** At most one extractors job, and one built fragment held until the next factory part takes it (bounded by the 5000-record cap). */
		TSharedPtr<FExtractorsJob> ExtractorsTask;
		TOptional<FReady> ExtractorsReady;
		/** At most one generators job, and the newest built fragment, held until the next power part takes it (bounded by the 5000-record cap). */
		TSharedPtr<FGeneratorsJob> GeneratorsTask;
		TOptional<FSharedReady> GeneratorsReady;
		/** At most one storage job, and one built fragment held until the next snapshot takes it (bounded by the 2000-container cap). */
		TSharedPtr<FStorageJob> StorageTask;
		TOptional<FReady> StorageReady;
		TSharedPtr<FEncodeJob> Encode;
		TSharedPtr<FNativeMapLiveJob> NativeMapLive;
		TOptional<FReady> FactoryReady;
		TOptional<FReady> MapLiveReady;
		FWorldSlot World[WorldLayerCount];
		TOptional<FEncodedBody> Encoded;
		/** Bounded: keys ("players", "factory", "mapLive", "mapLiveOverCap", "extractorsDropped", "snapshotOverCap", "storageCuts"), each at most once per 600 s. */
		FRateLimitedLog FailLog;
	};
}
