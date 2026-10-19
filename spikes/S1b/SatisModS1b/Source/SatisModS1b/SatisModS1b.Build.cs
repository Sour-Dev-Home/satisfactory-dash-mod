using UnrealBuildTool;

public class SatisModS1b : ModuleRules
{
	public SatisModS1b(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "HTTP" });
	}
}
