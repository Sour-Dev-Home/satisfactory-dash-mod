#include "SatisDash/Files.h"

#include "SatisDash/Log.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if PLATFORM_LINUX
#include <sys/stat.h>
#endif

bool SatisDash::WriteFileAtomic(const FString& Path, const FString& Text)
{
	IFileManager& Files = IFileManager::Get();
	if (!Files.MakeDirectory(*FPaths::GetPath(Path), true))
	{
		return false;
	}
	const FString Temp = Path + TEXT(".tmp");
	if (!FFileHelper::SaveStringToFile(Text, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		return false;
	}
#if PLATFORM_LINUX
	if (chmod(TCHAR_TO_UTF8(*Temp), S_IRUSR | S_IWUSR) != 0)
	{
		Files.Delete(*Temp, false, false, true);
		return false;
	}
#endif
	// bDoNotRetryOrError: the default Move sleeps 0.5 s after a failed delete and up to 10 x 0.5 s after a failed move,
	// on the game thread, and logs an Error. One attempt; the next save tries again.
	if (!Files.Move(*Path, *Temp, true, false, false, true))
	{
		// Move deletes the destination before it renames. If that delete worked and the rename did not, the .tmp is the
		// only copy of the new contents: keep it (FCredentialStore::Load recovers it). Delete it only while the
		// destination (a file, or a folder in the way) is still there.
		if (Files.FileExists(*Path) || Files.DirectoryExists(*Path))
		{
			Files.Delete(*Temp, false, false, true);
		}
		else
		{
			UE_LOG(LogSatisDash, Warning, TEXT("Could not replace %s; the new contents stay in the .tmp file next to it."), *FPaths::GetCleanFilename(Path));
		}
		return false;
	}
	return true;
}
