#include "MiniGameplayAbility.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"

UMiniGameplayAbility::UMiniGameplayAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

bool UMiniGameplayAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}
	const UMiniAbilitySystemComponent* ASC = ActorInfo
		? Cast<UMiniAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get()) : nullptr;
	return ASC && ActorInfo->AvatarActor.IsValid() &&
		ASC->AreAbilityTagRequirementsMet(GetAssetTags(), OptionalRelevantTags);
}
