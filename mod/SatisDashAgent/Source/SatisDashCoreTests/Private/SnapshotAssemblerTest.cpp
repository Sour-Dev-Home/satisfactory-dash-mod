#include "DriverFakes.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SatisDash/SnapshotAssembler.h"

using namespace SatisDash;

namespace
{
	struct FSnapshotAssemblerFixture
	{
		SatisDash::Test::FFakeWorker Worker;
		FSnapshotAssembler Assembler{ Worker };
		double T = 100;
		void tick(int32 Times = 1)
		{
			for (int32 I = 0; I < Times; ++I)
			{
				T += 0.5;
				Assembler.Tick(T);
			}
		}
	};
}

// Moved from FrmBridgeTest (SatisDash.Core.FrmBridge.Extractors) with the assembly role; the assertions are the same.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnapshotAssemblerExtractorsTest, "SatisDash.Core.SnapshotAssembler.Extractors", SatisDash::Test::Flags)

bool FSnapshotAssemblerExtractorsTest::RunTest(const FString& Parameters)
{
	FFactoryReading Machine;
	Machine.Id = TEXT("B_9");
	Machine.Name = TEXT("n");
	Machine.ClassName = TEXT("c");
	FExtractorReading Miner;
	Miner.Id = TEXT("Build_MinerMk1_C_1");
	Miner.Name = TEXT("Miner");
	Miner.ClassName = TEXT("Build_MinerMk1_C");

	// The part with no fragment at all: the bytes the flag-off path produces.
	FString Plain;
	{
		FSnapshotAssemblerFixture F;
		F.Assembler.SubmitNativeFactory(TArray<FFactoryReading>{ Machine }, F.T);
		F.tick(3);
		Plain = F.Assembler.TakeFactory(F.T, 30).Get(FString());
	}
	TestTrue(TEXT("the plain part ends with the closing brace"), Plain.EndsWith(TEXT("}")));

	// A fresh fragment is spliced before the closing brace; every other byte is unchanged and the whole parses.
	{
		FSnapshotAssemblerFixture F;
		TestTrue(TEXT("a fragment is taken"), F.Assembler.SubmitNativeExtractors(TArray<FExtractorReading>{ Miner }, F.T));
		TestFalse(TEXT("a second is refused while one is held"), F.Assembler.SubmitNativeExtractors(TArray<FExtractorReading>{ Miner }, F.T));
		F.Assembler.SubmitNativeFactory(TArray<FFactoryReading>{ Machine }, F.T);
		F.tick(4);
		const FString Part = F.Assembler.TakeFactory(F.T, 30).Get(FString());
		TestTrue(TEXT("the extractors are spliced in"), Part.Contains(TEXT(",\"extractors\":[{\"id\":\"Build_MinerMk1_C_1\"")));
		const int32 Spliced = Part.Find(TEXT(",\"extractors\":["));
		TestTrue(TEXT("the plain part's bytes come first, unchanged"), Spliced > 0 && Part.Left(Spliced) == Plain.LeftChop(1));
		TestTrue(TEXT("and the closing brace is last"), Part.EndsWith(TEXT("]}")));
		TSharedPtr<FJsonObject> Parsed;
		TestTrue(TEXT("the part parses as JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Part), Parsed) && Parsed.IsValid());
		TestTrue(TEXT("with both keys"), Parsed.IsValid() && Parsed->HasTypedField<EJson::Array>(TEXT("buildings")) && Parsed->HasTypedField<EJson::Array>(TEXT("extractors")));
		F.tick(2);
		TestFalse(TEXT("a fragment is consumed once"), F.Assembler.TakeFactory(F.T, 30).IsSet());
	}

	// A stale fragment (older than 1.5 x the factory age limit) is not spliced.
	{
		FSnapshotAssemblerFixture F;
		F.Assembler.SubmitNativeExtractors(TArray<FExtractorReading>{ Miner }, F.T - 100);
		F.Assembler.SubmitNativeFactory(TArray<FFactoryReading>{ Machine }, F.T);
		F.tick(4);
		SatisDash::Test::SameText(*this, TEXT("a stale fragment leaves the part as it was"), F.Assembler.TakeFactory(F.T, 30).Get(FString()), Plain);
	}

	// The fragment's worker job is still outstanding: the ready part is kept, not returned; when the job finishes the part carries it.
	{
		FSnapshotAssemblerFixture F;
		F.Worker.bHold = true;
		F.Assembler.SubmitNativeFactory(TArray<FFactoryReading>{ Machine }, F.T);
		F.tick();
		F.Worker.release();
		F.tick();
		F.Assembler.SubmitNativeExtractors(TArray<FExtractorReading>{ Miner }, F.T);
		F.tick();
		TestFalse(TEXT("the part waits while the fragment job is on the worker"), F.Assembler.TakeFactory(F.T, 30).IsSet());
		F.Worker.release();
		F.tick();
		TestTrue(TEXT("the part is returned with the fragment once the job finished"), F.Assembler.TakeFactory(F.T, 30).Get(FString()).Contains(TEXT(",\"extractors\":[{\"id\":\"Build_MinerMk1_C_1\"")));
	}

	// Past the fragment's own age gate (1.5 x 30 s) with the job still held, the part is returned without it.
	{
		FSnapshotAssemblerFixture F;
		F.Worker.bHold = true;
		F.Assembler.SubmitNativeFactory(TArray<FFactoryReading>{ Machine }, F.T + 40);
		F.tick();
		F.Worker.release();
		F.tick();
		F.Assembler.SubmitNativeExtractors(TArray<FExtractorReading>{ Miner }, F.T);
		F.tick();
		SatisDash::Test::SameText(*this, TEXT("past the deadline the part goes out without the fragment"), F.Assembler.TakeFactory(F.T + 50, 30).Get(FString()), Plain);
	}

	// The source flips to bridge: neither a held fragment nor a pending job is spliced after DropExtractors.
	{
		FSnapshotAssemblerFixture F;
		F.Assembler.SubmitNativeExtractors(TArray<FExtractorReading>{ Miner }, F.T);
		F.tick(2);
		F.Assembler.SubmitNativeExtractors(TArray<FExtractorReading>{ Miner }, F.T);
		F.Assembler.DropExtractors();
		F.Assembler.SubmitNativeFactory(TArray<FFactoryReading>{ Machine }, F.T);
		F.tick(4);
		SatisDash::Test::SameText(*this, TEXT("a dropped fragment and job leave the part as it was"), F.Assembler.TakeFactory(F.T, 30).Get(FString()), Plain);
	}

	// An empty list is a real, empty fragment.
	{
		FSnapshotAssemblerFixture F;
		F.Assembler.SubmitNativeExtractors(TArray<FExtractorReading>(), F.T);
		F.Assembler.SubmitNativeFactory(TArray<FFactoryReading>{ Machine }, F.T);
		F.tick(4);
		TestTrue(TEXT("an empty list is spliced as []"), F.Assembler.TakeFactory(F.T, 30).Get(FString()).EndsWith(TEXT(",\"extractors\":[]}")));
	}

	// S4-B generators (the power part's fragment, same fixture): once per read (owner 2026-10-27), handed to the next power part only.
	FGeneratorReading Coal;
	Coal.Id = TEXT("Gen_Coal_1");
	Coal.Name = TEXT("Coal");
	Coal.ClassName = TEXT("Build_GeneratorCoal_C");
	FGeneratorReading Fuel;
	Fuel.Id = TEXT("Gen_Fuel_2");
	Fuel.Name = TEXT("Fuel");
	Fuel.ClassName = TEXT("Build_GeneratorFuel_C");
	{
		FSnapshotAssemblerFixture F;
		TestFalse(TEXT("nothing held yet"), F.Assembler.TakeGenerators(F.T, 30).IsValid());
		const double At = F.T;
		TestTrue(TEXT("a fragment job is taken"), F.Assembler.SubmitNativeGenerators(TArray<FGeneratorReading>{ Coal }, At));
		TestFalse(TEXT("a second is refused while one is outstanding"), F.Assembler.SubmitNativeGenerators(TArray<FGeneratorReading>{ Coal }, At));
		F.tick(3);
		const TSharedPtr<const FString> First = F.Assembler.TakeGenerators(F.T, 30);
		TestTrue(TEXT("the built fragment is handed out"), First.IsValid() && First->StartsWith(TEXT("[{\"id\":\"Gen_Coal_1\"")) && First->EndsWith(TEXT("]")));
		TestFalse(TEXT("once: the next power part gets nothing"), F.Assembler.TakeGenerators(F.T + 5, 30).IsValid());
		TestFalse(TEXT("and nothing later either, until the next read"), F.Assembler.TakeGenerators(F.T + 25, 30).IsValid());
		TestTrue(TEXT("a new job is taken once the old fragment is gone"), F.Assembler.SubmitNativeGenerators(TArray<FGeneratorReading>{ Fuel }, F.T));
		F.tick(3);
		const TSharedPtr<const FString> Second = F.Assembler.TakeGenerators(F.T, 30);
		TestTrue(TEXT("the next read goes out again, once"), Second.IsValid() && Second->Contains(TEXT("Gen_Fuel_2")) && !F.Assembler.TakeGenerators(F.T, 30).IsValid());
	}
	{
		// A fragment older than 1.5 x the factory cadence is dropped, never sent under a fresh observedAt.
		FSnapshotAssemblerFixture F;
		const double At = F.T;
		F.Assembler.SubmitNativeGenerators(TArray<FGeneratorReading>{ Coal }, At);
		F.tick(3);
		TestFalse(TEXT("older than 1.5 x the factory cadence it is not sent"), F.Assembler.TakeGenerators(At + 45.5, 30).IsValid());
		TestFalse(TEXT("and it is forgotten, not brought back"), F.Assembler.TakeGenerators(At, 30).IsValid());
		F.Assembler.SubmitNativeGenerators(TArray<FGeneratorReading>{ Coal }, At);
		F.tick(3);
		TestTrue(TEXT("exactly 1.5 x the factory cadence old it is still sent"), F.Assembler.TakeGenerators(At + 45, 30).IsValid());
	}
	{
		// A newer fragment is built while an older one is untaken: the older is taken once, then the newer.
		FSnapshotAssemblerFixture F;
		F.Assembler.SubmitNativeGenerators(TArray<FGeneratorReading>{ Coal }, F.T);
		F.tick(3);
		F.Worker.bHold = true;
		F.Assembler.SubmitNativeGenerators(TArray<FGeneratorReading>{ Fuel }, F.T);
		F.tick();
		TestTrue(TEXT("the older fragment is sent while the newer is built"), F.Assembler.TakeGenerators(F.T, 30)->Contains(TEXT("Gen_Coal_1")));
		TestFalse(TEXT("and only once"), F.Assembler.TakeGenerators(F.T, 30).IsValid());
		F.Worker.release();
		F.tick(2);
		const FString Newer = *F.Assembler.TakeGenerators(F.T, 30);
		TestTrue(TEXT("then the newer one"), Newer.Contains(TEXT("Gen_Fuel_2")) && !Newer.Contains(TEXT("Gen_Coal_1")));
	}
	{
		// Untaken, a newer read replaces the older fragment.
		FSnapshotAssemblerFixture F;
		F.Assembler.SubmitNativeGenerators(TArray<FGeneratorReading>{ Coal }, F.T);
		F.tick(3);
		F.Assembler.SubmitNativeGenerators(TArray<FGeneratorReading>{ Fuel }, F.T);
		F.tick(3);
		const FString Newest = *F.Assembler.TakeGenerators(F.T, 30);
		TestTrue(TEXT("only the newest read is sent"), Newest.Contains(TEXT("Gen_Fuel_2")) && !Newest.Contains(TEXT("Gen_Coal_1")));
	}
	{
		// The source flips to bridge: neither a held fragment nor a pending job survives DropGenerators. Stop drops them too.
		FSnapshotAssemblerFixture F;
		F.Assembler.SubmitNativeGenerators(TArray<FGeneratorReading>{ Coal }, F.T);
		F.tick(3);
		F.Assembler.SubmitNativeGenerators(TArray<FGeneratorReading>{ Fuel }, F.T);
		F.Assembler.DropGenerators();
		F.tick(3);
		TestFalse(TEXT("a dropped fragment and job are gone"), F.Assembler.TakeGenerators(F.T, 30).IsValid());
		F.Assembler.SubmitNativeGenerators(TArray<FGeneratorReading>{ Coal }, F.T);
		F.tick(3);
		F.Assembler.Stop();
		TestFalse(TEXT("Stop forgets the held fragment"), F.Assembler.TakeGenerators(F.T, 30).IsValid());
	}
	{
		// Both fragments and the factory part are in flight together: the part comes out with the extractors, the generators are held.
		FSnapshotAssemblerFixture F;
		F.Assembler.SubmitNativeFactory(TArray<FFactoryReading>{ Machine }, F.T);
		F.Assembler.SubmitNativeExtractors(TArray<FExtractorReading>{ Miner }, F.T);
		F.Assembler.SubmitNativeGenerators(TArray<FGeneratorReading>{ Coal }, F.T);
		F.tick(6);
		TestTrue(TEXT("the factory part carries the extractors"), F.Assembler.TakeFactory(F.T, 30).Get(FString()).Contains(TEXT(",\"extractors\":[{\"id\":\"Build_MinerMk1_C_1\"")));
		TestTrue(TEXT("and the generators fragment is held for the power part"), F.Assembler.TakeGenerators(F.T, 30)->StartsWith(TEXT("[{\"id\":\"Gen_Coal_1\"")));
	}
	{
		// An empty list is a real, empty fragment.
		FSnapshotAssemblerFixture F;
		F.Assembler.SubmitNativeGenerators(TArray<FGeneratorReading>(), F.T);
		F.tick(3);
		SatisDash::Test::SameText(*this, TEXT("an empty list is []"), *F.Assembler.TakeGenerators(F.T, 30), TEXT("[]"));
		TestFalse(TEXT("and the [] is sent once, not again"), F.Assembler.TakeGenerators(F.T, 30).IsValid());
	}
	return true;
}
