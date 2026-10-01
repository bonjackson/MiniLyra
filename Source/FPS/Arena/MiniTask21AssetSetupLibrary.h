#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "MiniTask21AssetSetupLibrary.generated.h"

class UBlueprint;
class UGameFeatureData;
class UMiniArenaPhaseConfig;
class UMiniExperienceActionSet;
class UMiniExperienceDefinition;
class UMiniPawnData;

/**
 * Narrow editor bridge for fixed Task 21 phase defaults, component Blueprint
 * CDOs and stock AddComponents entries that UE 5.8 Python cannot author.
 * Each mutation is restricted to the named production/Diagnostics assets.
 * Verification never compiles a Blueprint or changes an asset.
 */
UCLASS()
class FPS_API UMiniTask21AssetSetupLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Fixed native classes, ordered Warmup/Playing/PostMatch, 10/60/5 or 8/20/3. */
	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool ConfigurePhaseConfig(UMiniArenaPhaseConfig* Config, bool bDiagnostics);

	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool EnsureRulesBlueprint(UBlueprint* RulesBlueprint, UMiniArenaPhaseConfig* Config);

	/** Sole MiniArena_AddRules Action, AMiniGameState, server=true/client=false. */
	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool EnsureArenaActionSet(UMiniExperienceActionSet* ArenaSet, UBlueprint* RulesBlueprint);

	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool VerifyAssembly(const UMiniArenaPhaseConfig* Config, const UBlueprint* RulesBlueprint,
		const UMiniExperienceActionSet* ArenaSet, const UMiniExperienceDefinition* Experience,
		const UMiniExperienceActionSet* CombatSet, const UMiniPawnData* PawnData,
		const UGameFeatureData* ArenaFeature, bool bDiagnostics);
};
