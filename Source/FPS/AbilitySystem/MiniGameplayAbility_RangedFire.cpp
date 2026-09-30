#include "MiniGameplayAbility_RangedFire.h"

#include "Character/MiniCharacter.h"
#include "Equipment/MiniEquipmentDefinition.h"
#include "Equipment/MiniEquipmentInstance.h"
#include "Inventory/MiniInventoryItemInstance.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "System/MiniGameplayTags.h"
#include "Weapons/MiniRangedWeaponComponent.h"

UMiniGameplayAbility_RangedFire::UMiniGameplayAbility_RangedFire()
{
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	FGameplayTagContainer FireTags;
	FireTags.AddTag(MiniGameplayTags::Ability_Fire);
	SetAssetTags(FireTags);
}

bool UMiniGameplayAbility_RangedFire::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	// Task 10's held-fire lifecycle probe exclusively owns this input in probe runs.
	if (FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask10")) ||
		!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}
	const UMiniEquipmentInstance* Equipment = GetEquipmentFromSpec(Handle, ActorInfo);
	const UMiniRangedWeaponEquipmentDefinition* Definition = Equipment
		? Cast<UMiniRangedWeaponEquipmentDefinition>(Equipment->GetEquipmentDefinition().GetDefaultObject()) : nullptr;
	const UMiniInventoryItemInstance* Item = Equipment ? Equipment->GetSourceItem() : nullptr;
	return Definition && Item && Item->GetInstanceId() == Equipment->GetSourceItemId();
}

void UMiniGameplayAbility_RangedFire::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	RequestOneShot(ActorInfo);
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

void UMiniGameplayAbility_RangedFire::RequestOneShot(const FGameplayAbilityActorInfo* ActorInfo) const
{
	// A predicted activation also runs on the remote server; only the local owner sends a shot RPC.
	AMiniCharacter* Pawn = ActorInfo ? Cast<AMiniCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	if (Pawn && Pawn->IsLocallyControlled())
	{
		if (UMiniRangedWeaponComponent* Weapon = Pawn->GetRangedWeaponComponent())
		{
			Weapon->RequestFire();
		}
	}
}
