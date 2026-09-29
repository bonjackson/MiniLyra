#include "MiniTask15ProbeSubsystem.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "AbilitySystem/MiniHealthSet.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHealthComponent.h"
#include "Character/MiniHeroComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Equipment/MiniEquipmentDefinition.h"
#include "Equipment/MiniEquipmentInstance.h"
#include "Equipment/MiniEquipmentManagerComponent.h"
#include "Equipment/MiniQuickBarComponent.h"
#include "GameModes/MiniGameMode.h"
#include "Inventory/MiniInventoryItemDefinition.h"
#include "Inventory/MiniInventoryItemInstance.h"
#include "Inventory/MiniInventoryManagerComponent.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MiniPlayerController.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniLogChannels.h"

namespace
{
UClass* DefinitionForSlot(int32 Slot)
{
	return Slot == 0 ? UMiniRifleEquipmentDefinition::StaticClass()
		: UMiniPistolEquipmentDefinition::StaticClass();
}

bool CheckWeaponMesh(const AMiniCharacter* Pawn, int32 Slot)
{
	const USkeletalMeshComponent* Mesh = Pawn ? Pawn->GetPracticeRifleMesh() : nullptr;
	const UMiniEquipmentDefinition* Definition = GetDefault<UMiniEquipmentDefinition>(DefinitionForSlot(Slot));
	return Mesh && Definition && Definition->GetWeaponMesh() && Mesh->IsVisible() &&
		Mesh->GetSkeletalMeshAsset() == Definition->GetWeaponMesh();
}

bool NoOldGrants(UMiniAbilitySystemComponent* ASC, UMiniEquipmentInstance* OldEquipment,
	const TArray<FGameplayAbilitySpecHandle>& OldHandles)
{
	if (!ASC || !OldEquipment || !OldEquipment->GetGrantedHandles().IsEmpty() ||
		OldEquipment->GetSourceItem())
	{
		return false;
	}
	for (const FGameplayAbilitySpecHandle& Handle : OldHandles)
	{
		if (ASC->FindAbilitySpecFromHandle(Handle))
		{
			return false;
		}
	}
	return true;
}
}

bool UMiniTask15ProbeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if !UE_BUILD_SHIPPING
	return Super::ShouldCreateSubsystem(Outer) &&
		FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask15"));
#else
	return false;
#endif
}

TStatId UMiniTask15ProbeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMiniTask15ProbeSubsystem, STATGROUP_Tickables);
}

void UMiniTask15ProbeSubsystem::Fail(const TCHAR* Reason)
{
	if (!bFailed)
	{
		bFailed = true;
		UE_LOG(LogMiniInit, Error, TEXT("MiniTask15Probe FAIL: Phase=%d Owner=%d Reason=%s"),
			static_cast<int32>(ServerPhase), ClientOwnerIndex, Reason);
	}
}

void UMiniTask15ProbeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bFailed || !GetWorld() || !GetWorld()->HasBegunPlay())
	{
		return;
	}
	if (GetWorld()->GetNetMode() == NM_ListenServer)
	{
		TickServer(DeltaTime);
	}
	else if (GetWorld()->GetNetMode() == NM_Client)
	{
		TickClient(DeltaTime);
	}
}

bool UMiniTask15ProbeSubsystem::CheckServerBar(int32 Owner, int32 ActiveSlot, bool bFreshItems) const
{
	const AMiniPlayerController* Controller = Controllers[Owner].Get();
	const UMiniQuickBarComponent* Bar = Controller ? Controller->GetQuickBar() : nullptr;
	const UMiniInventoryManagerComponent* Inventory = Controller ? Controller->GetInventoryManager() : nullptr;
	UMiniInventoryItemInstance* Rifle = Bar ? Bar->GetSlotItem(0) : nullptr;
	UMiniInventoryItemInstance* Pistol = Bar ? Bar->GetSlotItem(1) : nullptr;
	if (!Controller || !Controller->HasAuthority() || !Bar || !Inventory ||
		Inventory->GetEntries().Num() != 2 || Bar->GetActiveSlotIndex() != ActiveSlot ||
		!IsValid(Rifle) || !IsValid(Pistol) || Rifle == Pistol ||
		!Rifle->GetInstanceId().IsValid() || !Pistol->GetInstanceId().IsValid() ||
		Rifle->GetInstanceId() == Pistol->GetInstanceId() ||
		Bar->GetSlotItemId(0) != Rifle->GetInstanceId() ||
		Bar->GetSlotItemId(1) != Pistol->GetInstanceId() ||
		Rifle->GetItemDefinition() != UMiniRifleItemDefinition::StaticClass() ||
		Pistol->GetItemDefinition() != UMiniPistolItemDefinition::StaticClass() ||
		Rifle->GetStat(MiniInventoryTags::AmmoInMagazine) != 30 ||
		Rifle->GetStat(MiniInventoryTags::ReserveAmmo) != 90 ||
		Pistol->GetStat(MiniInventoryTags::AmmoInMagazine) != 12 ||
		Pistol->GetStat(MiniInventoryTags::ReserveAmmo) != 36)
	{
		return false;
	}
	bool bFoundRifle = false;
	bool bFoundPistol = false;
	for (const FMiniInventoryEntry& Entry : Inventory->GetEntries())
	{
		if (Entry.StackCount != 1)
		{
			return false;
		}
		bFoundRifle |= Entry.Instance == Rifle;
		bFoundPistol |= Entry.Instance == Pistol;
	}
	if (!bFoundRifle || !bFoundPistol)
	{
		return false;
	}
	if (InitialRifleIds[Owner].IsValid())
	{
		const bool bRifleChanged = Rifle->GetInstanceId() != InitialRifleIds[Owner];
		const bool bPistolChanged = Pistol->GetInstanceId() != InitialPistolIds[Owner];
		if (bRifleChanged != bFreshItems || bPistolChanged != bFreshItems)
		{
			return false;
		}
	}
	return true;
}

