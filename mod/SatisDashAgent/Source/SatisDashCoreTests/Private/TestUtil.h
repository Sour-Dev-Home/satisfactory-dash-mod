#pragma once

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "SatisDash/Responses.h"

namespace SatisDash::Test
{
	/** A fake secret of the contract's shape. Never a real credential. */
	inline FString FakeSecret() { return FString::ChrN(42, TEXT('a')) + TEXT("_"); }

	inline const TCHAR* CadenceJson = TEXT("{\"statusSeconds\":5,\"powerSeconds\":5,\"factorySeconds\":30}");
	inline const TCHAR* SnapshotAnswerJson =
		TEXT("{\"cadence\":{\"statusSeconds\":5,\"powerSeconds\":5,\"factorySeconds\":30},\"commandsPending\":false}");

	inline FString EnrollJson()
	{
		return FString::Printf(TEXT("{\"agentSecret\":\"%s\",\"serverId\":\"srv-1\",\"cadence\":%s}"), *FakeSecret(), CadenceJson);
	}

	inline FHttpReply Reply(int32 Code, const FString& Json, const FString& ContentType)
	{
		FHttpReply Out;
		Out.Code = Code;
		Out.ContentType = ContentType;
		const FTCHARToUTF8 Utf8(*Json);
		Out.Body.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		return Out;
	}

	/** A JSON reply (check-api cannot see a function through a default argument with parentheses, so this is an overload). */
	inline FHttpReply Reply(int32 Code, const FString& Json)
	{
		return Reply(Code, Json, TEXT("application/json"));
	}

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
