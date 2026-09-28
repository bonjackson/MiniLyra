#include "MiniProbeAbility.h"

#include "System/MiniLogChannels.h"

UMiniPawnProbeAbility::UMiniPawnProbeAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

void UMiniPawnProbeAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	UE_LOG(LogMiniInit, Display, TEXT("MiniInputProbe FIRE_ACTIVE: Avatar=%s Handle=%s"),
		*GetPathNameSafe(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr), *Handle.ToString());
}

void UMiniPawnProbeAbility::InputReleased(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
{
	UE_LOG(LogMiniInit, Display, TEXT("MiniInputProbe FIRE_RELEASED: Avatar=%s Handle=%s"),
		*GetPathNameSafe(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr), *Handle.ToString());
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

void UMiniPawnProbeAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	UE_LOG(LogMiniInit, Display, TEXT("MiniInputProbe FIRE_ENDED: Avatar=%s Handle=%s Cancelled=%d"),
		*GetPathNameSafe(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr), *Handle.ToString(), bWasCancelled);
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
