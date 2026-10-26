#include "SatisDash/FrmWorld.h"
#include "SatisDash/Native/ResourceNodes/NativeResourceNodes.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	const TCHAR* FrmWorldAt = TEXT("2026-10-24T12:00:00.000Z");
	const TCHAR* FrmWorldRails = TEXT("[{\"ID\":\"R1\",\"SplineData\":[{\"x\":0,\"y\":0},{\"x\":300,\"y\":0}]}]");
	const TCHAR* FrmWorldData = TEXT("[{\"id\":\"R1\",\"points\":[[0,0],[3,0]]}]");

	struct FFrmWorldRun
	{
		FWorldOutcome Outcome;
		int32 Steps = 0;
	};

	// No default arguments: check-api's function-definition scan does not accept a default value with parentheses.
	FFrmWorldRun RunWorldOf(const FString& Body, TOptional<FBlake3Hash> LastRaw, TOptional<FBlake3Hash> LastData, int64 MaxBytes)
	{
		FWorldRead Work{ EWorldLayer::Rails, SatisDash::Test::Utf8(Body), FString(FrmWorldAt), LastRaw, LastData, MaxBytes };
		FFrmWorldRun Run;
		bool bDone = false;
		while (!bDone)
		{
			bDone = Work.Step();
			++Run.Steps;
		}
		Run.Outcome = Work.TakeOutcome();
		return Run;
	}

	FFrmWorldRun RunWorldOf(const FString& Body, TOptional<FBlake3Hash> LastRaw, TOptional<FBlake3Hash> LastData)
	{
		return RunWorldOf(Body, LastRaw, LastData, WorldLayerMaxBytes);
	}

	FFrmWorldRun RunWorldOf(const FString& Body)
	{
		return RunWorldOf(Body, TOptional<FBlake3Hash>(), TOptional<FBlake3Hash>(), WorldLayerMaxBytes);
	}

	FBlake3Hash FrmWorldRawHash(const FString& Body)
	{
		const TArray<uint8> Bytes = SatisDash::Test::Utf8(Body);
		return FBlake3::HashBuffer(Bytes.GetData(), uint64(Bytes.Num()));
	}

	/** What the data hash covers: the data array text, as UTF-16, never observedAt. */
	FBlake3Hash FrmWorldDataHash(const FString& DataText)
	{
		return FBlake3::HashBuffer(*DataText, uint64(DataText.Len()) * sizeof(TCHAR));
	}

	FString FrmWorldText(const FEncodedBody& Body)
	{
		const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Body.Bytes.GetData()), Body.Bytes.Num());
		return FString::ConstructFromPtrSize(Converted.Get(), Converted.Length());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmWorldReadTest, "SatisDash.Core.FrmWorld.Read", SatisDash::Test::Flags)

