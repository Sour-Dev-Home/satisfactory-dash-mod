using UnrealBuildTool;

public class SatisModS7 : ModuleRules
{
	public SatisModS7(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "FactoryGame", "FactoryDedicatedServer" });
	}
}
