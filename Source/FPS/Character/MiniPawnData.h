#pragma once

#include "Engine/DataAsset.h"
#include "MiniPawnData.generated.h"

class APawn;
class UMiniAbilitySet;
class UMiniInputConfig;
class UMiniCameraMode;

UCLASS(BlueprintType, NotBlueprintable, Const)
class FPS_API UMiniPawnData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Pawn")
	TSubclassOf<APawn> PawnClass;

	/** Server grants these once to the PlayerState ASC. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Abilities")
	TArray<TObjectPtr<UMiniAbilitySet>> AbilitySets;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Input")
	TObjectPtr<UMiniInputConfig> InputConfig;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Camera")
	TSubclassOf<UMiniCameraMode> DefaultCameraMode;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Camera")
	TSubclassOf<UMiniCameraMode> AimCameraMode;

	bool ValidatePawnData(FString& OutError) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
