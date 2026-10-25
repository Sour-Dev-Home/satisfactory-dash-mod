#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Requests.h"
#include "SatisDash/Responses.h"

namespace SatisDash
{
	/** FRM bridge spec "Load spreading": live reads always go first; world reads only when no live read is due. */
	enum class EFrmLane : uint8 { Live, World };

	inline constexpr int64 FrmSmallCapBytes = 4 * 1024 * 1024;

	/** One FRM endpoint and how it is read. */
	struct FFrmEndpoint
	{
		const TCHAR* Name;
		EFrmLane Lane;
		/** A large body: hashed and parsed in slices on the worker, and at most one held at a time; FFrmQueue never starts a large read while one is held (spec "Memory"). */
		bool bLarge;
		/** May start while a large body is being parsed (only getPlayer, spec "Load spreading"). */
		bool bMayPassLarge;
		int64 CapBytes;
		float TimeoutSeconds;
	};

	// FRM serves getPlayer on the game thread and getFactory on its own server thread (FicsitRemoteMonitoring.cpp:839, :896).
	inline constexpr FFrmEndpoint FrmGetPlayer{ TEXT("getPlayer"), EFrmLane::Live, false, true, FrmSmallCapBytes, 5.f };
	inline constexpr FFrmEndpoint FrmGetFactory{ TEXT("getFactory"), EFrmLane::Live, true, false, MaxRequestResponseBytes, 15.f };

	/** GET http://127.0.0.1:<Port>/<Endpoint.Name>, with X-FRM-Authorization only when Token isn't empty. */
	SATISDASHCORE_API FRequestSpec BuildFrmRequest(int32 Port, const FString& Token, const FFrmEndpoint& Endpoint);
}
