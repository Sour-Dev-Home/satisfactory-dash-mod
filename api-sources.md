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
| TUniqueFunction | Engine/Source/Runtime/Core/Public/Templates/Function.h:827 | header only |
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
| FTCHARToUTF8_Convert | Engine/Source/Runtime/Core/Public/Containers/StringConv.h:362 | header only (global alias of UE::Core::Private::FTCHARToUTF8_Convert, class at :226; used by EncodeBody to convert straight into the output array) |
| FTCHARToUTF8_Convert::ConvertedLength | Engine/Source/Runtime/Core/Public/Containers/StringConv.h:342 | header only (UTF-8 byte count, no terminator) |
| FTCHARToUTF8_Convert::Convert | Engine/Source/Runtime/Core/Public/Containers/StringConv.h:327 | header only (the ToType = ANSICHAR* overload; returns bytes written or -1) |
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
| TSharedPtr::ToSharedRef | Engine/Source/Runtime/Core/Public/Templates/SharedPointer.h:998 | header only (the const& overload, inside TSharedPtr :664) |
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
| IPlugin::GetDescriptor | Engine/Source/Runtime/Projects/Public/Interfaces/IPluginManager.h:249 | header only (returns const FPluginDescriptor&) |
| FPluginDescriptor::VersionName | Engine/Source/Runtime/Projects/Public/PluginDescriptor.h:46 | header only (a field of struct FPluginDescriptor, :37) |
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
| TOptional::Get | Engine/Source/Runtime/Core/Public/Misc/Optional.h:389 | header only (the default-value overload) |
| TArray | Engine/Source/Runtime/Core/Public/Containers/Array.h:651 | header only |
| TArray::Num | Engine/Source/Runtime/Core/Public/Containers/Array.h:1116 | header only |
| TArray::SetNum | Engine/Source/Runtime/Core/Public/Containers/Array.h:2320 | header only |
| TArray::Shrink | Engine/Source/Runtime/Core/Public/Containers/Array.h:1269 | header only |
| FString::AppendChar | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:447 | header only |
| FString::AppendInt | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:2221 | header only (int32) |
| FString::Appendf | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:1614 | header only |
| FMath::FloorToDouble | Engine/Source/Runtime/Core/Public/GenericPlatform/GenericPlatformMath.h:295 | header only (declared in FGenericPlatformMath, which FMath inherits via FPlatformMath) |
| FMath::IsNaN | Engine/Source/Runtime/Core/Public/GenericPlatform/GenericPlatformMath.h:576 | header only (the double overload) |
| FMath::Abs | Engine/Source/Runtime/Core/Public/GenericPlatform/GenericPlatformMath.h:946 | header only (template) |
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
| FTimespan::FromMilliseconds | Engine/Source/Runtime/Core/Public/Misc/Timespan.h:649 | header only |
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
| FArchive | Engine/Source/Runtime/Core/Public/Serialization/Archive.h:1180 | header only |
| FArchive::Serialize | Engine/Source/Runtime/Core/Public/Serialization/Archive.h:1662 | header only |
| SetError | Engine/Source/Runtime/Core/Public/Serialization/Archive.h:1714 | header only (FArchive's using FArchiveState::SetError, declared at :110; called unqualified inside FCappedBody) |
| FArchive::IsError | Engine/Source/Runtime/Core/Public/Serialization/Archive.h:1972 | header only (using FArchiveState::IsError, :362) |
| SetIsSaving | Engine/Source/Runtime/Core/Public/Serialization/Archive.h:2195 | header only (FArchive's using FArchiveState::SetIsSaving, virtual at :947; called unqualified in FCappedBody's constructor) |
| TArray::SetNumZeroed | Engine/Source/Runtime/Core/Public/Containers/Array.h:2351 | header only |
| Misc/CoreMisc.h | Engine/Source/Runtime/Core/Public/Misc/CoreMisc.h:1 | header only |
| IsRunningDedicatedServer | Engine/Source/Runtime/Core/Public/Misc/CoreMisc.h:152 | header only |
| FPaths::ProjectDir | Engine/Source/Runtime/Core/Public/Misc/Paths.h:251 | header only |
| FMath::FRand | Engine/Source/Runtime/Core/Public/GenericPlatform/GenericPlatformMath.h:635 | header only (inherited through FPlatformMath) |
| FReplyHandler | mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Ports.h:10 | the plugin's own declaration, not an engine symbol: check-api reads a using-alias and a pure virtual (= 0) as uses (Plan 2B Task 1) |
| ReadStatus | mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Ports.h:30 | the plugin's own declaration, not an engine symbol: check-api reads a using-alias and a pure virtual (= 0) as uses (Plan 2B Task 1) |
| ReadAutoPause | mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Ports.h:32 | the plugin's own declaration, not an engine symbol: check-api reads a using-alias and a pure virtual (= 0) as uses (Plan 2B Task 1) |
| ReadPower | mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Ports.h:34 | the plugin's own declaration, not an engine symbol: check-api reads a using-alias and a pure virtual (= 0) as uses (Plan 2B Task 1) |
| ApplyAutoPause | mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/Ports.h:36 | the plugin's own declaration, not an engine symbol: check-api reads a using-alias and a pure virtual (= 0) as uses (Plan 2B Task 1) |
| ESearchDir::FromStart | Engine/Source/Runtime/Core/Public/Misc/CString.h:36 | header only (namespace ESearchDir, :31) |
| FString::Find | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:1314 | header only (the const TCHAR* overload with search case, direction and start position) |
| module:SSL | Engine/Source/Runtime/Online/SSL/SSL.Build.cs:1 | header only (WITH_SSL=1 on Win64 and Unix, :42; HTTP.Build.cs:135 depends on it the same way) |
| module:libcurl | Engine/Source/ThirdParty/libcurl/libcurl.Build.cs:1 | header only (an External module with a static libcurl.lib, :10 and :47, so the mod's DLL links its own copy; added with AddEngineThirdPartyPrivateStaticDependencies as HTTP.Build.cs:101 does; Plan 2B Task 1e) |
| module:OpenSSL | Engine/Source/ThirdParty/OpenSSL/OpenSSL.Build.cs:1 | header only (1.1.1t, static libssl.lib and libcrypto.lib, :51-52; added as HTTP.Build.cs:136 does) |
| HAL/Platform.h | Engine/Source/Runtime/Core/Public/HAL/Platform.h:1 | header only |
| PLATFORM_MICROSOFT | Engine/Source/Runtime/Core/Public/HAL/Platform.h:67 | header only (the default 0; platform headers set it) |
| Microsoft/AllowMicrosoftPlatformTypes.h | Engine/Source/Runtime/Core/Public/Microsoft/AllowMicrosoftPlatformTypes.h:1 | header only (used around curl.h as CurlHttp.h:16-18 does) |
| Microsoft/HideMicrosoftPlatformTypes.h | Engine/Source/Runtime/Core/Public/Microsoft/HideMicrosoftPlatformTypes.h:1 | header only (CurlHttp.h:26-28) |
| SslModule.h | Engine/Source/Runtime/Online/SSL/Public/SslModule.h:1 | header only |
| Interfaces/ISslCertificateManager.h | Engine/Source/Runtime/Online/SSL/Public/Interfaces/ISslCertificateManager.h:1 | header only |
| FSslModule::Get | Engine/Source/Runtime/Online/SSL/Public/SslModule.h:35 | header only (SSL_API; loads the module on demand on the game thread, SslModule.cpp:69-78) |
| FSslModule::GetCertificateManager | Engine/Source/Runtime/Online/SSL/Public/SslModule.h:42 | header only (inline) |
| ISslCertificateManager::AddCertificatesToSslContext | Engine/Source/Runtime/Online/SSL/Public/Interfaces/ISslCertificateManager.h:28 | header only (what the engine's sslctx_function calls, CurlHttp.cpp:55-60; its pinning callback, :31-53 and :61-66, is not mirrored, Task 1e) |
| SSL_CTX | Engine/Source/Runtime/Online/SSL/Public/Interfaces/ISslCertificateManager.h:11 | header only (forward typedef of ssl_ctx_st, so no OpenSSL include is needed) |
| WITH_SSL | Engine/Source/Runtime/Online/SSL/SSL.Build.cs:42 | header only (a public definition of the SSL module; CurlHttp.cpp:181-186 wires certificates under it) |
| TUniquePtr | Engine/Source/Runtime/Core/Public/Templates/UniquePtr.h:106 | header only |
| MakeUnique | Engine/Source/Runtime/Core/Public/Templates/UniquePtr.h:796 | header only (the TArgs&&... overload) |
| curl/curl.h | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:1 | header only (libcurl 8.12.1, ThirdParty; libcurl.Build.cs:15-16 adds the include path) |
| CURLcode | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:641 | header only (the enum from :511; CURLE_OK :512) |
| CURLoption | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:2232 | header only |
| CURLINFO | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:2965 | header only |
| CURL | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:117 | header only (typedef void CURL) |
| curl_global_init | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:2716 | header only (the mod's own copy, once, in StartupModule) |
| curl_global_cleanup | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:2746 | header only (in ShutdownModule after CancelAll, only if init succeeded) |
| CURL_GLOBAL_ALL | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:2985 | header only |
| curl_easy_init | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/easy.h:41 | header only |
| curl_easy_setopt | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/easy.h:42 | header only |
| curl_easy_cleanup | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/easy.h:44 | header only |
| curl_easy_getinfo | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/easy.h:59 | header only |
| curl_slist | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:2763 | header only |
| curl_slist_append | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:2818 | header only (copies the string) |
| curl_slist_free_all | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:2828 | header only |
| CURLM | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/multi.h:57 | header only (typedef void CURLM) |
| CURLM_OK | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/multi.h:62 | header only (CURLMcode, :78) |
| CURLMsg | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/multi.h:97 | header only (easy_handle and data.result, :97-104) |
| CURLMSG_DONE | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/multi.h:92 | header only |
| curl_multi_init | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/multi.h:127 | header only |
| curl_multi_add_handle | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/multi.h:136 | header only |
| curl_multi_remove_handle | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/multi.h:146 | header only |
| curl_multi_perform | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/multi.h:217 | header only |
| curl_multi_cleanup | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/multi.h:230 | header only |
| curl_multi_info_read | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/multi.h:260 | header only |
| CURLOPT_URL | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:1121 | header only |
| CURLOPT_PROXY | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:1127 | header only (CURLOPTTYPE_STRINGPOINT, "Name of proxy to use"; set to "" for plain-http loopback only, security review F3. The header doesn't say that "" turns off environment proxies too; that is libcurl's CURLOPT_PROXY man page, not captured here) |
| CURLOPT_FOLLOWLOCATION | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:1294 | header only (set to 0: UE's request sets 1, CurlHttp.cpp:127) |
| CURLOPT_MAXREDIRS | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:1349 | header only (0, a second line) |
| CURLOPT_PROTOCOLS_STR | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:2205 | header only ("https") |
| CURLOPT_REDIR_PROTOCOLS_STR | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:2208 | header only ("https") |
| CURLOPT_SSL_VERIFYPEER | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:1339 | header only (1) |
| CURLOPT_SSL_VERIFYHOST | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:1399 | header only (2) |
| CURLOPT_SSLVERSION | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:1235 | header only (a minimum; no CURL_SSLVERSION_MAX_* is or'd in) |
| CURL_SSLVERSION_TLSv1_2 | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:2342 | header only |
| CURLOPT_CAINFO | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:1343 | header only (nullptr, as CurlHttp.cpp:183) |
| CURLOPT_SSL_CTX_FUNCTION | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:1503 | header only (curl_ssl_ctx_callback, :776; as CurlHttp.cpp:184) |
| CURLOPT_NOSIGNAL | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:1464 | header only |
| CURLOPT_HTTP_VERSION | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:1410 | header only (CURL_HTTP_VERSION_1_1, :2285, UE's default, CurlHttp.cpp:107-114) |
| CURL_HTTP_VERSION_1_1 | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:2285 | header only |
| CURLOPT_TIMEOUT_MS | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:1678 | header only |
| CURLOPT_CONNECTTIMEOUT_MS | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:1679 | header only |
| CURLOPT_WRITEFUNCTION | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:1149 | header only (curl_write_callback, :276; a short count aborts with CURLE_WRITE_ERROR) |
| CURLOPT_WRITEDATA | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:1118 | header only |
| CURLOPT_HEADERFUNCTION | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:1389 | header only |
| CURLOPT_HEADERDATA | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:1227 | header only |
| CURLOPT_HTTPHEADER | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:1207 | header only |
| CURLOPT_HTTPGET | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:1394 | header only |
| CURLOPT_POST | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:1281 | header only |
| CURLOPT_POSTFIELDS | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:1170 | header only (read in place, not copied: the body lives in FPending) |
| CURLOPT_POSTFIELDSIZE | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:1325 | header only (a long) |
| CURLOPT_CUSTOMREQUEST | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:1250 | header only |
| CURLINFO_RESPONSE_CODE | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:2872 | header only (a long) |
| CURLINFO_CONTENT_TYPE | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:2908 | header only |
| CURLINFO_EFFECTIVE_URL | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:2871 | header only (UE read the same, CurlHttp.cpp:519) |
| CURLE_OPERATION_TIMEDOUT | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:548 | header only (the other CURLcode values in failureWord are on :513-636) |
| CURLE_PEER_FAILED_VERIFICATION | Engine/Source/ThirdParty/libcurl/8.12.1/include/curl/curl.h:581 | header only (an untrusted certificate: logged as TlsError) |
| module:nghttp2 | Engine/Source/ThirdParty/nghttp2/nghttp2.Build.cs:1 | header only (libcurl needs it, libcurl.Build.cs:51-56; its dependency is private, so the mod names it) |
| module:zlib | Engine/Source/ThirdParty/zlib/zlib.Build.cs:1 | header only (libcurl needs it, libcurl.Build.cs:51-56; its dependency is private, so the mod names it) |
| ELogVerbosity::VerbosityMask | Engine/Source/Runtime/Core/Public/Logging/LogVerbosity.h:58 | header only |
| ELogVerbosity::Error | Engine/Source/Runtime/Core/Public/Logging/LogVerbosity.h:28 | header only |
| TMap::GetKeys | Engine/Source/Runtime/Core/Public/Containers/Map.h:327 | header only (the TArray overload; declared in TMapBase, which TMap inherits) |
| TArray::Sort | Engine/Source/Runtime/Core/Public/Containers/Array.h:3425 | header only (the no-predicate overload) |
| TArray::StableSort | Engine/Source/Runtime/Core/Public/Containers/Array.h:3475 | header only (the predicate overload; :3457 is the no-predicate one) |
| FMath::RoundToDouble | Engine/Source/Runtime/Core/Public/GenericPlatform/GenericPlatformMath.h:341 | header only (declared in FGenericPlatformMath, which FMath inherits via FPlatformMath) |
| FMemory::Memcpy | Engine/Source/Runtime/Core/Public/HAL/UnrealMemory.h:160 | header only (the bit copy of a double; a cast would break strict aliasing) |
| TArray::IsValidIndex | Engine/Source/Runtime/Core/Public/Containers/Array.h:1094 | header only |
| TArray::Reserve | Engine/Source/Runtime/Core/Public/Containers/Array.h:3026 | header only |
| FPowerCircuitStats::MaximumPowerConsumption | Source/FactoryGame/Public/FGPowerCircuit.h:110 | header only |
| UFGCircuit::GetCircuitGroupID | Source/FactoryGame/Public/FGCircuit.h:71 | header only |
| UFGPowerCircuit::IsFuseTriggered | Source/FactoryGame/Public/FGPowerCircuit.h:163 | header only |
| UFGPowerCircuit::HasBatteries | Source/FactoryGame/Public/FGPowerCircuit.h:211 | header only |
| UFGPowerCircuit::GetBatterySumPowerStore | Source/FactoryGame/Public/FGPowerCircuit.h:215 | header only |
| UFGPowerCircuit::GetBatterySumPowerStoreCapacity | Source/FactoryGame/Public/FGPowerCircuit.h:219 | header only |
| UFGPowerCircuit::GetBatterySumPowerInput | Source/FactoryGame/Public/FGPowerCircuit.h:227 | header only (clamped at 0, the charge half) |
| UFGPowerCircuit::GetBatterySumPowerOutput | Source/FactoryGame/Public/FGPowerCircuit.h:231 | header only (clamped at 0, the discharge half) |
| TJsonReaderFactory::CreateFromView | Engine/Source/Runtime/Json/Public/Serialization/JsonReader.h:1085 | header only (TJsonStringViewReader, :1020; the engine's own UTF8CHAR use is Json/Private/Tests/JsonTests.cpp:752) |
| TJsonReader::ReadNext | Engine/Source/Runtime/Json/Public/Serialization/JsonReader.h:57 | header only (true with EJsonNotation::Error on bad input; false at a clean end or after an error, :59-91) |
| TJsonReader::GetIdentifier | Engine/Source/Runtime/Json/Public/Serialization/JsonReader.h:164 | header only |
| TJsonReader::GetValueAsString | Engine/Source/Runtime/Json/Public/Serialization/JsonReader.h:166 | header only (check()s the token: called only after a String notation) |
| TJsonReader::GetValueAsNumber | Engine/Source/Runtime/Json/Public/Serialization/JsonReader.h:192 | header only (called only after a Number notation) |
| TJsonReader::GetValueAsBoolean | Engine/Source/Runtime/Json/Public/Serialization/JsonReader.h:204 | header only (called only after a Boolean notation) |
| TJsonReader::GetErrorMessage | Engine/Source/Runtime/Json/Public/Serialization/JsonReader.h:210 | header only |
| EJsonNotation | Engine/Source/Runtime/Json/Public/Serialization/JsonTypes.h:59 | header only |
| EJsonNotation::ObjectStart | Engine/Source/Runtime/Json/Public/Serialization/JsonTypes.h:61 | header only |
| EJsonNotation::ObjectEnd | Engine/Source/Runtime/Json/Public/Serialization/JsonTypes.h:62 | header only |
| EJsonNotation::ArrayStart | Engine/Source/Runtime/Json/Public/Serialization/JsonTypes.h:63 | header only |
| EJsonNotation::ArrayEnd | Engine/Source/Runtime/Json/Public/Serialization/JsonTypes.h:64 | header only |
| EJsonNotation::Boolean | Engine/Source/Runtime/Json/Public/Serialization/JsonTypes.h:65 | header only |
| EJsonNotation::String | Engine/Source/Runtime/Json/Public/Serialization/JsonTypes.h:66 | header only |
| EJsonNotation::Number | Engine/Source/Runtime/Json/Public/Serialization/JsonTypes.h:67 | header only |
| EJsonNotation::Null | Engine/Source/Runtime/Json/Public/Serialization/JsonTypes.h:68 | header only |
| EJsonNotation::Error | Engine/Source/Runtime/Json/Public/Serialization/JsonTypes.h:69 | header only |
| TStringView | Engine/Source/Runtime/Core/Public/Containers/StringView.h:93 | header only (the pointer-and-size constructor, :111; the brief said :143) |
| UTF8CHAR | Engine/Source/Runtime/Core/Public/HAL/Platform.h:1087 | header only |
| TJsonWriterFactory::Create | Engine/Source/Runtime/Json/Public/Serialization/JsonWriter.h:697 | header only (the StringType* overload) |
| TJsonWriter::WriteObjectStart | Engine/Source/Runtime/Json/Public/Serialization/JsonWriter.h:109 | header only (the named overload is :127) |
| TJsonWriter::WriteObjectEnd | Engine/Source/Runtime/Json/Public/Serialization/JsonWriter.h:140 | header only |
| TJsonWriter::WriteArrayStart | Engine/Source/Runtime/Json/Public/Serialization/JsonWriter.h:153 | header only (the named overload is :171) |
| TJsonWriter::WriteArrayEnd | Engine/Source/Runtime/Json/Public/Serialization/JsonWriter.h:183 | header only |
| TJsonWriter::WriteValue | Engine/Source/Runtime/Json/Public/Serialization/JsonWriter.h:243 | header only (named; doubles with 17 significant digits, :438-443) |
| TJsonWriter::WriteNull | Engine/Source/Runtime/Json/Public/Serialization/JsonWriter.h:305 | header only (the named overload) |
| TJsonWriter::Close | Engine/Source/Runtime/Json/Public/Serialization/JsonWriter.h:334 | header only |
| FMath::Fmod | Engine/Source/Runtime/Core/Public/GenericPlatform/GenericPlatformMath.h:517 | header only (the double overload; Fmod(-2.8, 2) is -0.8, as JavaScript's %, :509) |
| FString::Empty | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:327 | header only |
| FString::LeftChop | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:1171 | header only (the const& overload; test only) |
| FString::Mid | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:1245 | header only (the Start-only const& overload, :1245, which calls RightChop; the test's Mid(Start, Count) is :1241) |
| FJsonObject::GetArrayField | Engine/Source/Runtime/Json/Public/Dom/JsonObject.h:271 | header only (the FStringView overload; test only) |
| Tasks/Task.h | Engine/Source/Runtime/Core/Public/Tasks/Task.h:1 | header only |
| UE::Tasks::Launch | Engine/Source/Runtime/Core/Public/Tasks/Task.h:299 | header only (the overload without prerequisites) |
| UE::Tasks::FTask | Engine/Source/Runtime/Core/Public/Tasks/Task.h:392 | header only (alias of Private::FTaskHandle, :33) |
| FTaskHandle::IsCompleted | Engine/Source/Runtime/Core/Public/Tasks/Task.h:75 | header only (true when not valid, :77; reads an atomic bIsClosed with seq_cst order, TaskPrivate.h:816-818, stored after the body ran, :812) |
| FTaskHandle::IsValid | Engine/Source/Runtime/Core/Public/Tasks/Task.h:69 | header only |
| FTaskHandle::Wait | Engine/Source/Runtime/Core/Public/Tasks/Task.h:91 | header only (the overload without a timeout) |
| UE::Tasks::ETaskPriority::BackgroundLow | Engine/Source/Runtime/Core/Public/Async/Fundamental/Task.h:25 | header only (LowLevelTasks::ETaskPriority, aliased into UE::Tasks at Tasks/TaskPrivate.h:54) |
| Hash/Blake3.h | Engine/Source/Runtime/Core/Public/Hash/Blake3.h:1 | header only |
| FBlake3Hash | Engine/Source/Runtime/Core/Public/Hash/Blake3.h:26 | header only (32 bytes; operator== at :58) |
| FBlake3 | Engine/Source/Runtime/Core/Public/Hash/Blake3.h:93 | header only (CORE_API members) |
| FBlake3::Update | Engine/Source/Runtime/Core/Public/Hash/Blake3.h:106 | header only (the const void*, uint64 overload) |
| FBlake3::Finalize | Engine/Source/Runtime/Core/Public/Hash/Blake3.h:114 | header only |
| FBlake3::HashBuffer | Engine/Source/Runtime/Core/Public/Hash/Blake3.h:118 | header only (tests; the const void*, uint64 overload; the brief said :117, the header has it at :118) |
| FGGameState.h | Source/FactoryGame/Public/FGGameState.h:1 | header only |
| AFGGameState | Source/FactoryGame/Public/FGGameState.h:116 | header only |
| AFGGameState::GetOfflineCachedPlayerInfosInSession | Source/FactoryGame/Public/FGGameState.h:519 | header only (public; returns a copy of the players not online in the session; the S1 survey did not find it). No longer used: its first call crashed the dedicated server with an access violation inside it (core run 40241942772) |
| AFGGameState::GetAllPlayerInfoData | Source/FactoryGame/Public/FGGameState.h:515 | header only (FORCEINLINE, returns `mPlayerInfoCache`, :845, by const reference) |
| FCachedPlayerPlatformInfo::UniqueNetId | Source/FactoryGame/Public/Online/PlayerInfoCache.h:16 | header only (`FUniqueNetIdRepl`) |
| APlayerState::GetUniqueId | Engine/Source/Runtime/Engine/Classes/GameFramework/PlayerState.h:408 | header only (returns `const FUniqueNetIdRepl&`) |
| FUniqueNetIdRepl | Engine/Source/Runtime/Engine/Classes/GameFramework/OnlineReplStructs.h:25 | header only (derives from FUniqueNetIdWrapper) |
| FUniqueNetIdWrapper::IsValid | Engine/Source/Runtime/CoreOnline/Public/Online/CoreOnline.h:590 | header only |
| FUniqueNetIdWrapper operator== | Engine/Source/Runtime/CoreOnline/Public/Online/CoreOnline.h:671 | header only (two invalid ids compare equal, so callers check IsValid first) |
| TArray::ContainsByPredicate | Engine/Source/Runtime/Core/Public/Containers/Array.h:1545 | header only |
| Online/PlayerInfoCache.h | Source/FactoryGame/Public/Online/PlayerInfoCache.h:1 | header only |
| FCachedPlayerInfo | Source/FactoryGame/Public/Online/PlayerInfoCache.h:49 | header only |
| FCachedPlayerInfo::PerPlatformInfo | Source/FactoryGame/Public/Online/PlayerInfoCache.h:55 | header only (the UPROPERTY comment is on :53, the declaration `TArray<FCachedPlayerPlatformInfo> PerPlatformInfo;` on :55) |
| FCachedPlayerInfo::GetLastSeenServicesType | Source/FactoryGame/Public/Online/PlayerInfoCache.h:62 | header only |
| FCachedPlayerInfo::GetForPlatform | Source/FactoryGame/Public/Online/PlayerInfoCache.h:73 | header only (returns `const FCachedPlayerPlatformInfo*`, null when none) |
| FCachedPlayerPlatformInfo | Source/FactoryGame/Public/Online/PlayerInfoCache.h:10 | header only |
| FCachedPlayerPlatformInfo::PlayerName | Source/FactoryGame/Public/Online/PlayerInfoCache.h:20 | header only |
| GameFramework/GameStateBase.h | Engine/Source/Runtime/Engine/Classes/GameFramework/GameStateBase.h:1 | header only |
| AGameStateBase::PlayerArray | Engine/Source/Runtime/Engine/Classes/GameFramework/GameStateBase.h:55 | header only (public) |
| GameFramework/PlayerState.h | Engine/Source/Runtime/Engine/Classes/GameFramework/PlayerState.h:1 | header only |
| APlayerState | Engine/Source/Runtime/Engine/Classes/GameFramework/PlayerState.h:41 | header only |
| APlayerState::GetPlayerName | Engine/Source/Runtime/Engine/Classes/GameFramework/PlayerState.h:232 | header only |
| UWorld::GetGameState | Engine/Source/Runtime/Engine/Classes/Engine/World.h:3727 | header only (template, `Cast<T>(GameState)`) |
| Resources/FGResourceNode.h | Source/FactoryGame/Public/Resources/FGResourceNode.h:1 | header only |
| AFGResourceNode | Source/FactoryGame/Public/Resources/FGResourceNode.h:66 | header only (AFGResourceDeposit, FGResourceDeposit.h:15, and AFGResourceNodeFrackingSatellite, FGResourceNodeFrackingSatellite.h:23, derive from it; fracking cores, FGResourceNodeFrackingCore.h:14, derive from AFGResourceNodeBase and are not matched) |
| AFGResourceNode::GetResourcePurity | Source/FactoryGame/Public/Resources/FGResourceNode.h:113 | header only |
| RP_Inpure | Source/FactoryGame/Public/Resources/FGResourceNode.h:28 | header only (EResourcePurity :26 is a plain enum; the enumerator is spelled RP_Inpure, DisplayName "Impure") |
| RP_Normal | Source/FactoryGame/Public/Resources/FGResourceNode.h:29 | header only |
| RP_Pure | Source/FactoryGame/Public/Resources/FGResourceNode.h:30 | header only |
| Resources/FGResourceNodeBase.h | Source/FactoryGame/Public/Resources/FGResourceNodeBase.h:1 | header only |
| EResourceNodeType | Source/FactoryGame/Public/Resources/FGResourceNodeBase.h:24 | header only (Node, FrackingSatellite, FrackingCore, Geyser, Deposit, Invalid: only Node, FrackingSatellite and Geyser are drawn) |
| EResourceNodeType::Node | Source/FactoryGame/Public/Resources/FGResourceNodeBase.h:26 | header only |
| EResourceNodeType::FrackingSatellite | Source/FactoryGame/Public/Resources/FGResourceNodeBase.h:27 | header only |
| EResourceNodeType::Geyser | Source/FactoryGame/Public/Resources/FGResourceNodeBase.h:29 | header only |
| AFGResourceNodeBase | Source/FactoryGame/Public/Resources/FGResourceNodeBase.h:93 | header only (the class declaration; its UCLASS macro is on :92) |
| AFGResourceNodeBase::GetResourceNodeType | Source/FactoryGame/Public/Resources/FGResourceNodeBase.h:176 | header only |
| AFGResourceNodeBase::GetResourceName | Source/FactoryGame/Public/Resources/FGResourceNodeBase.h:180 | header only (FText, "Localized name") |
| Resources/FGResourceDescriptor.h | Source/FactoryGame/Public/Resources/FGResourceDescriptor.h:1 | header only |
| UFGResourceDescriptor | Source/FactoryGame/Public/Resources/FGResourceDescriptor.h:13 | header only (derives from UFGItemDescriptor; the class whose short name, Desc_OreIron_C, is the node's className) |
| AFGResourceNodeBase::GetResourceClass | Source/FactoryGame/Public/Resources/FGResourceNodeBase.h:144 | header only (H: returns the effective class, `TSubclassOf< UFGResourceDescriptor >`; GetResourceClassOverride :146 and GetResourceClassOriginal :150 are the parts. The override's behaviour is parity's question; survey S2 says the same) |
| TSubclassOf | Engine/Source/Runtime/CoreUObject/Public/Templates/SubclassOf.h:29 | header only (the template; :12 is a forward declaration; operator-> at :125 returns UClass*) |
| UObjectBaseUtility::GetName | Engine/Source/Runtime/CoreUObject/Public/UObject/UObjectBaseUtility.h:429 | header only (FString, the object's short name, not the path: a class's is Desc_OreIron_C) |
| AFGResourceNodeBase::IsOccupied | Source/FactoryGame/Public/Resources/FGResourceNodeBase.h:140 | header only |
| Engine/Level.h | Engine/Source/Runtime/Engine/Classes/Engine/Level.h:1 | header only |
| ULevel | Engine/Source/Runtime/Engine/Classes/Engine/Level.h:422 | header only |
| ULevel::Actors | Engine/Source/Runtime/Engine/Classes/Engine/Level.h:432 | header only (public) |
| UWorld::GetLevels | Engine/Source/Runtime/Engine/Classes/Engine/World.h:3443 | header only |
| AActor::GetActorLocation | Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h:2505 | header only (the survey's :1580 is K2_GetActorLocation) |
| FString::Compare | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:1524 | header only |
| FJsonObject::GetNumberField | Engine/Source/Runtime/Json/Public/Dom/JsonObject.h:140 | header only (the FStringView overload; test only) |
| AActor | Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h:253 | header only |
| FVector | Engine/Source/Runtime/Core/Public/Math/MathFwd.h:47 | header only (a using for UE::Math::TVector<double>) |
| Buildables/FGBuildableManufacturer.h | Source/FactoryGame/Public/Buildables/FGBuildableManufacturer.h:1 | header only (R2a, S2 Task 7) |
| AFGBuildableManufacturer | Source/FactoryGame/Public/Buildables/FGBuildableManufacturer.h:85 | header only |
| AFGBuildableManufacturer::StaticClass | Engine/Source/Runtime/CoreUObject/Public/UObject/ObjectMacros.h:1824 | header only (generated by DECLARE_CLASS from GENERATED_BODY, FGBuildableManufacturer.h:85; used only for GetBuildableCount's class filter) |
| AFGBuildableManufacturer::GetInputInventory | Source/FactoryGame/Public/Buildables/FGBuildableManufacturer.h:157 | header only (may be null: checked) |
| AFGBuildableManufacturer::GetOutputInventory | Source/FactoryGame/Public/Buildables/FGBuildableManufacturer.h:161 | header only (may be null: checked) |
| AFGBuildableManufacturer::GetCurrentRecipe | Source/FactoryGame/Public/Buildables/FGBuildableManufacturer.h:165 | header only (a null class is "no recipe") |
| AFGBuildableFactory::GetPowerInfo | Source/FactoryGame/Public/Buildables/FGBuildableFactory.h:108 | header only (nullptr if the factory does not run on power: checked) |
| AFGBuildableFactory::IsProducing | Source/FactoryGame/Public/Buildables/FGBuildableFactory.h:151 | header only |
| AFGBuildableFactory::IsProductionPaused | Source/FactoryGame/Public/Buildables/FGBuildableFactory.h:184 | header only |
| AFGBuildableFactory::GetProductivity | Source/FactoryGame/Public/Buildables/FGBuildableFactory.h:213 | header only (the scale is not documented: the parity run judges it, see ProductivityFullScale) |
| AFGBuildableFactory::GetCurrentPotential | Source/FactoryGame/Public/Buildables/FGBuildableFactory.h:225 | header only |
| AFGBuildableFactory::GetCurrentProductionBoost | Source/FactoryGame/Public/Buildables/FGBuildableFactory.h:315 | header only (the multiplier; above 1 counts a boosted machine in the log) |
| AFGBuildable | Source/FactoryGame/Public/Buildables/FGBuildable.h:167 | header only (the element type of the subsystem's list) |
| FGBuildableSubsystem.h | Source/FactoryGame/Public/FGBuildableSubsystem.h:1 | header only |
| AFGBuildableSubsystem | Source/FactoryGame/Public/FGBuildableSubsystem.h:246 | header only |
| AFGBuildableSubsystem::Get | Source/FactoryGame/Public/FGBuildableSubsystem.h:272 | header only (the UWorld* overload; can be nullptr at game end or start) |
| AFGBuildableSubsystem::GetAllBuildablesRef | Source/FactoryGame/Public/FGBuildableSubsystem.h:360 | header only (header: "can be invalidated by buildables being added/removed": fetched again for every element, never kept between ticks) |
| AFGBuildableSubsystem::GetBuildableCount | Source/FactoryGame/Public/FGBuildableSubsystem.h:363 | header only (advisory count for the log) |
| FGPowerInfoComponent.h | Source/FactoryGame/Public/FGPowerInfoComponent.h:1 | header only |
| UFGPowerInfoComponent | Source/FactoryGame/Public/FGPowerInfoComponent.h:51 | header only |
| UFGPowerInfoComponent::GetPowerCircuit | Source/FactoryGame/Public/FGPowerInfoComponent.h:80 | header only (nullptr if not connected; "can be changed/removed at any time", so never saved) |
| FGRecipe.h | Source/FactoryGame/Public/FGRecipe.h:1 | header only |
| UFGRecipe | Source/FactoryGame/Public/FGRecipe.h:40 | header only |
| UFGRecipe::GetRecipeName | Source/FactoryGame/Public/FGRecipe.h:55 | header only |
| UFGRecipe::GetIngredients | Source/FactoryGame/Public/FGRecipe.h:59 | header only (the static taking a world context, used so the game's own effective cost is read; the instance getter at :127 is the raw list) |
| UFGRecipe::GetProducts | Source/FactoryGame/Public/FGRecipe.h:63 | header only (the static; the instance getter is at :126) |
| UFGRecipe::GetManufacturingDuration | Source/FactoryGame/Public/FGRecipe.h:75 | header only (the static: "the base manufacturing duration", no clock or boost) |
| FItemAmount | Source/FactoryGame/Public/ItemAmount.h:14 | header only (ItemClass :32, Amount :36) |
| FGInventoryComponent.h | Source/FactoryGame/Public/FGInventoryComponent.h:1 | header only |
| UFGInventoryComponent | Source/FactoryGame/Public/FGInventoryComponent.h:158 | header only |
| UFGInventoryComponent::GetSizeLinear | Source/FactoryGame/Public/FGInventoryComponent.h:443 | header only |
| UFGInventoryComponent::GetStackFromIndex | Source/FactoryGame/Public/FGInventoryComponent.h:326 | header only (the const two-argument overload; true for a valid index, the caller checks NumItems; the other overloads at :602 and :607 are not used) |
| UFGInventoryComponent::GetSlotSize | Source/FactoryGame/Public/FGInventoryComponent.h:493 | header only (comment: "the arbitrary slot size if one is set, otherwise the stack size": the slot's own capacity, so used for MaxAmount in place of the survey's GetStackSize, Resources/FGItemDescriptor.h:251; the parity run judges it) |
| FInventoryStack | Source/FactoryGame/Public/FGInventoryComponent.h:95 | header only (Item :112, NumItems :116) |
| FInventoryItem::GetItemClass | Source/FactoryGame/Public/FGInventoryComponent.h:45 | header only |
| Resources/FGItemDescriptor.h | Source/FactoryGame/Public/Resources/FGItemDescriptor.h:1 | header only |
| UFGItemDescriptor | Source/FactoryGame/Public/Resources/FGItemDescriptor.h:139 | header only |
| UFGItemDescriptor::GetForm | Source/FactoryGame/Public/Resources/FGItemDescriptor.h:158 | header only |
| UFGItemDescriptor::GetItemName | Source/FactoryGame/Public/Resources/FGItemDescriptor.h:191 | header only |
| EResourceForm | Source/FactoryGame/Public/Resources/FGItemDescriptor.h:18 | header only (RF_INVALID, RF_SOLID, RF_LIQUID, RF_GAS) |
| EResourceForm::RF_LIQUID | Source/FactoryGame/Public/Resources/FGItemDescriptor.h:22 | header only (a fluid: its counts are litres, scaled in Core) |
| EResourceForm::RF_GAS | Source/FactoryGame/Public/Resources/FGItemDescriptor.h:23 | header only (a fluid: its counts are litres, scaled in Core) |
| AActor::GetActorRotation | Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h:2511 | header only (the survey's :1601 is K2_GetActorRotation) |
| UObjectBase::GetClass | Engine/Source/Runtime/CoreUObject/Public/UObject/UObjectBase.h:190 | header only |
| IsValid | Engine/Source/Runtime/CoreUObject/Public/UObject/Object.h:1857 | header only (IsValid(const UObject*): null or pending-kill is false) |
| FStepBudget::Begin | mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/StepBudget.h:28 | the plugin's own declaration, not an engine symbol (Task 2): the game reader calls it on the budget the driver passes |
| FStepBudget::Spent | mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/StepBudget.h:30 | the plugin's own declaration (Task 2) |
| FStepBudget::End | mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/StepBudget.h:32 | the plugin's own declaration (Task 2) |
| FStepBudget::Steps | mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/StepBudget.h:33 | the plugin's own declaration (Task 2) |
| FStepBudget::LongestMs | mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/StepBudget.h:34 | the plugin's own declaration (Task 2) |
| FStepBudget::TotalMs | mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/StepBudget.h:35 | the plugin's own declaration (Task 2) |
