#include "SatisDash/Files.h"

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
		Files.Delete(*Temp, false, false, true);
		return false;
	}
	return true;
}
