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
	return Files.Move(*Path, *Temp, true);
}
