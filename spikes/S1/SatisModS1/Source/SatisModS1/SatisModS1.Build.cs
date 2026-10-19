using UnrealBuildTool;

public class SatisModS1 : ModuleRules
{
	public SatisModS1(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "HTTP" });
	}
}
