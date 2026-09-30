#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "MiniTask18CueAssetSetupLibrary.generated.h"

class UGameFeatureData;

/** Narrow editor bridge for authoring and inspecting the feature's Cue Action. */
UCLASS()
class FPS_API UMiniTask18CueAssetSetupLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool EnsurePluginCueAction(UGameFeatureData* FeatureData);

	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool VerifyPluginCueAction(const UGameFeatureData* FeatureData);

	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool VerifyProbeCueClass(UClass* CueClass);
};
