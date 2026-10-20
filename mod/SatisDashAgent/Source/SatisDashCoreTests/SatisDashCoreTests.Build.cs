using UnrealBuildTool;

public class SatisDashCoreTests : ModuleRules
{
	public SatisDashCoreTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		// An Editor module: never built into a server package, so no test code ships (spec S14).
		PrivateDependencyModuleNames.AddRange(new string[] { "Core", "Json", "Projects", "SatisDashCore" });
	}
}
