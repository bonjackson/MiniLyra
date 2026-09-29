#include "MiniTask14ProbeActor.h"

#include "Inventory/MiniInventoryItemDefinition.h"
#include "Inventory/MiniInventoryItemInstance.h"
#include "Inventory/MiniInventoryManagerComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"
#include "Player/MiniPlayerController.h"
#include "System/MiniLogChannels.h"

AMiniTask14ProbeActor::AMiniTask14ProbeActor()
{
	bReplicates = true;
	bOnlyRelevantToOwner = true;
}

void AMiniTask14ProbeActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMiniTask14ProbeActor, OwnerIndex);
}

void AMiniTask14ProbeActor::InitializeServer(int32 InOwnerIndex,
	UMiniInventoryItemInstance* Rifle, UMiniInventoryItemInstance* Pistol)
{
	if (!HasAuthority() || !Rifle || !Pistol || InOwnerIndex < 1)
	{
		Fail(TEXT("invalid server initialization"));
		return;
	}
	OwnerIndex = InOwnerIndex;
	InitialRifle = Rifle;
	InitialPistol = Pistol;
	InitialRifleId = Rifle->GetInstanceId();
	InitialPistolId = Pistol->GetInstanceId();
	ForceNetUpdate();
}

void AMiniTask14ProbeActor::Fail(const TCHAR* Reason)
{
	if (!bFailed)
	{
		bFailed = true;
		UE_LOG(LogMiniInit, Error, TEXT("MiniTask14Probe FAIL: Owner=%d Phase=%d Reason=%s"),
			OwnerIndex, static_cast<int32>(Phase), Reason);
	}
}

bool AMiniTask14ProbeActor::HasExpectedInventory(bool bHasRifle,
	UMiniInventoryItemInstance* ExpectedRifle, int32 ExpectedRifleAmmo) const
{
	const AMiniPlayerController* Controller = Cast<AMiniPlayerController>(GetOwner());
	const UMiniInventoryManagerComponent* Inventory = Controller ? Controller->GetInventoryManager() : nullptr;
	if (!Inventory || Inventory->GetEntries().Num() != (bHasRifle ? 2 : 1))
	{
		return false;
	}
	bool bFoundPistol = false;
	bool bFoundRifle = false;
	for (const FMiniInventoryEntry& Entry : Inventory->GetEntries())
	{
		UMiniInventoryItemInstance* Instance = Entry.Instance;
		if (!IsValid(Instance) || Entry.StackCount != 1 || !Instance->GetInstanceId().IsValid())
		{
			return false;
		}
		if (Instance->GetItemDefinition() == UMiniPistolItemDefinition::StaticClass() &&
			Instance->GetInstanceId() == InitialPistolId && Instance == InitialPistol.Get() &&
			Instance->GetStat(MiniInventoryTags::AmmoInMagazine) == 12 &&
			Instance->GetStat(MiniInventoryTags::ReserveAmmo) == 36)
		{
			bFoundPistol = true;
		}
		else if (bHasRifle && Instance->GetItemDefinition() == UMiniRifleItemDefinition::StaticClass() &&
			Instance == ExpectedRifle && Instance->GetInstanceId() != InitialPistolId &&
			Instance->GetStat(MiniInventoryTags::AmmoInMagazine) == ExpectedRifleAmmo &&
			Instance->GetStat(MiniInventoryTags::ReserveAmmo) == 90)
		{
			bFoundRifle = true;
		}
		else
		{
			return false;
		}
	}
	return bFoundPistol && bFoundRifle == bHasRifle;
}

void AMiniTask14ProbeActor::ServerChangeRifleStat_Implementation()
{
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask14")))
	{
		return;
	}
	AMiniPlayerController* Controller = Cast<AMiniPlayerController>(GetOwner());
	UMiniInventoryManagerComponent* Inventory = Controller ? Controller->GetInventoryManager() : nullptr;
	UMiniInventoryItemInstance* Rifle = InitialRifle.Get();
	if (bFailed || Phase != EPhase::Initial || !Inventory || !Rifle ||
		!HasExpectedInventory(true, Rifle) ||
		!Rifle->SetStat(MiniInventoryTags::AmmoInMagazine, 29) ||
		!HasExpectedInventory(true, Rifle, 29))
	{
		Fail(TEXT("server could not mutate Rifle stat in place"));
		return;
	}
	Controller->ForceNetUpdate();
	Phase = EPhase::RifleStatChanged;
	UE_LOG(LogMiniInit, Display,
		TEXT("MiniTask14Probe SERVER_STATS_CHANGED: Owner=%d Rifle=%s Ammo=29 Pistol=%s Count=2"),
		OwnerIndex, *InitialRifleId.ToString(), *InitialPistolId.ToString());
#endif
}

void AMiniTask14ProbeActor::ServerRemoveRifle_Implementation()
{
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask14")))
	{
		return;
	}
	AMiniPlayerController* Controller = Cast<AMiniPlayerController>(GetOwner());
	UMiniInventoryManagerComponent* Inventory = Controller ? Controller->GetInventoryManager() : nullptr;
	UMiniInventoryItemInstance* OldRifle = InitialRifle.Get();
	if (bFailed || Phase != EPhase::RifleStatChanged || !Inventory || !OldRifle ||
		!HasExpectedInventory(true, OldRifle, 29) || !Inventory->RemoveItem(OldRifle))
	{
		Fail(TEXT("server could not remove its original Rifle"));
		return;
	}
	if (!HasExpectedInventory(false, nullptr))
	{
		Fail(TEXT("removed Rifle remains in the server entries"));
		return;
	}
	for (const FMiniInventoryEntry& Entry : Inventory->GetEntries())
	{
		if (Entry.Instance == OldRifle ||
			(Entry.Instance && Entry.Instance->GetInstanceId() == InitialRifleId))
		{
			Fail(TEXT("stale Rifle instance survived removal"));
			return;
		}
	}
	Phase = EPhase::RifleRemoved;
	UE_LOG(LogMiniInit, Display,
		TEXT("MiniTask14Probe SERVER_REMOVED: Owner=%d OldRifle=%s Pistol=%s Count=1"),
		OwnerIndex, *InitialRifleId.ToString(), *InitialPistolId.ToString());
#endif
}

void AMiniTask14ProbeActor::ServerAddRifle_Implementation()
{
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask14")))
	{
		return;
	}
	AMiniPlayerController* Controller = Cast<AMiniPlayerController>(GetOwner());
	UMiniInventoryManagerComponent* Inventory = Controller ? Controller->GetInventoryManager() : nullptr;
	if (bFailed || Phase != EPhase::RifleRemoved || !Inventory ||
		!HasExpectedInventory(false, nullptr))
	{
		Fail(TEXT("server re-add preconditions failed"));
		return;
	}
	UMiniInventoryItemInstance* NewRifle = Inventory->AddItem(UMiniRifleItemDefinition::StaticClass(), 1);
	if (!IsValid(NewRifle) || NewRifle == InitialRifle.Get() ||
		NewRifle->GetInstanceId() == InitialRifleId ||
		!HasExpectedInventory(true, NewRifle))
	{
		Fail(TEXT("server reused the old Rifle or produced invalid entries"));
		return;
	}
	Phase = EPhase::RifleReadded;
	UE_LOG(LogMiniInit, Display,
		TEXT("MiniTask14Probe SERVER_READDED: Owner=%d OldRifle=%s NewRifle=%s Pistol=%s Count=2"),
		OwnerIndex, *InitialRifleId.ToString(), *NewRifle->GetInstanceId().ToString(),
		*InitialPistolId.ToString());
#endif
}
