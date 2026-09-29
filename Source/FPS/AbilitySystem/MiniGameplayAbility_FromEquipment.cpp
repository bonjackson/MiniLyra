#include "MiniGameplayAbility_FromEquipment.h"

#include "AbilitySystemComponent.h"
#include "Character/MiniCharacter.h"
#include "Equipment/MiniEquipmentInstance.h"
#include "Equipment/MiniEquipmentManagerComponent.h"
#include "System/MiniLogChannels.h"

UMiniEquipmentInstance* UMiniGameplayAbility_FromEquipment::GetCurrentEquipment() const
{
	return Cast<UMiniEquipmentInstance>(GetCurrentSourceObject());
}

UMiniEquipmentInstance* UMiniGameplayAbility_FromEquipment::GetEquipmentFromSpec(
	const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo) const
{
	return Cast<UMiniEquipmentInstance>(GetSourceObject(Handle, ActorInfo));
}

bool UMiniGameplayAbility_FromEquipment::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}
	const AMiniCharacter* Pawn = ActorInfo ? Cast<AMiniCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	const UMiniEquipmentManagerComponent* Manager = Pawn
		? Pawn->FindComponentByClass<UMiniEquipmentManagerComponent>() : nullptr;
	UMiniEquipmentInstance* Equipment = GetEquipmentFromSpec(Handle, ActorInfo);
	return IsValid(Equipment) && Equipment->GetOwningCharacter() == Pawn && Manager &&
		Manager->GetCurrentEquipment() == Equipment;
}

UMiniGameplayAbility_EquipmentProbe::UMiniGameplayAbility_EquipmentProbe()
{
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

void UMiniGameplayAbility_EquipmentProbe::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	const UMiniEquipmentInstance* Equipment = GetEquipmentFromSpec(Handle, ActorInfo);
	UE_LOG(LogMiniInit, Display, TEXT("MiniEquipment ABILITY_ACTIVE: Avatar=%s ItemId=%s Handle=%s"),
		*GetPathNameSafe(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr),
		Equipment ? *Equipment->GetSourceItemId().ToString() : TEXT("None"), *Handle.ToString());
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
