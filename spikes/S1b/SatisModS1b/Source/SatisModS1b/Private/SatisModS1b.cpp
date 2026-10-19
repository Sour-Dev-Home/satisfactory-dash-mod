#include "SatisModS1b.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

// Our own category: the game's config filters LogTemp to Warning ([Core.Log] LogTemp=all Warning).
DEFINE_LOG_CATEGORY_STATIC(LogSatisMod, Display, All);

// check-api reads "Name(" as a call, so: an alias keeps parentheses out of Probe's parameter list (so it is seen as the
// spike's own function), the label is assigned rather than constructed, and the continuation has a lowercase name.
using ThenFn = TFunction<void()>;

// The bad-host probe starts only after the good one completes, so "s1b bad" is always the last line the harness waits for.
static void Probe(const TCHAR* Label, const TCHAR* Url, ThenFn next)
{
	auto Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("GET"));
	const FString Name = Label;
	Request->OnProcessRequestComplete().BindLambda([Name, next](FHttpRequestPtr Req, FHttpResponsePtr Response, bool bSucceeded)
	{
		if (bSucceeded && Response.IsValid())
		{
			UE_LOG(LogSatisMod, Display, TEXT("satis-mod s1b %s status=%d"), *Name, Response->GetResponseCode());
		}
		else
		{
			const TCHAR* Reason = Req.IsValid() ? LexToString(Req->GetFailureReason()) : TEXT("unknown");
			UE_LOG(LogSatisMod, Display, TEXT("satis-mod s1b %s error=%s"), *Name, Reason);
		}
		if (next) { next(); }
	});
	Request->ProcessRequest();
}

void FSatisModS1bModule::StartupModule()
{
	UE_LOG(LogSatisMod, Display, TEXT("satis-mod loaded"));
	Probe(TEXT("good"), TEXT("https://api.satis-manager.com/api/health"), []()
	{
		Probe(TEXT("bad"), TEXT("https://wrong.host.badssl.com/"), nullptr);
	});
}

IMPLEMENT_MODULE(FSatisModS1bModule, SatisModS1b)
