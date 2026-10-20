#pragma once

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

namespace SatisDash::Test
{
	/** Every SatisDash test: fast, editor-only, no world. */
	inline constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** A case-sensitive text check (FAutomationTestBase's FString comparison is not). */
	inline bool SameText(FAutomationTestBase& Test, const TCHAR* What, const FString& Actual, const FString& Expected)
	{
		if (Actual.Equals(Expected, ESearchCase::CaseSensitive))
		{
			return true;
		}
		Test.AddError(FString::Printf(TEXT("%s: expected '%s', got '%s'"), What, *Expected, *Actual));
		return false;
	}

	/** A fresh folder under Saved/SatisDashTests, deleted when it goes out of scope. */
	struct FTempDir
	{
		FString Path = FPaths::ProjectSavedDir() / TEXT("SatisDashTests") / FGuid::NewGuid().ToString();
		FTempDir() { IFileManager::Get().MakeDirectory(*Path, true); }
		~FTempDir() { IFileManager::Get().DeleteDirectory(*Path, false, true); }
		FString File(const TCHAR* Name) const { return Path / Name; }
	};

	inline FString ReadText(const FString& Path)
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *Path);
		return Text;
	}

	inline void WriteText(const FString& Path, const FString& Text)
	{
		FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
}
