#pragma once

#include "Engine/DataAsset.h"
#include "MiniExperienceActionSet.generated.h"

class UGameFeatureAction;

UCLASS(BlueprintType, NotBlueprintable, Const)
class FPS_API UMiniExperienceActionSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	// An empty ActionSet is valid and useful while bootstrapping an Experience.
	UPROPERTY(EditDefaultsOnly, Instanced, Category = "Mini|Actions")
	TArray<TObjectPtr<UGameFeatureAction>> Actions;

	UPROPERTY(EditDefaultsOnly, Category = "Mini|Features")
	TArray<FString> GameFeaturesToEnable;

	bool ValidateActionSet(FString& OutError) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

#if WITH_EDITORONLY_DATA
	virtual void UpdateAssetBundleData() override;
#endif
};
