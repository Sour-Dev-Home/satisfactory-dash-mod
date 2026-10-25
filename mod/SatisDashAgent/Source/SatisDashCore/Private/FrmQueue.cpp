#include "SatisDash/FrmQueue.h"

#include "SatisDash/Log.h"

SatisDash::FFrmQueue::FFrmQueue(ITransport& InTransport)
	: Transport{InTransport}
{
}

bool SatisDash::FFrmQueue::Has(const FFrmEndpoint* Endpoint) const
{
	if (bInFlight && Current == Endpoint)
	{
		return true;
	}
	for (const FFrmRead& Each : Live)
	{
		if (Each.Endpoint == Endpoint)
		{
			return true;
		}
	}
	for (const FFrmRead& Each : World)
	{
		if (Each.Endpoint == Endpoint)
		{
			return true;
		}
	}
	return false;
}

bool SatisDash::FFrmQueue::Enqueue(FFrmRead Read)
{
	if (Read.Endpoint == nullptr || Has(Read.Endpoint))
	{
		return false;
	}
	(Read.Endpoint->Lane == EFrmLane::Live ? Live : World).Add(MoveTemp(Read));
	return true;
}

void SatisDash::FFrmQueue::NoteCode(int32 Code)
{
	if (Code == 401 || Code == 403)
	{
		if (!bTokenLogged)
		{
			UE_LOG(LogSatisDash, Warning, TEXT("FRM requires a token: set frmToken in SatisDashAgent.cfg"));
			bTokenLogged = true;
		}
	}
	else if (Code >= 200 && Code < 300)
	{
		bTokenLogged = false;
	}
}

void SatisDash::FFrmQueue::Pump(int32 Port, const FString& Token, bool bLargeBusy)
{
	if (bInFlight)
	{
		return;
	}
	for (TArray<FFrmRead>* Lane : { &Live, &World })
	{
		for (int32 I = 0; I < Lane->Num(); ++I)
		{
			if (bLargeBusy && !(*Lane)[I].Endpoint->bMayPassLarge)
			{
				continue;
			}
			FFrmRead Read = MoveTemp((*Lane)[I]);
			Lane->RemoveAt(I);
			Current = Read.Endpoint;
			bInFlight = true;
			const uint32 SentIn = Generation;
			Transport.Send(BuildFrmRequest(Port, Token, *Read.Endpoint), [this, SentIn, deliver = MoveTemp(Read.OnReply)](const FHttpReply& Reply)
			{
				bInFlight = false;
				Current = nullptr;
				if (SentIn != Generation)
				{
					return; // cancelled (the agent left Running): this reply belongs to nobody
				}
				NoteCode(Reply.Code);
				deliver(Reply);
			});
			return;
		}
	}
}

void SatisDash::FFrmQueue::Cancel()
{
	Live.Reset();
	World.Reset();
	++Generation;
}
