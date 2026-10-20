#include "SatisModS5.h"
#include "SatisModS5Config.h"
#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/ConfigPropertyString.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogSatisMod, Display, All);

static FString ReadString(UConfigPropertySection* Root, const TCHAR* Key)
{
	UConfigPropertyString* Property = Root ? Cast<UConfigPropertyString>(Root->SectionProperties.FindRef(Key)) : nullptr;
	return Property ? Property->Value : FString();
}

// Never logs the code or the secret: only status codes (-1 when a step never ran).
static void Report(int32 EnrolStatus, int32 SnapshotStatus)
{
	UE_LOG(LogSatisMod, Display, TEXT("satis-mod s5 enroll=%d snapshot=%d"), EnrolStatus, SnapshotStatus);
}

void FSatisModS5Module::StartupModule()
{
	UE_LOG(LogSatisMod, Display, TEXT("satis-mod loaded"));
	WorldInitHandle = FWorldDelegates::OnPostWorldInitialization.AddLambda([this](UWorld* World, const UWorld::InitializationValues)
	{
		Enrol(World);
	});
}

void FSatisModS5Module::ShutdownModule()
{
	FWorldDelegates::OnPostWorldInitialization.Remove(WorldInitHandle);
}

void FSatisModS5Module::Enrol(UWorld* World)
{
	if (bStarted || World == nullptr || World->GetGameInstance() == nullptr) { return; }
	UConfigManager* Configs = World->GetGameInstance()->GetSubsystem<UConfigManager>();
	if (Configs == nullptr) { return; }
	bStarted = true;
	Configs->RegisterModConfiguration(USatisModS5Config::StaticClass());
	UConfigPropertySection* Root = Configs->GetConfigurationRootSection(GetDefault<USatisModS5Config>()->ConfigId);
	const FString Code = ReadString(Root, TEXT("code"));
	const FString BaseUrl = ReadString(Root, TEXT("backendUrl"));
	if (Code.IsEmpty() || BaseUrl.IsEmpty())
	{
		UE_LOG(LogSatisMod, Display, TEXT("satis-mod s5 error=config-missing"));
		return;
	}

	// POST /agent/v1/enroll with {code, agentVersion} (satisfactory-dash endpoints.ts agentApi.enroll; 201 on success).
	auto Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(BaseUrl + TEXT("/agent/v1/enroll"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetContentAsString(FString::Printf(TEXT("{\"code\":\"%s\",\"agentVersion\":\"spike-s5\"}"), *Code));
	Request->OnProcessRequestComplete().BindLambda([BaseUrl](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
	{
		const int32 Status = (bSucceeded && Response.IsValid()) ? Response->GetResponseCode() : -1;
		FString Secret;
		if (Status == 201)
		{
			TSharedPtr<FJsonObject> Body;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
			if (FJsonSerializer::Deserialize(Reader, Body) && Body.IsValid()) { Body->TryGetStringField(TEXT("agentSecret"), Secret); }
		}
		if (Secret.IsEmpty()) { Report(Status, -1); return; }

		// POST /agent/v1/snapshots, Bearer secret; body shaped on fixtures/agent.ts agentSnapshotRequestUnreachable (200 on success).
		auto Snapshot = FHttpModule::Get().CreateRequest();
		Snapshot->SetURL(BaseUrl + TEXT("/agent/v1/snapshots"));
		Snapshot->SetVerb(TEXT("POST"));
		Snapshot->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Snapshot->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Secret);
		Snapshot->SetContentAsString(FString::Printf(
			TEXT("{\"agentVersion\":\"spike-s5\",\"observedAt\":\"%s\",\"reachable\":false,\"paused\":null}"),
			*FDateTime::UtcNow().ToIso8601()));
		Snapshot->OnProcessRequestComplete().BindLambda([Status](FHttpRequestPtr, FHttpResponsePtr SnapshotResponse, bool bSnapshotSent)
		{
			Report(Status, (bSnapshotSent && SnapshotResponse.IsValid()) ? SnapshotResponse->GetResponseCode() : -1);
		});
		Snapshot->ProcessRequest();
	});
	Request->ProcessRequest();
}

IMPLEMENT_MODULE(FSatisModS5Module, SatisModS5)
