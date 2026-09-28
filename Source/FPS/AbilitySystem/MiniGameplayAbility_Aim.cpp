#include "MiniGameplayAbility_Aim.h"

#include "System/MiniGameplayTags.h"
#include "System/MiniLogChannels.h"

UMiniGameplayAbility_Aim::UMiniGameplayAbility_Aim()
{
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(MiniGameplayTags::Ability_Aim);
	SetAssetTags(AssetTags);
	ActivationOwnedTags.AddTag(MiniGameplayTags::State_Aiming);
}

void UMiniGameplayAbility_Aim::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	UE_LOG(LogMiniInit, Display, TEXT("MiniAbility AIM_ACTIVE: Avatar=%s Handle=%s"),
		*GetPathNameSafe(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr), *Handle.ToString());
}

void UMiniGameplayAbility_Aim::InputReleased(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
{
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

void UMiniGameplayAbility_Aim::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	UE_LOG(LogMiniInit, Display, TEXT("MiniAbility AIM_ENDED: Avatar=%s Cancelled=%d"),
		*GetPathNameSafe(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr), bWasCancelled);
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