bool UMiniTask15ProbeSubsystem::CheckServerEquipment(int32 Owner, int32 Slot) const
{
	AMiniCharacter* Pawn = Cast<AMiniCharacter>(Controllers[Owner].IsValid()
		? Controllers[Owner]->GetPawn() : nullptr);
	UMiniEquipmentManagerComponent* Manager = Pawn ? Pawn->GetEquipmentManager() : nullptr;
	UMiniEquipmentInstance* Equipment = Manager ? Manager->GetCurrentEquipment() : nullptr;
	UMiniQuickBarComponent* Bar = Controllers[Owner].IsValid()
		? Controllers[Owner]->GetQuickBar() : nullptr;
	UMiniInventoryItemInstance* Item = Bar ? Bar->GetSlotItem(Slot) : nullptr;
	UMiniAbilitySystemComponent* ASC = AbilitySystems[Owner].Get();
	if (!Pawn || !Manager || !IsValid(Equipment) || !Item || !ASC ||
		ASC->GetAvatarActor() != Pawn || Manager->GetCurrentDefinitionClass() != DefinitionForSlot(Slot) ||
		Manager->GetCurrentItemId() != Item->GetInstanceId() ||
		Equipment->GetSourceItem() != Item || Equipment->GetSourceItemId() != Item->GetInstanceId() ||
		Equipment->GetGrantedHandles().GetAbilityCount() != 1 || !CheckWeaponMesh(Pawn, Slot))
	{
		return false;
	}
	for (const FGameplayAbilitySpecHandle& Handle : Equipment->GetGrantedHandles().AbilityHandles)
	{
		const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Handle);
		if (!Spec || Spec->SourceObject.Get() != Equipment)
		{
			return false;
		}
	}
	return true;
}

bool UMiniTask15ProbeSubsystem::TryStartServer()
{
	TArray<AMiniPlayerController*> Remote;
	for (TActorIterator<AMiniPlayerController> It(GetWorld()); It; ++It)
	{
		if (!It->IsLocalController())
		{
			Remote.Add(*It);
		}
	}
	if (Remote.Num() < 2)
	{
		return false;
	}
	if (Remote.Num() != 2)
	{
		Fail(TEXT("expected exactly two remote QuickBar owners"));
		return false;
	}
	for (int32 Index = 0; Index < 2; ++Index)
	{
		Controllers[Index] = Remote[Index];
		Pawns[Index] = Cast<AMiniCharacter>(Remote[Index]->GetPawn());
		AMiniPlayerState* State = Remote[Index]->GetPlayerState<AMiniPlayerState>();
		AbilitySystems[Index] = State ? State->GetMiniAbilitySystemComponent() : nullptr;
		if (!Pawns[Index].IsValid() || !AbilitySystems[Index].IsValid() ||
			!CheckServerBar(Index, 0, false) || !CheckServerEquipment(Index, 0))
		{
			return false;
		}
	}
	if (Pawns[0].Get() == Pawns[1].Get() ||
		AbilitySystems[0].Get() == AbilitySystems[1].Get())
	{
		Fail(TEXT("the two owners share a Pawn or ASC"));
		return false;
	}
	InitialEquipment.SetNum(2);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		InitialEquipment[Index] = Pawns[Index]->GetEquipmentManager()->GetCurrentEquipment();
		InitialAbilityHandles[Index] = InitialEquipment[Index]->GetGrantedHandles().AbilityHandles;
		if (InitialAbilityHandles[Index].Num() != 1)
		{
			Fail(TEXT("initial Rifle ability grant was missing"));
			return false;
		}
		UMiniQuickBarComponent* Bar = Controllers[Index]->GetQuickBar();
		InitialRifleIds[Index] = Bar->GetSlotItemId(0);
		InitialPistolIds[Index] = Bar->GetSlotItemId(1);
		FActorSpawnParameters Params;
		Params.Owner = Controllers[Index].Get();
		AMiniTask15ProbeActor* Probe = GetWorld()->SpawnActor<AMiniTask15ProbeActor>(
			AMiniTask15ProbeActor::StaticClass(), FTransform::Identity, Params);
		if (!Probe)
		{
			Fail(TEXT("could not spawn owner-only probe"));
			return false;
		}
		Probes[Index] = Probe;
		Probe->InitializeServer(Index + 1, Pawns[Index].Get(), Pawns[1 - Index].Get());
		UE_LOG(LogMiniInit, Display,
			TEXT("MiniTask15Probe SERVER_INITIAL: Owner=%d Rifle=%s Pistol=%s Ammo=30/90,12/36 Active=0 Source=1 Mesh=Rifle"),
			Index + 1, *InitialRifleIds[Index].ToString(), *InitialPistolIds[Index].ToString());
	}
	bServerStarted = true;
	StageSeconds = 0.0f;
	return true;
}

