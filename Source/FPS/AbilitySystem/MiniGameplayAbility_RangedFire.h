#pragma once

#include "AbilitySystem/MiniGameplayAbility_FromEquipment.h"
#include "MiniGameplayAbility_RangedFire.generated.h"

/** Shared input-to-weapon path. The equipped weapon definition owns combat tuning. */
UCLASS()
class FPS_API UMiniGameplayAbility_RangedFire : public UMiniGameplayAbility_FromEquipment
{
	GENERATED_BODY()

public:
	UMiniGameplayAbility_RangedFire();
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

protected:
	void RequestOneShot(const FGameplayAbilityActorInfo* ActorInfo) const;
};
