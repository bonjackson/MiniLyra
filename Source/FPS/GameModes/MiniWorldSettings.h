#pragma once

#include "GameFramework/WorldSettings.h"
#include "MiniWorldSettings.generated.h"

class UMiniExperienceDefinition;

UCLASS()
class FPS_API AMiniWorldSettings : public AWorldSettings
{
	GENERATED_BODY()

public:
	// An unset map override returns an invalid ID without an error; Task 05 may then use the project default.
	// A set but unscanned/incorrect asset returns an invalid ID and a descriptive error.
	FPrimaryAssetId GetDefaultGameplayExperience(FString* OutError = nullptr) const;

	UPROPERTY(EditAnywhere, Category = "Mini|Experience")
	TSoftObjectPtr<UMiniExperienceDefinition> DefaultGameplayExperience;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
