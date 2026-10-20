#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "SatisDash/Files.h"
#include "TestUtil.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFilesAtomicTest, "SatisDash.Core.Files.Atomic", SatisDash::Test::Flags)

bool FFilesAtomicTest::RunTest(const FString& Parameters)
{
	const SatisDash::Test::FTempDir Dir;
	const FString Path = Dir.Path / TEXT("SatisDash/out.json");
	TestTrue(TEXT("a plain write"), SatisDash::WriteFileAtomic(Path, TEXT("one")));
	SatisDash::Test::SameText(*this, TEXT("the text"), SatisDash::Test::ReadText(Path), TEXT("one"));
	TestTrue(TEXT("a replace"), SatisDash::WriteFileAtomic(Path, TEXT("two")));
	SatisDash::Test::SameText(*this, TEXT("the new text"), SatisDash::Test::ReadText(Path), TEXT("two"));
	TestFalse(TEXT("no temp file left"), IFileManager::Get().FileExists(*(Path + TEXT(".tmp"))));

	// A destination the move can't replace: a non-empty folder where the file should be. (A read-only file does not
	// work: the engine clears the attribute before deleting.) The default Move would retry for about 5.5 s and log an
	// error; the write must give up at once, leave no temp file, and say false.
	const FString Blocked = Dir.Path / TEXT("SatisDash/blocked.json");
	IFileManager::Get().MakeDirectory(*Blocked, true);
	SatisDash::Test::WriteText(Blocked / TEXT("inner.txt"), TEXT("x"));
	const double Start = FPlatformTime::Seconds();
	const bool bWritten = SatisDash::WriteFileAtomic(Blocked, TEXT("three"));
	const double Took = FPlatformTime::Seconds() - Start;
	TestFalse(TEXT("an unreplaceable destination fails"), bWritten);
	TestFalse(TEXT("no temp file left behind"), IFileManager::Get().FileExists(*(Blocked + TEXT(".tmp"))));
	TestTrue(TEXT("and it gives up in under a second"), Took < 1.0);
	return true;
}
