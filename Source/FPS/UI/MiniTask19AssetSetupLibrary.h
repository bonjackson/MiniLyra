#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "MiniTask19AssetSetupLibrary.generated.h"

class UBlueprint;
class UGameFeatureData;

/** Editor-only authoring and fresh-process checks for the practice UI assets. */
UCLASS()
class FPS_API UMiniTask19AssetSetupLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool EnsurePolicyBlueprint(UBlueprint* PolicyBlueprint);

	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool VerifyPolicyBlueprint(const UBlueprint* PolicyBlueprint);

	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool EnsurePluginWidgetAction(UGameFeatureData* FeatureData);

	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool VerifyPluginWidgetAction(const UGameFeatureData* FeatureData);
};
