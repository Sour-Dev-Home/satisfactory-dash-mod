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
| LogTemp | Engine/Source/Runtime/Core/Public/CoreGlobals.h:55 | header only |
| TEXT | Engine/Source/Runtime/Core/Public/HAL/Platform.h:1222 | header only |
| module:Core | Engine/Source/Runtime/Core/Core.Build.cs:1 | header only |
| plugin:SML | Mods/SML/SML.uplugin:1 | header only |
