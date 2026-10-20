#include "HttpTransport.h"

#include "Containers/Ticker.h"
#include "HAL/PlatformTime.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "SatisDash/Log.h"

namespace
{
	/** scheme://host:port of a URL: everything before the path. */
	FString originOf(const FString& Url)
	{
		const int32 HostAt = Url.Find(TEXT("://"), ESearchCase::CaseSensitive);
		const int32 PathAt = Url.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, HostAt == INDEX_NONE ? 0 : HostAt + 3);
		return PathAt == INDEX_NONE ? Url : Url.Left(PathAt);
	}
}

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
	InFlight.Add(Pending);
	// Ruling 5: the cap holds while the body downloads. Without the stream the body would be buffered uncapped (S4),
	// so such a request is never sent.
	if (!Request->SetResponseBodyReceiveStream(Pending->Body.ToSharedRef()))
	{
		FRawReply Raw;
		Raw.Failure = TEXT("response cap unavailable, not sent");
		Defer(Pending, Raw);
		return;
	}

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
		if (Pending->bInSend)
		{
			// A request that fails to start completes synchronously, inside ProcessRequest (HttpRequestCommon.cpp:158-161,
			// :200-205). The handler must never run inside Send (Ports.h), so that answer waits for the next tick.
			Defer(Pending, Raw);
			return;
		}
		Finish(Pending, Raw);
	});
	Pending->bInSend = true;
	const bool bStarted = Request->ProcessRequest();
	Pending->bInSend = false;
	if (!bStarted)
	{
		// UE has usually answered already (deferred above); bAnswered lets only the first answer through.
		FRawReply Raw;
		Raw.Failure = TEXT("not started");
		Defer(Pending, Raw);
	}
}

void SatisDash::FHttpTransport::Defer(const TSharedRef<FPending>& Pending, const FRawReply& Raw)
{
	const TWeakPtr<bool> alive = Alive;
	FTSTicker::GetCoreTicker().AddTicker(TEXT("SatisDashDeferredReply"), 0.0f, [this, alive, Pending, Raw](float)
	{
		if (alive.IsValid())
		{
			Finish(Pending, Raw);
		}
		return false;
	});
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
	// S9: a refused response and a TLS failure are logged, once per kind per ten minutes; never a header and never more
	// of the URL than its origin (the secret is only in the Authorization header, S7). A failure to reach a backend is
	// keyed by its origin too, so a newly configured backend's failure is logged even if the old one's was just now.
	if (Refusal == EReplyRefusal::Redirected && RefusalLog.ShouldLog(TEXT("redirect"), Now))
	{
		UE_LOG(LogSatisDash, Warning, TEXT("The backend redirected a request; its answer was refused (redirects are never accepted)"));
	}
	else if (Refusal == EReplyRefusal::Oversized && RefusalLog.ShouldLog(TEXT("oversized"), Now))
	{
		UE_LOG(LogSatisDash, Warning, TEXT("A backend response was over %d bytes; refused it"), MaxResponseBytes);
	}
	else if (Refusal == EReplyRefusal::NoAnswer)
	{
		const FString Origin = originOf(Pending->Url);
		if (RefusalLog.ShouldLog(Raw.Failure + TEXT(" ") + Origin, Now))
		{
			UE_LOG(LogSatisDash, Warning, TEXT("Couldn't reach the backend at %s (%s)"), *Origin, *Raw.Failure);
		}
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
