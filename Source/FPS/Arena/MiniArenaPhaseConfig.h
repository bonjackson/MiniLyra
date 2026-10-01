#pragma once

#include "Engine/DataAsset.h"
#include "MiniArenaPhaseConfig.generated.h"

class UMiniGamePhaseAbility;

USTRUCT(BlueprintType)
struct FPS_API FMiniArenaPhaseEntry
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSubclassOf<UMiniGamePhaseAbility> AbilityClass;
	/** Zero waits indefinitely; task 22 will use this for a player-count policy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0"))
	float DurationSeconds = 0.0f;
};

UCLASS(BlueprintType)
class FPS_API UMiniArenaPhaseConfig : public UDataAsset
{
	GENERATED_BODY()
public:
	UMiniArenaPhaseConfig();
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FMiniArenaPhaseEntry> Phases;
	bool ValidateConfig(FString& OutError) const;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
