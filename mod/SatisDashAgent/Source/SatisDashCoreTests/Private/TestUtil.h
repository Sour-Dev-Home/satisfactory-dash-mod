#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

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
}
