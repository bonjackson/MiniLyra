#include "MiniEquipmentManagerComponent.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHealthComponent.h"
#include "Equipment/MiniEquipmentDefinition.h"
#include "Equipment/MiniEquipmentInstance.h"
#include "Engine/ActorChannel.h"
#include "Inventory/MiniInventoryItemDefinition.h"
#include "Inventory/MiniInventoryItemInstance.h"
#include "Inventory/MiniInventoryManagerComponent.h"
#include "Net/UnrealNetwork.h"
#include "Player/MiniPlayerController.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniLogChannels.h"

UMiniEquipmentManagerComponent::UMiniEquipmentManagerComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsReplicatedByDefault(true);
}

void UMiniEquipmentManagerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnequipItem();
	Super::EndPlay(EndPlayReason);
}

void UMiniEquipmentManagerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, CurrentEquipment);
}

bool UMiniEquipmentManagerComponent::ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch,
	FReplicationFlags* RepFlags)
{
	bool bWrote = Super::ReplicateSubobjects(Channel, Bunch, RepFlags);
	if (IsValid(CurrentEquipment))
	{
		bWrote |= Channel->ReplicateSubobject(CurrentEquipment, *Bunch, *RepFlags);
	}
	return bWrote;
}

bool UMiniEquipmentManagerComponent::EquipItem(UMiniInventoryItemInstance* Item)
{
	AMiniCharacter* Pawn = Cast<AMiniCharacter>(GetOwner());
	AMiniPlayerController* Controller = Pawn ? Cast<AMiniPlayerController>(Pawn->GetController()) : nullptr;
	UMiniInventoryManagerComponent* Inventory = Controller ? Controller->GetInventoryManager() : nullptr;
	AMiniPlayerState* PlayerState = Pawn ? Pawn->GetPlayerState<AMiniPlayerState>() : nullptr;
	UMiniAbilitySystemComponent* ASC = PlayerState ? PlayerState->GetMiniAbilitySystemComponent() : nullptr;
	if (!Pawn || !Pawn->HasAuthority() || !IsValid(Item) || !Inventory || !ASC ||
		Controller->GetPawn() != Pawn ||
		(Pawn->GetHealthComponent() && Pawn->GetHealthComponent()->IsDead()) ||
		!Item->GetInstanceId().IsValid() || Item->GetOuter() != Controller ||
		ASC->GetAvatarActor() != Pawn ||
		!Inventory->GetEntries().ContainsByPredicate(
			[Item](const FMiniInventoryEntry& Entry) { return Entry.Instance == Item && Entry.StackCount > 0; }))
	{
		return false;
	}

	const UMiniInventoryFragment_Equippable* Equippable = Item->FindFragment<UMiniInventoryFragment_Equippable>();
	UClass* DefinitionClass = Equippable ? Equippable->EquipmentDefinition.LoadSynchronous() : nullptr;
	if (!DefinitionClass || !DefinitionClass->IsChildOf(UMiniEquipmentDefinition::StaticClass()) ||
		DefinitionClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return false;
	}
	const UMiniEquipmentDefinition* DefinitionCDO = GetDefault<UMiniEquipmentDefinition>(DefinitionClass);
	if (!DefinitionCDO || !DefinitionCDO->GetAbilitySet())
	{
		return false;
	}
	if (CurrentEquipment && CurrentEquipment->GetSourceItem() == Item &&
		CurrentEquipment->GetEquipmentDefinition() == DefinitionClass)
	{
		return true;
	}

	// Remove the old grant before giving the new one; no frame has both weapon grants.
	UnequipItem();
	UMiniEquipmentInstance* NewEquipment = NewObject<UMiniEquipmentInstance>(Pawn);
	if (!NewEquipment || !NewEquipment->InitializeEquipment(Item, DefinitionClass, ASC))
	{
		return false;
	}

	CurrentEquipment = NewEquipment;
	Pawn->ForceNetUpdate();
	Pawn->RefreshEquipmentAppearance();
	UE_LOG(LogMiniInit, Display, TEXT("MiniEquipment EQUIPPED: Pawn=%s Definition=%s ItemId=%s Abilities=%d"),
		*Pawn->GetPathName(), *GetNameSafe(DefinitionClass), *Item->GetInstanceId().ToString(),
		NewEquipment->GetGrantedHandles().GetAbilityCount());
	return true;
}

bool UMiniEquipmentManagerComponent::UnequipItem()
{
	AMiniCharacter* Pawn = Cast<AMiniCharacter>(GetOwner());
	if (!Pawn || !Pawn->HasAuthority())
	{
		return false;
	}
	if (!CurrentEquipment)
	{
		return true;
	}
	UMiniEquipmentInstance* OldEquipment = CurrentEquipment;
	const FGuid OldItemId = OldEquipment->GetSourceItemId();
	OldEquipment->RevokeAbilities();
	CurrentEquipment = nullptr;
	DestroyReplicatedSubObjectOnRemotePeers(OldEquipment);
	Pawn->ForceNetUpdate();
	Pawn->RefreshEquipmentAppearance();
	UE_LOG(LogMiniInit, Display, TEXT("MiniEquipment UNEQUIPPED: Pawn=%s ItemId=%s"),
		*Pawn->GetPathName(), *OldItemId.ToString());
	return true;
}

TSubclassOf<UMiniEquipmentDefinition> UMiniEquipmentManagerComponent::GetCurrentDefinitionClass() const
{
	return IsValid(CurrentEquipment) ? CurrentEquipment->GetEquipmentDefinition() : nullptr;
}

FGuid UMiniEquipmentManagerComponent::GetCurrentItemId() const
{
	return IsValid(CurrentEquipment) ? CurrentEquipment->GetSourceItemId() : FGuid();
}

void UMiniEquipmentManagerComponent::OnRep_CurrentEquipment()
{
	if (AMiniCharacter* Pawn = Cast<AMiniCharacter>(GetOwner()))
	{
		Pawn->RefreshEquipmentAppearance();
	}
}
