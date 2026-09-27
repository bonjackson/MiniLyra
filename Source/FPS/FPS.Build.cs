using UnrealBuildTool;

public class FPS : ModuleRules
{
	public FPS(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore",
			"CommonGame", "GameplayTags"
		});
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ModularGameplay", "ModularGameplayActors"
		});
	}
}
