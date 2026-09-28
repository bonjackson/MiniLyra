#pragma once

#include "Engine/DataAsset.h"
#include "MiniPawnData.generated.h"

class APawn;
class UMiniAbilitySet;

UCLASS(BlueprintType, NotBlueprintable, Const)
class FPS_API UMiniPawnData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Pawn")
	TSubclassOf<APawn> PawnClass;

	/** Server grants these once to the PlayerState ASC; input and camera data arrive later. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Abilities")
	TArray<TObjectPtr<UMiniAbilitySet>> AbilitySets;

	bool ValidatePawnData(FString& OutError) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
