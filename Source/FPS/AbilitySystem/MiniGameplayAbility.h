#pragma once

#include "Abilities/GameplayAbility.h"
#include "MiniGameplayAbility.generated.h"

/** Input activation choices used by the Mini ASC. */
UENUM(BlueprintType)
enum class EMiniAbilityActivationPolicy : uint8
{
	OnInputTriggered,
	WhileInputActive
};

/** Shared activation policy and data-driven tag gate for player abilities. */
UCLASS(Abstract, Blueprintable)
class FPS_API UMiniGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UMiniGameplayAbility();
	EMiniAbilityActivationPolicy GetActivationPolicy() const { return ActivationPolicy; }

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Input")
	EMiniAbilityActivationPolicy ActivationPolicy = EMiniAbilityActivationPolicy::OnInputTriggered;
};
