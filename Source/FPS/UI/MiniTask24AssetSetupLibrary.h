#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "MiniTask24AssetSetupLibrary.generated.h"

class UMiniExperienceDefinition;

/** Editor-only authoring for the one FrontEnd Experience; verification is read-only. */
UCLASS()
class FPS_API UMiniTask24AssetSetupLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool EnsureFrontEndExperience(UMiniExperienceDefinition* Experience);
	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool VerifyFrontEndExperience(const UMiniExperienceDefinition* Experience);
};
