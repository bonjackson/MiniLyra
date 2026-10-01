#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "MiniTask20AssemblyAssetLibrary.generated.h"

class UMiniAbilitySet;
class UMiniPawnData;
class UMiniExperienceDefinition;
class UMiniExperienceActionSet;
class UInputMappingContext;
class UGameFeatureData;
class AActor;

/** Narrow editor bridge for production assembly and legacy probe isolation. */
UCLASS()
class FPS_API UMiniTask20AssemblyAssetLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool ConfigureCombat(UMiniAbilitySet* PawnSet, UMiniPawnData* PawnData,
		UMiniExperienceActionSet* CombatSet, UInputMappingContext* MappingContext,
		UMiniExperienceDefinition* PracticeExperience, UGameFeatureData* CoreFeature);

	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool ConfigurePracticeActors(UMiniExperienceActionSet* PracticeSet,
		TSubclassOf<AActor> TargetClass, const TArray<FTransform>& TargetTransforms,
		TSubclassOf<AActor> SupplyClass, const FTransform& SupplyTransform);

	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool VerifyAssembly(const UMiniAbilitySet* PawnSet, const UMiniPawnData* PawnData,
		const UMiniExperienceActionSet* CombatSet, const UMiniExperienceActionSet* PracticeSet,
		const UInputMappingContext* MappingContext, const UMiniExperienceDefinition* Experience,
		const UGameFeatureData* CoreFeature);
};
