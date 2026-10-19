#include "SatisModS1.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

// Our own category: the game's config filters LogTemp to Warning ([Core.Log] LogTemp=all Warning).
DEFINE_LOG_CATEGORY_STATIC(LogSatisMod, Display, All);

void FSatisModS1Module::StartupModule()
{
	UE_LOG(LogSatisMod, Display, TEXT("satis-mod loaded"));

	// One fixed POST to the harness's loopback echo server (the spike contract).
	auto Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(TEXT("http://127.0.0.1:18080/s1"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetContentAsString(TEXT("{\"spike\":\"S1\",\"hello\":\"satis\"}"));
	Request->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
	{
		const int32 Status = (bSucceeded && Response.IsValid()) ? Response->GetResponseCode() : -1;
		UE_LOG(LogSatisMod, Display, TEXT("satis-mod s1 done status=%d"), Status);
	});
	Request->ProcessRequest();
}

IMPLEMENT_MODULE(FSatisModS1Module, SatisModS1)
