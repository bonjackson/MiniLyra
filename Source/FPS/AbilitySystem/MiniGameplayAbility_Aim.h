#pragma once

#include "AbilitySystem/MiniGameplayAbility.h"
#include "MiniGameplayAbility_Aim.generated.h"

/** Hold to aim; the active ability owns State.Aiming and the camera follows it. */
UCLASS()
class FPS_API UMiniGameplayAbility_Aim : public UMiniGameplayAbility
{
	GENERATED_BODY()

public:
	UMiniGameplayAbility_Aim();
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
};
