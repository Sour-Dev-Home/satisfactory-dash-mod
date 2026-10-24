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
					Call.OnReply(Reply);
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

		void writeConfig(const TCHAR* Url, const TCHAR* Code) const
		{
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(ConfigPath), true);
			WriteText(ConfigPath, FString::Printf(TEXT("{\"backendUrl\":\"%s\",\"enrollmentCode\":\"%s\"}"), Url, Code));
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
		void enrol(SatisDash::FDriver& Driver, const FString& Secret)
		{
			writeConfig(TEXT("http://127.0.0.1:1"), TEXT("AB3D-7XQ2-K5MN"));
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