bool UMiniTask15ProbeSubsystem::AllAcknowledged() const
{
	for (const TWeakObjectPtr<AMiniTask15ProbeActor>& Probe : Probes)
	{
		if (!Probe.IsValid() || !Probe->HasAcknowledged(ServerPhase))
		{
			return false;
		}
	}
	return true;
}

void UMiniTask15ProbeSubsystem::AdvanceServer(EMiniTask15Phase NewPhase)
{
	for (const TWeakObjectPtr<AMiniTask15ProbeActor>& Probe : Probes)
	{
		if (Probe.IsValid())
		{
			Probe->SetServerPhase(NewPhase);
		}
	}
	ServerPhase = NewPhase;
	StageSeconds = 0.0f;
}

bool UMiniTask15ProbeSubsystem::SwitchBoth(int32 Slot)
{
	for (int32 Index = 0; Index < 2; ++Index)
	{
		AMiniCharacter* Pawn = Pawns[Index].Get();
		UMiniEquipmentManagerComponent* Manager = Pawn ? Pawn->GetEquipmentManager() : nullptr;
		UMiniEquipmentInstance* Old = Manager ? Manager->GetCurrentEquipment() : nullptr;
		UMiniAbilitySystemComponent* ASC = AbilitySystems[Index].Get();
		const TArray<FGameplayAbilitySpecHandle> OldHandles = Old
			? Old->GetGrantedHandles().AbilityHandles : TArray<FGameplayAbilitySpecHandle>();
		UMiniQuickBarComponent* Bar = Controllers[Index].IsValid()
			? Controllers[Index]->GetQuickBar() : nullptr;
		if (!Old || OldHandles.Num() != 1 || !Bar || !Bar->SelectSlot(Slot) ||
			Manager->GetCurrentEquipment() == Old || !NoOldGrants(ASC, Old, OldHandles) ||
			!CheckServerBar(Index, Slot, false) || !CheckServerEquipment(Index, Slot))
		{
			return false;
		}
		UE_LOG(LogMiniInit, Display,
			TEXT("MiniTask15Probe SERVER_SWITCH: Owner=%d Slot=%d Item=%s Revoked=1 Source=1 Mesh=%s Path=ServerSelect"),
			Index + 1, Slot, *Bar->GetSlotItemId(Slot).ToString(),
			Slot == 0 ? TEXT("Rifle") : TEXT("Pistol"));
	}
	return true;
}

bool UMiniTask15ProbeSubsystem::KillOwner(int32 Owner)
{
	AMiniCharacter* Pawn = Pawns[Owner].Get();
	UMiniEquipmentManagerComponent* Manager = Pawn ? Pawn->GetEquipmentManager() : nullptr;
	UMiniEquipmentInstance* Old = Manager ? Manager->GetCurrentEquipment() : nullptr;
	UMiniAbilitySystemComponent* ASC = AbilitySystems[Owner].Get();
	UMiniQuickBarComponent* Bar = Controllers[Owner].IsValid()
		? Controllers[Owner]->GetQuickBar() : nullptr;
	AMiniGameMode* GameMode = GetWorld()->GetAuthGameMode<AMiniGameMode>();
	AMiniPlayerController* Source = Controllers[1 - Owner].Get();
	const TArray<FGameplayAbilitySpecHandle> OldHandles = Old
		? Old->GetGrantedHandles().AbilityHandles : TArray<FGameplayAbilitySpecHandle>();
	if (!Pawn || !Manager || !Old || OldHandles.Num() != 1 || !ASC || !Bar ||
		!GameMode || !Source || !CheckServerEquipment(Owner, 0))
	{
		return false;
	}
	DeathTime[Owner] = GetWorld()->GetTimeSeconds();
	if (!GameMode->TryApplyTestDamage(Source, Pawn, 150.0f) ||
		!Pawn->GetHealthComponent() || !Pawn->GetHealthComponent()->IsDead() ||
		Manager->GetCurrentEquipment() || Manager->GetCurrentItemId().IsValid() ||
		Bar->GetActiveSlotIndex() != INDEX_NONE || Pawn->GetPracticeRifleMesh()->IsVisible() ||
		!NoOldGrants(ASC, Old, OldHandles) ||
		!CheckServerBar(Owner, INDEX_NONE, Owner == 0 && ServerPhase == EMiniTask15Phase::FirstRespawn))
	{
		return false;
	}
	UE_LOG(LogMiniInit, Display,
		TEXT("MiniTask15Probe SERVER_DEAD: Owner=%d Pawn=%s Active=-1 Equipment=0 Revoked=1 Mesh=None"),
		Owner + 1, *Pawn->GetPathName());
	return true;
}

