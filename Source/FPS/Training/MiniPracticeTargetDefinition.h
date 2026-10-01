#pragma once

#include "Engine/DataAsset.h"
#include "MiniPracticeTargetDefinition.generated.h"

class UMaterialInterface;

/** A small editable data asset for the practice target's health and reset cycle. */
UCLASS(BlueprintType)
class FPS_API UMiniPracticeTargetDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UMiniPracticeTargetDefinition();
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	bool ValidateDefinition(FString& OutError) const;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Target")
	FText DisplayName;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Target", meta = (ClampMin = "1.0"))
	float MaxHealth = 100.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Target", meta = (ClampMin = "0.1"))
	float ResetDelay = 2.0f;
	/** Author this material with a VectorParameter named TargetColor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Target", meta = (AssetBundles = "Client"))
	TSoftObjectPtr<UMaterialInterface> BoardMaterial;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Target")
	FLinearColor ActiveColor = FLinearColor(0.02f, 0.65f, 1.0f);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Target")
	FLinearColor DisabledColor = FLinearColor(1.0f, 0.25f, 0.05f);

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
