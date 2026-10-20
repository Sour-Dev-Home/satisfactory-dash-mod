#include "HttpTransport.h"

#include "Containers/Ticker.h"
#include "HAL/PlatformTime.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "SatisDash/Log.h"

SatisDash::FHttpTransport::~FHttpTransport()
{
	CancelAll();
}

void SatisDash::FHttpTransport::Send(const FRequestSpec& Spec, FReplyHandler OnReply)
{
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetVerb(Spec.Verb);
	Request->SetURL(Spec.Url);
	for (const TPair<FString, FString>& Header : Spec.Headers)
	{
		Request->SetHeader(Header.Key, Header.Value);
	}
	if (Spec.Body.Num() > 0)
	{
		Request->SetContent(Spec.Body);
	}
	Request->SetTimeout(Spec.TimeoutSeconds);

	const TSharedRef<FPending> Pending = MakeShared<FPending>();
	Pending->Url = Spec.Url;
	Pending->OnReply = MoveTemp(OnReply);
	Pending->Body = MakeShared<FCappedBody>();
	Pending->Request = Request;
	// Ruling 5: the cap holds while the body downloads.
	Request->SetResponseBodyReceiveStream(Pending->Body.ToSharedRef());

	const TWeakPtr<bool> alive = Alive;
	Request->OnProcessRequestComplete().BindLambda([this, alive, Pending](FHttpRequestPtr Done, FHttpResponsePtr Response, bool bProcessed)
	{
		if (!alive.IsValid())
		{
			return;
		}
		FRawReply Raw;
		Raw.bProcessed = bProcessed && Response.IsValid();
		if (Response.IsValid())
		{
			Raw.Code = Response->GetResponseCode();
			Raw.ContentType = Response->GetContentType();
			Raw.RetryAfter = Response->GetHeader(TEXT("Retry-After"));
			Raw.EffectiveUrl = Response->GetEffectiveURL();
		}
		Raw.Failure = Done.IsValid() ? FString(LexToString(Done->GetFailureReason())) : FString(TEXT("none"));
		Finish(Pending, Raw);
	});
	InFlight.Add(Pending);
	if (!Request->ProcessRequest())
	{
		// Whether UE also calls the delegate for a request that never started isn't documented; bAnswered lets only
		// the first answer through, and the ticker keeps the answer out of Send (Ports.h).
		FTSTicker::GetCoreTicker().AddTicker(TEXT("SatisDashNotStarted"), 0.0f, [this, alive, Pending](float)
		{
			if (alive.IsValid())
			{
				FRawReply Raw;
				Raw.Failure = TEXT("not started");
				Finish(Pending, Raw);
			}
			return false;
		});
	}
}

void SatisDash::FHttpTransport::Finish(const TSharedRef<FPending>& Pending, const FRawReply& Raw)
{
	if (Pending->bAnswered)
	{
		return;
	}
	Pending->bAnswered = true;
	InFlight.Remove(Pending);
	Pending->Request.Reset(); // the delegate holds Pending; this ends the cycle
	EReplyRefusal Refusal = EReplyRefusal::None;
	const FHttpReply Reply = MakeReply(Raw, Pending->Url, *Pending->Body, Refusal);
	const double Now = FPlatformTime::Seconds();
	// S9: a refused response and a TLS failure are logged, once per kind per ten minutes; never the URL's secret parts
	// (there are none: the secret is only in the Authorization header, S7) and never a header.
	if (Refusal == EReplyRefusal::Redirected && RefusalLog.ShouldLog(TEXT("redirect"), Now))
	{
		UE_LOG(LogSatisDash, Warning, TEXT("The backend redirected a request; its answer was refused (redirects are never accepted)"));
	}
	else if (Refusal == EReplyRefusal::Oversized && RefusalLog.ShouldLog(TEXT("oversized"), Now))
	{
		UE_LOG(LogSatisDash, Warning, TEXT("A backend response was over %d bytes; refused it"), MaxResponseBytes);
	}
	else if (Refusal == EReplyRefusal::NoAnswer && RefusalLog.ShouldLog(Raw.Failure, Now))
	{
		UE_LOG(LogSatisDash, Warning, TEXT("Couldn't reach the backend (%s)"), *Raw.Failure);
	}
	const FReplyHandler& onReply = Pending->OnReply;
	onReply(Reply);
}

void SatisDash::FHttpTransport::CancelAll()
{
	Alive = MakeShared<bool>(true);
	const TArray<TSharedRef<FPending>> Out = MoveTemp(InFlight);
	InFlight.Reset();
	for (const TSharedRef<FPending>& Pending : Out)
	{
		Pending->bAnswered = true;
		if (Pending->Request.IsValid())
		{
			Pending->Request->OnProcessRequestComplete().Unbind();
			Pending->Request->CancelRequest();
			Pending->Request.Reset();
		}
	}
}
