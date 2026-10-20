# API sources (G2 anti-invention check)

Every SML, FactoryGame or Unreal Engine symbol, `#include` and module dependency used under `spikes/` has a row here.
`npm run harness -- check-api` (in CI, and before every spike run) fails on any use without a row.

- **Symbol:** as the code writes it. That's one of:
  - a type or engine global (`FHttpModule`, `GEngine`);
  - a qualified name, the whole chain (`FHttpModule::Get`, `UE::Tasks::Launch`);
  - a free function called without a qualifier (`GetWorld`, `Cast`, `NewObject`);
  - a macro (`UE_LOG`);
  - an include path as written (`HttpModule.h`);
  - a module dependency (`module:HTTP`) or a `.uplugin` plugin dependency (`plugin:SML`).

  A method called through `->` or `.` is covered by any row ending in `::<Method>`. The spike's own classes,
  functions and macros need no row, but its own methods are excused only when called on `this`.
- **Header:** `path:line` in the pinned SML or engine checkout (`harness/toolchain.json`) where the symbol is declared.
  Look it up; never write it from memory. If the docs and the header disagree, the header wins, and both are noted in
  Source.
- **Source:** the satisfactory-dash `docs-vault/raw-sources/` file that documents the symbol, or `header only` when no
  captured document covers it. In that case the spike's result is the only evidence.

