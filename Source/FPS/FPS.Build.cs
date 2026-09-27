using UnrealBuildTool;

public class FPS : ModuleRules
{
	public FPS(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicIncludePaths.Add(ModuleDirectory);
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore",
			"CommonGame", "GameplayTags", "GameFeatures"
		});
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ModularGameplay", "ModularGameplayActors"
		});
	}
}
