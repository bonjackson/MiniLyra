using UnrealBuildTool;

public class FPS : ModuleRules
{
	public FPS(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicIncludePaths.Add(ModuleDirectory);
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "NetCore",
			"CommonGame", "GameplayTags", "GameplayAbilities", "GameplayTasks", "EnhancedInput",
			"GameFeatures", "ModularGameplayActors"
		});
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ModularGameplay"
		});
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"AnimGraph", "BlueprintGraph", "UnrealEd"
			});
		}
	}
}
