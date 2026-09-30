#include "MiniGameplayAbility_RifleFire.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "Character/MiniCharacter.h"
#include "Equipment/MiniEquipmentDefinition.h"
#include "Equipment/MiniEquipmentInstance.h"
#include "Equipment/MiniEquipmentManagerComponent.h"
#include "Engine/World.h"
#include "Inventory/MiniInventoryItemDefinition.h"
#include "Inventory/MiniInventoryItemInstance.h"
#include "System/MiniGameplayTags.h"

UMiniGameplayAbility_RifleFire::UMiniGameplayAbility_RifleFire()
{
	ActivationPolicy = EMiniAbilityActivationPolicy::OnInputTriggered;
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

	RequestOneShot(ActorInfo);
	const AMiniCharacter* Pawn = ActorInfo ? Cast<AMiniCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	const UMiniEquipmentInstance* Equipment = GetEquipmentFromSpec(Handle, ActorInfo);
	const UMiniRifleEquipmentDefinition* Rifle = Equipment
		? Cast<UMiniRifleEquipmentDefinition>(Equipment->GetEquipmentDefinition().GetDefaultObject()) : nullptr;
	const UMiniInventoryItemInstance* Item = Equipment ? Equipment->GetSourceItem() : nullptr;
	if (!Pawn || !Rifle || Rifle->GetFireInterval() <= 0.0f || !Pawn->GetWorld())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	// The initial RequestOneShot above already asks the server for empty-magazine
	// feedback. Keep this held activation alive until release, but do not repeat it.
	if (Item && Item->GetStat(MiniInventoryTags::AmmoInMagazine) <= 0)
	{
		return;
	}
	// Remote server activation remains active to receive release/cancel, but never issues a second shot.
	if (Pawn->IsLocallyControlled())
	{
		Pawn->GetWorldTimerManager().SetTimer(FireTimer, this, &ThisClass::FireWhileHeld,
			FMath::Max(0.01f, Rifle->GetFireInterval() + 0.01f), true);
	}
}

void UMiniGameplayAbility_RifleFire::FireWhileHeld()
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	const AMiniCharacter* Pawn = ActorInfo ? Cast<AMiniCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	const UMiniEquipmentInstance* Equipment = GetCurrentEquipment();
	const UMiniInventoryItemInstance* Item = Equipment ? Equipment->GetSourceItem() : nullptr;
	const UMiniAbilitySystemComponent* ASC = ActorInfo
		? Cast<UMiniAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get()) : nullptr;
	if (!Pawn || !Pawn->IsLocallyControlled() || !Equipment ||
		!Pawn->GetEquipmentManager() || Pawn->GetEquipmentManager()->GetCurrentEquipment() != Equipment ||
		!Item ||
		!ASC || ASC->IsAbilityInputBlocked() ||
		ASC->HasMatchingGameplayTag(MiniGameplayTags::State_Dead) ||
		ASC->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading))
	{
		EndAbility(GetCurrentAbilitySpecHandle(), ActorInfo, GetCurrentActivationInfo(), true, true);
		return;
	}
	if (Item->GetStat(MiniInventoryTags::AmmoInMagazine) <= 0)
	{
		// A held burst that just exhausted its local magazine requests exactly one
		// authoritative empty result, then waits for release or server cancellation.
		RequestOneShot(ActorInfo);
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(FireTimer);
		}
		return;
	}
	RequestOneShot(ActorInfo);
}

void UMiniGameplayAbility_RifleFire::InputReleased(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
{
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

void UMiniGameplayAbility_RifleFire::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FireTimer);
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
