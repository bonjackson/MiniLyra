#include "MiniTask14ProbeSubsystem.h"

#include "MiniTask14ProbeActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Inventory/MiniInventoryItemDefinition.h"
#include "Inventory/MiniInventoryItemInstance.h"
#include "Inventory/MiniInventoryManagerComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MiniPlayerController.h"
#include "System/MiniLogChannels.h"

bool UMiniTask14ProbeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if !UE_BUILD_SHIPPING
	return Super::ShouldCreateSubsystem(Outer) &&
		FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask14"));
#else
	return false;
#endif
}

TStatId UMiniTask14ProbeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMiniTask14ProbeSubsystem, STATGROUP_Tickables);
}

void UMiniTask14ProbeSubsystem::Fail(const TCHAR* Reason)
{
	if (!bFailed)
	{
		bFailed = true;
		UE_LOG(LogMiniInit, Error, TEXT("MiniTask14Probe FAIL: Owner=%d Stage=%d Reason=%s"),
			OwnerIndex, static_cast<int32>(Stage), Reason);
	}
}

void UMiniTask14ProbeSubsystem::SetStage(EStage NewStage)
{
	Stage = NewStage;
	StageSeconds = 0.0f;
}

void UMiniTask14ProbeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bFailed || !GetWorld() || !GetWorld()->HasBegunPlay())
	{
		return;
	}
	if (GetWorld()->GetNetMode() == NM_ListenServer)
	{
		TickServer();
	}
	else if (GetWorld()->GetNetMode() == NM_Client && Stage != EStage::Done)
	{
		TickClient(DeltaTime);
	}
}

void UMiniTask14ProbeSubsystem::TickServer()
{
	if (bServerStarted)
	{
		return;
	}
	TArray<AMiniPlayerController*> Controllers;
	for (TActorIterator<AMiniPlayerController> It(GetWorld()); It; ++It)
	{
		AMiniPlayerController* Controller = *It;
		if (Controller && !Controller->IsLocalController() && Controller->GetPawn() &&
			Controller->GetInventoryManager())
		{
			Controllers.Add(Controller);
		}
	}
	if (Controllers.Num() < 2)
	{
		return;
	}
	if (Controllers.Num() != 2)
	{
		Fail(TEXT("expected exactly two remote inventory owners"));
		return;
	}
	for (AMiniPlayerController* Controller : Controllers)
	{
		if (!Controller->GetInventoryManager()->GetEntries().IsEmpty())
		{
			Fail(TEXT("inventory was not empty before server preload"));
			return;
		}
	}
	for (int32 Index = 0; Index < Controllers.Num(); ++Index)
	{
		AMiniPlayerController* Controller = Controllers[Index];
		UMiniInventoryManagerComponent* Inventory = Controller->GetInventoryManager();
		UMiniInventoryItemInstance* Rifle = Inventory->AddItem(UMiniRifleItemDefinition::StaticClass(), 1);
		UMiniInventoryItemInstance* Pistol = Inventory->AddItem(UMiniPistolItemDefinition::StaticClass(), 1);
		if (!IsValid(Rifle) || !IsValid(Pistol) || Inventory->GetEntries().Num() != 2 ||
			!Rifle->GetInstanceId().IsValid() || !Pistol->GetInstanceId().IsValid() ||
			Rifle->GetInstanceId() == Pistol->GetInstanceId() ||
			Rifle->GetStat(MiniInventoryTags::AmmoInMagazine) != 30 ||
			Rifle->GetStat(MiniInventoryTags::ReserveAmmo) != 90 ||
			Pistol->GetStat(MiniInventoryTags::AmmoInMagazine) != 12 ||
			Pistol->GetStat(MiniInventoryTags::ReserveAmmo) != 36)
		{
			Fail(TEXT("server preload did not create two unique item instances"));
			return;
		}
		FActorSpawnParameters Params;
		Params.Owner = Controller;
		AMiniTask14ProbeActor* Probe = GetWorld()->SpawnActor<AMiniTask14ProbeActor>(
			AMiniTask14ProbeActor::StaticClass(), FTransform::Identity, Params);
		if (!Probe)
		{
			Fail(TEXT("could not spawn owner-only inventory probe"));
			return;
		}
		Probe->InitializeServer(Index + 1, Rifle, Pistol);
		UE_LOG(LogMiniInit, Display,
			TEXT("MiniTask14Probe SERVER_PRELOAD: Owner=%d Rifle=%s Pistol=%s Count=2"),
			Index + 1, *Rifle->GetInstanceId().ToString(), *Pistol->GetInstanceId().ToString());
	}
	bServerStarted = true;
}

