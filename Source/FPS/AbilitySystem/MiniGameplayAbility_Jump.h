#pragma once

#include "AbilitySystem/MiniGameplayAbility.h"
#include "MiniGameplayAbility_Jump.generated.h"

class AMiniCharacter;

/** Predictable held jump; the server independently checks CanJump and tag rules. */
UCLASS()
class FPS_API UMiniGameplayAbility_Jump : public UMiniGameplayAbility
{
	GENERATED_BODY()

public:
	UMiniGameplayAbility_Jump();
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	TWeakObjectPtr<AMiniCharacter> JumpingCharacter;
};
