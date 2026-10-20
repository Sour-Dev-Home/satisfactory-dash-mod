using UnrealBuildTool;

public class SatisDashCore : ModuleRules
{
	public SatisDashCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		// Pure logic only (spec "Portability"): no FactoryGame, SML or HTTP here. Plan 2B's module reads the game.
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "Json" });
	}
}
