#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "MiniTask22AssetSetupLibrary.generated.h"

class UBlueprint;
class UMiniArenaPhaseConfig;
class UMiniMatchRulesConfig;
class UMiniExperienceActionSet;

/** Fixed-path editor authoring bridge. Saved verification does not repair assets. */
UCLASS()
class FPS_API UMiniTask22AssetSetupLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool ConfigurePhaseConfig(UMiniArenaPhaseConfig* Config, bool bDiagnostics);
	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool ConfigureMatchConfig(UMiniMatchRulesConfig* Config, bool bDiagnostics);
	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool EnsurePhaseRulesBlueprint(UBlueprint* Blueprint, UMiniArenaPhaseConfig* Config, bool bDiagnostics);
	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool EnsureMatchRulesBlueprint(UBlueprint* Blueprint, UMiniMatchRulesConfig* Config, bool bDiagnostics);
	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool EnsureArenaActionSet(UMiniExperienceActionSet* ArenaSet, UBlueprint* PhaseBlueprint, UBlueprint* MatchBlueprint, bool bDiagnostics);
	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool VerifySavedAssembly(bool bDiagnostics);
};
