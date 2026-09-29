#pragma once

#include "AbilitySystem/MiniGameplayAbility_FromEquipment.h"
#include "MiniGameplayAbility_RifleFire.generated.h"

/** A press activates the equipped rifle and forwards one fire request from its owning player. */
UCLASS()
class FPS_API UMiniGameplayAbility_RifleFire : public UMiniGameplayAbility_FromEquipment
{
	GENERATED_BODY()

public:
	UMiniGameplayAbility_RifleFire();
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
};
