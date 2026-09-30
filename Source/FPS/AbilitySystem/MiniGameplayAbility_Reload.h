#pragma once

#include "AbilitySystem/MiniGameplayAbility_FromEquipment.h"
#include "ActiveGameplayEffectHandle.h"
#include "TimerManager.h"
#include "MiniGameplayAbility_Reload.generated.h"

class AMiniCharacter;
class UMiniAbilitySystemComponent;
class UMiniEquipmentInstance;
class UMiniInventoryItemInstance;
class UMiniRangedWeaponEquipmentDefinition;

/** Server-owned timed transfer from the equipped item's reserve to its magazine. */
UCLASS()
class FPS_API UMiniGameplayAbility_Reload : public UMiniGameplayAbility_FromEquipment
{
	GENERATED_BODY()

public:
	UMiniGameplayAbility_Reload();
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	bool ResolveReloadContext(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, AMiniCharacter*& OutPawn,
		UMiniAbilitySystemComponent*& OutASC, UMiniEquipmentInstance*& OutEquipment,
		UMiniInventoryItemInstance*& OutItem,
		const UMiniRangedWeaponEquipmentDefinition*& OutDefinition) const;
	void FinishReload();

	FTimerHandle ReloadTimer;
	FActiveGameplayEffectHandle ReloadEffectHandle;
	TWeakObjectPtr<UMiniAbilitySystemComponent> ReloadASC;
	TWeakObjectPtr<UMiniEquipmentInstance> ReloadEquipment;
	TWeakObjectPtr<UMiniInventoryItemInstance> ReloadItem;
	bool bReloadCompleted = false;
};