bool FFrmWorldReadTest::RunTest(const FString& Parameters)
{
	using EKind = FWorldOutcome::EKind;
	const FFrmWorldRun First = RunWorldOf(FrmWorldRails);
	TestTrue(TEXT("the first read of a layer is Ready"), First.Outcome.Kind == EKind::Ready);
	TestEqual(TEXT("one item"), First.Outcome.Items, 1);
	TestEqual(TEXT("a hash step, then the parse and finish"), First.Steps, 2);
	SatisDash::Test::SameText(*this, TEXT("the body is the Node agent's { observedAt, data }, not gzipped under 1 KB"), FrmWorldText(First.Outcome.Body),
		FString::Printf(TEXT("{\"observedAt\":\"%s\",\"data\":%s}"), FrmWorldAt, FrmWorldData));
	TestTrue(TEXT("the raw hash is of the raw bytes"), First.Outcome.RawHash == FrmWorldRawHash(FrmWorldRails));
	TestTrue(TEXT("the data hash is of the data array text only"), First.Outcome.DataHash == FrmWorldDataHash(FrmWorldData));
	TestEqual(TEXT("the byte count is the whole body's"), First.Outcome.Bytes, int64(FTCHARToUTF8(*FrmWorldText(First.Outcome.Body)).Length()));

	// The same raw body: one hash step, no parse, nothing to send.
	const FFrmWorldRun Same = RunWorldOf(FrmWorldRails, First.Outcome.RawHash, First.Outcome.DataHash);
	TestTrue(TEXT("same raw: SameRaw"), Same.Outcome.Kind == EKind::SameRaw);
	TestEqual(TEXT("and only the hash step ran: no parse"), Same.Steps, 1);
	TestTrue(TEXT("no body"), Same.Outcome.Body.Bytes.Num() == 0 && Same.Outcome.Items == 0);

	// A different raw body that maps to the same data (an extra z): parsed, but not sent.
	const FString Noisy = TEXT("[{\"ID\":\"R1\",\"SplineData\":[{\"x\":0,\"y\":0,\"z\":5},{\"x\":300,\"y\":0,\"z\":7}]}]");
	const FFrmWorldRun SameData = RunWorldOf(Noisy, First.Outcome.RawHash, First.Outcome.DataHash);
	TestTrue(TEXT("same mapped data: SameData"), SameData.Outcome.Kind == EKind::SameData);
	TestTrue(TEXT("with its own raw hash, to be remembered"), SameData.Outcome.RawHash == FrmWorldRawHash(Noisy) && !(SameData.Outcome.RawHash == First.Outcome.RawHash));
	TestEqual(TEXT("it was parsed"), SameData.Steps, 2);
	TestTrue(TEXT("and has no body"), SameData.Outcome.Body.Bytes.Num() == 0);

	// A layer that changed is Ready.
	const FFrmWorldRun Changed = RunWorldOf(TEXT("[{\"ID\":\"R1\",\"SplineData\":[{\"x\":0,\"y\":0},{\"x\":600,\"y\":0}]}]"), First.Outcome.RawHash, First.Outcome.DataHash);
	TestTrue(TEXT("changed data is Ready"), Changed.Outcome.Kind == EKind::Ready && !(Changed.Outcome.DataHash == First.Outcome.DataHash));
	// The first read after start always sends, even an empty layer.
	const FFrmWorldRun Empty = RunWorldOf(TEXT("[]"));
	SatisDash::Test::SameText(*this, TEXT("an empty layer is Ready"), FrmWorldText(Empty.Outcome.Body), FString::Printf(TEXT("{\"observedAt\":\"%s\",\"data\":[]}"), FrmWorldAt));
	// A bad body.
	TestTrue(TEXT("not an array: Invalid"), RunWorldOf(TEXT("{}")).Outcome.Kind == EKind::Invalid);
	TestTrue(TEXT("an invalid record: Invalid"), RunWorldOf(TEXT("[{\"ID\":\"\"}]")).Outcome.Kind == EKind::Invalid);
	TestTrue(TEXT("empty: Invalid"), RunWorldOf(FString()).Outcome.Kind == EKind::Invalid);
	// Dropped and over-cap items are reported.
	const FFrmWorldRun Dropped = RunWorldOf(TEXT("[{\"ID\":\"R1\",\"SplineData\":[]}]"));
	TestTrue(TEXT("a dropped segment is counted and the empty layer still Ready"), Dropped.Outcome.Kind == EKind::Ready && Dropped.Outcome.Dropped == 1);

	// 2,000,000 bytes of the whole body, before gzip: exactly the cap is allowed, one more is not.
	const int64 Exact = First.Outcome.Bytes;
	TestTrue(TEXT("exactly the cap: Ready"), RunWorldOf(FrmWorldRails, TOptional<FBlake3Hash>(), TOptional<FBlake3Hash>(), Exact).Outcome.Kind == EKind::Ready);
	const FFrmWorldRun Over = RunWorldOf(FrmWorldRails, TOptional<FBlake3Hash>(), TOptional<FBlake3Hash>(), Exact - 1);
	TestTrue(TEXT("one byte over: OverCap, with no body"), Over.Outcome.Kind == EKind::OverCap && Over.Outcome.Body.Bytes.Num() == 0);
	TestTrue(TEXT("and its hashes, so it can be remembered"), Over.Outcome.RawHash == First.Outcome.RawHash && Over.Outcome.DataHash == First.Outcome.DataHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmWorldSlicesTest, "SatisDash.Core.FrmWorld.Slices", SatisDash::Test::Flags)

bool FFrmWorldSlicesTest::RunTest(const FString& Parameters)
{
	// 1.1 MB of raw body is two hash slices (1 MiB each), then the parse: three steps. The sliced hash equals the one-shot one.
	const FString Big = TEXT("[{\"ID\":\"R1\",\"Pad\":\"") + FString::ChrN(1100000, TEXT('a')) + TEXT("\",\"SplineData\":[{\"x\":0,\"y\":0},{\"x\":300,\"y\":0}]}]");
	const FFrmWorldRun Run = RunWorldOf(Big);
	TestEqual(TEXT("two hash slices and a parse"), Run.Steps, 3);
	TestTrue(TEXT("Ready"), Run.Outcome.Kind == FWorldOutcome::EKind::Ready);
	TestTrue(TEXT("the sliced raw hash equals the one-shot hash"), Run.Outcome.RawHash == FrmWorldRawHash(Big));
	TestEqual(TEXT("the pad was skipped, not stored: the data is one rail"), Run.Outcome.Items, 1);
	// A body under 1 MiB is a single hash step.
	TestEqual(TEXT("a small body: one hash step and a parse"), RunWorldOf(FrmWorldRails).Steps, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmWorldNativeTest, "SatisDash.Core.FrmWorld.Native", SatisDash::Test::Flags)

bool FFrmWorldNativeTest::RunTest(const FString& Parameters)
{
	TArray<FResourceNodeReading> In;
	FResourceNodeReading R;
	R.Resource = TEXT("Iron Ore");
	R.Purity = ENodePurity::Normal;
	R.Kind = ENodeKind::Node;
	R.XCm = 100;
	R.YCm = 200;
	In.Add(R);

	const FNativeWorldBody Body = BuildNativeResourceNodes(TEXT("T"), In);
	FBlake3 Hasher;
	Hasher.Update(*Body.Json + Body.DataFrom, uint64(Body.DataEnd - Body.DataFrom) * sizeof(TCHAR));
	const FBlake3Hash Expected = Hasher.Finalize();

	// The readings go in and the worker builds the body: one Step does the build, the hash and the encode.
	FWorldRead NativeRead{ EWorldLayer::ResourceNodes, In, FString(TEXT("T")), TOptional<FBlake3Hash>() };
	TestTrue(TEXT("a native read is one step"), NativeRead.Step());
	FWorldOutcome Ready = NativeRead.TakeOutcome();
	TestTrue(TEXT("ready to post"), Ready.Kind == FWorldOutcome::EKind::Ready);
	TestEqual(TEXT("one item"), Ready.Items, 1);
	TestEqual(TEXT("the body's bytes"), Ready.Bytes, Body.Bytes);
	TestTrue(TEXT("the data hash covers exactly the array text, as for an FRM body"), Ready.DataHash == Expected);
	TestTrue(TEXT("a body to post"), Ready.Body.Bytes.Num() > 0);

	FWorldRead NativeSameRead{ EWorldLayer::ResourceNodes, In, FString(TEXT("T2")), TOptional<FBlake3Hash>(Expected) };
	TestTrue(TEXT("same data, one step"), NativeSameRead.Step());
	TestTrue(TEXT("a new observedAt over the same nodes is not a change"), NativeSameRead.TakeOutcome().Kind == FWorldOutcome::EKind::SameData);

	FWorldRead NativeOverRead{ EWorldLayer::ResourceNodes, In, FString(TEXT("T")), TOptional<FBlake3Hash>(), 10 };
	TestTrue(TEXT("over the cap, one step"), NativeOverRead.Step());
	TestTrue(TEXT("over the byte cap is skipped"), NativeOverRead.TakeOutcome().Kind == FWorldOutcome::EKind::OverCap);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrmWorldLayerTableTest, "SatisDash.Core.FrmWorld.LayerTable", SatisDash::Test::Flags)
bool FFrmWorldLayerTableTest::RunTest(const FString&)
{
	struct FExpected { EWorldLayer Layer; const TCHAR* Name; bool bHasEndpoint; double First; double Interval; double NativeFirst; };
	const FExpected Table[] = {
		{ EWorldLayer::Rails, TEXT("rails"), true, 120.0, 600.0, 480.0 },
		{ EWorldLayer::ResourceNodes, TEXT("resourceNodes"), true, 480.0, 1800.0, 0.0 },
		{ EWorldLayer::TrainRoutes, TEXT("trainRoutes"), false, 480.0, 600.0, 0.0 },
	};
	TestEqual(TEXT("three layers"), WorldLayerCount, 3);
	for (const FExpected& E : Table)
	{
		const FWorldLayerSpec& Spec = WorldLayer(E.Layer);
		TestEqual(*FString::Printf(TEXT("%s name"), E.Name), FString(Spec.Name), FString(E.Name));
		TestEqual(*FString::Printf(TEXT("%s has an endpoint"), E.Name), Spec.Endpoint != nullptr, E.bHasEndpoint);
		TestEqual(*FString::Printf(TEXT("%s first"), E.Name), Spec.FirstSeconds, E.First);
		TestEqual(*FString::Printf(TEXT("%s interval"), E.Name), Spec.IntervalSeconds, E.Interval);
		TestEqual(*FString::Printf(TEXT("%s native first"), E.Name), Spec.NativeFirstSeconds, E.NativeFirst);
	}
	return true;
}