bool UMiniTask15ProbeSubsystem::CheckServerRespawn(int32 Owner)
{
	AMiniCharacter* NewPawn = Controllers[Owner].IsValid()
		? Cast<AMiniCharacter>(Controllers[Owner]->GetPawn()) : nullptr;
	AMiniPlayerState* State = Controllers[Owner].IsValid()
		? Controllers[Owner]->GetPlayerState<AMiniPlayerState>() : nullptr;
	const UMiniHealthSet* Health = State ? State->GetHealthSet() : nullptr;
	UMiniAbilitySystemComponent* ASC = AbilitySystems[Owner].Get();
	if (!NewPawn || NewPawn == Pawns[Owner].Get() || !ASC || ASC->GetAvatarActor() != NewPawn ||
		!Health || !FMath::IsNearlyEqual(Health->GetHealth(), 100.0f, 0.01f) ||
		!NewPawn->GetHealthComponent() || NewPawn->GetHealthComponent()->IsDead() ||
		!CheckServerBar(Owner, 0, true) || !CheckServerEquipment(Owner, 0))
	{
		return false;
	}
	const float Delay = GetWorld()->GetTimeSeconds() - DeathTime[Owner];
	if (Delay < 2.75f || Delay > 15.0f)
	{
		return false;
	}
	UMiniQuickBarComponent* Bar = Controllers[Owner]->GetQuickBar();
	UE_LOG(LogMiniInit, Display,
		TEXT("MiniTask15Probe SERVER_RESPAWN: Owner=%d OldRifle=%s NewRifle=%s OldPistol=%s NewPistol=%s Delay=%.2f Source=1 Mesh=Rifle"),
		Owner + 1, *InitialRifleIds[Owner].ToString(), *Bar->GetSlotItemId(0).ToString(),
		*InitialPistolIds[Owner].ToString(), *Bar->GetSlotItemId(1).ToString(), Delay);
	Pawns[Owner] = NewPawn;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		Probes[Index]->SetServerPawns(Pawns[Index].Get(), Pawns[1 - Index].Get());
	}
	return true;
}

bool UMiniTask15ProbeSubsystem::CheckNoSamePawnRefill()
{
	AMiniPlayerController* Host = nullptr;
	for (TActorIterator<AMiniPlayerController> It(GetWorld()); It; ++It)
	{
		if (It->IsLocalController())
		{
			Host = *It;
			break;
		}
	}
	AMiniCharacter* Pawn = Host ? Cast<AMiniCharacter>(Host->GetPawn()) : nullptr;
	UMiniQuickBarComponent* Bar = Host ? Host->GetQuickBar() : nullptr;
	UMiniInventoryManagerComponent* Inventory = Host ? Host->GetInventoryManager() : nullptr;
	UMiniEquipmentManagerComponent* Equipment = Pawn ? Pawn->GetEquipmentManager() : nullptr;
	AMiniPlayerState* State = Host ? Host->GetPlayerState<AMiniPlayerState>() : nullptr;
	UMiniAbilitySystemComponent* ASC = State ? State->GetMiniAbilitySystemComponent() : nullptr;
	UMiniInventoryItemInstance* Rifle = Bar ? Bar->GetSlotItem(0) : nullptr;
	UMiniInventoryItemInstance* Pistol = Bar ? Bar->GetSlotItem(1) : nullptr;
	UMiniEquipmentInstance* OldEquipment = Equipment ? Equipment->GetCurrentEquipment() : nullptr;
	if (!Pawn || !Bar || !Inventory || !Equipment || !ASC || !Rifle || !Pistol ||
		!OldEquipment || Inventory->GetEntries().Num() != 2 || Bar->GetActiveSlotIndex() != 0 ||
		Equipment->GetCurrentItemId() != Rifle->GetInstanceId())
	{
		return false;
	}
	const FGuid RifleId = Rifle->GetInstanceId();
	const TArray<FGameplayAbilitySpecHandle> OldHandles = OldEquipment->GetGrantedHandles().AbilityHandles;
	if (!Inventory->RemoveItem(Pistol))
	{
		return false;
	}
	Pawn->NotifyInitDependenciesChanged();
	if (Inventory->GetEntries().Num() != 1 || Bar->GetSlotItemId(1).IsValid() ||
		Bar->GetSlotItem(1) || Bar->GetActiveSlotIndex() != 0 ||
		Bar->GetSlotItemId(0) != RifleId || Equipment->GetCurrentItemId() != RifleId ||
		Rifle->GetStat(MiniInventoryTags::AmmoInMagazine) != 30)
	{
		return false;
	}
	if (!Inventory->RemoveItem(Rifle))
	{
		return false;
	}
	Pawn->NotifyInitDependenciesChanged();
	if (!Inventory->GetEntries().IsEmpty() || Bar->GetSlotItemId(0).IsValid() ||
		Bar->GetActiveSlotIndex() != INDEX_NONE || Equipment->GetCurrentEquipment() ||
		!NoOldGrants(ASC, OldEquipment, OldHandles) || Pawn->GetPracticeRifleMesh()->IsVisible())
	{
		return false;
	}
	UE_LOG(LogMiniInit, Display,
		TEXT("MiniTask15Probe HOST_REMOVE_NO_REFILL: Items=0 Slots=0 Equipment=0 Revoked=1 NoRefill=1"));
	return true;
}

