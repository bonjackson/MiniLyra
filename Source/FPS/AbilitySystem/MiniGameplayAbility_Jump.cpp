#include "MiniGameplayAbility_Jump.h"

#include "Character/MiniCharacter.h"
#include "System/MiniGameplayTags.h"
#include "System/MiniLogChannels.h"

UMiniGameplayAbility_Jump::UMiniGameplayAbility_Jump()
{
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(MiniGameplayTags::Ability_Jump);
	SetAssetTags(AssetTags);
}

bool UMiniGameplayAbility_Jump::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	const AMiniCharacter* Character = ActorInfo ? Cast<AMiniCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	return Character && Character->CanJump() &&
		Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
}

void UMiniGameplayAbility_Jump::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	AMiniCharacter* Character = ActorInfo ? Cast<AMiniCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	if (!Character || !CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	JumpingCharacter = Character;
	Character->Jump();
	UE_LOG(LogMiniInit, Display, TEXT("MiniAbility JUMP_ACTIVE: Avatar=%s Handle=%s"),
		*Character->GetPathName(), *Handle.ToString());
}

void UMiniGameplayAbility_Jump::InputReleased(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
{
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

void UMiniGameplayAbility_Jump::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	if (AMiniCharacter* Character = JumpingCharacter.Get())
	{
		Character->StopJumping();
	}
	JumpingCharacter.Reset();
	UE_LOG(LogMiniInit, Display, TEXT("MiniAbility JUMP_ENDED: Avatar=%s Cancelled=%d"),
		*GetPathNameSafe(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr), bWasCancelled);
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
