#pragma once

#include "Engine/DataAsset.h"
#include "MiniMatchRulesConfig.generated.h"

UCLASS(BlueprintType)
class FPS_API UMiniMatchRulesConfig : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "2")) int32 MinPlayers = 2;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "1")) int32 ScoreLimit = 10;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0")) float SpawnProtectionSeconds = 2.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0")) float RespawnDelaySeconds = 3.0f;
	bool ValidateConfig(FString& OutError) const;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