void UMiniTask15ProbeSubsystem::TickServer(float DeltaTime)
{
	if (!bServerStarted)
	{
		StageSeconds += DeltaTime;
		if (StageSeconds > 90.0f) { Fail(TEXT("server initial equipment timed out")); return; }
		TryStartServer();
		return;
	}
	if (ServerPhase == EMiniTask15Phase::Complete)
	{
		return;
	}
	StageSeconds += DeltaTime;
	if (StageSeconds > 30.0f)
	{
		Fail(TEXT("server phase timed out waiting for both client checkpoints or respawn"));
		return;
	}
	if (!AllAcknowledged())
	{
		return;
	}
	switch (ServerPhase)
	{
	case EMiniTask15Phase::Initial:
		AdvanceServer(EMiniTask15Phase::RequestQ);
		break;
	case EMiniTask15Phase::RequestQ:
		// Both owners acknowledged RequestQ after pressing Q. Only the
		// client input path may now send ServerSelectSlot; wait for both RPCs.
		for (int32 Index = 0; Index < 2; ++Index)
		{
			UMiniQuickBarComponent* Bar = Controllers[Index].IsValid()
				? Controllers[Index]->GetQuickBar() : nullptr;
			if (!Bar || Bar->GetActiveSlotIndex() != 1)
			{
				return;
			}
		}
		for (int32 Index = 0; Index < 2; ++Index)
		{
			AMiniCharacter* Pawn = Pawns[Index].Get();
			UMiniEquipmentManagerComponent* Manager = Pawn ? Pawn->GetEquipmentManager() : nullptr;
			UMiniEquipmentInstance* Old = InitialEquipment.IsValidIndex(Index)
				? InitialEquipment[Index].Get() : nullptr;
			if (!Manager || !Old || Manager->GetCurrentEquipment() == Old ||
				!NoOldGrants(AbilitySystems[Index].Get(), Old, InitialAbilityHandles[Index]) ||
				!CheckServerBar(Index, 1, false) || !CheckServerEquipment(Index, 1))
			{
				Fail(TEXT("Q/RPC Pistol switch did not revoke old grants or set current SourceObject"));
				return;
			}
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask15Probe SERVER_SWITCH: Owner=%d Slot=1 Item=%s Revoked=1 Source=1 Mesh=Pistol Path=ClientQ"),
				Index + 1, *Controllers[Index]->GetQuickBar()->GetSlotItemId(1).ToString());
		}
		InitialEquipment.Reset();
		AdvanceServer(EMiniTask15Phase::Pistol);
		break;
	case EMiniTask15Phase::Pistol:
		if (!SwitchBoth(0)) { Fail(TEXT("Rifle switch or old ability revocation failed")); return; }
		AdvanceServer(EMiniTask15Phase::RifleAgain);
		break;
	case EMiniTask15Phase::RifleAgain:
		if (!KillOwner(0)) { Fail(TEXT("first death did not immediately clear equipment and grants")); return; }
		AdvanceServer(EMiniTask15Phase::FirstDead);
		break;
	case EMiniTask15Phase::FirstDead:
		if (CheckServerRespawn(0)) { AdvanceServer(EMiniTask15Phase::FirstRespawn); }
		break;
	case EMiniTask15Phase::FirstRespawn:
		if (!KillOwner(1)) { Fail(TEXT("second death did not immediately clear equipment and grants")); return; }
		AdvanceServer(EMiniTask15Phase::SecondDead);
		break;
	case EMiniTask15Phase::SecondDead:
		if (CheckServerRespawn(1)) { AdvanceServer(EMiniTask15Phase::SecondRespawn); }
		break;
	case EMiniTask15Phase::SecondRespawn:
		if (!CheckNoSamePawnRefill()) { Fail(TEXT("same-Pawn item removal refilled slots or retained a grant")); return; }
		UE_LOG(LogMiniInit, Display,
			TEXT("MiniTask15Probe SERVER_PASS: Owners=2 Switches=4 Deaths=2 Respawns=2 Revoked=1 Source=1"));
		AdvanceServer(EMiniTask15Phase::Complete);
		break;
	case EMiniTask15Phase::Complete:
		break;
	}
}

