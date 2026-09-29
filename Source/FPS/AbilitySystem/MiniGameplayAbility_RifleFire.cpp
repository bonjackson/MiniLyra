#include "MiniGameplayAbility_RifleFire.h"

#include "Character/MiniCharacter.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "System/MiniGameplayTags.h"
#include "Weapons/MiniRangedWeaponComponent.h"

UMiniGameplayAbility_RifleFire::UMiniGameplayAbility_RifleFire()
{
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	FGameplayTagContainer FireTags;
	FireTags.AddTag(MiniGameplayTags::Ability_Fire);
	SetAssetTags(FireTags);
}

bool UMiniGameplayAbility_RifleFire::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	// Task 10's legacy held-fire lifecycle probe exclusively owns this input in probe runs.
	return !FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask10")) &&
		Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
}

void UMiniGameplayAbility_RifleFire::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// The local owner sends a single request. The remote server activation runs too,
	// but must not send the request again or it would count as a second shot.
	AMiniCharacter* Pawn = ActorInfo ? Cast<AMiniCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	if (Pawn && Pawn->IsLocallyControlled())
	{
		if (UMiniRangedWeaponComponent* Weapon = Pawn->FindComponentByClass<UMiniRangedWeaponComponent>())
		{
			Weapon->RequestFire();
		}
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
