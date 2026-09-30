#include "MiniGameplayAbility_Reload.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "AbilitySystem/MiniReloadGameplayEffect.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHealthComponent.h"
#include "Equipment/MiniEquipmentDefinition.h"
#include "Equipment/MiniEquipmentInstance.h"
#include "Equipment/MiniEquipmentManagerComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "Inventory/MiniInventoryItemDefinition.h"
#include "Inventory/MiniInventoryItemInstance.h"
#include "System/MiniGameplayTags.h"
#include "System/MiniLogChannels.h"

UMiniGameplayAbility_Reload::UMiniGameplayAbility_Reload()
{
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

bool UMiniGameplayAbility_Reload::ResolveReloadContext(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, AMiniCharacter*& OutPawn,
	UMiniAbilitySystemComponent*& OutASC, UMiniEquipmentInstance*& OutEquipment,
	UMiniInventoryItemInstance*& OutItem,
	const UMiniRangedWeaponEquipmentDefinition*& OutDefinition) const
{
	OutPawn = ActorInfo ? Cast<AMiniCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	OutASC = ActorInfo
		? Cast<UMiniAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get()) : nullptr;
	OutEquipment = GetEquipmentFromSpec(Handle, ActorInfo);
	OutItem = IsValid(OutEquipment) ? OutEquipment->GetSourceItem() : nullptr;
	OutDefinition = IsValid(OutEquipment)
		? Cast<UMiniRangedWeaponEquipmentDefinition>(
			OutEquipment->GetEquipmentDefinition().GetDefaultObject()) : nullptr;
	const UMiniEquipmentManagerComponent* Manager = OutPawn ? OutPawn->GetEquipmentManager() : nullptr;
	const AController* Controller = OutPawn ? OutPawn->GetController() : nullptr;
	return OutPawn && OutASC && OutASC->GetAvatarActor() == OutPawn &&
		Controller && Controller->GetPawn() == OutPawn &&
		OutPawn->GetHealthComponent() && !OutPawn->GetHealthComponent()->IsDead() &&
		!OutASC->HasMatchingGameplayTag(MiniGameplayTags::State_Dead) &&
		!OutASC->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading) &&
		!OutASC->IsAbilityInputBlocked() &&
		Manager && Manager->GetCurrentEquipment() == OutEquipment &&
		IsValid(OutEquipment) && IsValid(OutItem) && OutDefinition &&
		OutItem->GetInstanceId().IsValid() &&
		OutItem->GetInstanceId() == OutEquipment->GetSourceItemId() &&
		OutDefinition->GetMagazineCapacity() > 0 &&
		OutDefinition->GetReloadDuration() > 0.0f &&
		OutItem->GetStat(MiniInventoryTags::AmmoInMagazine) < OutDefinition->GetMagazineCapacity() &&
		OutItem->GetStat(MiniInventoryTags::ReserveAmmo) > 0;
}

bool UMiniGameplayAbility_Reload::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}
	AMiniCharacter* Pawn = nullptr;
	UMiniAbilitySystemComponent* ASC = nullptr;
	UMiniEquipmentInstance* Equipment = nullptr;
	UMiniInventoryItemInstance* Item = nullptr;
	const UMiniRangedWeaponEquipmentDefinition* Definition = nullptr;
	return ResolveReloadContext(Handle, ActorInfo, Pawn, ASC, Equipment, Item, Definition);
}

