#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "MiniTask09AssetSetupLibrary.generated.h"

class UMiniAbilitySet;
class UMiniPawnData;
class UMiniExperienceDefinition;

/** Narrow editor bridge for repeatable Task 09 asset authoring. */
UCLASS()
class FPS_API UMiniTask09AssetSetupLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool ConfigurePracticeAbilities(UMiniAbilitySet* PawnSet, UMiniAbilitySet* FeatureSet,
		UMiniPawnData* PawnData, UMiniExperienceDefinition* Experience);

	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool VerifyPracticeAbilities(const UMiniAbilitySet* PawnSet, const UMiniAbilitySet* FeatureSet,
		const UMiniPawnData* PawnData, const UMiniExperienceDefinition* Experience);
};
