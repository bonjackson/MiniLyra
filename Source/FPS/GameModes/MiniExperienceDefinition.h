#pragma once

#include "Engine/DataAsset.h"
#include "MiniExperienceDefinition.generated.h"

class UGameFeatureAction;
class UMiniExperienceActionSet;
class UMiniPawnData;

UCLASS(BlueprintType, NotBlueprintable, Const)
class FPS_API UMiniExperienceDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	/** A non-gameplay Experience may intentionally omit PawnData. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Gameplay")
	bool bIsFrontEnd = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Gameplay")
	TObjectPtr<UMiniPawnData> DefaultPawnData;

	UPROPERTY(EditDefaultsOnly, Category = "Mini|Features")
	TArray<FString> GameFeaturesToEnable;

	UPROPERTY(EditDefaultsOnly, Instanced, Category = "Mini|Actions")
	TArray<TObjectPtr<UGameFeatureAction>> Actions;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Gameplay")
	TArray<TObjectPtr<UMiniExperienceActionSet>> ActionSets;

	bool ValidateDefinition(FString& OutError) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

#if WITH_EDITORONLY_DATA
	virtual void UpdateAssetBundleData() override;
#endif
};
