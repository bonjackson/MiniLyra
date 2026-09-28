#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "MiniTask10AssetSetupLibrary.generated.h"

class UInputAction;
class UInputMappingContext;
class UMiniAbilitySet;
class UMiniInputConfig;
class UMiniPawnData;
class UMiniExperienceDefinition;

/** Narrow editor bridge for repeatable Task 10 Enhanced Input asset authoring. */
UCLASS()
class FPS_API UMiniTask10AssetSetupLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool ConfigurePracticeInput(UMiniInputConfig* InputConfig, UInputMappingContext* MappingContext,
		UMiniAbilitySet* PawnAbilitySet,
		UInputAction* Move, UInputAction* Look, UInputAction* Jump, UInputAction* Fire,
		UInputAction* Reload, UInputAction* SwitchWeapon, UInputAction* Aim,
		UMiniPawnData* PawnData, UMiniExperienceDefinition* Experience);

	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool VerifyPracticeInput(const UMiniInputConfig* InputConfig, const UInputMappingContext* MappingContext,
		const UMiniAbilitySet* PawnAbilitySet,
		const UInputAction* Move, const UInputAction* Look, const UInputAction* Jump, const UInputAction* Fire,
		const UInputAction* Reload, const UInputAction* SwitchWeapon, const UInputAction* Aim,
		const UMiniPawnData* PawnData, const UMiniExperienceDefinition* Experience);
};
