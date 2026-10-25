#pragma once

#include "CoreMinimal.h"
#include "SatisDash/Ports.h"
#include "Tasks/Task.h"

namespace SatisDash
{
	/**
	 * IWorker over UE::Tasks (FRM bridge spec "Threading"): one task at a time, at background-low priority, so parsing
	 * never competes with the game's own work. Waits for its task when destroyed, so nothing it runs outlives the module.
	 *
	 * Memory ordering: IsIdle() is FTask::IsCompleted(), which reads FSubsequents::bIsClosed, a std::atomic<bool> loaded with the
	 * default seq_cst order (acquire). The task closes it (seq_cst store) after its body has run, so a true IsIdle() makes
	 * everything the job wrote visible to the game thread (TaskPrivate.h:409-411, 812, 816-818).
	 */
	class FTaskWorker final : public IWorker
	{
	public:
		FTaskWorker() = default;
		FTaskWorker(const FTaskWorker&) = delete;
		FTaskWorker& operator=(const FTaskWorker&) = delete;
		virtual ~FTaskWorker() override { Wait(); }

		virtual void Launch(FWorkFn Work) override
		{
			Wait(); // a no-op unless a caller broke the contract (Launch only while IsIdle()): never orphan a running task
			Current = UE::Tasks::Launch(TEXT("SatisDashFrm"), MoveTemp(Work), UE::Tasks::ETaskPriority::BackgroundLow);
		}
		virtual bool IsIdle() const override { return Current.IsCompleted(); }
		void Wait()
		{
			if (Current.IsValid())
			{
				Current.Wait();
			}
		}

	private:
		UE::Tasks::FTask Current;
	};
}
