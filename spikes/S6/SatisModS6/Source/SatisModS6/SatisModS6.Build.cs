using UnrealBuildTool;

public class SatisModS6 : ModuleRules
{
	public SatisModS6(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "FactoryGame", "FactoryDedicatedServer" });
	}
}
