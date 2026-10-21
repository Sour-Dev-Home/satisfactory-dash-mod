#pragma once

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/OutputDevice.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "SatisDash/Log.h"
#include "SatisDash/Responses.h"

namespace SatisDash::Test
{
	/** A fake secret of the contract's shape. Never a real credential. */
	inline FString FakeSecret() { return FString::ChrN(42, TEXT('a')) + TEXT("_"); }

	/** {"a":{"a":...1...}}: Levels objects deep. */
	inline FString NestedObjects(int32 Levels)
	{
		FString Text;
		for (int32 I = 0; I < Levels; ++I)
		{
			Text += TEXT("{\"a\":");
		}
		return Text + TEXT("1") + FString::ChrN(Levels, TEXT('}'));
	}

	inline const TCHAR* CadenceJson =TEXT("{\"statusSeconds\":5,\"powerSeconds\":5,\"factorySeconds\":30}");
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

	/** Collects every LogSatisDash line written while it lives. */
	class FLogCapture : public FOutputDevice
	{
	public:
		FLogCapture() { GLog->AddOutputDevice(this); }
		virtual ~FLogCapture() override { GLog->RemoveOutputDevice(this); }
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Category == LogSatisDash.GetCategoryName())
			{
				FScopeLock Lock(&Guard);
				Lines.Add(Text);
				Levels.Add(static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask));
			}
		}
		TArray<ELogVerbosity::Type> Verbosities()
		{
			GLog->Flush();
			FScopeLock Lock(&Guard);
			return Levels;
		}
		TArray<FString> Take()
		{
			GLog->Flush();
			FScopeLock Lock(&Guard);
			return Lines;
		}

	private:
		FCriticalSection Guard;
		TArray<FString> Lines;
		TArray<ELogVerbosity::Type> Levels;
	};

	/** Global Constraints: Display or Warning, never Error or Fatal. */
	inline bool NoErrorLines(FAutomationTestBase& Test, FLogCapture& Capture)
	{
		const TArray<FString> Lines = Capture.Take();
		const TArray<ELogVerbosity::Type> Levels = Capture.Verbosities();
		for (int32 I = 0; I < Levels.Num(); ++I)
		{
			if (Levels[I] <= ELogVerbosity::Error)
			{
				Test.AddError(FString::Printf(TEXT("logged at Error: %s"), Lines.IsValidIndex(I) ? *Lines[I] : TEXT("?")));
				return false;
			}
		}
		return true;
	}
}
