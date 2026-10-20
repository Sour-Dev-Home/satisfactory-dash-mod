#include "Containers/Ticker.h"
#include "CurlTransport.h"
#include "HAL/PlatformTime.h"
#include "Misc/CoreMisc.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "SatisDash/Agent.h"
#include "SatisDash/Log.h"
#include "SatisDash/Pusher.h"
#include "SatisDash/Snapshot.h"
#include "SatisDash/Version.h"
#include "SatisDashCurl.h"

namespace
{
	double random01() { return double(FMath::FRand()); }
}

/** Task 1's wiring: enrols and pushes empty snapshots, so the HTTP probe can measure the transport. Task 6 replaces it. */
class FSatisDashGameModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// The editor loads this module for the unit-test build; only a dedicated server runs the agent.
		if (!IsRunningDedicatedServer())
		{
			return;
		}
		// libcurl is a static library (libcurl.Build.cs:47, an External module), so this DLL links its own copy; the
		// engine's curl_global_init_mem (CurlHttpManager.cpp:143) initialises only the HTTP module's copy, and HTTP's DLL
		// exports no curl symbol. This copy is the mod's to initialise, once, here on the game thread before any request,
		// and to clean up at shutdown. If it can't start, the agent stays idle (fail closed).
		if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK)
		{
			UE_LOG(LogSatisDash, Warning, TEXT("Couldn't initialise libcurl; the agent stays idle"));
			return;
		}
		bCurlReady = true;
		Agent.Boot();
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("SatisDashProbe"), 0.5f, [this](float) { Tick(); return true; });
	}

	virtual void ShutdownModule() override
	{
		FTSTicker::RemoveTicker(TickHandle);
		// Every easy handle and the multi handle are released here, before the mod's libcurl copy is cleaned up.
		Transport.CancelAll();
		if (bCurlReady)
		{
			bCurlReady = false;
			curl_global_cleanup();
		}
	}

private:
	void Tick()
	{
		const double Now = FPlatformTime::Seconds();
		if (Now >= NextConfigAt)
		{
			NextConfigAt = Now + 60.0;
			Agent.CheckConfig();
		}
		if (TOptional<SatisDash::FRequestSpec> Enroll = Agent.StartEnroll(Now))
		{
			Transport.Send(*Enroll, [this](const SatisDash::FHttpReply& Reply) { Agent.FinishEnroll(Reply, FPlatformTime::Seconds()); });
		}
		const SatisDash::FCredential* Credential = Agent.GetCredential();
		if (Credential == nullptr)
		{
			return;
		}
		if (Now >= NextSnapshotAt)
		{
			NextSnapshotAt = Now + 5.0;
			SatisDash::FSnapshot Snapshot;
			Snapshot.AgentVersion = SatisDash::AgentVersion();
			Snapshot.ObservedAt = FDateTime::UtcNow();
			Pusher.Enqueue(SatisDash::SnapshotToJson(Snapshot));
		}
		if (bSending)
		{
			return;
		}
		if (TOptional<FString> Json = Pusher.StartSend(Now))
		{
			bSending = true;
			Transport.Send(SatisDash::BuildSnapshotRequest(Agent.GetConfig(), *Credential, *Json), [this](const SatisDash::FHttpReply& Reply)
			{
				bSending = false;
				if (Pusher.FinishSend(Reply, FPlatformTime::Seconds()).bHalt)
				{
					Agent.OnAuthRejected();
				}
			});
		}
	}

	SatisDash::FCurlTransport Transport;
	// Held by value (check-api excuses calls only on receivers declared with the plugin's own type, not a TUniquePtr).
	// Their constructors only store their arguments; nothing reads or writes a file before Boot, which only a dedicated
	// server reaches.
	SatisDash::FAgent Agent = SatisDash::FAgent(FPaths::ProjectDir() / TEXT("Configs/SatisDashAgent.cfg"),
		FPaths::ProjectSavedDir() / TEXT("SatisDash/credential.json"), [] { return random01(); });
	SatisDash::FPusher Pusher = SatisDash::FPusher([] { return random01(); });
	FTSTicker::FDelegateHandle TickHandle;
	double NextConfigAt = 0;
	double NextSnapshotAt = 0;
	bool bSending = false;
	bool bCurlReady = false;
};

IMPLEMENT_MODULE(FSatisDashGameModule, SatisDashGame)
