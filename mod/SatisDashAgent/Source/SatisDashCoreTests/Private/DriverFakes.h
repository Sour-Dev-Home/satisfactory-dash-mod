#pragma once

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "SatisDash/Config.h"
#include "SatisDash/Driver.h"
#include "SatisDash/Ports.h"
#include "TestUtil.h"

namespace SatisDash::Test
{
	/** Holds every request until the test answers it, so the order of replies is the test's to choose. */
	struct FFakeTransport : public SatisDash::ITransport
	{
		struct FCall
		{
			SatisDash::FRequestSpec Spec;
			SatisDash::FReplyHandler OnReply;
		};
		TArray<FCall> Open;
		TArray<SatisDash::FRequestSpec> Sent;

		virtual void Send(const SatisDash::FRequestSpec& Spec, SatisDash::FReplyHandler OnReply) override
		{
			Sent.Add(Spec);
			Open.Add({ Spec, MoveTemp(OnReply) });
		}

		/** Unanswered requests whose URL path ends with Path. */
		int32 pending(const TCHAR* Path) const
		{
			int32 N = 0;
			for (const FCall& Call : Open)
			{
				N += Call.Spec.Url.EndsWith(Path, ESearchCase::CaseSensitive) ? 1 : 0;
			}
			return N;
		}

		int32 sent(const TCHAR* Path) const
		{
			int32 N = 0;
			for (const SatisDash::FRequestSpec& Spec : Sent)
			{
				N += Spec.Url.EndsWith(Path, ESearchCase::CaseSensitive) ? 1 : 0;
			}
			return N;
		}

		/** Answers the oldest open request for Path; false when there is none. */
		bool answer(const TCHAR* Path, const SatisDash::FHttpReply& Reply)
		{
			for (int32 I = 0; I < Open.Num(); ++I)
			{
				if (Open[I].Spec.Url.EndsWith(Path, ESearchCase::CaseSensitive))
				{
					FCall Call = MoveTemp(Open[I]);
					Open.RemoveAt(I);
					SatisDash::FHttpReply Mutable = Reply; // handlers take a mutable reply (they may MoveTemp the body)
					Call.OnReply(Mutable);
					return true;
				}
			}
			return false;
		}

		/** The body of the newest request for Path, as text ("" when none). */
		FString lastBody(const TCHAR* Path) const
		{
			for (int32 I = Sent.Num() - 1; I >= 0; --I)
			{
				if (Sent[I].Url.EndsWith(Path, ESearchCase::CaseSensitive))
				{
					const FUTF8ToTCHAR Text = FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(Sent[I].Body.GetData()), Sent[I].Body.Num());
					return FString::ConstructFromPtrSize(Text.Get(), Text.Length());
				}
			}
			return FString();
		}