bool UMiniTask14ProbeSubsystem::CheckClientPrivacy(AMiniPlayerController*& OutController,
	AMiniTask14ProbeActor*& OutProbe) const
{
	OutController = nullptr;
	OutProbe = nullptr;
	int32 ControllerCount = 0;
	int32 ProbeCount = 0;
	for (TActorIterator<AMiniPlayerController> It(GetWorld()); It; ++It)
	{
		++ControllerCount;
		if (It->IsLocalController())
		{
			OutController = *It;
		}
	}
	for (TActorIterator<AMiniTask14ProbeActor> It(GetWorld()); It; ++It)
	{
		++ProbeCount;
		OutProbe = *It;
	}
	return ControllerCount == 1 && ProbeCount == 1 && OutController && OutProbe &&
		OutProbe->GetOwner() == OutController && OutProbe->GetOwnerIndex() > 0;
}

bool UMiniTask14ProbeSubsystem::CheckEntries(const UMiniInventoryManagerComponent* Inventory,
	bool bExpectRifle, FGuid ExpectedPistolId, FGuid ExcludedRifleId,
	FGuid& OutRifleId, UMiniInventoryItemInstance*& OutRifle, int32 ExpectedRifleAmmo) const
{
	OutRifleId.Invalidate();
	OutRifle = nullptr;
	if (!Inventory || Inventory->GetEntries().Num() != (bExpectRifle ? 2 : 1))
	{
		return false;
	}
	bool bFoundPistol = false;
	bool bFoundRifle = false;
	for (const FMiniInventoryEntry& Entry : Inventory->GetEntries())
	{
		UMiniInventoryItemInstance* Instance = Entry.Instance;
		if (!IsValid(Instance) || Entry.StackCount != 1 || !Instance->GetInstanceId().IsValid() ||
			Instance->GetInstanceId() == ExcludedRifleId)
		{
			return false;
		}
		if (Instance->GetItemDefinition() == UMiniPistolItemDefinition::StaticClass() &&
			(!ExpectedPistolId.IsValid() || Instance->GetInstanceId() == ExpectedPistolId) &&
			Instance->GetStat(MiniInventoryTags::AmmoInMagazine) == 12 &&
			Instance->GetStat(MiniInventoryTags::ReserveAmmo) == 36)
		{
			bFoundPistol = true;
		}
		else if (bExpectRifle &&
			Instance->GetItemDefinition() == UMiniRifleItemDefinition::StaticClass() &&
			Instance->GetStat(MiniInventoryTags::AmmoInMagazine) == ExpectedRifleAmmo &&
			Instance->GetStat(MiniInventoryTags::ReserveAmmo) == 90)
		{
			bFoundRifle = true;
			OutRifleId = Instance->GetInstanceId();
			OutRifle = Instance;
		}
		else
		{
			return false;
		}
	}
	return bFoundPistol && bFoundRifle == bExpectRifle;
}

