# API sources (G2 anti-invention check)

Every SML, FactoryGame or Unreal Engine symbol, `#include` and module dependency used under `spikes/` and `mod/` has a row here.
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
| FGServerSubsystem.h | Source/FactoryDedicatedServer/Public/FGServerSubsystem.h:1 | header only |
| UFGServerSubsystem | Source/FactoryDedicatedServer/Public/FGServerSubsystem.h:34 | header only |
| UFGServerSubsystem::Get | Source/FactoryDedicatedServer/Public/FGServerSubsystem.h:62 | header only |
| UFGServerSubsystem::GetServerAPIManager | Source/FactoryDedicatedServer/Public/FGServerSubsystem.h:65 | header only ("Useful for Mods") |
| UFGServerSubsystem::GetServerGameState | Source/FactoryDedicatedServer/Public/FGServerSubsystem.h:140 | header only |
| FServerGameState | Source/FactoryGame/Public/Server/FGDSGameplayTypes.h:34 | header only (the struct Handler_QueryServerState fills, FGServerStateController.h:32) |
| UFGServerAPIManager | Source/FactoryDedicatedServer/Public/Networking/FGServerAPIManager.h:153 | header only |
| Controller/FGServerStateController.h | Source/FactoryDedicatedServer/Public/Controller/FGServerStateController.h:1 | header only |
| UFGServerStateController | Source/FactoryDedicatedServer/Public/Controller/FGServerStateController.h:23 | header only |
| UFGServerStateController::Handler_HealthCheck | Source/FactoryDedicatedServer/Public/Controller/FGServerStateController.h:49 | header only |
| module:FactoryDedicatedServer | Source/FactoryDedicatedServer/FactoryDedicatedServer.Build.cs:1 | header only (Server and Editor targets only, FactoryGame.uproject:47-53) |
| NewObject | Engine/Source/Runtime/CoreUObject/Public/UObject/UObjectGlobals.h:1889 | header only (the outer-only overload) |
| FSHAHash | Engine/Source/Runtime/Core/Public/Misc/SecureHash.h:225 | header only |
| UFGServerStateController::Handler_GetServerOptions | Source/FactoryDedicatedServer/Public/Controller/FGServerStateController.h:40 | header only |
| Controller/FGServerManagementController.h | Source/FactoryDedicatedServer/Public/Controller/FGServerManagementController.h:1 | header only |
| UFGServerManagementController | Source/FactoryDedicatedServer/Public/Controller/FGServerManagementController.h:22 | header only |
| UFGServerManagementController::Handler_ApplyServerOptions | Source/FactoryDedicatedServer/Public/Controller/FGServerManagementController.h:60 | header only |
| TMap | Engine/Source/Runtime/Core/Public/Containers/Map.h:1290 | header only |
| TMap::Find | Engine/Source/Runtime/Core/Public/Containers/Map.h:567 | header only (declared in TMapBase, which TMap inherits) |
| TMap::Contains | Engine/Source/Runtime/Core/Public/Containers/Map.h:788 | header only (declared in TMapBase, which TMap inherits) |
| FString::Reset | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:345 | header only |
| Misc/AutomationTest.h | Engine/Source/Runtime/Core/Public/Misc/AutomationTest.h:1 | header only |
| IMPLEMENT_SIMPLE_AUTOMATION_TEST | Engine/Source/Runtime/Core/Public/Misc/AutomationTest.h:4161 | header only (inside #if WITH_AUTOMATION_WORKER, :4160) |
| EAutomationTestFlags | Engine/Source/Runtime/Core/Public/Misc/AutomationTest.h:85 | header only |
| EAutomationTestFlags::EditorContext | Engine/Source/Runtime/Core/Public/Misc/AutomationTest.h:90 | header only |
| EAutomationTestFlags::EngineFilter | Engine/Source/Runtime/Core/Public/Misc/AutomationTest.h:124 | header only |
| TestTrue | Engine/Source/Runtime/Core/Public/Misc/AutomationTest.h:2534 | header only |
| TestFalse | Engine/Source/Runtime/Core/Public/Misc/AutomationTest.h:2298 | header only |
| TestEqual | Engine/Source/Runtime/Core/Public/Misc/AutomationTest.h:1949 | header only (the int32 overload; there are many) |
| FAutomationTestBase | Engine/Source/Runtime/Core/Public/Misc/AutomationTest.h:1538 | header only |
| FAutomationTestBase::AddError | Engine/Source/Runtime/Core/Public/Misc/AutomationTest.h:1602 | header only |
| DECLARE_LOG_CATEGORY_EXTERN | Engine/Source/Runtime/Core/Public/Logging/LogMacros.h:332 | header only (the logging-enabled definition; :195 is the no-logging one) |
| DEFINE_LOG_CATEGORY | Engine/Source/Runtime/Core/Public/Logging/LogMacros.h:342 | header only (the logging-enabled definition; :196 is the no-logging one) |
| FDefaultModuleImpl | Engine/Source/Runtime/Core/Public/Modules/ModuleManager.h:854 | header only |
| ESearchCase::CaseSensitive | Engine/Source/Runtime/Core/Public/Misc/CString.h:23 | header only |
| FString::Equals | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:1492 | header only |
| module:Projects | Engine/Source/Runtime/Projects/Projects.Build.cs:1 | header only |
| HAL/FileManager.h | Engine/Source/Runtime/Core/Public/HAL/FileManager.h:1 | header only |
| Misc/FileHelper.h | Engine/Source/Runtime/Core/Public/Misc/FileHelper.h:1 | header only |
| Misc/Guid.h | Engine/Source/Runtime/Core/Public/Misc/Guid.h:1 | header only |
| Misc/Paths.h | Engine/Source/Runtime/Core/Public/Misc/Paths.h:1 | header only |
| Policies/CondensedJsonPrintPolicy.h | Engine/Source/Runtime/Json/Public/Policies/CondensedJsonPrintPolicy.h:1 | header only |
| Serialization/JsonWriter.h | Engine/Source/Runtime/Json/Public/Serialization/JsonWriter.h:1 | header only |
| sys/stat.h | Engine/Source/Runtime/Core/Private/Unix/UnixForkPageProtector.cpp:16 | header only (POSIX system header; the engine checkout has no copy, the row points at the engine file that includes it) |
| IFileManager | Engine/Source/Runtime/Core/Public/HAL/FileManager.h:56 | header only |
| IFileManager::Get | Engine/Source/Runtime/Core/Public/HAL/FileManager.h:66 | header only |
| IFileManager::Delete | Engine/Source/Runtime/Core/Public/HAL/FileManager.h:107 | header only |
| IFileManager::Move | Engine/Source/Runtime/Core/Public/HAL/FileManager.h:113 | header only |
| IFileManager::FileExists | Engine/Source/Runtime/Core/Public/HAL/FileManager.h:116 | header only |
| IFileManager::DirectoryExists | Engine/Source/Runtime/Core/Public/HAL/FileManager.h:119 | header only |
| FPaths::GetCleanFilename | Engine/Source/Runtime/Core/Public/Misc/Paths.h:510 | header only |
| IFileManager::MakeDirectory | Engine/Source/Runtime/Core/Public/HAL/FileManager.h:122 | header only |
| IFileManager::DeleteDirectory | Engine/Source/Runtime/Core/Public/HAL/FileManager.h:125 | header only |
| IFileManager::GetTimeStamp | Engine/Source/Runtime/Core/Public/HAL/FileManager.h:218 | header only |
| IFileManager::SetTimeStamp | Engine/Source/Runtime/Core/Public/HAL/FileManager.h:231 | header only |
| FFileHelper::LoadFileToString | Engine/Source/Runtime/Core/Public/Misc/FileHelper.h:121 | header only |
| FFileHelper::SaveStringToFile | Engine/Source/Runtime/Core/Public/Misc/FileHelper.h:178 | header only |
| FFileHelper::EEncodingOptions::ForceUTF8 | Engine/Source/Runtime/Core/Public/Misc/FileHelper.h:44 | header only |
| FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM | Engine/Source/Runtime/Core/Public/Misc/FileHelper.h:45 | header only |
| FGuid::NewGuid | Engine/Source/Runtime/Core/Public/Misc/Guid.h:370 | header only |
| FPaths::GetPath | Engine/Source/Runtime/Core/Public/Misc/Paths.h:522 | header only |
| FPaths::ProjectSavedDir | Engine/Source/Runtime/Core/Public/Misc/Paths.h:279 | header only |
| FChar::IsDigit | Engine/Source/Runtime/Core/Public/Misc/Char.h:240 | header only |
| FCString::Atoi | Engine/Source/Runtime/Core/Public/Misc/CString.h:491 | header only |
| ESearchCase::IgnoreCase | Engine/Source/Runtime/Core/Public/Misc/CString.h:26 | header only |
| FDateTime | Engine/Source/Runtime/Core/Public/Misc/DateTime.h:75 | header only |
| FDateTime::MinValue | Engine/Source/Runtime/Core/Public/Misc/DateTime.h:668 | header only |
| FString::StartsWith | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:1726 | header only |
| FString::EndsWith | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:1782 | header only |
| FString::FindChar | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:1440 | header only |
| FString::FindLastChar | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:1452 | header only |
| FString::RightChop | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:1221 | header only |
| FString::ToUpper | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:1553 | header only |
| FString::TrimStartAndEnd | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:1878 | header only |
| FString::LeftChopInline | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:1184 | header only |
| MakeShared | Engine/Source/Runtime/Core/Public/Templates/SharedPointer.h:2252 | header only |
| TCHAR_TO_UTF8 | Engine/Source/Runtime/Core/Public/Containers/StringConv.h:1021 | header only |
| FJsonValue | Engine/Source/Runtime/Json/Public/Dom/JsonValue.h:21 | header only |
| FJsonValue::AsString | Engine/Source/Runtime/Json/Public/Dom/JsonValue.h:29 | header only |
| EJson | Engine/Source/Runtime/Json/Public/Serialization/JsonTypes.h:22 | header only |
| EJson::String | Engine/Source/Runtime/Json/Public/Serialization/JsonTypes.h:26 | header only |
| FJsonObject::TryGetField | Engine/Source/Runtime/Json/Public/Dom/JsonObject.h:62 | header only |
| FJsonObject::HasField | Engine/Source/Runtime/Json/Public/Dom/JsonObject.h:74 | header only |
| FJsonObject::SetStringField | Engine/Source/Runtime/Json/Public/Dom/JsonObject.h:231 | header only |
| FJsonSerializer::Serialize | Engine/Source/Runtime/Json/Public/Serialization/JsonSerializer.h:166 | header only |
| TJsonWriter | Engine/Source/Runtime/Json/Public/Serialization/JsonWriter.h:84 | header only |
| TJsonWriterFactory | Engine/Source/Runtime/Json/Public/Serialization/JsonWriter.h:687 | header only |
| TCondensedJsonPrintPolicy | Engine/Source/Runtime/Json/Public/Policies/CondensedJsonPrintPolicy.h:14 | header only |
| TOptional | Engine/Source/Runtime/Core/Public/Misc/Optional.h:56 | header only |
| TOptional::IsSet | Engine/Source/Runtime/Core/Public/Misc/Optional.h:343 | header only |
| FString::ChrN | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:1683 | header only |
| MoveTemp | Engine/Source/Runtime/Core/Public/Templates/UnrealTemplate.h:549 | header only |
| Dom/JsonValue.h | Engine/Source/Runtime/Json/Public/Dom/JsonValue.h:1 | header only |
| Interfaces/IPluginManager.h | Engine/Source/Runtime/Projects/Public/Interfaces/IPluginManager.h:1 | header only |
| limits | Engine/Source/Runtime/Core/Public/GenericPlatform/GenericPlatformMath.h:20 | header only (C++ standard header; the engine checkout has no copy, the row points at the engine file that includes it) |
| IPluginManager::Get | Engine/Source/Runtime/Projects/Public/Interfaces/IPluginManager.h:649 | header only |
| IPluginManager::FindPlugin | Engine/Source/Runtime/Projects/Public/Interfaces/IPluginManager.h:382 | header only (the FStringView overload) |
| IPlugin | Engine/Source/Runtime/Projects/Public/Interfaces/IPluginManager.h:110 | header only |
| IPlugin::GetBaseDir | Engine/Source/Runtime/Projects/Public/Interfaces/IPluginManager.h:140 | header only |
| FJsonValue::CompareEqual | Engine/Source/Runtime/Json/Public/Dom/JsonValue.h:139 | header only |
| FJsonValue::AsArray | Engine/Source/Runtime/Json/Public/Dom/JsonValue.h:38 | header only |
| FJsonValueObject | Engine/Source/Runtime/Json/Public/Dom/JsonValue.h:293 | header only |
| FJsonValueNull | Engine/Source/Runtime/Json/Public/Dom/JsonValue.h:312 | header only |
| FJsonObject::GetBoolField | Engine/Source/Runtime/Json/Public/Dom/JsonObject.h:261 | header only |
| FJsonObject::GetObjectField | Engine/Source/Runtime/Json/Public/Dom/JsonObject.h:290 | header only |
| FJsonObject::SetField | Engine/Source/Runtime/Json/Public/Dom/JsonObject.h:123 | header only |
| FJsonObject::SetNumberField | Engine/Source/Runtime/Json/Public/Dom/JsonObject.h:182 | header only |
| FJsonObject::SetBoolField | Engine/Source/Runtime/Json/Public/Dom/JsonObject.h:268 | header only |
| FJsonObject::SetArrayField | Engine/Source/Runtime/Json/Public/Dom/JsonObject.h:280 | header only (the const FString& and const TArray& overload) |
| FJsonObject::SetObjectField | Engine/Source/Runtime/Json/Public/Dom/JsonObject.h:297 | header only |
| EJson::Null | Engine/Source/Runtime/Json/Public/Serialization/JsonTypes.h:25 | header only |
| EJson::Boolean | Engine/Source/Runtime/Json/Public/Serialization/JsonTypes.h:28 | header only |
| EJson::Array | Engine/Source/Runtime/Json/Public/Serialization/JsonTypes.h:29 | header only |
| TOptional::GetValue | Engine/Source/Runtime/Core/Public/Misc/Optional.h:365 | header only (the const overload; non-const at :360) |
| TArray | Engine/Source/Runtime/Core/Public/Containers/Array.h:651 | header only |
| TArray::Num | Engine/Source/Runtime/Core/Public/Containers/Array.h:1116 | header only |
| TArray::SetNum | Engine/Source/Runtime/Core/Public/Containers/Array.h:2320 | header only |
| FString::AppendChar | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:447 | header only |
| TCHAR | Engine/Source/Runtime/Core/Public/GenericPlatform/GenericPlatform.h:84 | header only |
| FMath::IsFinite | Engine/Source/Runtime/Core/Public/GenericPlatform/GenericPlatformMath.h:586 | header only (FMath derives FPlatformMath, UnrealMathUtility.h:266; the double overload is :586) |
| FMath::Max | Engine/Source/Runtime/Core/Public/GenericPlatform/GenericPlatformMath.h:960 | header only (inherited through FPlatformMath) |
| FMath::Min | Engine/Source/Runtime/Core/Public/GenericPlatform/GenericPlatformMath.h:982 | header only (inherited through FPlatformMath) |
| EJson::Number | Engine/Source/Runtime/Json/Public/Serialization/JsonTypes.h:27 | header only |
| EJson::Object | Engine/Source/Runtime/Json/Public/Serialization/JsonTypes.h:30 | header only |
| FJsonValue::AsNumber | Engine/Source/Runtime/Json/Public/Dom/JsonValue.h:26 | header only |
| FJsonValue::AsBool | Engine/Source/Runtime/Json/Public/Dom/JsonValue.h:35 | header only |
| FJsonValue::AsObject | Engine/Source/Runtime/Json/Public/Dom/JsonValue.h:41 | header only |
| FUTF8ToTCHAR | Engine/Source/Runtime/Core/Public/Containers/StringConv.h:1017 | header only (alias of TStringConversion) |
| FString::ConstructFromPtrSize | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:128 | header only (the WIDECHAR overload; declared in this engine, no fallback needed) |
| FString::Join | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:2292 | header only |
| FString::LeftInline | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:1158 | header only |
| FDateTime::ParseIso8601 | Engine/Source/Runtime/Core/Public/Misc/DateTime.h:729 | header only (converts any offset to UTC per its doc, so Z times and offsets both work) |
| FMath::Clamp | Engine/Source/Runtime/Core/Public/Math/UnrealMathUtility.h:601 | header only (the double overload) |
| FMath::RoundToInt | Engine/Source/Runtime/Core/Public/Math/UnrealPlatformMathSSE.h:283 | header only (the double overload; FMath derives FPlatformMath) |
| TArray::GetData | Engine/Source/Runtime/Core/Public/Containers/Array.h:1009 | header only (the const overload; non-const at :999) |
| TArray::Append | Engine/Source/Runtime/Core/Public/Containers/Array.h:2505 | header only (the pointer and count overload) |
| FCString::Strcmp | Engine/Source/Runtime/Core/Public/Misc/CString.h:328 | header only (FCString is TCString<TCHAR>) |
| FTimespan | Engine/Source/Runtime/Core/Public/Misc/Timespan.h:75 | header only |
| FTimespan::FromMinutes | Engine/Source/Runtime/Core/Public/Misc/Timespan.h:661 | header only |
| FTimespan::FromSeconds | Engine/Source/Runtime/Core/Public/Misc/Timespan.h:673 | header only |
| FTimespan::Zero | Engine/Source/Runtime/Core/Public/Misc/Timespan.h:747 | header only |
| FTimespan::GetTotalSeconds | Engine/Source/Runtime/Core/Public/Misc/Timespan.h:520 | header only |
| TArray::RemoveAll | Engine/Source/Runtime/Core/Public/Containers/Array.h:3118 | header only (the predicate overload) |
| TArray::RemoveAt | Engine/Source/Runtime/Core/Public/Containers/Array.h:2118 | header only (the index and count overload) |
| Misc/Compression.h | Engine/Source/Runtime/Core/Public/Misc/Compression.h:1 | header only |
| FCompression::CompressMemoryBound | Engine/Source/Runtime/Core/Public/Misc/Compression.h:71 | header only (the int32 overload) |
| FCompression::CompressMemory | Engine/Source/Runtime/Core/Public/Misc/Compression.h:110 | header only (the int32 overload) |
| NAME_Gzip | Engine/Source/Runtime/Core/Public/UObject/UnrealNames.inl:207 | header only (REGISTER_NAME(258, Gzip)) |
| FString::Chr | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:1673 | header only |
| TArray::SetNumUninitialized | Engine/Source/Runtime/Core/Public/Containers/Array.h:2379 | header only |
| TPair | Engine/Source/Runtime/Core/Public/Templates/Tuple.h:46 | header only (alias of TTuple) |
| FJsonObject::GetStringField | Engine/Source/Runtime/Json/Public/Dom/JsonObject.h:185 | header only |
| FMath::Pow | Engine/Source/Runtime/Core/Public/GenericPlatform/GenericPlatformMath.h:555 | header only (the double overload; FMath derives FPlatformMath) |
| FCString::Atod | Engine/Source/Runtime/Core/Public/Misc/CString.h:506 | header only |
| FString::IsNumeric | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:1583 | header only (digits with one dot and an optional sign, no exponent, so "1e309" and HTTP dates are refused) |
| Misc/OutputDevice.h | Engine/Source/Runtime/Core/Public/Misc/OutputDevice.h:1 | header only |
| Misc/ScopeLock.h | Engine/Source/Runtime/Core/Public/Misc/ScopeLock.h:1 | header only |
| FOutputDevice | Engine/Source/Runtime/Core/Public/Misc/OutputDevice.h:132 | header only |
| ELogVerbosity::Type | Engine/Source/Runtime/Core/Public/Logging/LogVerbosity.h:16 | header only |
| FName | Engine/Source/Runtime/Core/Public/UObject/NameTypes.h:575 | header only |
| GLog | Engine/Source/Runtime/Core/Public/CoreGlobals.h:95 | header only (macro for GetGlobalLogSingleton(), an FOutputDeviceRedirector*, declared at CoreGlobals.h:68) |
| FOutputDeviceRedirector::AddOutputDevice | Engine/Source/Runtime/Core/Public/Misc/OutputDeviceRedirector.h:68 | header only |
| FOutputDeviceRedirector::RemoveOutputDevice | Engine/Source/Runtime/Core/Public/Misc/OutputDeviceRedirector.h:75 | header only |
| FOutputDeviceRedirector::Flush | Engine/Source/Runtime/Core/Public/Misc/OutputDeviceRedirector.h:147 | header only |
| FLogCategoryBase::GetCategoryName | Engine/Source/Runtime/Core/Public/Logging/LogCategory.h:41 | header only |
| FScopeLock | Engine/Source/Runtime/Core/Public/Misc/ScopeLock.h:140 | header only |
| FCriticalSection | Engine/Source/Runtime/Core/Public/HAL/CriticalSection.h:53 | header only (alias of UE::FPlatformRecursiveMutex) |
| HAL/PlatformTime.h | Engine/Source/Runtime/Core/Public/HAL/PlatformTime.h:1 | header only |
| FPlatformTime::Seconds | Engine/Source/Runtime/Core/Public/Windows/WindowsPlatformTime.h:23 | header only (FPlatformTime is a typedef of FWindowsPlatformTime, :64; the Linux build uses UnixPlatformTime.h) |
| Serialization/Archive.h | Engine/Source/Runtime/Core/Public/Serialization/Archive.h:1 | header only |
| HAL/CriticalSection.h | Engine/Source/Runtime/Core/Public/HAL/CriticalSection.h:1 | header only |
| FArchive | Engine/Source/Runtime/Core/Public/Serialization/Archive.h:1180 | header only |
| FArchive::Serialize | Engine/Source/Runtime/Core/Public/Serialization/Archive.h:1662 | header only |
| SetError | Engine/Source/Runtime/Core/Public/Serialization/Archive.h:1714 | header only (FArchive's using FArchiveState::SetError, declared at :110; called unqualified inside FCappedBody) |
| FArchive::IsError | Engine/Source/Runtime/Core/Public/Serialization/Archive.h:1972 | header only (using FArchiveState::IsError, :362) |
| SetIsSaving | Engine/Source/Runtime/Core/Public/Serialization/Archive.h:2195 | header only (FArchive's using FArchiveState::SetIsSaving, virtual at :947; called unqualified in FCappedBody's constructor) |
| TArray::SetNumZeroed | Engine/Source/Runtime/Core/Public/Containers/Array.h:2351 | header only |
| IHttpRequest | Engine/Source/Runtime/Online/HTTP/Public/Interfaces/IHttpRequest.h:142 | header only |
| IHttpRequest::SetContent | Engine/Source/Runtime/Online/HTTP/Public/Interfaces/IHttpRequest.h:195 | header only (the const TArray<uint8>& overload; the rvalue one is :205) |
| IHttpRequest::SetTimeout | Engine/Source/Runtime/Online/HTTP/Public/Interfaces/IHttpRequest.h:317 | header only |
| IHttpRequest::SetResponseBodyReceiveStream | Engine/Source/Runtime/Online/HTTP/Public/Interfaces/IHttpRequest.h:256 | header only (takes TSharedRef<FArchive>) |
| IHttpRequest::CancelRequest | Engine/Source/Runtime/Online/HTTP/Public/Interfaces/IHttpRequest.h:385 | header only |
| IHttpBase::GetEffectiveURL | Engine/Source/Runtime/Online/HTTP/Public/Interfaces/IHttpBase.h:104 | header only |
| IHttpBase::GetHeader | Engine/Source/Runtime/Online/HTTP/Public/Interfaces/IHttpBase.h:135 | header only |
| IHttpBase::GetContentType | Engine/Source/Runtime/Online/HTTP/Public/Interfaces/IHttpBase.h:149 | header only |
| ESPMode::ThreadSafe | Engine/Source/Runtime/Core/Public/Templates/SharedPointerFwd.h:17 | header only (enum class ESPMode, :11) |
| TWeakPtr | Engine/Source/Runtime/Core/Public/Templates/SharedPointer.h:1241 | header only |
| TSharedPtr::ToSharedRef | Engine/Source/Runtime/Core/Public/Templates/SharedPointer.h:998 | header only (the const& overload; && at :1012) |
| TDelegateBase::Unbind | Engine/Source/Runtime/Core/Public/Delegates/DelegateBase.h:202 | header only (FHttpRequestCompleteDelegate is a TTSDelegate, IHttpRequest.h:42) |
| Misc/CoreMisc.h | Engine/Source/Runtime/Core/Public/Misc/CoreMisc.h:1 | header only |
| IsRunningDedicatedServer | Engine/Source/Runtime/Core/Public/Misc/CoreMisc.h:152 | header only |
| FPaths::ProjectDir | Engine/Source/Runtime/Core/Public/Misc/Paths.h:251 | header only |
| FMath::FRand | Engine/Source/Runtime/Core/Public/GenericPlatform/GenericPlatformMath.h:635 | header only (inherited through FPlatformMath) |
| FReplyHandler | mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Ports.h:10 | the plugin's own declaration, not an engine symbol: check-api reads a using-alias and a pure virtual (= 0) as uses (Plan 2B Task 1) |
| ReadStatus | mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Ports.h:30 | the plugin's own declaration, not an engine symbol: check-api reads a using-alias and a pure virtual (= 0) as uses (Plan 2B Task 1) |
| ReadAutoPause | mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Ports.h:32 | the plugin's own declaration, not an engine symbol: check-api reads a using-alias and a pure virtual (= 0) as uses (Plan 2B Task 1) |
| ReadPower | mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Ports.h:34 | the plugin's own declaration, not an engine symbol: check-api reads a using-alias and a pure virtual (= 0) as uses (Plan 2B Task 1) |
| ApplyAutoPause | mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Ports.h:36 | the plugin's own declaration, not an engine symbol: check-api reads a using-alias and a pure virtual (= 0) as uses (Plan 2B Task 1) |
