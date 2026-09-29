#pragma once

#include "AbilitySystem/MiniGameplayAbility.h"
#include "MiniGameplayAbility_FromEquipment.generated.h"

class UMiniEquipmentInstance;

/** An equipment-granted ability resolves the exact active instance from its spec SourceObject. */
UCLASS(Abstract, Blueprintable)
class FPS_API UMiniGameplayAbility_FromEquipment : public UMiniGameplayAbility
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Mini|Equipment")
	UMiniEquipmentInstance* GetCurrentEquipment() const;

	UMiniEquipmentInstance* GetEquipmentFromSpec(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo) const;

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
};

/** No input binding; makes grant/revocation and SourceObject observable before fire is built. */
UCLASS()
class FPS_API UMiniGameplayAbility_EquipmentProbe : public UMiniGameplayAbility_FromEquipment
{
	GENERATED_BODY()

public:
	UMiniGameplayAbility_EquipmentProbe();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
};
