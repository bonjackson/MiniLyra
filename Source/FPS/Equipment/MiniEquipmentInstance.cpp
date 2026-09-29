#include "MiniEquipmentInstance.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "Character/MiniCharacter.h"
#include "Equipment/MiniEquipmentDefinition.h"
#include "Inventory/MiniInventoryItemInstance.h"
#include "Inventory/MiniInventoryManagerComponent.h"
#include "Net/UnrealNetwork.h"
#include "Player/MiniPlayerController.h"

void UMiniEquipmentInstance::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, EquipmentDefinition);
	DOREPLIFETIME(ThisClass, SourceItemId);
}

bool UMiniEquipmentInstance::InitializeEquipment(UMiniInventoryItemInstance* InItem,
	TSubclassOf<UMiniEquipmentDefinition> InDefinition, UMiniAbilitySystemComponent* ASC)
{
	AMiniCharacter* Pawn = GetOwningCharacter();
	if (!Pawn || !Pawn->HasAuthority() || !IsValid(InItem) || !InItem->GetInstanceId().IsValid() ||
		!InDefinition || InDefinition->HasAnyClassFlags(CLASS_Abstract) ||
		!ASC || !ASC->IsOwnerActorAuthoritative() || ASC->GetAvatarActor() != Pawn ||
		EquipmentDefinition || SourceItemId.IsValid())
	{
		return false;
	}

	const UMiniEquipmentDefinition* DefinitionCDO = GetDefault<UMiniEquipmentDefinition>(InDefinition);
	const UMiniAbilitySet* Set = DefinitionCDO->GetAbilitySet();
	if (!Set || !Set->GiveToAbilitySystem(ASC, GrantedHandles, this))
	{
		return false;
	}

	EquipmentDefinition = InDefinition;
	SourceItemId = InItem->GetInstanceId();
	SourceItem = InItem;
	GrantedAbilitySystem = ASC;
	return true;
}

void UMiniEquipmentInstance::RevokeAbilities()
{
	AMiniCharacter* Pawn = GetOwningCharacter();
	if (!Pawn || !Pawn->HasAuthority())
	{
		return;
	}
	if (UMiniAbilitySystemComponent* ASC = GrantedAbilitySystem.Get())
	{
		GrantedHandles.TakeFromAbilitySystem(ASC);
	}
	GrantedAbilitySystem.Reset();
	SourceItem.Reset();
}

AMiniCharacter* UMiniEquipmentInstance::GetOwningCharacter() const
{
	return Cast<AMiniCharacter>(GetOuter());
}

UMiniInventoryItemInstance* UMiniEquipmentInstance::GetSourceItem() const
{
	const AMiniCharacter* Pawn = GetOwningCharacter();
	if (!Pawn || !SourceItemId.IsValid())
	{
		return nullptr;
	}
	if (Pawn->HasAuthority())
	{
		return SourceItem.Get();
	}
	if (!Pawn->IsLocallyControlled())
	{
		return nullptr;
	}
	const AMiniPlayerController* Controller = Cast<AMiniPlayerController>(Pawn->GetController());
	const UMiniInventoryManagerComponent* Inventory = Controller && Controller->GetPawn() == Pawn
		? Controller->GetInventoryManager() : nullptr;
	if (!Inventory)
	{
		return nullptr;
	}
	for (const FMiniInventoryEntry& Entry : Inventory->GetEntries())
	{
		if (IsValid(Entry.Instance) && Entry.Instance->GetInstanceId() == SourceItemId)
		{
			return Entry.Instance;
		}
	}
	return nullptr;
}

void UMiniEquipmentInstance::OnRep_EquipmentDefinition()
{
	if (AMiniCharacter* Pawn = GetOwningCharacter())
	{
		Pawn->RefreshEquipmentAppearance();
	}
}
