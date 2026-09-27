#pragma once

#include "Engine/DataAsset.h"
#include "MiniPawnData.generated.h"

class APawn;

UCLASS(BlueprintType, NotBlueprintable, Const)
class FPS_API UMiniPawnData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	// Ability sets, input and camera data are added in later tasks.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Pawn")
	TSubclassOf<APawn> PawnClass;

	bool ValidatePawnData(FString& OutError) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
