using UnrealBuildTool;

public class SatisDashGame : ModuleRules
{
	public SatisDashGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		// The thin layer over the game (spec "Runtime"): every decision is in SatisDashCore, behind Ports.h.
		PrivateDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "HTTP", "Json", "SatisDashCore", "FactoryGame", "FactoryDedicatedServer" });
	}
}