void UMiniTask14ProbeSubsystem::TickClient(float DeltaTime)
{
	StageSeconds += DeltaTime;
	if (StageSeconds > (Stage == EStage::WaitSnapshot ? 90.0f : 20.0f))
	{
		Fail(TEXT("client inventory stage timed out"));
		return;
	}
	AMiniPlayerController* Controller = nullptr;
	AMiniTask14ProbeActor* Probe = nullptr;
	if (!CheckClientPrivacy(Controller, Probe))
	{
		return;
	}
	UMiniInventoryManagerComponent* Inventory = Controller->GetInventoryManager();
	if (!Inventory)
	{
		return;
	}
	FGuid RifleId;
	UMiniInventoryItemInstance* Rifle = nullptr;
	switch (Stage)
	{
	case EStage::WaitSnapshot:
		if (CheckEntries(Inventory, true, FGuid(), FGuid(), RifleId, Rifle))
		{
			if (Inventory->AddItem(UMiniRifleItemDefinition::StaticClass(), 1) ||
				Inventory->RemoveItem(Rifle) ||
				Rifle->SetStat(MiniInventoryTags::AmmoInMagazine, 999) ||
				Rifle->GetStat(MiniInventoryTags::AmmoInMagazine) != 30 ||
				Inventory->GetEntries().Num() != 2)
			{
				Fail(TEXT("client was allowed to mutate authoritative inventory"));
				return;
			}
			for (const FMiniInventoryEntry& Entry : Inventory->GetEntries())
			{
				if (Entry.Instance->GetItemDefinition() == UMiniPistolItemDefinition::StaticClass())
				{
					InitialPistolId = Entry.Instance->GetInstanceId();
				}
			}
			OwnerIndex = Probe->GetOwnerIndex();
			InitialRifleId = RifleId;
			InitialRifle = Rifle;
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask14Probe CLIENT_SNAPSHOT: Owner=%d Rifle=%s Pistol=%s Count=2 Private=1 Stats=1 AuthorityRejected=1"),
				OwnerIndex, *InitialRifleId.ToString(), *InitialPistolId.ToString());
			Probe->ServerChangeRifleStat();
			SetStage(EStage::WaitStatChanged);
		}
		break;
	case EStage::WaitStatChanged:
		if (CheckEntries(Inventory, true, InitialPistolId, FGuid(), RifleId, Rifle, 29) &&
			RifleId == InitialRifleId && Rifle == InitialRifle.Get())
		{
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask14Probe CLIENT_STATS_CHANGED: Owner=%d Rifle=%s Ammo=29 Pistol=%s Count=2 Stable=1"),
				OwnerIndex, *InitialRifleId.ToString(), *InitialPistolId.ToString());
			Probe->ServerRemoveRifle();
			SetStage(EStage::WaitRemoved);
		}
		break;
	case EStage::WaitRemoved:
		// FastArray removal and explicit remote subobject deletion can arrive in
		// separate bunches. Do not advance until both are visible to this client.
		if (CheckEntries(Inventory, false, InitialPistolId, InitialRifleId, RifleId, Rifle) &&
			!InitialRifle.IsValid())
		{
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask14Probe CLIENT_REMOVED: Owner=%d OldRifle=%s Pistol=%s Count=1 Stale=0 RemoteDestroyed=1"),
				OwnerIndex, *InitialRifleId.ToString(), *InitialPistolId.ToString());
			Probe->ServerAddRifle();
			SetStage(EStage::WaitReadded);
		}
		break;
	case EStage::WaitReadded:
		if (CheckEntries(Inventory, true, InitialPistolId, InitialRifleId, RifleId, Rifle) &&
			Rifle != InitialRifle.Get())
		{
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask14Probe CLIENT_READDED: Owner=%d OldRifle=%s NewRifle=%s Pistol=%s Count=2 Stale=0 Stats=1"),
				OwnerIndex, *InitialRifleId.ToString(), *RifleId.ToString(),
				*InitialPistolId.ToString());
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask14Probe PASS: Owner=%d Initial=2 Removed=1 Readded=2 Private=1 Stale=0"),
				OwnerIndex);
			SetStage(EStage::Done);
		}
		break;
	case EStage::Done:
		break;
	}
}
