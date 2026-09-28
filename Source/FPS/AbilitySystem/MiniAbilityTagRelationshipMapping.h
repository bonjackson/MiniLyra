#pragma once

#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "MiniAbilityTagRelationshipMapping.generated.h"

/** One ability's activation rules, or the abilities cancelled when a status tag appears. */
USTRUCT(BlueprintType)
struct FMiniAbilityTagRelationship
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category = "Mini|Abilities")
	FGameplayTag Tag;

	UPROPERTY(EditDefaultsOnly, Category = "Mini|Abilities")
	FGameplayTagContainer ActivationRequiredTags;

	UPROPERTY(EditDefaultsOnly, Category = "Mini|Abilities")
	FGameplayTagContainer ActivationBlockedTags;

	UPROPERTY(EditDefaultsOnly, Category = "Mini|Abilities")
	FGameplayTagContainer CancelAbilitiesWithTags;
};

/** Small, data-driven subset of Lyra's ability tag relationship mapping. */
UCLASS(BlueprintType, Const)
class FPS_API UMiniAbilityTagRelationshipMapping : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, Category = "Mini|Abilities")
	TArray<FMiniAbilityTagRelationship> Relationships;

	void GetActivationTagRequirements(const FGameplayTagContainer& AbilityTags,
		FGameplayTagContainer& OutRequired, FGameplayTagContainer& OutBlocked) const;
	void GetCancelAbilityTags(FGameplayTag AddedStatusTag, FGameplayTagContainer& OutCancelTags) const;
};