bool UMiniTask15ProbeSubsystem::CheckClientBar(const AMiniPlayerController* Controller,
	int32 ActiveSlot, FGuid& OutRifleId, FGuid& OutPistolId) const
{
	OutRifleId.Invalidate();
	OutPistolId.Invalidate();
	const UMiniQuickBarComponent* Bar = Controller ? Controller->GetQuickBar() : nullptr;
	const UMiniInventoryManagerComponent* Inventory = Controller ? Controller->GetInventoryManager() : nullptr;
	UMiniInventoryItemInstance* Rifle = Bar ? Bar->GetSlotItem(0) : nullptr;
	UMiniInventoryItemInstance* Pistol = Bar ? Bar->GetSlotItem(1) : nullptr;
	if (!Controller || Controller->HasAuthority() || !Bar || !Inventory ||
		Inventory->GetEntries().Num() != 2 || Bar->GetActiveSlotIndex() != ActiveSlot ||
		!IsValid(Rifle) || !IsValid(Pistol) || Rifle == Pistol ||
		!Rifle->GetInstanceId().IsValid() || !Pistol->GetInstanceId().IsValid() ||
		Rifle->GetInstanceId() == Pistol->GetInstanceId() ||
		Bar->GetSlotItemId(0) != Rifle->GetInstanceId() ||
		Bar->GetSlotItemId(1) != Pistol->GetInstanceId() ||
		Rifle->GetItemDefinition() != UMiniRifleItemDefinition::StaticClass() ||
		Pistol->GetItemDefinition() != UMiniPistolItemDefinition::StaticClass() ||
		Rifle->GetStat(MiniInventoryTags::AmmoInMagazine) != 30 ||
		Rifle->GetStat(MiniInventoryTags::ReserveAmmo) != 90 ||
		Pistol->GetStat(MiniInventoryTags::AmmoInMagazine) != 12 ||
		Pistol->GetStat(MiniInventoryTags::ReserveAmmo) != 36)
	{
		return false;
	}
	bool bFoundRifle = false;
	bool bFoundPistol = false;
	for (const FMiniInventoryEntry& Entry : Inventory->GetEntries())
	{
		if (Entry.StackCount != 1)
		{
			return false;
		}
		bFoundRifle |= Entry.Instance == Rifle;
		bFoundPistol |= Entry.Instance == Pistol;
	}
	if (!bFoundRifle || !bFoundPistol)
	{
		return false;
	}
	OutRifleId = Rifle->GetInstanceId();
	OutPistolId = Pistol->GetInstanceId();
	return true;
}

bool UMiniTask15ProbeSubsystem::CheckClientAppearance(AMiniCharacter* Pawn, int32 Slot,
	FGuid ExpectedId, UMiniInventoryItemInstance* ExpectedPrivateItem) const
{
	const UMiniEquipmentManagerComponent* Manager = Pawn ? Pawn->GetEquipmentManager() : nullptr;
	UMiniEquipmentInstance* Equipment = Manager ? Manager->GetCurrentEquipment() : nullptr;
	const FGuid SourceId = Manager ? Manager->GetCurrentItemId() : FGuid();
	if (!IsValid(Pawn) || !Manager || !IsValid(Equipment) ||
		Manager->GetCurrentDefinitionClass() != DefinitionForSlot(Slot) ||
		!SourceId.IsValid() || Equipment->GetSourceItemId() != SourceId ||
		(ExpectedId.IsValid() && SourceId != ExpectedId) ||
		Equipment->GetSourceItem() != ExpectedPrivateItem || !CheckWeaponMesh(Pawn, Slot))
	{
		return false;
	}
	return true;
}

bool UMiniTask15ProbeSubsystem::CheckClientDeath(AMiniCharacter* Pawn) const
{
	const UMiniEquipmentManagerComponent* Manager = Pawn ? Pawn->GetEquipmentManager() : nullptr;
	const USkeletalMeshComponent* Mesh = Pawn ? Pawn->GetPracticeRifleMesh() : nullptr;
	return IsValid(Pawn) && Pawn->GetHealthComponent() && Pawn->GetHealthComponent()->IsDead() &&
		Manager && !Manager->GetCurrentEquipment() && !Manager->GetCurrentItemId().IsValid() &&
		Mesh && !Mesh->IsVisible();
}

