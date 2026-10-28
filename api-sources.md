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
| FString::ReplaceInline | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:2059 | header only (returns the number of replacements; the test counts an id's copies with it) |
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
| FString::SanitizeFloat | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:2281 | header only (InMinFractionalDigits 0 gives "60" for 60.0 and "12.346" for 12.346; the station rate writer uses 0) |
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
| TArray::Swap | Engine/Source/Runtime/Core/Public/Containers/Array.h:3307 | header only |
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
| UObjectBaseUtility::IsA | Engine/Source/Runtime/CoreUObject/Public/UObject/UObjectBaseUtility.h:626 | header only (template<class T> bool IsA() const: "true if this object is of the template type"; the platform walk keeps a second station out) |
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
| FStepBudget::SetTag | mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/StepBudget.h:37 | the plugin's own declaration (longest-step diagnostic, 2026-10-27) |
| FStepBudget::LongestItems | mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/StepBudget.h:39 | the plugin's own declaration (longest-step diagnostic) |
| FStepBudget::LongestStartTag | mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/StepBudget.h:41 | the plugin's own declaration (longest-step diagnostic) |
| FStepBudget::LongestEndTag | mod/SatisDashAgent/Source/SatisDashCore/Public/SatisDash/StepBudget.h:42 | the plugin's own declaration (longest-step diagnostic) |
| LogisticsStepWork | mod/SatisDashAgent/Source/SatisDashCoreTests/Private/DriverFakes.h:240 | the test fake's own hook (longest-step diagnostic test) |
| FJsonObject::TryGetArrayField | Engine/Source/Runtime/Json/Public/Dom/JsonObject.h:274 | header only (the FStringView overload; test only, Task 8) |
| FJsonValue::TryGetString | Engine/Source/Runtime/Json/Public/Dom/JsonValue.h:74 | header only (test only, Task 8) |
| FGRailroadSubsystem.h | Source/FactoryGame/Public/FGRailroadSubsystem.h:1 | header only (R3, S2 Task 9) |
| AFGRailroadSubsystem | Source/FactoryGame/Public/FGRailroadSubsystem.h:88 | header only |
| AFGRailroadSubsystem::Get | Source/FactoryGame/Public/FGRailroadSubsystem.h:117 | header only (the `UWorld*` overload; null before a world has the subsystem: the read is then not begun) |
| AFGRailroadSubsystem::GetAllTrains | Source/FactoryGame/Public/FGRailroadSubsystem.h:174 | header only (const, public; fills an array of `AFGTrain*`, copied once into weak pointers) |
| AFGRailroadSubsystem::GetAllTrainStations | Source/FactoryGame/Public/FGRailroadSubsystem.h:207 | header only (const, public; fills an array of `AFGTrainStationIdentifier*`, copied once into weak pointers) |
| FGTrain.h | Source/FactoryGame/Public/FGTrain.h:1 | header only (R3, S2 Task 9) |
| AFGTrain | Source/FactoryGame/Public/FGTrain.h:386 | header only |
| AFGTrain::GetTrainName | Source/FactoryGame/Public/FGTrain.h:460 | header only (the player-chosen name as FText; copied with ToString, never logged) |
| ETrainStatus | Source/FactoryGame/Public/FGTrain.h:64 | header only (TS_Parked :66, TS_ManualDriving :67, TS_SelfDriving :68, TS_Derailed :69; the survey said :64-70) |
| ETrainStatus::TS_ManualDriving | Source/FactoryGame/Public/FGTrain.h:67 | header only |
| ETrainStatus::TS_SelfDriving | Source/FactoryGame/Public/FGTrain.h:68 | header only |
| ETrainStatus::TS_Derailed | Source/FactoryGame/Public/FGTrain.h:69 | header only (TS_Parked :66 is the default arm of the mapping) |
| AFGTrain::GetTrainStatus | Source/FactoryGame/Public/FGTrain.h:614 | header only |
| AFGTrain::GetTimeTable | Source/FactoryGame/Public/FGTrain.h:511 | header only (may be null: checked) |
| AFGTrain::HasTimeTable | Source/FactoryGame/Public/FGTrain.h:519 | header only |
| AFGTrain::GetFirstVehicle | Source/FactoryGame/Public/FGTrain.h:523 | header only (may be null: the train is then placed at its actor) |
| AFGTrain::GetLastVehicle | Source/FactoryGame/Public/FGTrain.h:527 | header only (selected only by NativeTrainPositionSource = LastVehicle) |
| FTrainConsist | Source/FactoryGame/Public/FGTrain.h:103 | header only (`Vehicles` :109) |
| AFGTrain::mConsistData | Source/FactoryGame/Public/FGTrain.h:709 | header only (public: the `public:` at :702 covers it; the plan's row named FGFreightWagon.h, which is ambiguous; the member and the struct are in FGTrain.h) |
| AFGRailroadVehicle | Source/FactoryGame/Public/FGRailroadVehicle.h:36 | header only (derives AFGVehicle) |
| AFGRailroadVehicle::GetVehicleLocation | Source/FactoryGame/Public/FGVehicle.h:347 | header only (declared on AFGVehicle, FGVehicle.h:126; the comment at :337-339 says to use it instead of GetActorLocation) |
| FGRailroadTimeTable.h | Source/FactoryGame/Public/FGRailroadTimeTable.h:1 | header only (R3, S2 Task 9) |
| AFGRailroadTimeTable | Source/FactoryGame/Public/FGRailroadTimeTable.h:32 | header only |
| AFGRailroadTimeTable::GetStops | Source/FactoryGame/Public/FGRailroadTimeTable.h:70 | header only (const; fills an array of FTimeTableStop) |
| FTimeTableStop | Source/FactoryGame/Public/FGRailroadTimeTable.h:15 | header only |
| FTimeTableStop::Station | Source/FactoryGame/Public/FGRailroadTimeTable.h:21 | header only (a `TObjectPtr`; null stations are skipped) |
| FGFreightWagon.h | Source/FactoryGame/Public/FGFreightWagon.h:1 | header only (R3, S2 Task 9) |
| AFGFreightWagon | Source/FactoryGame/Public/FGFreightWagon.h:24 | header only (derives AFGRailroadVehicle; reached by Cast from the consist's vehicles) |
| AFGFreightWagon::GetFreightInventory | Source/FactoryGame/Public/FGFreightWagon.h:47 | header only (may be null: checked) |
| FGTrainStationIdentifier.h | Source/FactoryGame/Public/FGTrainStationIdentifier.h:1 | header only (R3, S2 Task 9) |
| AFGTrainStationIdentifier | Source/FactoryGame/Public/FGTrainStationIdentifier.h:17 | header only |
| AFGTrainStationIdentifier::GetStation | Source/FactoryGame/Public/FGTrainStationIdentifier.h:84 | header only (the building; null when it is not on this machine: checked) |
| AFGTrainStationIdentifier::GetStationName | Source/FactoryGame/Public/FGTrainStationIdentifier.h:88 | header only (FText; copied exactly with ToString, no trimming; never logged) |
| Buildables/FGBuildableRailroadStation.h | Source/FactoryGame/Public/Buildables/FGBuildableRailroadStation.h:1 | header only (R3, S2 Task 9) |
| AFGBuildableRailroadStation | Source/FactoryGame/Public/Buildables/FGBuildableRailroadStation.h:16 | header only (derives AFGBuildableTrainPlatform) |
| AFGBuildableRailroadStation::GetStationOutputConnection | Source/FactoryGame/Public/Buildables/FGBuildableRailroadStation.h:56 | header only (non-const; may be null: checked) |
| FGTrainPlatformConnection.h | Source/FactoryGame/Public/FGTrainPlatformConnection.h:1 | header only (R3, S2 Task 9) |
| UFGTrainPlatformConnection | Source/FactoryGame/Public/FGTrainPlatformConnection.h:26 | header only |
| UFGTrainPlatformConnection::GetConnectedTo | Source/FactoryGame/Public/FGTrainPlatformConnection.h:42 | header only (the connection on the next platform; may be null: ends the walk) |
| UFGTrainPlatformConnection::GetPlatformOwner | Source/FactoryGame/Public/FGTrainPlatformConnection.h:53 | header only (non-const; may be null: ends the walk) |
| Buildables/FGBuildableTrainPlatform.h | Source/FactoryGame/Public/Buildables/FGBuildableTrainPlatform.h:1 | header only (R3, S2 Task 9) |
| AFGBuildableTrainPlatform | Source/FactoryGame/Public/Buildables/FGBuildableTrainPlatform.h:29 | header only |
| AFGBuildableTrainPlatform::GetConnectionInOppositeDirection | Source/FactoryGame/Public/Buildables/FGBuildableTrainPlatform.h:66 | header only (takes the connection on this platform, gives the opposite one; the walk is bounded at 256 hops with a visited set) |
| Buildables/FGBuildableTrainPlatformCargo.h | Source/FactoryGame/Public/Buildables/FGBuildableTrainPlatformCargo.h:1 | header only (R3, S2 Task 9) |
| AFGBuildableTrainPlatformCargo | Source/FactoryGame/Public/Buildables/FGBuildableTrainPlatformCargo.h:18 | header only (kept; other platform kinds are walked through but not listed) |
| AFGBuildableTrainPlatformCargo::GetInventory | Source/FactoryGame/Public/Buildables/FGBuildableTrainPlatformCargo.h:37 | header only (may be null: checked) |
| AFGBuildableTrainPlatformCargo::GetIsInLoadMode | Source/FactoryGame/Public/Buildables/FGBuildableTrainPlatformCargo.h:57 | header only (the comment above it: "whether this station should load or unload from vehicles"; true is "load") |
| UFGInventoryComponent::GetInventoryStacks | Source/FactoryGame/Public/FGInventoryComponent.h:451 | header only (const; empty stacks are left out by default; NumItems and the item class are checked) |
| TSet | Engine/Source/Runtime/Core/Public/Containers/Set.h:153 | header only |
| TArray::AddUnique | Engine/Source/Runtime/Core/Public/Containers/Array.h:3015 | header only (the const overload; the rvalue one is :3003) |
| Invoke | Engine/Source/Runtime/Core/Public/Templates/Invoke.h:44 | header only (calls the native world builder, a TUniqueFunction) |
| Buildables/FGBuildableRailroadTrack.h | Source/FactoryGame/Public/Buildables/FGBuildableRailroadTrack.h:1 | header only (R5, S3 Task 6; survey: the railroad subsystem's track list is private, so tracks come from the buildable list; H: the track header has no public list either) |
| AFGBuildableRailroadTrack | Source/FactoryGame/Public/Buildables/FGBuildableRailroadTrack.h:100 | header only (UCLASS( Abstract ) at :99: Cast to it still works on a concrete track; derives AFGBuildable) |
| AFGBuildableRailroadTrack::StaticClass | Engine/Source/Runtime/CoreUObject/Public/UObject/ObjectMacros.h:1824 | header only (generated by GENERATED_BODY, FGBuildableRailroadTrack.h:100; used only for GetBuildableCount's class filter) |
| AFGBuildableRailroadTrack::GetLength | Source/FactoryGame/Public/Buildables/FGBuildableRailroadTrack.h:150 | header only (float, centimetres; non-finite or non-positive is handled by RailSampleIntervals) |
| AFGBuildableRailroadTrack::GetSplineComponent | Source/FactoryGame/Public/Buildables/FGBuildableRailroadTrack.h:247 | header only (returns mSplineComponent, may be null: checked, the track is counted in Dropped) |
| AFGBuildableRailroadTrack::GetWorldLocationAndDirectionAtDistance | Source/FactoryGame/Public/Buildables/FGBuildableRailroadTrack.h:171 | header only (float distance, FVector& out_location, FVector& out_direction; the GameTrackSampler fallback, off while NativeRailSampler is SplineComponent) |
| Buildables/FGBuildableWire.h | Source/FactoryGame/Public/Buildables/FGBuildableWire.h:1 | header only (R10, S4-A Task 12; wires come from the buildable list, like tracks) |
| AFGBuildableWire | Source/FactoryGame/Public/Buildables/FGBuildableWire.h:33 | header only (UCLASS( Abstract ) at :32: Cast to it still works on a concrete wire; derives AFGBuildable) |
| AFGBuildableWire::StaticClass | Engine/Source/Runtime/CoreUObject/Public/UObject/ObjectMacros.h:1824 | header only (generated by GENERATED_BODY, FGBuildableWire.h:35; used only for GetBuildableCount's class filter; the count is advisory, subclasses are not assumed counted) |
| AFGBuildableWire::GetConnection | Source/FactoryGame/Public/Buildables/FGBuildableWire.h:61 | header only (index 0 or 1, else nullptr; returns UFGCircuitConnectionComponent* from a weak mConnections entry, so null when the end is gone: null-checked, the wire is counted in Dropped) |
| AFGBuildableWire::GetConnectionLocation | Source/FactoryGame/Public/Buildables/FGBuildableWire.h:65 | header only (FVector, cached and replicated location of the end, centimetres; index 0 or 1, else the actor location) |
| FGCircuitConnectionComponent.h | Source/FactoryGame/Public/FGCircuitConnectionComponent.h:1 | header only (the full type, so IsValid on GetConnection's result compiles; FGBuildableWire.h only forward-declares it) |
| UFGCircuitConnectionComponent | Source/FactoryGame/Public/FGCircuitConnectionComponent.h:18 | header only (derives UFGConnectionComponent) |
| Components/SplineComponent.h | Engine/Source/Runtime/Engine/Classes/Components/SplineComponent.h:1 | header only |
| USplineComponent | Engine/Source/Runtime/Engine/Classes/Components/SplineComponent.h:284 | header only |
| USplineComponent::GetLocationAtDistanceAlongSpline | Engine/Source/Runtime/Engine/Classes/Components/SplineComponent.h:763 | header only (survey: the coordinate-space overload at :763, confirmed by H: float Distance, ESplineCoordinateSpace::Type; the World-only helper at :1136 is not used) |
| ESplineCoordinateSpace::World | Engine/Source/Runtime/Engine/Classes/Components/SplineComponent.h:40 | header only (namespace enum at :35-41) |
| FVector::ZeroVector | Engine/Source/Runtime/Core/Public/Math/Vector.h:79 | header only (TVector<T>::ZeroVector static) |
| TArray::Empty | Engine/Source/Runtime/Core/Public/Containers/Array.h:2286 | header only |
| USplineComponent::GetSplineLength | Engine/Source/Runtime/Engine/Classes/Components/SplineComponent.h:731 | header only (float; the length used for sampling in the spline sampler, checked finite and positive before any distance is built) |
| Buildables/FGBuildableConveyorBase.h | Source/FactoryGame/Public/Buildables/FGBuildableConveyorBase.h:1 | header only (R6, S3 Task 9; belts and lifts are only reachable through the buildable list, FGBuildableSubsystem.h:360) |
| AFGBuildableConveyorBase | Source/FactoryGame/Public/Buildables/FGBuildableConveyorBase.h:97 | header only (UCLASS(Abstract) at :96: Cast to it works on a concrete belt or lift; derives AFGBuildable) |
| AFGBuildableConveyorBase::GetSpeed | Source/FactoryGame/Public/Buildables/FGBuildableConveyorBase.h:160 | header only (float, no unit stated; ITEM_SPACING 120 at :327; the census run settles the unit) |
| AFGBuildableConveyorBase::GetConnection0 | Source/FactoryGame/Public/Buildables/FGBuildableConveyorBase.h:163 | header only (may be null: checked, the edge is dropped and counted) |
| AFGBuildableConveyorBase::GetConnection1 | Source/FactoryGame/Public/Buildables/FGBuildableConveyorBase.h:164 | header only (may be null: that end is open) |
| Buildables/FGBuildableConveyorBelt.h | Source/FactoryGame/Public/Buildables/FGBuildableConveyorBelt.h:1 | header only (R6, S3 Task 9) |
| AFGBuildableConveyorBelt | Source/FactoryGame/Public/Buildables/FGBuildableConveyorBelt.h:22 | header only (UCLASS() at :21; derives AFGBuildableConveyorBase) |
| Buildables/FGBuildableConveyorLift.h | Source/FactoryGame/Public/Buildables/FGBuildableConveyorLift.h:1 | header only (R6, S3 Task 9) |
| AFGBuildableConveyorLift | Source/FactoryGame/Public/Buildables/FGBuildableConveyorLift.h:72 | header only (UCLASS() at :71; derives AFGBuildableConveyorBase) |
| Buildables/FGBuildablePipeline.h | Source/FactoryGame/Public/Buildables/FGBuildablePipeline.h:1 | header only (R6, S3 Task 9) |
| AFGBuildablePipeline | Source/FactoryGame/Public/Buildables/FGBuildablePipeline.h:32 | header only (derives AFGBuildablePipeBase, FGBuildablePipeBase.h:22, which hypertubes also derive from: Cast to the pipeline class so hypertubes cannot enter, ruling S10) |
| AFGBuildablePipeline::GetConnection0 | Source/FactoryGame/Public/Buildables/FGBuildablePipeBase.h:68 | header only (declared on AFGBuildablePipeBase, returns UFGPipeConnectionComponentBase*; may be null: checked) |
| AFGBuildablePipeline::GetConnection1 | Source/FactoryGame/Public/Buildables/FGBuildablePipeBase.h:69 | header only (declared on AFGBuildablePipeBase; may be null: that end is open) |
| AFGBuildablePipeline::GetFlowLimit | Source/FactoryGame/Public/Buildables/FGBuildablePipeline.h:136 | header only (float; FGBuildablePipeline.h:134 says "Get the maximum flow of the pipe [m^3/s]", so the pure side's x60 gives per minute; the census run confirms the values) |
| AFGBuildable::mDisplayName | Source/FactoryGame/Public/Buildables/FGBuildable.h:824 | header only (public FText, UPROPERTY at :823; the same field the factory reader reads on a machine) |
| FGFactoryConnectionComponent.h | Source/FactoryGame/Public/FGFactoryConnectionComponent.h:1 | header only (R6, S3 Task 9) |
| UFGFactoryConnectionComponent | Source/FactoryGame/Public/FGFactoryConnectionComponent.h:41 | header only |
| UFGFactoryConnectionComponent::GetConnection | Source/FactoryGame/Public/FGFactoryConnectionComponent.h:96 | header only (returns mConnectedComponent, may be null: checked at each hop) |
| UFGFactoryConnectionComponent::IsConnected | Source/FactoryGame/Public/FGFactoryConnectionComponent.h:111 | header only (returns mHasConnectedComponent) |
| UFGFactoryConnectionComponent::GetConnectorLocation | Source/FactoryGame/Public/FGFactoryConnectionComponent.h:141 | header only (FVector, world space; withClearance defaults off) |
| UFGFactoryConnectionComponent::GetOuterBuildable | Source/FactoryGame/Public/FGFactoryConnectionComponent.h:180 | header only (AFGBuildable*, may be null: an ownerless end, counted and open) |
| FGPipeConnectionComponent.h | Source/FactoryGame/Public/FGPipeConnectionComponent.h:1 | header only (R6, S3 Task 9) |
| UFGPipeConnectionComponentBase | Source/FactoryGame/Public/FGPipeConnectionComponent.h:38 | header only (the type of AFGBuildablePipeBase's connections; derives UFGConnectionComponent) |
| UFGPipeConnectionComponentBase::GetConnection | Source/FactoryGame/Public/FGPipeConnectionComponent.h:69 | header only (null when the connected component is not valid: checked) |
| UFGPipeConnectionComponentBase::IsConnected | Source/FactoryGame/Public/FGPipeConnectionComponent.h:94 | header only (non-inline) |
| UActorComponent::GetOwner | Engine/Source/Runtime/Engine/Classes/Components/ActorComponent.h:491 | header only (AActor*; used on the far pipe connection, may be null: an ownerless end) |
| USceneComponent::GetComponentLocation | Engine/Source/Runtime/Engine/Classes/Components/SceneComponent.h:1031 | header only (world-space FVector; the pipe end-0 position) |
| UObject | Engine/Source/Runtime/CoreUObject/Public/UObject/Object.h:93 | header only (the argument type of the file-local kind-by-cast helper) |
| TArray::Contains | Engine/Source/Runtime/Core/Public/Containers/Array.h:1525 | header only (bounds the differing-class list) |
| Buildables/FGBuildableAttachmentSplitter.h | Source/FactoryGame/Public/Buildables/FGBuildableAttachmentSplitter.h:1 | header only (R6 kind cross-check, S3 Task 9) |
| AFGBuildableAttachmentSplitter | Source/FactoryGame/Public/Buildables/FGBuildableAttachmentSplitter.h:14 | header only (Cast target) |
| Buildables/FGBuildableSplitterSmart.h | Source/FactoryGame/Public/Buildables/FGBuildableSplitterSmart.h:1 | header only |
| AFGBuildableSplitterSmart | Source/FactoryGame/Public/Buildables/FGBuildableSplitterSmart.h:64 | header only (Cast target) |
| Buildables/FGBuildableAttachmentMerger.h | Source/FactoryGame/Public/Buildables/FGBuildableAttachmentMerger.h:1 | header only |
| AFGBuildableAttachmentMerger | Source/FactoryGame/Public/Buildables/FGBuildableAttachmentMerger.h:14 | header only (Cast target) |
| Buildables/FGBuildableMergerPriority.h | Source/FactoryGame/Public/Buildables/FGBuildableMergerPriority.h:1 | header only |
| AFGBuildableMergerPriority | Source/FactoryGame/Public/Buildables/FGBuildableMergerPriority.h:14 | header only (Cast target) |
| Buildables/FGBuildablePipelineJunction.h | Source/FactoryGame/Public/Buildables/FGBuildablePipelineJunction.h:1 | header only |
| AFGBuildablePipelineJunction | Source/FactoryGame/Public/Buildables/FGBuildablePipelineJunction.h:14 | header only (Cast target) |
| Buildables/FGBuildablePipelinePump.h | Source/FactoryGame/Public/Buildables/FGBuildablePipelinePump.h:1 | header only |
| AFGBuildablePipelinePump | Source/FactoryGame/Public/Buildables/FGBuildablePipelinePump.h:36 | header only (Cast target) |
| Buildables/FGBuildableResourceExtractorBase.h | Source/FactoryGame/Public/Buildables/FGBuildableResourceExtractorBase.h:1 | header only |
| AFGBuildableResourceExtractorBase | Source/FactoryGame/Public/Buildables/FGBuildableResourceExtractorBase.h:65 | header only (Cast target) |
| AFGBuildableResourceExtractorBase::GetExtractableResource | Source/FactoryGame/Public/Buildables/FGBuildableResourceExtractorBase.h:103 | header only (S4-B Task 5; returns a TScriptInterface to the node or well: its GetInterface() is null for an unoccupied or unknown resource, checked) |
| Buildables/FGBuildableResourceExtractor.h | Source/FactoryGame/Public/Buildables/FGBuildableResourceExtractor.h:1 | header only (S4-B Task 5) |
| AFGBuildableResourceExtractor | Source/FactoryGame/Public/Buildables/FGBuildableResourceExtractor.h:13 | header only (Cast target; Abstract, covers miners, pumps and the Blueprint oil pump: the parity id-set check judges that last, plan V2) |
| AFGBuildableResourceExtractor::GetOutputInventory | Source/FactoryGame/Public/Buildables/FGBuildableResourceExtractor.h:35 | header only (returns mOutputInventory; may be null: checked. The survey's :117 is the base, which returns nullptr) |
| AFGBuildableResourceExtractor::GetNumExtractedItemsPerCycle | Source/FactoryGame/Public/Buildables/FGBuildableResourceExtractor.h:39 | header only (int32 mItemsPerCycle :101; GetNumExtractedItemsPerCycleConverted :43 is "converted for displaying in the UI", so the raw count is not in display units; the header does not say whether a fluid's count is in litres, so it is read as litres like every other fluid amount: NEEDS VERIFICATION, the parity run judges) |
| AFGBuildableResourceExtractor::GetProductionCycleTime | Source/FactoryGame/Public/Buildables/FGBuildableResourceExtractor.h:29 | header only (override of AFGBuildableFactory::GetProductionCycleTime, FGBuildableFactory.h:197, "with potential multiplier applied"; the headers do not say whether purity is inside it. OnExtractableResourceSet :78 is an override, read as "the cycle depends on the resource", so purity is NOT applied again: NEEDS VERIFICATION, the parity run judges, ruling B4) |
| Resources/FGExtractableResourceInterface.h | Source/FactoryGame/Public/Resources/FGExtractableResourceInterface.h:1 | header only (S4-B Task 5) |
| IFGExtractableResourceInterface | Source/FactoryGame/Public/Resources/FGExtractableResourceInterface.h:18 | header only |
| IFGExtractableResourceInterface::GetResourceClass | Source/FactoryGame/Public/Resources/FGExtractableResourceInterface.h:41 | header only ("the descriptor for the resource being mined"; a null class is treated as unoccupied) |
| UObject/ScriptInterface.h | Engine/Source/Runtime/CoreUObject/Public/UObject/ScriptInterface.h:1 | header only |
| TScriptInterface | Engine/Source/Runtime/CoreUObject/Public/UObject/ScriptInterface.h:138 | header only |
| TScriptInterface::GetObject | Engine/Source/Runtime/CoreUObject/Public/UObject/ScriptInterface.h:351 | header only (the UObject behind the interface; checked with IsValid before GetInterface is used) |
| TScriptInterface::GetInterface | Engine/Source/Runtime/CoreUObject/Public/UObject/ScriptInterface.h:335 | header only (the typed pointer; null when the object does not natively implement the interface, so an unoccupied extractor reads as no resource) |
| Buildables/FGBuildableGenerator.h | Source/FactoryGame/Public/Buildables/FGBuildableGenerator.h:1 | header only |
| AFGBuildableGenerator | Source/FactoryGame/Public/Buildables/FGBuildableGenerator.h:60 | header only (Cast target) |
| AFGBuildableGenerator::CalcPowerProductionCapacityForPotential | Source/FactoryGame/Public/Buildables/FGBuildableGenerator.h:95 | header only (S4-B Task 9; "the power this generator can produce with a given potential", so capacity at the current clock; mPowerProduction :126 is "in MW". GetPowerProductionCapacity :87 says only "the power this generator can produce", so whether it follows the clock is not stated and it is NOT used: survey Q3. Geothermal gives whatever this returns, no min/max invented; whether it includes the production boost (FGPowerInfoComponent.h:168, :171) is not stated: NEEDS VERIFICATION, the parity run judges) |
| UFGPowerInfoComponent::GetBaseProduction | Source/FactoryGame/Public/FGPowerInfoComponent.h:137 | header only (S4-B Task 9; "the base power production", the power "we always provide to the circuit"; the header gives no unit, MW assumed; whether it includes the production boost (GetProductionBoostFactor :168, GetActualProductionBoost :171) is not stated: NEEDS VERIFICATION) |
| UFGPowerInfoComponent::GetProductionBoostFactor | Source/FactoryGame/Public/FGPowerInfoComponent.h:168 | header only (S4-B Task 9; "the production boost factor [%]"; cited only as an open question, not called: the header does not say whether base + regulated production or CalcPowerProductionCapacityForPotential include it; parity exercises a boosted generator or records it NOT EXERCISED) |
| UFGPowerInfoComponent::GetActualProductionBoost | Source/FactoryGame/Public/FGPowerInfoComponent.h:171 | header only (S4-B Task 9; "the actual production boost provided by this building [MW]"; cited only as an open question, not called; a ruling PR adds the call if parity misses on a boosted circuit) |
| UFGPowerInfoComponent::GetRegulatedDynamicProduction | Source/FactoryGame/Public/FGPowerInfoComponent.h:160 | header only (S4-B Task 9; "the dynamic power production we provide to the circuit"; no unit stated, MW assumed. Current MW = base + regulated; whether a generator's output sits in the base or the dynamic part is not stated, so the sum is read: NEEDS VERIFICATION) |
| Buildables/FGBuildableGeneratorFuel.h | Source/FactoryGame/Public/Buildables/FGBuildableGeneratorFuel.h:1 | header only (S4-B Task 9) |
| AFGBuildableGeneratorFuel | Source/FactoryGame/Public/Buildables/FGBuildableGeneratorFuel.h:14 | header only (Cast target; coal, fuel, biomass and the nuclear base. Geothermal is not one: no fuel) |
| AFGBuildableGeneratorFuel::GetFuelInventory | Source/FactoryGame/Public/Buildables/FGBuildableGeneratorFuel.h:54 | header only (mFuelInventory; "can be nullptr on client", checked: a null inventory on a Fuel generator means the fuel read failed) |
| AFGBuildableGeneratorFuel::GetRequiresSupplementalResource | Source/FactoryGame/Public/Buildables/FGBuildableGeneratorFuel.h:74 | header only (the supplement is read only when true) |
| AFGBuildableGeneratorFuel::GetCurrentFuelClass | Source/FactoryGame/Public/Buildables/FGBuildableGeneratorFuel.h:103 | header only ("the currently used fuel class"; a held stack labels itself with its own class so label and count match; cite only, not called: an empty fuel slot is Fuel null, as the contract says) |
| AFGBuildableGeneratorFuel::GetSupplementalResourceClass | Source/FactoryGame/Public/Buildables/FGBuildableGeneratorFuel.h:111 | header only ("the class of the supplemental resource required (can be null)") |
| AFGBuildableGeneratorFuel::GetFuelInventoryIndex | Source/FactoryGame/Public/Buildables/FGBuildableGeneratorFuel.h:116 | header only (mFuelInventoryIndex :218, "input inventory index to store the fuel"; the amount is the count in that slot; a fluid slot holds litres, "1 unit equates to 1 Liter" :176, so it is scaled to m3 by MakeSlotReading) |
| AFGBuildableGeneratorFuel::GetSupplementalResourceInventoryIndex | Source/FactoryGame/Public/Buildables/FGBuildableGeneratorFuel.h:117 | header only (mSupplementalInventoryIndex :221, in the same fuel inventory; litres, scaled as the fuel) |
| Buildables/FGBuildableResourceSink.h | Source/FactoryGame/Public/Buildables/FGBuildableResourceSink.h:1 | header only |
| AFGBuildableResourceSink | Source/FactoryGame/Public/Buildables/FGBuildableResourceSink.h:14 | header only (Cast target) |
| Buildables/FGBuildableSpaceElevator.h | Source/FactoryGame/Public/Buildables/FGBuildableSpaceElevator.h:1 | header only |
| AFGBuildableSpaceElevator | Source/FactoryGame/Public/Buildables/FGBuildableSpaceElevator.h:51 | header only (Cast target) |
| Buildables/FGBuildableStorage.h | Source/FactoryGame/Public/Buildables/FGBuildableStorage.h:1 | header only |
| AFGBuildableStorage | Source/FactoryGame/Public/Buildables/FGBuildableStorage.h:13 | header only (Cast target) |
| Buildables/FGBuildablePipeReservoir.h | Source/FactoryGame/Public/Buildables/FGBuildablePipeReservoir.h:1 | header only |
| AFGBuildablePipeReservoir | Source/FactoryGame/Public/Buildables/FGBuildablePipeReservoir.h:43 | header only (Cast target) |
| Buildables/FGBuildableDroneStation.h | Source/FactoryGame/Public/Buildables/FGBuildableDroneStation.h:1 | header only |
| AFGBuildableDroneStation | Source/FactoryGame/Public/Buildables/FGBuildableDroneStation.h:37 | header only (Cast target) |
| AFGBuildableDroneStation::GetInputInventory | Source/FactoryGame/Public/Buildables/FGBuildableDroneStation.h:145 | header only (R9 Input; returns mInputInventory :317. V9: the getter comment says "the docked drone unloads into", the field comment says "we transfer items from when loading a drone"; the plan follows the fields and the capture, so Input = this getter; swap with the output one if the parity judge reports input/output swapped) |
| AFGBuildableDroneStation::GetOutputInventory | Source/FactoryGame/Public/Buildables/FGBuildableDroneStation.h:149 | header only (R9 Output; returns mOutputInventory :321, the field comment says "we transfer items to when unloading from a drone"; see GetInputInventory for V9) |
| AFGBuildableDroneStation::GetFuelInventory | Source/FactoryGame/Public/Buildables/FGBuildableDroneStation.h:153 | header only (R9 Fuel; returns mFuelInventory :325; null-checked by the item helper) |
| FGDroneSubsystem.h | Source/FactoryGame/Public/FGDroneSubsystem.h:1 | header only |
| AFGDroneSubsystem | Source/FactoryGame/Public/FGDroneSubsystem.h:61 | header only (R9 drone subsystem) |
| AFGDroneSubsystem::Get | Source/FactoryGame/Public/FGDroneSubsystem.h:68 | header only (the UWorld* overload; :72 is the UObject* one; null-checked) |
| AFGDroneSubsystem::GetAllStations | Source/FactoryGame/Public/FGDroneSubsystem.h:81 | header only (const, returns const TArray<AFGDroneStationInfo*>& to mStations; re-fetched for every station, never kept) |
| FGDroneStationInfo.h | Source/FactoryGame/Public/FGDroneStationInfo.h:1 | header only |
| AFGDroneStationInfo | Source/FactoryGame/Public/FGDroneStationInfo.h:159 | header only (R9 per-station info; also an IFGBuildingTagInterface) |
| AFGDroneStationInfo::GetPairedStation | Source/FactoryGame/Public/FGDroneStationInfo.h:201 | header only (the partner info, mPairedStation :404; null-checked) |
| AFGDroneStationInfo::GetStation | Source/FactoryGame/Public/FGDroneStationInfo.h:252 | header only (server-only; the building, mStation :394; null-checked, a null one counts Dropped) |
| AFGDroneStationInfo::GetStationLocation | Source/FactoryGame/Public/FGDroneStationInfo.h:208 | header only (not used: V10 reads the actor location; this is the one-line flip) |
| AFGDroneStationInfo::GetBuildingTag_Implementation | Source/FactoryGame/Public/FGDroneStationInfo.h:190 | header only (returns FString mBuildingTag :411, not FText; not called directly, the interface wrapper is) |
| FGBuildingTagInterface.h | Source/FactoryGame/Public/FGBuildingTagInterface.h:1 | header only |
| IFGBuildingTagInterface::Execute_GetBuildingTag | Source/FactoryGame/Public/FGBuildingTagInterface.h:46 | header only (generated by UHT for the BlueprintNativeEvent at :46: static FString Execute_GetBuildingTag(const UObject* O), FGBuildingTagInterface.generated.h:79 in the build's Intermediate) |
| IFGBuildingTagInterface::GetBuildingTag | Source/FactoryGame/Public/FGBuildingTagInterface.h:46 | header only (BlueprintNativeEvent, FString; called through the UHT wrapper Execute_GetBuildingTag(const UObject*), FGBuildingTagInterface.generated.h:79 in the build's Intermediate; the plan's ".ToString()" does not apply to FString; never logged) |
| Buildables/FGBuildableDockingStation.h | Source/FactoryGame/Public/Buildables/FGBuildableDockingStation.h:1 | header only |
| AFGBuildableDockingStation | Source/FactoryGame/Public/Buildables/FGBuildableDockingStation.h:63 | header only (Cast target) |
| FLinearColor | Engine/Source/Runtime/Core/Public/Math/Color.h:47 | header only (the marker colour: FGMapMarker's color slot is a linear colour) |
| FLinearColor::White | Engine/Source/Runtime/Core/Public/Math/Color.h:465 | header only |
| FLinearColor::FromSRGBColor | Engine/Source/Runtime/Core/Public/Math/Color.h:123 | header only (the inverse of ToFColor(true): used by the tests to build a colour with a known sRGB byte value) |
| FLinearColor::ToFColor | Engine/Source/Runtime/Core/Public/Math/Color.h:381 | header only (bSRGB true = ToFColorSRGB, false = QuantizeRound; the NativeMarkerColorSRGB constant picks) |
| FGMapManager.h | Source/FactoryGame/Public/FGMapManager.h:1 | header only |
| AFGMapManager | Source/FactoryGame/Public/FGMapManager.h:70 | header only (the map-markers manager) |
| AFGMapManager::Get | Source/FactoryGame/Public/FGMapManager.h:95 | header only (the UWorld* overload; :99 is the UObject* one; null-checked) |
| AFGMapManager::GetMapMarkers | Source/FactoryGame/Public/FGMapManager.h:118 | header only (public, non-const, fills a copy; the cap is mMaxNumMapMarkers = 250 at :299) |
| FGMapMarker.h | Source/FactoryGame/Public/FGMapMarker.h:1 | header only |
| FMapMarker | Source/FactoryGame/Public/FGMapMarker.h:13 | header only (copied once per read) |
| FMapMarker::MarkerGUID | Source/FactoryGame/Public/FGMapMarker.h:37 | header only |
| FMapMarker::Location | Source/FactoryGame/Public/FGMapMarker.h:40 | header only (centimetres; FVector_NetQuantize, X and Y read) |
| FMapMarker::Name | Source/FactoryGame/Public/FGMapMarker.h:43 | header only |
| FMapMarker::MapMarkerType | Source/FactoryGame/Public/FGMapMarker.h:49 | header only |
| FMapMarker::IconID | Source/FactoryGame/Public/FGMapMarker.h:52 | header only |
| FMapMarker::Color | Source/FactoryGame/Public/FGMapMarker.h:55 | header only (FLinearColor) |
| FMapMarker::HasValidID | Source/FactoryGame/Public/FGMapMarker.h:71 | header only (MarkerGUID.IsValid()) |
| FGActorRepresentation.h | Source/FactoryGame/Public/FGActorRepresentation.h:1 | header only |
| ERepresentationType | Source/FactoryGame/Public/FGActorRepresentation.h:14 | header only |
| ERepresentationType::RT_Stamp | Source/FactoryGame/Public/FGActorRepresentation.h:33 | header only (a stamp; every other type is a marker) |
| EGuidFormats::Digits | Engine/Source/Runtime/Core/Public/Misc/Guid.h:39 | header only (32 hex digits, no hyphens) |
| FGuid::ToString | Engine/Source/Runtime/Core/Public/Misc/Guid.h:329 | header only (EGuidFormats::Digits, defined at :39 in the same header: 32 hex digits) |
| FColor | Engine/Source/Runtime/Core/Public/Math/Color.h:494 | header only |
| FColor::ToHex | Engine/Source/Runtime/Core/Public/Math/Color.h:706 | header only (RRGGBBAA, upper case; Color.cpp:585) |
| TestNotEqual | Engine/Source/Runtime/Core/Public/Misc/AutomationTest.h:1972 | header only (the FString overload) |
| FJsonObject::HasTypedField | Engine/Source/Runtime/Json/Public/Dom/JsonObject.h:93 | header only (the template overload; :105 takes the EJson as an argument) |
| UObject/Class.h | Engine/Source/Runtime/CoreUObject/Public/UObject/Class.h:1 | header only (S4-B Task 12: UFunction, UClass) |
| UObject/Script.h | Engine/Source/Runtime/CoreUObject/Public/UObject/Script.h:1 | header only (S4-B Task 12: FUNC_BlueprintPure) |
| UObject/UnrealType.h | Engine/Source/Runtime/CoreUObject/Public/UObject/UnrealType.h:1 | header only (S4-B Task 12: FFloatProperty, FIntProperty, FindFProperty) |
| UClass | Engine/Source/Runtime/CoreUObject/Public/UObject/Class.h:2988 | header only (S4-B Task 12: the cache key and FindFunctionByName's owner) |
| UFunction | Engine/Source/Runtime/CoreUObject/Public/UObject/Class.h:2021 | header only (S4-B Task 12, R2: only GetCurrentItemTransferRate is called, after NumParms == 1, BlueprintPure, a float or int return and ParmsSize <= 16 are checked) |
| UFunction::NumParms | Engine/Source/Runtime/CoreUObject/Public/UObject/Class.h:2034 | header only (total parameters, the return value included; 1 means no input parameter) |
| UFunction::ParmsSize | Engine/Source/Runtime/CoreUObject/Public/UObject/Class.h:2036 | header only (total size of the parameters in memory; must fit the 16-byte block) |
| UFunction::ReturnValueOffset | Engine/Source/Runtime/CoreUObject/Public/UObject/Class.h:2038 | header only (checked against ParmsSize) |
| UFunction::GetReturnProperty | Engine/Source/Runtime/CoreUObject/Public/UObject/Class.h:2116 | header only (null when there is no return value: lookup failed) |
| UFunction::HasAnyFunctionFlags | Engine/Source/Runtime/CoreUObject/Public/UObject/Class.h:2133 | header only |
| FUNC_BlueprintPure | Engine/Source/Runtime/CoreUObject/Public/UObject/Script.h:164 | header only (the cargo header marks GetCurrentItemTransferRate UFUNCTION( BlueprintPure ) at FGBuildableTrainPlatformCargo.h:179) |
| UClass::FindFunctionByName | Engine/Source/Runtime/CoreUObject/Public/UObject/Class.h:3300 | header only (UClass at :2988; looked up once per class and kept) |
| UObject::ProcessEvent | Engine/Source/Runtime/CoreUObject/Public/UObject/Object.h:1474 | header only (the one call, with a zeroed parameter block, on a valid object on the game thread) |
| FProperty | Engine/Source/Runtime/CoreUObject/Public/UObject/UnrealType.h:180 | header only |
| FFloatProperty | Engine/Source/Runtime/CoreUObject/Public/UObject/UnrealType.h:2301 | header only |
| FIntProperty | Engine/Source/Runtime/CoreUObject/Public/UObject/UnrealType.h:2175 | header only |
| FFloatProperty::GetPropertyValue_InContainer | Engine/Source/Runtime/CoreUObject/Public/UObject/UnrealType.h:1576 | header only (declared in TProperty, the base of TProperty_Numeric; read-only) |
| FIntProperty::GetPropertyValue_InContainer | Engine/Source/Runtime/CoreUObject/Public/UObject/UnrealType.h:1576 | header only (declared in TProperty; read-only) |
| CastField | Engine/Source/Runtime/CoreUObject/Public/UObject/Field.h:975 | header only (the const FField* overload; null when the type differs) |
| FindFProperty | Engine/Source/Runtime/CoreUObject/Public/UObject/UnrealType.h:6869 | header only (the FName overload; mSmoothedLoadRate and mSmoothedUnloadRate, found once per class) |
| FMemory::Memzero | Engine/Source/Runtime/Core/Public/HAL/UnrealMemory.h:131 | header only |
| IsInGameThread | Engine/Source/Runtime/Core/Public/CoreGlobals.h:747 | header only |
| AActor::FindComponentByClass | Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h:3807 | header only (the template overload; read-only, stops at the first match; non-null UFGPipeConnectionComponent = fluid platform) |
| AActor::GetFunctionCallspace | Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h:2358 | header only (public virtual override of UObject's :1484; Absorbed or Remote = the call would not run here, so it is not made) |
| FunctionCallspace | Engine/Source/Runtime/CoreUObject/Public/UObject/Script.h:95 | header only (namespace; Absorbed :100, Remote :102, Local :104) |
| FunctionCallspace::Local | Engine/Source/Runtime/CoreUObject/Public/UObject/Script.h:104 | header only |
| UObjectBaseUtility::IsUnreachable | Engine/Source/Runtime/CoreUObject/Public/UObject/UObjectBaseUtility.h:236 | header only (IsValid does not test it, and ProcessEvent asserts on it: ScriptCore.cpp:2036) |
| FDoubleProperty | Engine/Source/Runtime/CoreUObject/Public/UObject/UnrealType.h:2332 | header only |
| FDoubleProperty::GetPropertyValue_InContainer | Engine/Source/Runtime/CoreUObject/Public/UObject/UnrealType.h:1576 | header only (declared in TProperty; read-only) |
| UFunction::HasAllFunctionFlags | Engine/Source/Runtime/CoreUObject/Public/UObject/Class.h:2144 | header only |
| FUNC_Native | Engine/Source/Runtime/CoreUObject/Public/UObject/Script.h:146 | header only (required: the game's own native getter) |
| FUNC_Net | Engine/Source/Runtime/CoreUObject/Public/UObject/Script.h:142 | header only (refused) |
| FUNC_Static | Engine/Source/Runtime/CoreUObject/Public/UObject/Script.h:149 | header only (refused) |
| FUNC_HasOutParms | Engine/Source/Runtime/CoreUObject/Public/UObject/Script.h:158 | header only (refused) |
| FUNC_Event | Engine/Source/Runtime/CoreUObject/Public/UObject/Script.h:147 | header only (refused) |
| TWeakObjectPtr::IsValid | Engine/Source/Runtime/Core/Public/UObject/WeakObjectPtrTemplates.h:243 | header only (the no-argument overload) |
| TWeakObjectPtr::Get | Engine/Source/Runtime/Core/Public/UObject/WeakObjectPtrTemplates.h:139 | header only (the no-argument overload) |
| UFGPipeConnectionComponent | Source/FactoryGame/Public/FGPipeConnectionComponent.h:177 | header only (V8: a cargo platform with any such component is a fluid platform and is skipped) |
| AFGBuildableTrainPlatformCargo::GetCurrentItemTransferRate | Source/FactoryGame/Public/Buildables/FGBuildableTrainPlatformCargo.h:180 | header only (private, BlueprintPure at :179, returns float; never called directly: found by name through reflection, R2) |
| AFGBuildableTrainPlatformCargo::mSmoothedLoadRate | Source/FactoryGame/Public/Buildables/FGBuildableTrainPlatformCargo.h:363 | header only (private float UPROPERTY; fallback, read through FindFProperty only) |
| AFGBuildableTrainPlatformCargo::mSmoothedUnloadRate | Source/FactoryGame/Public/Buildables/FGBuildableTrainPlatformCargo.h:366 | header only (private float UPROPERTY; fallback, read through FindFProperty only) |
| FCString::Strlen | Engine/Source/Runtime/Core/Public/Misc/CString.h:444 | header only (FCString is TCString<TCHAR>; the byte length of a part key in the snapshot size rule, S4-B) |
| ESearchDir::FromEnd | Engine/Source/Runtime/Core/Public/Misc/CString.h:39 | header only (namespace ESearchDir, :31; the last ,"extractors": key of the factory part) |
| FString::AppendChars | Engine/Source/Runtime/Core/Public/Containers/UnrealString.h.inl:406 | header only (the WIDECHAR overload with a count, declared in the class body; a prefix of a held part is appended without a copy) |
