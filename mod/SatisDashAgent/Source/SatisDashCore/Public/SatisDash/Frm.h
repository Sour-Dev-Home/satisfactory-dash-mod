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
	inline constexpr FFrmEndpoint FrmGetTrains{ TEXT("getTrains"), EFrmLane::Live, false, false, FrmSmallCapBytes, 5.f };
	inline constexpr FFrmEndpoint FrmGetTrainStation{ TEXT("getTrainStation"), EFrmLane::Live, false, false, FrmSmallCapBytes, 5.f };

	// Both are large (the captured world's 608 nodes are about 288 KB; a big world's rails more), so they are world-lane reads
	// that never start while another large body is held. FRM runs getTrainRails on its own server thread and getResourceNode
	// on the game thread (spec "Load spreading"), which is why the node read has the longest cadence. The 15 s timeout is the
	// mod's; the Node agent uses 5 s for every FRM read (connection.ts:47). The cap is the spec's "4 MB for the rest".
	inline constexpr FFrmEndpoint FrmGetTrainRails{ TEXT("getTrainRails"), EFrmLane::World, true, false, FrmSmallCapBytes, 15.f };
	inline constexpr FFrmEndpoint FrmGetResourceNode{ TEXT("getResourceNode"), EFrmLane::World, true, false, FrmSmallCapBytes, 15.f };

	/** GET http://127.0.0.1:<Port>/<Endpoint.Name>, with X-FRM-Authorization only when Token isn't empty. */
	SATISDASHCORE_API FRequestSpec BuildFrmRequest(int32 Port, const FString& Token, const FFrmEndpoint& Endpoint);
}
