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
#include "TaskWorker.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/HideWindowsPlatformTypes.h"
#elif PLATFORM_LINUX
#include <time.h>
#endif

namespace
{
	SatisDash::FDriverClock gameClock()
	{
		SatisDash::FDriverClock Clock;
		Clock.Now = [] { return FPlatformTime::Seconds(); };
		Clock.UtcNow = [] { return FDateTime::UtcNow(); };
		Clock.Random01 = [] { return double(FMath::FRand()); };
#if PLATFORM_WINDOWS
		// Owner, 2026-11-01 (tick-cost run 41334503941): the calling thread's CPU cycles, so a slow step can be told from a stalled one. Diagnostic only.
		Clock.ThreadCycles = []() -> TOptional<uint64>
		{
			ULONG64 Cycles = 0;
			if (QueryThreadCycleTime(GetCurrentThread(), &Cycles) == 0)
			{
				return {};
			}
			return TOptional<uint64>(uint64(Cycles));
		};
#elif PLATFORM_LINUX
		// Owner ruling D4, 2026-11-07 (Linux tick-cost run 42393340775): the calling thread's CPU time in nanoseconds, as
		// UE's own Unix timer reads it (UnixPlatformTime.cpp:186). The judge's waited-step rule compares each step's count per
		// ms with the same run's median, so the unit cancels: nanoseconds here, cycles on Windows. Diagnostic only.
		Clock.ThreadCycles = []() -> TOptional<uint64>
		{
			struct timespec ThreadTime;
			if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ThreadTime) != 0)
			{
				return {};
			}
			return TOptional<uint64>(uint64(ThreadTime.tv_sec) * 1000000000ull + uint64(ThreadTime.tv_nsec));
		};
#endif
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
		// A parse or encode still running finishes here: its job is shared with the task, never freed under it.
		Worker.Wait();
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
	// Declared before the driver, so the driver (and its bridge) go first and the worker last, after its task.
	SatisDash::FTaskWorker Worker;
	// Held by value (check-api excuses calls only on receivers declared with the plugin's own type, not a TUniquePtr), and
	// declared after the transport and the reader it holds references to, so it is destroyed before them. Its constructor
	// only stores its arguments; nothing reads or writes a file before Boot, which only a dedicated server reaches.
	SatisDash::FDriver Driver = SatisDash::FDriver(FPaths::ProjectDir() / TEXT("Configs/SatisDashAgent.cfg"),
		FPaths::ProjectSavedDir() / TEXT("SatisDash/credential.json"), Transport, Reader, Worker, gameClock());
	FDelegateHandle WorldInitHandle;
	FTSTicker::FDelegateHandle TickHandle;
	bool bCurlReady = false;
};

IMPLEMENT_MODULE(FSatisDashGameModule, SatisDashGame)
