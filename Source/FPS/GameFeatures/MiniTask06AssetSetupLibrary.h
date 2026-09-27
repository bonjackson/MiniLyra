#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "MiniTask06AssetSetupLibrary.generated.h"

class AActor;
class UActorComponent;

/** Editor asset-authoring bridge for UE's non-Python-exposed FGameFeatureComponentEntry. */
UCLASS()
class FPS_API UMiniTask06AssetSetupLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Adds or updates one named AddComponents Action on a GameFeatureData or MiniExperienceDefinition. */
	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool EnsureAddComponentsAction(UObject* Owner, FName ActionName,
		TSubclassOf<AActor> ActorClass, TSubclassOf<UActorComponent> ComponentClass);
};