| Symbol | Header | Source |
| --- | --- | --- |
| CoreMinimal.h | Engine/Source/Runtime/Core/Public/CoreMinimal.h:1 | header only |
| Modules/ModuleManager.h | Engine/Source/Runtime/Core/Public/Modules/ModuleManager.h:1 | header only |
| IModuleInterface | Engine/Source/Runtime/Core/Public/Modules/ModuleInterface.h:13 | sml-cpp-modding-2026-09-27.adoc ("Mod Modules") |
| IMPLEMENT_MODULE | Engine/Source/Runtime/Core/Public/Modules/ModuleManager.h:916 | header only |
| UE_LOG | Engine/Source/Runtime/Core/Public/Logging/LogMacros.h:240 | header only |
| DEFINE_LOG_CATEGORY_STATIC | Engine/Source/Runtime/Core/Public/Logging/LogMacros.h:350 | header only (spikes don't use LogTemp: the game's Config/DefaultEngine.ini:111 sets LogTemp=all Warning) |
| TEXT | Engine/Source/Runtime/Core/Public/HAL/Platform.h:1222 | header only |
| module:Core | Engine/Source/Runtime/Core/Core.Build.cs:1 | header only |
| plugin:SML | Mods/SML/SML.uplugin:1 | header only |
| HttpModule.h | Engine/Source/Runtime/Online/HTTP/Public/HttpModule.h:1 | ue-http-timer-json-2026-09-27.md |
| Interfaces/IHttpRequest.h | Engine/Source/Runtime/Online/HTTP/Public/Interfaces/IHttpRequest.h:1 | ue-http-timer-json-2026-09-27.md |
| Interfaces/IHttpResponse.h | Engine/Source/Runtime/Online/HTTP/Public/Interfaces/IHttpResponse.h:1 | header only |
| FHttpModule::Get | Engine/Source/Runtime/Online/HTTP/Public/HttpModule.h:59 | ue-http-timer-json-2026-09-27.md |
| FHttpModule::CreateRequest | Engine/Source/Runtime/Online/HTTP/Public/HttpModule.h:71 | ue-http-timer-json-2026-09-27.md |
| IHttpRequest::SetVerb | Engine/Source/Runtime/Online/HTTP/Public/Interfaces/IHttpRequest.h:162 | ue-http-timer-json-2026-09-27.md |
| IHttpRequest::SetURL | Engine/Source/Runtime/Online/HTTP/Public/Interfaces/IHttpRequest.h:171 | ue-http-timer-json-2026-09-27.md |
| IHttpRequest::SetContentAsString | Engine/Source/Runtime/Online/HTTP/Public/Interfaces/IHttpRequest.h:212 | ue-http-timer-json-2026-09-27.md |
| IHttpRequest::SetHeader | Engine/Source/Runtime/Online/HTTP/Public/Interfaces/IHttpRequest.h:296 | header only |
| IHttpRequest::ProcessRequest | Engine/Source/Runtime/Online/HTTP/Public/Interfaces/IHttpRequest.h:355 | ue-http-timer-json-2026-09-27.md |
| IHttpRequest::OnProcessRequestComplete | Engine/Source/Runtime/Online/HTTP/Public/Interfaces/IHttpRequest.h:360 | header only (the delegate type is TTSDelegate, IHttpRequest.h:42) |
| TDelegate::BindLambda | Engine/Source/Runtime/Core/Public/Delegates/DelegateSignatureImpl.inl:127 | header only (TTSDelegate = TDelegate, DelegateSignatureImpl.inl:696) |
| FHttpRequestPtr | Engine/Source/Runtime/Online/HTTP/Public/HttpFwd.h:12 | header only |
| FHttpResponsePtr | Engine/Source/Runtime/Online/HTTP/Public/HttpFwd.h:13 | header only |
| TSharedPtr::IsValid | Engine/Source/Runtime/Core/Public/Templates/SharedPointer.h:1055 | header only (inside class TSharedPtr, line 664) |
| IHttpResponse::GetResponseCode | Engine/Source/Runtime/Online/HTTP/Public/Interfaces/IHttpResponse.h:120 | header only |
| module:HTTP | Engine/Source/Runtime/Online/HTTP/HTTP.Build.cs:1 | header only |
| TFunction | Engine/Source/Runtime/Core/Public/Templates/Function.h:707 | header only |
| FString | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h:11 | header only (UE_STRING_CLASS = FString, class body at UnrealString.h.inl:54) |
| IHttpBase::GetFailureReason | Engine/Source/Runtime/Online/HTTP/Public/Interfaces/IHttpBase.h:118 | header only |
| LexToString | Engine/Source/Runtime/Online/HTTP/Public/Interfaces/IHttpBase.h:73 | header only (EHttpFailureReason, line 58) |
| Containers/Ticker.h | Engine/Source/Runtime/Core/Public/Containers/Ticker.h:1 | ue-http-timer-json-2026-09-27.md (FTSTicker) |
| FTSTicker::GetCoreTicker | Engine/Source/Runtime/Core/Public/Containers/Ticker.h:35 | ue-http-timer-json-2026-09-27.md |
| FTSTicker::AddTicker | Engine/Source/Runtime/Core/Public/Containers/Ticker.h:56 | header only (the name, delay and TUniqueFunction overload) |
| FTSTicker::RemoveTicker | Engine/Source/Runtime/Core/Public/Containers/Ticker.h:66 | header only |
| FTSTicker::FDelegateHandle | Engine/Source/Runtime/Core/Public/Containers/Ticker.h:32 | header only |
| FString::Printf | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:1597 | header only |
| Configuration/ModConfiguration.h | Mods/SML/Source/SML/Public/Configuration/ModConfiguration.h:1 | header only |
| UModConfiguration | Mods/SML/Source/SML/Public/Configuration/ModConfiguration.h:30 | header only |
| FConfigId | Mods/SML/Source/SML/Public/Configuration/ModConfiguration.h:7 | header only |
| UModConfiguration::ConfigId | Mods/SML/Source/SML/Public/Configuration/ModConfiguration.h:35 | header only |
| UModConfiguration::RootSection | Mods/SML/Source/SML/Public/Configuration/ModConfiguration.h:47 | header only |
| Configuration/ConfigManager.h | Mods/SML/Source/SML/Public/Configuration/ConfigManager.h:1 | header only |
| UConfigManager | Mods/SML/Source/SML/Public/Configuration/ConfigManager.h:44 | header only |
| UConfigManager::RegisterModConfiguration | Mods/SML/Source/SML/Public/Configuration/ConfigManager.h:73 | header only (loads from disk: ConfigManager.cpp:287) |
| UConfigManager::GetConfigurationRootSection | Mods/SML/Source/SML/Public/Configuration/ConfigManager.h:81 | header only |
| Configuration/Properties/ConfigPropertySection.h | Mods/SML/Source/SML/Public/Configuration/Properties/ConfigPropertySection.h:1 | header only |
| UConfigPropertySection | Mods/SML/Source/SML/Public/Configuration/Properties/ConfigPropertySection.h:8 | header only |
| UConfigPropertySection::SectionProperties | Mods/SML/Source/SML/Public/Configuration/Properties/ConfigPropertySection.h:16 | header only |
| Configuration/Properties/ConfigPropertyString.h | Mods/SML/Source/SML/Public/Configuration/Properties/ConfigPropertyString.h:1 | header only |
| UConfigPropertyString | Mods/SML/Source/SML/Public/Configuration/Properties/ConfigPropertyString.h:6 | header only |
| UConfigPropertyString::Value | Mods/SML/Source/SML/Public/Configuration/Properties/ConfigPropertyString.h:15 | header only |
| Engine/World.h | Engine/Source/Runtime/Engine/Classes/Engine/World.h:1 | header only |
| FWorldDelegates | Engine/Source/Runtime/Engine/Classes/Engine/World.h:4321 | header only |
| FWorldDelegates::OnPostWorldInitialization | Engine/Source/Runtime/Engine/Classes/Engine/World.h:4375 | header only (FWorldInitializationEvent, :4324) |
| UWorld::GetGameInstance | Engine/Source/Runtime/Engine/Classes/Engine/World.h:4225 | header only |
| Engine/GameInstance.h | Engine/Source/Runtime/Engine/Classes/Engine/GameInstance.h:1 | header only |
| UGameInstance::GetSubsystem | Engine/Source/Runtime/Engine/Classes/Engine/GameInstance.h:440 | header only |
| Misc/SecureHash.h | Engine/Source/Runtime/Core/Public/Misc/SecureHash.h:1 | header only |
| FSHA1::HashBuffer | Engine/Source/Runtime/Core/Public/Misc/SecureHash.h:375 | header only |
| FSHAHash::ToString | Engine/Source/Runtime/Core/Public/Misc/SecureHash.h:242 | header only |
| FTCHARToUTF8 | Engine/Source/Runtime/Core/Public/Containers/StringConv.h:1016 | header only |
| TDelegate::AddLambda | Engine/Source/Runtime/Core/Public/Delegates/DelegateSignatureImpl.inl:806 | header only (multicast AddLambda) |
| CreateDefaultSubobject | Engine/Source/Runtime/CoreUObject/Public/UObject/Object.h:150 | header only (UObject member template) |
| GetDefault | Engine/Source/Runtime/CoreUObject/Public/UObject/UObjectGlobals.h:2102 | header only |
| GENERATED_BODY | Engine/Source/Runtime/CoreUObject/Public/UObject/ObjectMacros.h:746 | header only |
| UCLASS | Engine/Source/Runtime/CoreUObject/Public/UObject/ObjectMacros.h:754 | header only |
| module:CoreUObject | Engine/Source/Runtime/CoreUObject/CoreUObject.Build.cs:1 | header only |
| module:Engine | Engine/Source/Runtime/Engine/Engine.Build.cs:1 | header only |
| module:SML | Mods/SML/Source/SML/SML.Build.cs:1 | header only |
| UWorld | Engine/Source/Runtime/Engine/Classes/Engine/World.h:919 | header only |
| UWorld::InitializationValues | Engine/Source/Runtime/Engine/Classes/Engine/World.h:3196 | header only (alias of FWorldInitializationValues) |
| FDelegateHandle | Engine/Source/Runtime/Core/Public/Delegates/IDelegateInstance.h:13 | header only |
| TMulticastDelegate::Remove | Engine/Source/Runtime/Core/Public/Delegates/DelegateSignatureImpl.inl:1007 | header only |
| TMap::FindRef | Engine/Source/Runtime/Core/Public/Containers/Map.h:739 | header only |
| TMap::Add | Engine/Source/Runtime/Core/Public/Containers/Map.h:408 | header only |
| Cast | Engine/Source/Runtime/CoreUObject/Public/Templates/Casts.h:95 | header only |
| TStringConversion::Length | Engine/Source/Runtime/Core/Public/Containers/StringConv.h:791 | header only (FTCHARToUTF8 is a TStringConversion) |
| FString::Left | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:1146 | header only |
| FString::ToLower | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:1565 | header only |
| FString::Len | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:1128 | header only |
| Dom/JsonObject.h | Engine/Source/Runtime/Json/Public/Dom/JsonObject.h:1 | header only |
| FJsonObject | Engine/Source/Runtime/Json/Public/Dom/JsonObject.h:22 | header only |
| FJsonObject::TryGetStringField | Engine/Source/Runtime/Json/Public/Dom/JsonObject.h:191 | header only |
| Serialization/JsonReader.h | Engine/Source/Runtime/Json/Public/Serialization/JsonReader.h:1 | header only |
| TJsonReader | Engine/Source/Runtime/Json/Public/Serialization/JsonReader.h:40 | header only |
| TJsonReaderFactory::Create | Engine/Source/Runtime/Json/Public/Serialization/JsonReader.h:1070 | header only (the StringType&& overload; class at :1065) |
| Serialization/JsonSerializer.h | Engine/Source/Runtime/Json/Public/Serialization/JsonSerializer.h:1 | header only |
| FJsonSerializer::Deserialize | Engine/Source/Runtime/Json/Public/Serialization/JsonSerializer.h:51 | header only (the TSharedPtr<FJsonObject> overload; class at :14) |
| FDateTime::ToIso8601 | Engine/Source/Runtime/Core/Public/Misc/DateTime.h:493 | header only |
| FDateTime::UtcNow | Engine/Source/Runtime/Core/Public/Misc/DateTime.h:762 | header only |
| module:Json | Engine/Source/Runtime/Json/Json.Build.cs:1 | header only |
| TJsonReaderFactory | Engine/Source/Runtime/Json/Public/Serialization/JsonReader.h:1065 | header only |
| FString::IsEmpty | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:335 | header only |
| IHttpResponse::GetContentAsString | Engine/Source/Runtime/Online/HTTP/Public/Interfaces/IHttpResponse.h:127 | header only |
| TSharedRef | Engine/Source/Runtime/Core/Public/Templates/SharedPointer.h:151 | header only |
| TSharedPtr | Engine/Source/Runtime/Core/Public/Templates/SharedPointer.h:664 | header only |
| FGCircuitSubsystem.h | Source/FactoryGame/Public/FGCircuitSubsystem.h:1 | sml-power-network-2026-10-19.md (names the subsystem's circuits) |
| AFGCircuitSubsystem | Source/FactoryGame/Public/FGCircuitSubsystem.h:32 | header only |
| AFGCircuitSubsystem::GetCircuitSubsystem | Source/FactoryGame/Public/FGCircuitSubsystem.h:49 | header only |
| AFGCircuitSubsystem::FindCircuit | Source/FactoryGame/Public/FGCircuitSubsystem.h:88 | header only (template; mCircuits is private, :248-251) |
| FGPowerCircuit.h | Source/FactoryGame/Public/FGPowerCircuit.h:1 | sml-power-network-2026-10-19.md (E2: UFGPowerCircuit) |
| UFGPowerCircuit | Source/FactoryGame/Public/FGPowerCircuit.h:148 | sml-power-network-2026-10-19.md (E2) |
| UFGPowerCircuit::GetStats | Source/FactoryGame/Public/FGPowerCircuit.h:171 | header only |
| FPowerCircuitStats | Source/FactoryGame/Public/FGPowerCircuit.h:57 | header only |
| FPowerCircuitStats::PowerProductionCapacity | Source/FactoryGame/Public/FGPowerCircuit.h:101 | header only |
| FPowerCircuitStats::PowerProduced | Source/FactoryGame/Public/FGPowerCircuit.h:104 | header only |
| FPowerCircuitStats::PowerConsumed | Source/FactoryGame/Public/FGPowerCircuit.h:107 | header only |
| UFGCircuit::GetCircuitID | Source/FactoryGame/Public/FGCircuit.h:57 | header only |
| module:FactoryGame | Source/FactoryGame/FactoryGame.Build.cs:1 | header only |
| UWorld::IsGameWorld | Engine/Source/Runtime/Engine/Classes/Engine/World.h:4006 | header only |
| TWeakObjectPtr | Engine/Source/Runtime/Core/Public/UObject/WeakObjectPtrTemplates.h:23 | header only |