void UMiniGameplayAbility_Reload::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	AMiniCharacter* Pawn = nullptr;
	UMiniAbilitySystemComponent* ASC = nullptr;
	UMiniEquipmentInstance* Equipment = nullptr;
	UMiniInventoryItemInstance* Item = nullptr;
	const UMiniRangedWeaponEquipmentDefinition* Definition = nullptr;
	if (!ResolveReloadContext(Handle, ActorInfo, Pawn, ASC, Equipment, Item, Definition) ||
		!Pawn->HasAuthority() || !Pawn->GetWorld() ||
		!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	FGameplayEffectSpecHandle EffectSpec = ASC->MakeOutgoingSpec(
		UMiniReloadGameplayEffect::StaticClass(), 1.0f, ASC->MakeEffectContext());
	if (!EffectSpec.IsValid())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	ReloadASC = ASC;
	ReloadEffectHandle = ASC->ApplyGameplayEffectSpecToSelf(*EffectSpec.Data.Get());
	if (!ReloadEffectHandle.IsValid() ||
		!ASC->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	ReloadEquipment = Equipment;
	ReloadItem = Item;
	FGameplayCueParameters ReloadCue;
	ReloadCue.Location = Pawn->GetActorLocation();
	ReloadCue.Instigator = Pawn;
	ReloadCue.EffectCauser = Pawn;
	ReloadCue.SourceObject = Equipment;
	ReloadCue.RawMagnitude = Definition->IsA<UMiniPistolEquipmentDefinition>() ? 1.0f : 2.0f;
	ASC->AddGameplayCue(MiniGameplayTags::GameplayCue_Mini_Reload, ReloadCue);
	Pawn->GetWorldTimerManager().SetTimer(ReloadTimer, this, &ThisClass::FinishReload,
		Definition->GetReloadDuration(), false);
	UE_LOG(LogMiniInit, Display,
		TEXT("MiniWeapon RELOAD_STARTED: Pawn=%s ItemId=%s Magazine=%d Reserve=%d Duration=%.2f"),
		*Pawn->GetPathName(), *Item->GetInstanceId().ToString(),
		Item->GetStat(MiniInventoryTags::AmmoInMagazine),
		Item->GetStat(MiniInventoryTags::ReserveAmmo), Definition->GetReloadDuration());
}

void UMiniGameplayAbility_Reload::FinishReload()
{
	if (!IsActive())
	{
		return;
	}
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	AMiniCharacter* Pawn = ActorInfo ? Cast<AMiniCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	UMiniAbilitySystemComponent* ASC = ReloadASC.Get();
	UMiniEquipmentInstance* Equipment = ReloadEquipment.Get();
	UMiniInventoryItemInstance* Item = ReloadItem.Get();
	const UMiniEquipmentManagerComponent* Manager = Pawn ? Pawn->GetEquipmentManager() : nullptr;
	const UMiniRangedWeaponEquipmentDefinition* Definition = Equipment
		? Cast<UMiniRangedWeaponEquipmentDefinition>(Equipment->GetEquipmentDefinition().GetDefaultObject()) : nullptr;
	const bool bStillValid = Pawn && Pawn->HasAuthority() && ASC &&
		ActorInfo->AbilitySystemComponent.Get() == ASC && ASC->GetAvatarActor() == Pawn &&
		Pawn->GetController() && Pawn->GetController()->GetPawn() == Pawn &&
		Pawn->GetHealthComponent() && !Pawn->GetHealthComponent()->IsDead() &&
		!ASC->HasMatchingGameplayTag(MiniGameplayTags::State_Dead) &&
		!ASC->IsAbilityInputBlocked() &&
		ASC->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading) &&
		ASC->GetActiveGameplayEffect(ReloadEffectHandle) &&
		Manager && IsValid(Equipment) && Manager->GetCurrentEquipment() == Equipment &&
		GetCurrentEquipment() == Equipment &&
		IsValid(Item) && Equipment->GetSourceItem() == Item &&
		Item->GetInstanceId() == Equipment->GetSourceItemId() && Definition;
	if (!bStillValid)
	{
		EndAbility(GetCurrentAbilitySpecHandle(), ActorInfo, GetCurrentActivationInfo(), true, true);
		return;
	}

	const int32 Magazine = Item->GetStat(MiniInventoryTags::AmmoInMagazine);
	const int32 Reserve = Item->GetStat(MiniInventoryTags::ReserveAmmo);
	const int32 Transfer = FMath::Min(FMath::Max(0, Definition->GetMagazineCapacity() - Magazine), Reserve);
	bool bTransferred = false;
	if (Transfer > 0 && Item->SetStat(MiniInventoryTags::AmmoInMagazine, Magazine + Transfer))
	{
		bTransferred = Item->SetStat(MiniInventoryTags::ReserveAmmo, Reserve - Transfer);
		if (!bTransferred)
		{
			Item->SetStat(MiniInventoryTags::AmmoInMagazine, Magazine);
		}
	}
	if (bTransferred)
	{
		bReloadCompleted = true;
		UE_LOG(LogMiniInit, Display,
			TEXT("MiniWeapon RELOAD_COMPLETED: Pawn=%s ItemId=%s Magazine=%d Reserve=%d Transfer=%d"),
			*Pawn->GetPathName(), *Item->GetInstanceId().ToString(), Magazine + Transfer,
			Reserve - Transfer, Transfer);
	}
	EndAbility(GetCurrentAbilitySpecHandle(), ActorInfo, GetCurrentActivationInfo(), true, !bTransferred);
}

void UMiniGameplayAbility_Reload::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReloadTimer);
	}
	if (UMiniAbilitySystemComponent* ASC = ReloadASC.Get())
	{
		if (ReloadEffectHandle.IsValid())
		{
			ASC->RemoveGameplayCue(MiniGameplayTags::GameplayCue_Mini_Reload);
		}
		if (ReloadEffectHandle.IsValid())
		{
			ASC->RemoveActiveGameplayEffect(ReloadEffectHandle);
		}
	}
	// ClearAbility uses EndAbility(..., bWasCancelled=false) even for forced removal.
	if (!bReloadCompleted && ReloadEquipment.IsValid())
	{
		UE_LOG(LogMiniInit, Display, TEXT("MiniWeapon RELOAD_CANCELLED: Pawn=%s ItemId=%s"),
			*GetPathNameSafe(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr),
			*ReloadEquipment->GetSourceItemId().ToString());
	}
	ReloadEffectHandle.Invalidate();
	ReloadASC.Reset();
	ReloadEquipment.Reset();
	ReloadItem.Reset();
	bReloadCompleted = false;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