		/** The Authorization header of the newest request for Path ("" when none). */
		FString lastAuth(const TCHAR* Path) const
		{
			for (int32 I = Sent.Num() - 1; I >= 0; --I)
			{
				if (!Sent[I].Url.EndsWith(Path, ESearchCase::CaseSensitive))
				{
					continue;
				}
				for (const TPair<FString, FString>& Header : Sent[I].Headers)
				{
					if (Header.Key.Equals(TEXT("Authorization"), ESearchCase::CaseSensitive))
					{
						return Header.Value;
					}
				}
				return FString();
			}
			return FString();
		}
	};

	struct FFakeGame : public SatisDash::IGameReader
	{
		TOptional<SatisDash::FStatusReading> Status;
		TOptional<SatisDash::FAutoPauseReading> AutoPause = SatisDash::FAutoPauseReading();
		TOptional<TArray<SatisDash::FCircuitReading>> Power;
		bool bApplyWorks = true;
		/** The server defers the change: it lands in the pending options, the applied value stays. */
		bool bApplyDefers = false;
		TArray<bool> Applied;
		int32 PowerReads = 0;
		/** R1: unset = the game state can't be read. */
		TOptional<TArray<SatisDash::FPlayerReading>> Players;
		int32 PlayerReads = 0;
		/** R4: what a finished scan returns; bHasWorld false = no world. A scan finishes on its StepsToFinish-th step. */
		TArray<SatisDash::FResourceNodeReading> Nodes;
		bool bHasWorld = true;
		int32 StepsToFinish = 3;
		int32 ScanBegins = 0;
		int32 ScanSteps = 0;
		/** R2a: what a finished factory read returns; bFactoryWorld false = no world or subsystem. A read finishes on its FactorySteps-th step. */
		TArray<SatisDash::FFactoryReading> Factory;
		bool bFactoryWorld = true;
		int32 FactorySteps = 3;
		int32 FactoryBegins = 0;
		int32 FactoryStepCalls = 0;

		FFakeGame()
		{
			SatisDash::FStatusReading S;
			S.bIsGameRunning = true;
			S.SessionName = TEXT("test");
			Status = S;
			Power = TArray<SatisDash::FCircuitReading>{ SatisDash::FCircuitReading() };
		}
		virtual TOptional<SatisDash::FStatusReading> ReadStatus() override { return Status; }
		virtual TOptional<SatisDash::FAutoPauseReading> ReadAutoPause() override { return AutoPause; }
		virtual TOptional<TArray<SatisDash::FCircuitReading>> ReadPower() override { ++PowerReads; return Power; }
		virtual TOptional<TArray<SatisDash::FPlayerReading>> ReadPlayers() override { ++PlayerReads; return Players; }
		virtual bool BeginResourceNodeScan() override { ++ScanBegins; ScanSteps = 0; return bHasWorld; }
		virtual TOptional<TArray<SatisDash::FResourceNodeReading>> StepResourceNodeScan(int32 MaxActors) override
		{
			return ++ScanSteps >= StepsToFinish ? TOptional<TArray<SatisDash::FResourceNodeReading>>(Nodes) : TOptional<TArray<SatisDash::FResourceNodeReading>>();
		}
		/** The read ends abandoned (a world swap): empty and flagged, as the game reader does. */
		bool bFactoryAbandon = false;
		int32 FactoryEnds = 0;
		virtual bool FactoryReadAbandoned() const override { return bFactoryAbandon; }
		virtual void EndFactoryRead() override { ++FactoryEnds; }
		virtual bool BeginFactoryRead() override { ++FactoryBegins; FactoryStepCalls = 0; return bFactoryWorld; }
		virtual TOptional<TArray<SatisDash::FFactoryReading>> StepFactoryRead(SatisDash::FStepBudget& Budget) override
		{
			return ++FactoryStepCalls >= FactorySteps ? TOptional<TArray<SatisDash::FFactoryReading>>(bFactoryAbandon ? TArray<SatisDash::FFactoryReading>() : Factory) : TOptional<TArray<SatisDash::FFactoryReading>>();
		}
		/** R3: what a finished trains read returns; bTrainsWorld false = no world or subsystem. A read finishes on its TrainsSteps-th step. */
		TArray<SatisDash::FTrainReading> Trains;
		TArray<SatisDash::FStationReading> Stations;
		bool bTrainsWorld = true;
		int32 TrainsSteps = 3;
		int32 TrainsBegins = 0;
		int32 TrainsStepCalls = 0;
		int32 TrainsEnds = 0;
		/** The read ends abandoned (a world swap): empty and flagged, as the game reader does. */
		bool bTrainsAbandon = false;
		/** A read is open (begun, not finished or ended): the real reader keeps one cursor, so a second Begin is refused. */
		bool bTrainsOpen = false;
		virtual bool BeginTrainsRead() override
		{
			++TrainsBegins;
			if (!bTrainsWorld || bTrainsOpen)
			{
				return false;
			}
			TrainsStepCalls = 0;
			bTrainsOpen = true;
			return true;
		}
		virtual TOptional<SatisDash::FTrainsSnapshot> StepTrainsRead(SatisDash::FStepBudget& Budget) override
		{
			Budget.Begin();
			Budget.End();
			if (++TrainsStepCalls < TrainsSteps)
			{
				return {};
			}
			bTrainsOpen = false;
			SatisDash::FTrainsSnapshot Out;
			if (!bTrainsAbandon)
			{
				Out.Trains = Trains;
				Out.Stations = Stations;
			}
			return Out;
		}
		virtual bool TrainsReadAbandoned() const override { return bTrainsAbandon; }
		virtual void EndTrainsRead() override { ++TrainsEnds; bTrainsOpen = false; }
		virtual bool IsTrainsReadOpen() const override { return bTrainsOpen; }
		/** R5: what a finished logistics pass returns. A pass finishes on its LogisticsStepsToFinish-th step. */
		int32 LogisticsStepsToFinish = 3;
		TArray<SatisDash::FRailReading> LogisticsRails;
		/** R6: what a pass that asked for conveyors returns (the rails and conveyors lists are each given only when asked, as the real reader's). */
		TArray<SatisDash::FConveyorReading> LogisticsConveyors;
		SatisDash::FLogisticsCounts LogisticsCounts;
		/** The pass ends abandoned (no subsystem, a world swap, or zero buildables): empty and flagged, as the game reader does. */
		bool bLogisticsAbandoned = false;
		bool bBeginLogisticsOk = true;
		int32 LogisticsBegins = 0;
		int32 LogisticsEnds = 0;
		int32 LogisticsStepCalls = 0;
		bool bLastBeginRails = false;
		bool bLastBeginConveyors = false;
		/** A pass is open (begun, not finished or ended): a second Begin is refused, as the real reader's. */
		bool bLogisticsOpen = false;
		/** Which layers the open pass was asked for. */
		bool bOpenRails = false;
		bool bOpenConveyors = false;
		virtual bool BeginLogisticsRead(bool bRails, bool bConveyors) override
		{
			++LogisticsBegins;
			bLastBeginRails = bRails;
			bLastBeginConveyors = bConveyors;
			if (!bBeginLogisticsOk || bLogisticsOpen)
			{
				return false;
			}
			LogisticsStepCalls = 0;
			bLogisticsOpen = true;
			bOpenRails = bRails;
			bOpenConveyors = bConveyors;
			return true;
		}
		virtual TOptional<SatisDash::FLogisticsSnapshot> StepLogisticsRead(SatisDash::FStepBudget& Budget) override
		{
			Budget.Begin();
			Budget.End();
			if (++LogisticsStepCalls < LogisticsStepsToFinish)
			{
				return {};
			}
			bLogisticsOpen = false;
			SatisDash::FLogisticsSnapshot Out;
			if (!bLogisticsAbandoned)
			{
				if (bOpenRails)
				{
					Out.Rails = LogisticsRails;
				}
				if (bOpenConveyors)
				{
					Out.Conveyors = LogisticsConveyors;
				}
				Out.Counts = LogisticsCounts;
			}
			return Out;
		}
		virtual bool LogisticsReadAbandoned() const override { return bLogisticsAbandoned; }
		virtual void EndLogisticsRead() override { ++LogisticsEnds; bLogisticsOpen = false; }
		virtual bool IsLogisticsReadOpen() const override { return bLogisticsOpen; }
		virtual TOptional<SatisDash::FAutoPauseReading> ApplyAutoPause(bool bEnabled) override
		{
			Applied.Add(bEnabled);
			if (!bApplyWorks)
			{
				// The game kept the other value: the read-back disagrees, whatever the fake held before.
				SatisDash::FAutoPauseReading Kept;
				Kept.bEnabled = !bEnabled;
				return Kept;
			}
			if (AutoPause.IsSet())
			{
				if (bApplyDefers)
				{
					AutoPause->PendingValue = bEnabled;
				}
				else
				{
					AutoPause->bEnabled = bEnabled;
				}
			}
			return AutoPause;
		}
	};

	/** Runs work inline when launched, or holds it until release() when bHold (to test one task at a time). */
	struct FFakeWorker : public SatisDash::IWorker
	{
		bool bHold = false;
		int32 Launched = 0;
		SatisDash::FWorkFn Held;

		virtual void Launch(SatisDash::FWorkFn job) override
		{
			++Launched;
			if (bHold)
			{
				Held = MoveTemp(job);
				return;
			}
			job();
		}
		virtual bool IsIdle() const override { return !Held; }
		void release()
		{
			SatisDash::FWorkFn job = MoveTemp(Held);
			Held = SatisDash::FWorkFn();
			if (job)
			{
				job();
			}
		}
	};

	/** A config, a fake clock (monotonic and UTC move together), a fake network and a fake game. */
	struct FDriverFixture
	{
		FTempDir Dir;
		FString ConfigPath = Dir.Path / TEXT("Configs/SatisDashAgent.cfg");
		FString CredentialPath = Dir.Path / TEXT("SatisDash/credential.json");
		double T = 1000.0;
		FDateTime Utc = FDateTime(2026, 10, 20, 12, 0, 0);
		FFakeTransport Net;
		FFakeGame Game;
		FFakeWorker Worker;

		void writeConfig(const TCHAR* Url, const TCHAR* Code, const TCHAR* Extra = TEXT("")) const
		{
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(ConfigPath), true);
			WriteText(ConfigPath, FString::Printf(TEXT("{\"backendUrl\":\"%s\",\"enrollmentCode\":\"%s\"%s}"), Url, Code, Extra));
		}

		SatisDash::FDriverClock clock()
		{
			SatisDash::FDriverClock Clock;
			Clock.Now = [this] { return T; };
			Clock.UtcNow = [this] { return Utc; };
			Clock.Random01 = [] { return 1.0; };
			return Clock;
		}

		/** Moves both clocks, ticking the driver every half second as the game does. */
		void run(SatisDash::FDriver& Driver, double Seconds)
		{
			for (double Done = 0; Done < Seconds; Done += 0.5)
			{
				T += 0.5;
				Utc += FTimespan::FromMilliseconds(500);
				Driver.Tick();
			}
		}

		/** Ticks until done says so, for at most Seconds; false if it never did. */
		bool runUntil(SatisDash::FDriver& Driver, const TFunction<bool()>& done, double Seconds)
		{
			for (double Spent = 0; Spent < Seconds; Spent += 0.5)
			{
				if (done())
				{
					return true;
				}
				run(Driver, 0.5);
			}
			return done();
		}

		/** Boots a driver against http://127.0.0.1:1 with a code and answers its enrolment with 201. */
		void enrol(SatisDash::FDriver& Driver, const FString& Secret, const TCHAR* Extra = TEXT(""))
		{
			writeConfig(TEXT("http://127.0.0.1:1"), TEXT("AB3D-7XQ2-K5MN"), Extra);
			Driver.Boot();
			Driver.Tick();
			Net.answer(TEXT("/agent/v1/enroll"), Reply(201, FString::Printf(
				TEXT("{\"agentSecret\":\"%s\",\"serverId\":\"srv-1\",\"cadence\":%s}"), *Secret, CadenceJson)));
		}
	};

	inline FHttpReply SnapshotOk(bool bCommandsPending)
	{
		return Reply(200, FString::Printf(TEXT("{\"cadence\":%s,\"commandsPending\":%s}"), CadenceJson, bCommandsPending ? TEXT("true") : TEXT("false")));
	}
}
