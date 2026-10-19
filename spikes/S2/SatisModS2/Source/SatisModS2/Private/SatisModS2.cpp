#include "SatisModS2.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

// Our own category: the game's config filters LogTemp to Warning ([Core.Log] LogTemp=all Warning).
DEFINE_LOG_CATEGORY_STATIC(LogSatisMod, Display, All);

void FSatisModS2Module::StartupModule()
{
	UE_LOG(LogSatisMod, Display, TEXT("satis-mod loaded"));
	// First fire after 30 s, then every 30 s while PostNext returns true (Ticker.h:56).
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("SatisModS2"), 30.0f, [this](float) { return PostNext(); });
}

void FSatisModS2Module::ShutdownModule()
{
	FTSTicker::RemoveTicker(TickerHandle);
}

bool FSatisModS2Module::PostNext()
{
	const int32 N = ++Sent;
	auto Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(TEXT("http://127.0.0.1:18080/s2"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetContentAsString(FString::Printf(TEXT("{\"spike\":\"S2\",\"n\":%d}"), N));
	Request->OnProcessRequestComplete().BindLambda([N](FHttpRequestPtr, FHttpResponsePtr, bool)
	{
		if (N == FSatisModS2Module::Total)
		{
			UE_LOG(LogSatisMod, Display, TEXT("satis-mod s2 done"));
		}
	});
	Request->ProcessRequest();
	return Sent < Total; // false after the 40th post removes the ticker
}

IMPLEMENT_MODULE(FSatisModS2Module, SatisModS2)
