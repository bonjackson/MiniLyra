#pragma once

#include "AbilitySystem/MiniGameplayAbility.h"
#include "MiniProbeAbility.generated.h"

/** Grants can be inspected without binding combat input yet. */
UCLASS()
class FPS_API UMiniPawnProbeAbility : public UMiniGameplayAbility
{
	GENERATED_BODY()

public:
	UMiniPawnProbeAbility();
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;
};

UCLASS()
class FPS_API UMiniFeatureProbeAbility : public UGameplayAbility
{
	GENERATED_BODY()
};
