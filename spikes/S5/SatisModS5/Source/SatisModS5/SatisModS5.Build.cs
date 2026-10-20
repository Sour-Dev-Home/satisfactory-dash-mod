using UnrealBuildTool;

public class SatisModS5 : ModuleRules
{
	public SatisModS5(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "SML", "HTTP", "Json" });
	}
}
