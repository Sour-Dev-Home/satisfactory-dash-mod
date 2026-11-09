#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Config.h"
#include "SatisDash/Requests.h"
#include "SatisDash/Responses.h"

namespace SatisDash
{
	inline constexpr double PairingWindowSeconds = 3600.0;
	inline constexpr int32 PairingSlowDownSeconds = 5;
	inline constexpr double PairingRefusedRetrySeconds = 3600.0;

	/** Chat is posted only with exactly one player connected (owner ruling 2026-11-09). Players < 0 = unknown. */
	SATISDASHCORE_API bool ShouldPostCode(int32 Players, bool bNewCode, bool bJoined);
	/** The console and chat line for a code (Global Constraints wording). */
	SATISDASHCORE_API FString PairingCodeLine(const FAgentConfig& Config, const FString& UserCode, int32 ExpiresInSeconds);

	/** Device-code pairing (spec §1-§3). Pure logic: the agent drives it, the driver sends. */
	class SATISDASHCORE_API FPairing
	{
	public:
		explicit FPairing(TFunction<double()> InRandom01);
		/** Opens a window (boot, or a join after it closed), unless one opened in the last hour. */
		void Open(double Now);
		/** Drops everything (a cfg code, a credential, a revoke): no request after this until Open. */
		void Stop();
		bool IsWindowOpen(double Now) const;
		/** The next request, or unset (nothing due, one in flight, window closed). Exactly one Finish per returned request. */
		/** ServerName is called only when a start is built (a game read; never for polls). */
		TOptional<FRequestSpec> Next(const FAgentConfig& Config, TFunctionRef<FString()> ServerName, double Now);
		/** The reply to the last Next. Set when approved: the agent saves it like an enrol. */
		TOptional<FPairingPoll> Finish(const FHttpReply& Reply, double Now);
		/** The player count, about every 5 s; Players < 0 = unreadable. Reopens a closed window on a join to 1. */
		void OnPlayers(int32 Players, double Now);
		/** A chat line to post now, once. */
		TOptional<FString> TakeChatLine();

	private:
		enum class ENext : uint8 { Start, Poll };
		bool HasOpenCode(double Now) const;
		void ClearCode();
		void WaitBackoff(double Now);
		void WaitRetryAfter(const FHttpReply& Reply, double Now);

		TFunction<double()> random01;
		bool bStopped = true;
		bool bInFlight = false;
		ENext InFlightKind = ENext::Start;
		bool bInFlightNamed = false;
		/** Stop() during a request: its reply is ignored, and nothing else is sent until it arrives. */
		bool bDropReply = false;
		double WindowOpenedAt = -1e18;
		double WindowEndsAt = -1e18;
		double NextAt = 0;
		int32 Failures = 0;
		bool bSendName = true;
		bool bClosedLogged = false;
		bool bRefusedLogged = false;
		bool bBadStartLogged = false;
		// The open pairing. Secret: memory only, never logged.
		FString Secret;
		FString UserCode;
		double CodeExpiresAt = 0;
		int32 IntervalSeconds = 5;
		int32 LastPlayers = -1;
		FAgentConfig LastConfig; // for the chat line's URL on a join
		TOptional<FString> PendingChat;
		int32 PlayersNow = -1;
	};
}
