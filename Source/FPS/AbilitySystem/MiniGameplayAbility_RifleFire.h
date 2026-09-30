#pragma once

#include "AbilitySystem/MiniGameplayAbility_RangedFire.h"
#include "TimerManager.h"
#include "MiniGameplayAbility_RifleFire.generated.h"

/** Rifle variant repeats at its configured interval while Fire is held. */
UCLASS()
class FPS_API UMiniGameplayAbility_RifleFire : public UMiniGameplayAbility_RangedFire
{
	GENERATED_BODY()

public:
	UMiniGameplayAbility_RifleFire();
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
	void FireWhileHeld();
	FTimerHandle FireTimer;
};
