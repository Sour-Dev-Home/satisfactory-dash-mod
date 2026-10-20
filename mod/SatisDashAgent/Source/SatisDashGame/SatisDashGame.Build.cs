using UnrealBuildTool;

public class SatisDashGame : ModuleRules
{
	public SatisDashGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		// The thin layer over the game (spec "Runtime"): every decision is in SatisDashCore, behind Ports.h.
		// SSL: the trusted roots for the transport (FSslModule, and WITH_SSL, SSL.Build.cs:42), as HTTP.Build.cs:135 uses it.
		PrivateDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "Json", "SatisDashCore", "FactoryGame", "FactoryDedicatedServer", "SSL" });
		// The mod's own libcurl (Plan 2B Task 1e), linked as HTTP.Build.cs:101 and :136 link it: libcurl 8.12.1 is a static
		// library (libcurl.Build.cs:10-13, :47) that needs nghttp2, OpenSSL and zlib (:51-56). Its own dependencies on them
		// are private, so this DLL names them too (without nghttp2 and zlib the link fails on their symbols).
		AddEngineThirdPartyPrivateStaticDependencies(Target, "libcurl", "OpenSSL", "nghttp2", "zlib");
	}
}
