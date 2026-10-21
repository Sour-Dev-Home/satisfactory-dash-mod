#include "Containers/Ticker.h"
#include "CurlTransport.h"
#include "Engine/World.h"
#include "GameReader.h"
#include "HAL/PlatformTime.h"
#include "Misc/CoreMisc.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "SatisDash/Driver.h"
#include "SatisDash/Log.h"
#include "SatisDashCurl.h"

namespace
{
	SatisDash::FDriverClock gameClock()
	{
		SatisDash::FDriverClock Clock;
		Clock.Now = [] { return FPlatformTime::Seconds(); };
		Clock.UtcNow = [] { return FDateTime::UtcNow(); };
		Clock.Random01 = [] { return double(FMath::FRand()); };
		return Clock;
	}
}

/** Plan 2B Task 6: the game ticks the driver, which owns enrolment, sampling, pushing and commands (Driver.h). */
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
		// The entry map is a game world too; each loaded save is a new one (S6). The reader always reads the newest.
		WorldInitHandle = FWorldDelegates::OnPostWorldInitialization.AddLambda([this](UWorld* InWorld, const UWorld::InitializationValues)
		{
			if (InWorld != nullptr && InWorld->IsGameWorld())
			{
				Reader.SetWorld(InWorld);
			}
		});
		Driver.Boot();
		// The core ticker runs on the game thread (spec "Runtime"); a tick costs a few comparisons unless something is due.
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("SatisDashAgent"), 0.5f, [this](float)
		{
			Driver.Tick();
			return true;
		});
	}

	virtual void ShutdownModule() override
	{
		// The driver is never ticked again from here on: its ticker goes first, then the transport drops every handler
		// without calling it (CurlTransport.h), so no reply runs into a driver that has stopped. The two are destroyed
		// together with the module (Driver.h: never cancel and keep ticking).
		FTSTicker::RemoveTicker(TickHandle);
		FWorldDelegates::OnPostWorldInitialization.Remove(WorldInitHandle);
		// Every easy handle and the multi handle are released here, before the mod's libcurl copy is cleaned up.
		Transport.CancelAll();
		if (bCurlReady)
		{
			bCurlReady = false;
			curl_global_cleanup();
		}
	}

private:
	SatisDash::FCurlTransport Transport;
	SatisDash::FGameReader Reader;
	// Held by value (check-api excuses calls only on receivers declared with the plugin's own type, not a TUniquePtr), and
	// declared after the transport and the reader it holds references to, so it is destroyed before them. Its constructor
	// only stores its arguments; nothing reads or writes a file before Boot, which only a dedicated server reaches.
	SatisDash::FDriver Driver = SatisDash::FDriver(FPaths::ProjectDir() / TEXT("Configs/SatisDashAgent.cfg"),
		FPaths::ProjectSavedDir() / TEXT("SatisDash/credential.json"), Transport, Reader, gameClock());
	FDelegateHandle WorldInitHandle;
	FTSTicker::FDelegateHandle TickHandle;
	bool bCurlReady = false;
};

IMPLEMENT_MODULE(FSatisDashGameModule, SatisDashGame)