void UMiniTask15ProbeSubsystem::TickClient(float DeltaTime)
{
	if (ClientCompletedPhase >= static_cast<int32>(EMiniTask15Phase::Complete))
	{
		return;
	}
	StageSeconds += DeltaTime;
	if (StageSeconds > (ClientCompletedPhase == INDEX_NONE ? 90.0f : 35.0f))
	{
		Fail(TEXT("client phase timed out waiting for replicated QuickBar, equipment, or Pawn"));
		return;
	}
	AMiniPlayerController* Controller = nullptr;
	int32 ControllerCount = 0;
	for (TActorIterator<AMiniPlayerController> It(GetWorld()); It; ++It)
	{
		++ControllerCount;
		if (It->IsLocalController()) { Controller = *It; }
	}
	AMiniTask15ProbeActor* Probe = nullptr;
	int32 ProbeCount = 0;
	for (TActorIterator<AMiniTask15ProbeActor> It(GetWorld()); It; ++It)
	{
		++ProbeCount;
		Probe = *It;
	}
	if (ControllerCount != 1 || ProbeCount != 1 || !Controller || !Probe ||
		Probe->GetOwner() != Controller || Probe->GetOwnerIndex() < 1 ||
		Probe->GetOwnerIndex() > 2)
	{
		return;
	}
	ClientOwnerIndex = Probe->GetOwnerIndex();
	const EMiniTask15Phase Phase = Probe->GetPhase();
	if (static_cast<int32>(Phase) <= ClientCompletedPhase)
	{
		return;
	}
	if (static_cast<int32>(Phase) != ClientCompletedPhase + 1)
	{
		Fail(TEXT("client skipped a required phase"));
		return;
	}
	AMiniCharacter* OwnerPawn = Probe->GetOwnerPawn();
	AMiniCharacter* PeerPawn = Probe->GetPeerPawn();
	if (!IsValid(OwnerPawn) || !IsValid(PeerPawn) || OwnerPawn == PeerPawn ||
		Controller->GetPawn() != OwnerPawn || !OwnerPawn->IsLocallyControlled() ||
		PeerPawn->IsLocallyControlled())
	{
		return;
	}
	int32 ExpectedActive = Phase == EMiniTask15Phase::Pistol ? 1 : 0;
	if ((Phase == EMiniTask15Phase::FirstDead && ClientOwnerIndex == 1) ||
		(Phase == EMiniTask15Phase::SecondDead && ClientOwnerIndex == 2))
	{
		ExpectedActive = INDEX_NONE;
	}
	FGuid RifleId;
	FGuid PistolId;
	if (!CheckClientBar(Controller, ExpectedActive, RifleId, PistolId))
	{
		return;
	}
	UMiniQuickBarComponent* Bar = Controller->GetQuickBar();
	bool bReady = false;
	switch (Phase)
	{
	case EMiniTask15Phase::Initial:
		if (CheckClientAppearance(OwnerPawn, 0, RifleId, Bar->GetSlotItem(0)) &&
			CheckClientAppearance(PeerPawn, 0, FGuid(), nullptr))
		{
			ClientRifleId = RifleId;
			ClientPistolId = PistolId;
			ClientOwnerPawn = OwnerPawn;
			ClientPeerPawn = PeerPawn;
			ClientOwnerPawnPath = OwnerPawn->GetPathName();
			ClientPeerPawnPath = PeerPawn->GetPathName();
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask15Probe CLIENT_INITIAL: Owner=%d Rifle=%s Pistol=%s Ammo=30/90,12/36 Active=0 Private=1 Own=Rifle Peer=Rifle"),
				ClientOwnerIndex, *RifleId.ToString(), *PistolId.ToString());
			bReady = true;
		}
		break;
	case EMiniTask15Phase::RequestQ:
		if (RifleId == ClientRifleId && PistolId == ClientPistolId &&
			OwnerPawn == ClientOwnerPawn.Get() && PeerPawn == ClientPeerPawn.Get() &&
			OwnerPawn->GetHeroComponent() && OwnerPawn->GetHeroComponent()->IsInputActive() &&
			CheckClientAppearance(OwnerPawn, 0, RifleId, Bar->GetSlotItem(0)))
		{
			Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Q, IE_Pressed, 1.0f));
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask15Probe CLIENT_Q_PRESSED: Owner=%d From=Rifle To=Pistol Input=Q"),
				ClientOwnerIndex);
			bReady = true;
		}
		break;
	case EMiniTask15Phase::Pistol:
	case EMiniTask15Phase::RifleAgain:
		{
			const int32 Slot = Phase == EMiniTask15Phase::Pistol ? 1 : 0;
			if (RifleId == ClientRifleId && PistolId == ClientPistolId &&
				OwnerPawn == ClientOwnerPawn.Get() && PeerPawn == ClientPeerPawn.Get() &&
				CheckClientAppearance(OwnerPawn, Slot, Slot == 0 ? RifleId : PistolId,
					Bar->GetSlotItem(Slot)) &&
				CheckClientAppearance(PeerPawn, Slot, FGuid(), nullptr))
			{
				if (Slot == 1)
				{
					Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Q, IE_Released, 0.0f));
					UE_LOG(LogMiniInit, Display,
						TEXT("MiniTask15Probe CLIENT_Q_RELEASED: Owner=%d Active=1 Input=Q"), ClientOwnerIndex);
				}
				UE_LOG(LogMiniInit, Display,
					TEXT("MiniTask15Probe CLIENT_SWITCH: Owner=%d Slot=%d Rifle=%s Pistol=%s Own=%s Peer=%s Path=%s"),
					ClientOwnerIndex, Slot, *RifleId.ToString(), *PistolId.ToString(),
					Slot == 0 ? TEXT("Rifle") : TEXT("Pistol"),
					Slot == 0 ? TEXT("Rifle") : TEXT("Pistol"),
					Slot == 0 ? TEXT("ServerSelect") : TEXT("ClientQ"));
				bReady = true;
			}
		}
		break;
	case EMiniTask15Phase::FirstDead:
	case EMiniTask15Phase::SecondDead:
		{
			const int32 Victim = Phase == EMiniTask15Phase::FirstDead ? 1 : 2;
			const bool bOwnDeath = ClientOwnerIndex == Victim;
			AMiniCharacter* DeadPawn = bOwnDeath ? OwnerPawn : PeerPawn;
			AMiniCharacter* LivingPawn = bOwnDeath ? PeerPawn : OwnerPawn;
			if (RifleId == ClientRifleId && PistolId == ClientPistolId &&
				OwnerPawn == ClientOwnerPawn.Get() && PeerPawn == ClientPeerPawn.Get() &&
				CheckClientDeath(DeadPawn) &&
				CheckClientAppearance(LivingPawn, 0, bOwnDeath ? FGuid() : RifleId,
					bOwnDeath ? nullptr : Bar->GetSlotItem(0)))
			{
				UE_LOG(LogMiniInit, Display,
					TEXT("MiniTask15Probe CLIENT_DEAD: Owner=%d Victim=%d Rifle=%s Pistol=%s Own=%s Peer=%s Equipment=0 Mesh=None"),
					ClientOwnerIndex, Victim, *RifleId.ToString(), *PistolId.ToString(),
					bOwnDeath ? TEXT("None") : TEXT("Rifle"),
					bOwnDeath ? TEXT("Rifle") : TEXT("None"));
				bReady = true;
			}
		}
		break;
	case EMiniTask15Phase::FirstRespawn:
	case EMiniTask15Phase::SecondRespawn:
		{
			const int32 Victim = Phase == EMiniTask15Phase::FirstRespawn ? 1 : 2;
			const bool bOwnRespawn = ClientOwnerIndex == Victim;
			const bool bFreshRifle = RifleId != ClientRifleId;
			const bool bFreshPistol = PistolId != ClientPistolId;
			AMiniCharacter* ReplacedPawn = bOwnRespawn ? OwnerPawn : PeerPawn;
			const FString& OldPath = bOwnRespawn ? ClientOwnerPawnPath : ClientPeerPawnPath;
			if (bFreshRifle == bOwnRespawn && bFreshPistol == bOwnRespawn &&
				ReplacedPawn->GetPathName() != OldPath &&
				(bOwnRespawn ? OwnerPawn != ClientOwnerPawn.Get() : PeerPawn != ClientPeerPawn.Get()) &&
				(bOwnRespawn ? PeerPawn == ClientPeerPawn.Get() : OwnerPawn == ClientOwnerPawn.Get()) &&
				CheckClientAppearance(OwnerPawn, 0, RifleId, Bar->GetSlotItem(0)) &&
				CheckClientAppearance(PeerPawn, 0, FGuid(), nullptr))
			{
				UE_LOG(LogMiniInit, Display,
					TEXT("MiniTask15Probe CLIENT_RESPAWN: Owner=%d Victim=%d Rifle=%s Pistol=%s Fresh=%d Own=Rifle Peer=Rifle"),
					ClientOwnerIndex, Victim, *RifleId.ToString(), *PistolId.ToString(), bOwnRespawn ? 1 : 0);
				ClientRifleId = RifleId;
				ClientPistolId = PistolId;
				ClientOwnerPawn = OwnerPawn;
				ClientPeerPawn = PeerPawn;
				ClientOwnerPawnPath = OwnerPawn->GetPathName();
				ClientPeerPawnPath = PeerPawn->GetPathName();
				bReady = true;
			}
		}
		break;
	case EMiniTask15Phase::Complete:
		if (RifleId == ClientRifleId && PistolId == ClientPistolId &&
			OwnerPawn == ClientOwnerPawn.Get() && PeerPawn == ClientPeerPawn.Get() &&
			CheckClientAppearance(OwnerPawn, 0, RifleId, Bar->GetSlotItem(0)) &&
			CheckClientAppearance(PeerPawn, 0, FGuid(), nullptr))
		{
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask15Probe PASS: Owner=%d Private=1 Switches=2 Deaths=2 Respawns=2 Own=Rifle Peer=Rifle"),
				ClientOwnerIndex);
			bReady = true;
		}
		break;
	}
	if (bReady)
	{
		ClientCompletedPhase = static_cast<int32>(Phase);
		StageSeconds = 0.0f;
		if (Phase != EMiniTask15Phase::Complete)
		{
			Probe->ServerAcknowledge(Phase);
		}
	}
}
