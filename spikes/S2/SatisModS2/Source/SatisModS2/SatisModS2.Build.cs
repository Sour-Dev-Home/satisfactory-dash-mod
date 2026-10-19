using UnrealBuildTool;

public class SatisModS2 : ModuleRules
{
	public SatisModS2(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "HTTP" });
	}
}
