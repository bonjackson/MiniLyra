#include "MiniTask17ProbeSubsystem.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "AbilitySystem/MiniGameplayAbility_RangedFire.h"
#include "AbilitySystem/MiniGameplayAbility_Reload.h"
#include "AbilitySystem/MiniHealthSet.h"
#include "Camera/MiniCameraComponent.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHealthComponent.h"
#include "Character/MiniHeroComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Equipment/MiniEquipmentDefinition.h"
#include "Equipment/MiniEquipmentInstance.h"
#include "Equipment/MiniEquipmentManagerComponent.h"
#include "Equipment/MiniQuickBarComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameModes/MiniGameMode.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Inventory/MiniInventoryItemDefinition.h"
#include "Inventory/MiniInventoryItemInstance.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MiniPlayerController.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniGameplayTags.h"
#include "System/MiniLogChannels.h"
#include "Weapons/MiniRangedWeaponComponent.h"

namespace
{
const FVector ShooterPosition(0.0, 0.0, 3000.0);
const FVector TargetPosition(800.0, 0.0, 3000.0);

void SendKey(AMiniPlayerController* Controller, FKey Key, EInputEvent Event)
{
	Controller->InputKey(FInputKeyEventArgs::CreateSimulated(
		Key, Event, Event == IE_Released ? 0.0f : 1.0f));
}

UMiniInventoryItemInstance* GetItem(const AMiniPlayerController* Controller, int32 Slot)
{
	const UMiniQuickBarComponent* Bar = Controller ? Controller->GetQuickBar() : nullptr;
	return Bar ? Bar->GetSlotItem(Slot) : nullptr;
}

UMiniAbilitySystemComponent* GetASC(const AMiniCharacter* Pawn)
{
	const AMiniPlayerState* State = Pawn ? Pawn->GetPlayerState<AMiniPlayerState>() : nullptr;
	return State ? State->GetMiniAbilitySystemComponent() : nullptr;
}

const UMiniHealthSet* GetHealth(const AMiniCharacter* Pawn)
{
	const AMiniPlayerState* State = Pawn ? Pawn->GetPlayerState<AMiniPlayerState>() : nullptr;
	return State ? State->GetHealthSet() : nullptr;
}

bool HasAmmo(const UMiniInventoryItemInstance* Item, int32 Magazine, int32 Reserve)
{
	return Item && Item->GetStat(MiniInventoryTags::AmmoInMagazine) == Magazine &&
		Item->GetStat(MiniInventoryTags::ReserveAmmo) == Reserve;
}

bool HasWeapon(const AMiniCharacter* Pawn, UClass* Definition, int32 Slot)
{
	const UMiniEquipmentManagerComponent* Manager = Pawn ? Pawn->GetEquipmentManager() : nullptr;
	const UMiniEquipmentInstance* Equipment = Manager ? Manager->GetCurrentEquipment() : nullptr;
	const AMiniPlayerController* Controller = Pawn ? Cast<AMiniPlayerController>(Pawn->GetController()) : nullptr;
	return Equipment && Equipment->GetEquipmentDefinition() == Definition &&
		Equipment->GetSourceItemId() == (GetItem(Controller, Slot) ? GetItem(Controller, Slot)->GetInstanceId() : FGuid()) &&
		(!Pawn->HasAuthority() || Equipment->GetGrantedHandles().GetAbilityCount() == 2);
}

int32 GetReadyWeaponSpecCount(const AMiniCharacter* Pawn)
{
	const UMiniEquipmentManagerComponent* Manager = Pawn ? Pawn->GetEquipmentManager() : nullptr;
	const UMiniEquipmentInstance* Equipment = Manager ? Manager->GetCurrentEquipment() : nullptr;
	const UMiniAbilitySystemComponent* ASC = GetASC(Pawn);
	if (!Equipment || !ASC || ASC->GetAvatarActor() != Pawn || !Equipment->GetSourceItem())
	{
		return 0;
	}
	bool bFire = false;
	bool bReload = false;
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (!Spec.Ability || Spec.SourceObject.Get() != Equipment)
		{
			continue;
		}
		bFire |= Spec.Ability->IsA<UMiniGameplayAbility_RangedFire>() &&
			Spec.GetDynamicSpecSourceTags().HasTagExact(MiniGameplayTags::InputTag_Fire);
		bReload |= Spec.Ability->IsA<UMiniGameplayAbility_Reload>() &&
			Spec.GetDynamicSpecSourceTags().HasTagExact(MiniGameplayTags::InputTag_Reload);
	}
	return static_cast<int32>(bFire) + static_cast<int32>(bReload);
}
}

bool UMiniTask17ProbeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if !UE_BUILD_SHIPPING
	return Super::ShouldCreateSubsystem(Outer) &&
		FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask17"));
#else
	return false;
#endif
}

TStatId UMiniTask17ProbeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMiniTask17ProbeSubsystem, STATGROUP_Tickables);
}

void UMiniTask17ProbeSubsystem::Fail(const TCHAR* Reason)
{
	if (!bFailed)
	{
		bFailed = true;
		const EMiniTask17Phase Phase = GetWorld() && GetWorld()->GetNetMode() == NM_Client
			? ClientPhase : ServerPhase;
		UE_LOG(LogMiniInit, Error, TEXT("MiniTask17Probe FAIL: Phase=%d Reason=%s"),
			static_cast<int32>(Phase), Reason);
	}
}

void UMiniTask17ProbeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bFailed || !GetWorld() || !GetWorld()->HasBegunPlay())
	{
		return;
	}
	if (GetWorld()->GetNetMode() == NM_ListenServer && ServerPhase != EMiniTask17Phase::Complete)
	{
		TickServer(DeltaTime);
	}
	else if (GetWorld()->GetNetMode() == NM_Client)
	{
		TickClient(DeltaTime);
	}
}

bool UMiniTask17ProbeSubsystem::StartServer()
{
	TArray<AMiniPlayerController*> Remote;
	for (TActorIterator<AMiniPlayerController> It(GetWorld()); It; ++It)
	{
		if (!It->IsLocalController())
		{
			Remote.Add(*It);
		}
	}
	if (Remote.Num() != 2)
	{
		return false;
	}
	for (int32 Index = 0; Index < 2; ++Index)
	{
		Controllers[Index] = Remote[Index];
		Pawns[Index] = Cast<AMiniCharacter>(Remote[Index]->GetPawn());
		if (!Pawns[Index].IsValid() || !GetASC(Pawns[Index].Get()) ||
			GetASC(Pawns[Index].Get())->GetAvatarActor() != Pawns[Index].Get() ||
			!HasWeapon(Pawns[Index].Get(), UMiniRifleEquipmentDefinition::StaticClass(), 0) ||
			!CheckServerAmmo(Index, 30, 90, 12, 36))
		{
			return false;
		}
	}
	if (Pawns[0].Get() == Pawns[1].Get())
	{
		Fail(TEXT("the two clients share a Pawn"));
		return false;
	}
	RifleItem = GetItem(Controllers[0].Get(), 0);
	PistolItem = GetItem(Controllers[0].Get(), 1);
	RifleItemId = RifleItem->GetInstanceId();
	RifleReloadDuration = GetDefault<UMiniRifleEquipmentDefinition>()->GetReloadDuration();
	PistolReloadDuration = GetDefault<UMiniPistolEquipmentDefinition>()->GetReloadDuration();
	PistolDamage = GetDefault<UMiniPistolEquipmentDefinition>()->GetFireDamage();
	if (RifleReloadDuration < 0.2f || PistolReloadDuration < 0.2f || PistolDamage <= 0.0f)
	{
		Fail(TEXT("weapon data is missing reload duration or pistol damage"));
		return false;
	}
	for (int32 Index = 0; Index < 2; ++Index)
	{
		AMiniCharacter* Pawn = Pawns[Index].Get();
		// Preserve the network movement path that uploads the owner's view,
		// while keeping the two headless test Pawns fixed in the air.
		UCharacterMovementComponent* Movement = Pawn->GetCharacterMovement();
		Movement->StopMovementImmediately();
		Movement->SetMovementMode(MOVE_Flying);
		Pawn->SetActorLocation(Index == 0 ? ShooterPosition : TargetPosition,
			false, nullptr, ETeleportType::TeleportPhysics);
		Pawn->ForceNetUpdate();
		FActorSpawnParameters Params;
		Params.Owner = Controllers[Index].Get();
		AMiniTask17ProbeActor* Probe = GetWorld()->SpawnActor<AMiniTask17ProbeActor>(
			AMiniTask17ProbeActor::StaticClass(), FTransform::Identity, Params);
		if (!Probe)
		{
			Fail(TEXT("could not create owner-only checkpoint actor"));
			return false;
		}
		Probes[Index] = Probe;
		Probe->InitializeServer(Index + 1, Pawn, Pawns[1 - Index].Get());
	}
	bServerStarted = true;
	StageSeconds = 0.0f;
	UE_LOG(LogMiniInit, Display,
		TEXT("MiniTask17Probe SERVER_READY: Owners=2 Rifle=30/90 Pistol=12/36 Reload=%.2f/%.2f Damage=%.1f"),
		RifleReloadDuration, PistolReloadDuration, PistolDamage);
	return true;
}

bool UMiniTask17ProbeSubsystem::AllAcknowledged() const
{
	for (const TWeakObjectPtr<AMiniTask17ProbeActor>& Probe : Probes)
	{
		if (!Probe.IsValid() || !Probe->HasAcknowledged(ServerPhase))
		{
			return false;
		}
	}
	return true;
}

void UMiniTask17ProbeSubsystem::Advance(EMiniTask17Phase NewPhase)
{
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask17Probe SERVER_PHASE: %d -> %d"),
		static_cast<int32>(ServerPhase), static_cast<int32>(NewPhase));
	for (const TWeakObjectPtr<AMiniTask17ProbeActor>& Probe : Probes)
	{
		if (Probe.IsValid())
		{
			Probe->SetServerPhase(NewPhase);
		}
	}
	ServerPhase = NewPhase;
	StageSeconds = 0.0f;
}

bool UMiniTask17ProbeSubsystem::CheckServerAmmo(int32 Owner, int32 RifleMag,
	int32 RifleReserve, int32 PistolMag, int32 PistolReserve) const
{
	return Owner >= 0 && Owner < 2 && Controllers[Owner].IsValid() &&
		HasAmmo(GetItem(Controllers[Owner].Get(), 0), RifleMag, RifleReserve) &&
		HasAmmo(GetItem(Controllers[Owner].Get(), 1), PistolMag, PistolReserve);
}

void UMiniTask17ProbeSubsystem::TickServer(float DeltaTime)
{
	StageSeconds += DeltaTime;
	if (!bServerStarted)
	{
		if (StageSeconds > 90.0f)
		{
			Fail(TEXT("two gameplay-ready clients never appeared"));
		}
		else
		{
			StartServer();
		}
		return;
	}
	if (StageSeconds > 30.0f)
	{
		if (ServerPhase == EMiniTask17Phase::RifleShot)
		{
			const AMiniCharacter* Source = Pawns[0].Get();
			const UMiniRangedWeaponComponent* TimedOutWeapon = Source ? Source->GetRangedWeaponComponent() : nullptr;
			UE_LOG(LogMiniInit, Error,
				TEXT("MiniTask17Probe SERVER_FIRE_TIMEOUT: Shots=%u Sequence=%u Reason=%d ClientAck=%d"),
				TimedOutWeapon ? TimedOutWeapon->GetAcceptedShotCount() : 0,
				TimedOutWeapon ? TimedOutWeapon->GetLastProcessedSequence() : 0,
				TimedOutWeapon ? static_cast<int32>(TimedOutWeapon->GetLastFireRejectionReason()) : -1,
				Probes[0].IsValid() && Probes[0]->HasAcknowledged(ServerPhase) ? 1 : 0);
		}
		Fail(TEXT("server phase timed out"));
		return;
	}
	AMiniCharacter* Shooter = Pawns[0].Get();
	AMiniCharacter* Target = Pawns[1].Get();
	UMiniRangedWeaponComponent* Weapon = Shooter ? Shooter->GetRangedWeaponComponent() : nullptr;
	UMiniAbilitySystemComponent* ShooterASC = GetASC(Shooter);
	UMiniAbilitySystemComponent* TargetASC = GetASC(Target);
	const UMiniHealthSet* TargetHealth = GetHealth(Target);
	if (!Shooter || !Target || !Weapon || !ShooterASC || !TargetASC || !TargetHealth)
	{
		Fail(TEXT("server lost a player or combat dependency"));
		return;
	}
	const bool bReloading = ShooterASC->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading);
	switch (ServerPhase)
	{
	case EMiniTask17Phase::Initial:
		if (AllAcknowledged())
		{
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask17Probe SERVER_INITIAL: PrivateAmmo=1 TwoOwners=1 Grants=2"));
			Advance(EMiniTask17Phase::RifleShot);
		}
		return;
	case EMiniTask17Phase::RifleShot:
		if (!AllAcknowledged())
		{
			return;
		}
		if (Weapon->GetAcceptedShotCount() != 1 || Weapon->GetLastHitCharacter() != Target ||
			!CheckServerAmmo(0, 29, 90, 12, 36) ||
			!CheckServerAmmo(1, 30, 90, 12, 36) ||
			!FMath::IsNearlyEqual(TargetHealth->GetHealth(), 75.0f, 0.01f))
		{
			Fail(TEXT("client rifle shot did not consume one round and damage only the target"));
			return;
		}
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask17Probe SERVER_RIFLE_SHOT: Shots=1 Rifle=29/90 Pistol=12/36 Target=75"));
		Advance(EMiniTask17Phase::FullReload);
		return;
	case EMiniTask17Phase::FullReload:
		if (TargetASC->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading))
		{
			Fail(TEXT("full-magazine request entered reload state"));
			return;
		}
		if (!AllAcknowledged() || StageSeconds < RifleReloadDuration + 0.25f)
		{
			return;
		}
		if (TargetASC->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading) ||
			!CheckServerAmmo(1, 30, 90, 12, 36) ||
			!CheckServerAmmo(0, 29, 90, 12, 36))
		{
			Fail(TEXT("full-magazine reload wasted reserve or entered reload state"));
			return;
		}
		if (!RifleItem.IsValid() || !RifleItem->SetStat(MiniInventoryTags::AmmoInMagazine, 0))
		{
			Fail(TEXT("could not prepare empty rifle"));
			return;
		}
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask17Probe SERVER_FULL_RELOAD: Rifle=30/90 ReserveWasted=0"));
		Advance(EMiniTask17Phase::EmptyFire);
		return;
	case EMiniTask17Phase::EmptyFire:
		if (!AllAcknowledged())
		{
			return;
		}
		if (Weapon->GetEmptyMagazineRejectionCount() != 1 ||
			Weapon->GetLastFireRejectionReason() != EMiniFireRejectionReason::EmptyMagazine ||
			Weapon->GetAcceptedShotCount() != 1 ||
			!CheckServerAmmo(0, 0, 90, 12, 36) ||
			!FMath::IsNearlyEqual(TargetHealth->GetHealth(), 75.0f, 0.01f))
		{
			Fail(TEXT("empty rifle request caused damage/ammo change or did not notify owner"));
			return;
		}
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask17Probe SERVER_EMPTY: Rejected=1 Shots=1 Target=75"));
		Advance(EMiniTask17Phase::RifleReload);
		return;
	case EMiniTask17Phase::RifleReload:
	{
		bSawRifleReload |= bReloading;
		if (bReloading && !bServerTestedFireBlock)
		{
			const uint32 Sequence = Weapon->GetLastProcessedSequence() + 100;
			if (!CheckServerAmmo(0, 0, 90, 12, 36) ||
				Weapon->TryFireOnServer(Weapon->GetLastAcceptedCameraOrigin(),
					Weapon->GetLastAcceptedAimDirection(), Sequence, RifleItemId) ||
				Weapon->GetLastFireRejectionReason() != EMiniFireRejectionReason::UnusableWeapon ||
				Weapon->GetAcceptedShotCount() != 1 ||
				!CheckServerAmmo(0, 0, 90, 12, 36) ||
				!FMath::IsNearlyEqual(TargetHealth->GetHealth(), 75.0f, 0.01f))
			{
				Fail(TEXT("server accepted a shot while reloading"));
				return;
			}
			bServerTestedFireBlock = true;
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask17Probe SERVER_RELOAD_FIRE_BLOCK: Rejected=1 Ammo=0/90 Target=75"));
		}
		if (!AllAcknowledged() || bReloading)
		{
			return;
		}
		if (!bSawRifleReload || !bServerTestedFireBlock ||
			Weapon->GetAcceptedShotCount() != 1 ||
			!FMath::IsNearlyEqual(TargetHealth->GetHealth(), 75.0f, 0.01f) ||
			!CheckServerAmmo(0, 30, 60, 12, 36) ||
			!CheckServerAmmo(1, 30, 90, 12, 36))
		{
			Fail(TEXT("rifle reload did not transfer precisely 30 reserve rounds"));
			return;
		}
		UMiniInventoryItemInstance* TargetRifle = GetItem(Controllers[1].Get(), 0);
		if (!TargetRifle || !TargetRifle->SetStat(MiniInventoryTags::AmmoInMagazine, 25) ||
			!TargetRifle->SetStat(MiniInventoryTags::ReserveAmmo, 3))
		{
			Fail(TEXT("could not prepare partial-reserve rifle reload"));
			return;
		}
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask17Probe SERVER_RIFLE_RELOAD: Rifle=30/60 OwnerAgreement=1"));
		Advance(EMiniTask17Phase::PartialReload);
		return;
	}
	case EMiniTask17Phase::PartialReload:
		bSawPartialReload |= TargetASC->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading);
		if (!AllAcknowledged() || TargetASC->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading))
		{
			return;
		}
		if (!bSawPartialReload || !CheckServerAmmo(1, 28, 0, 12, 36) ||
			!CheckServerAmmo(0, 30, 60, 12, 36))
		{
			Fail(TEXT("partial reserve reload did not transfer only available rounds"));
			return;
		}
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask17Probe SERVER_PARTIAL_RELOAD: Rifle=28/0 Transfer=3"));
		Advance(EMiniTask17Phase::NoReserveReload);
		return;
	case EMiniTask17Phase::NoReserveReload:
		if (TargetASC->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading))
		{
			Fail(TEXT("zero-reserve request entered reload state"));
			return;
		}
		if (!AllAcknowledged() || StageSeconds < RifleReloadDuration + 0.25f)
		{
			return;
		}
		if (TargetASC->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading) ||
			!CheckServerAmmo(1, 28, 0, 12, 36))
		{
			Fail(TEXT("zero-reserve reload changed ammo or entered reload state"));
			return;
		}
		if (!RifleItem->SetStat(MiniInventoryTags::AmmoInMagazine, 20))
		{
			Fail(TEXT("could not prepare rifle switch cancellation"));
			return;
		}
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask17Probe SERVER_NO_RESERVE: Rifle=28/0 NoTransfer=1"));
		Advance(EMiniTask17Phase::RifleReloadCancelStart);
		return;
	case EMiniTask17Phase::RifleReloadCancelStart:
		if (AllAcknowledged() && bReloading && CheckServerAmmo(0, 20, 60, 12, 36))
		{
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask17Probe SERVER_RIFLE_CANCEL_START: Reloading=1 Ammo=20/60"));
			Advance(EMiniTask17Phase::RifleReloadCancelSwitch);
		}
		return;
	case EMiniTask17Phase::RifleReloadCancelSwitch:
		if (!AllAcknowledged() || StageSeconds < RifleReloadDuration + 0.25f)
		{
			return;
		}
		if (bReloading || !HasWeapon(Shooter, UMiniPistolEquipmentDefinition::StaticClass(), 1) ||
			!CheckServerAmmo(0, 20, 60, 12, 36) ||
			!CheckServerAmmo(1, 28, 0, 12, 36))
		{
			Fail(TEXT("switch did not cancel rifle reload or changed the unequipped item"));
			return;
		}
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask17Probe SERVER_SWITCH_CANCEL: Rifle=20/60 Pistol=12/36 Reloading=0"));
		Advance(EMiniTask17Phase::PistolShot);
		return;
	case EMiniTask17Phase::PistolShot:
		if (!AllAcknowledged() || StageSeconds < 0.5f)
		{
			return;
		}
		if (Weapon->GetAcceptedShotCount() != 2 || Weapon->GetLastHitCharacter() != Target ||
			!CheckServerAmmo(0, 20, 60, 11, 36) ||
			!FMath::IsNearlyEqual(TargetHealth->GetHealth(), 75.0f - PistolDamage, 0.01f))
		{
			Fail(TEXT("held pistol input did not produce exactly one configured shot"));
			return;
		}
		UE_LOG(LogMiniInit, Display,
			TEXT("MiniTask17Probe SERVER_PISTOL_SHOT: Shots=2 Pistol=11/36 Rifle=20/60 Damage=%.1f SemiAuto=1"),
			PistolDamage);
		{
			const uint32 SequenceBefore = Weapon->GetLastProcessedSequence();
			if (Weapon->TryFireOnServer(Weapon->GetLastAcceptedCameraOrigin(),
				Weapon->GetLastAcceptedAimDirection(), SequenceBefore + 100, RifleItemId) ||
				Weapon->GetLastFireRejectionReason() != EMiniFireRejectionReason::WrongEquipment ||
				Weapon->GetLastProcessedSequence() != SequenceBefore ||
				Weapon->GetAcceptedShotCount() != 2 ||
				!CheckServerAmmo(0, 20, 60, 11, 36) ||
				!FMath::IsNearlyEqual(TargetHealth->GetHealth(), 75.0f - PistolDamage, 0.01f))
			{
				Fail(TEXT("stale rifle item ID changed the active pistol or its sequence"));
				return;
			}
		}
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask17Probe SERVER_STALE_ITEM: WrongEquipment=1 SequenceUnchanged=1"));
		Advance(EMiniTask17Phase::PistolReload);
		return;
	case EMiniTask17Phase::PistolReload:
		bSawPistolReload |= bReloading;
		if (!AllAcknowledged() || bReloading)
		{
			return;
		}
		if (!bSawPistolReload || !CheckServerAmmo(0, 20, 60, 12, 35))
		{
			Fail(TEXT("pistol reload did not consume precisely one reserve round"));
			return;
		}
		if (!PistolItem.IsValid() || !PistolItem->SetStat(MiniInventoryTags::AmmoInMagazine, 5))
		{
			Fail(TEXT("could not prepare pistol death cancellation"));
			return;
		}
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask17Probe SERVER_PISTOL_RELOAD: Pistol=12/35 Rifle=20/60"));
		Advance(EMiniTask17Phase::PistolReloadCancelStart);
		return;
	case EMiniTask17Phase::PistolReloadCancelStart:
		if (!AllAcknowledged() || !bReloading)
		{
			return;
		}
		if (!CheckServerAmmo(0, 20, 60, 5, 35))
		{
			Fail(TEXT("pistol ammo changed before interrupted reload completed"));
			return;
		}
		if (AMiniGameMode* GameMode = GetWorld()->GetAuthGameMode<AMiniGameMode>())
		{
			if (!GameMode->TryApplyTestDamage(Controllers[1].Get(), Shooter, 150.0f))
			{
				Fail(TEXT("could not kill reloading pistol owner"));
				return;
			}
		}
		else
		{
			Fail(TEXT("missing authoritative game mode"));
			return;
		}
		if (!Shooter->GetHealthComponent()->IsDead() ||
			ShooterASC->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading) ||
			Shooter->GetEquipmentManager()->GetCurrentEquipment())
		{
			Fail(TEXT("death did not cancel active reload and clear equipment"));
			return;
		}
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask17Probe SERVER_DEATH_CANCEL_START: Reloading=0 Equipment=0 Pistol=5/35"));
		Advance(EMiniTask17Phase::Death);
		return;
	case EMiniTask17Phase::Death:
		if (!AllAcknowledged() || StageSeconds < PistolReloadDuration + 0.25f)
		{
			return;
		}
		if (!RifleItem.IsValid() || !PistolItem.IsValid() ||
			!HasAmmo(RifleItem.Get(), 20, 60) || !HasAmmo(PistolItem.Get(), 5, 35) ||
			!Shooter->GetHealthComponent()->IsDead() ||
			ShooterASC->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading) ||
			Shooter->GetEquipmentManager()->GetCurrentEquipment())
		{
			Fail(TEXT("cancelled death reload later filled ammo or restored weapon"));
			return;
		}
		UE_LOG(LogMiniInit, Display,
			TEXT("MiniTask17Probe SERVER_PASS: OwnerAmmo=1 FullNoWaste=1 EmptyFeedback=1 RifleReload=1 FireBlocked=1 PartialReserve=1 NoReserve=1 SwitchCancel=1 PistolSemiAuto=1 PistolReload=1 DeathCancel=1 IndependentAmmo=1 StaleItem=1"));
		Advance(EMiniTask17Phase::Complete);
		return;
	case EMiniTask17Phase::Complete:
		return;
	}
}

bool UMiniTask17ProbeSubsystem::AimClientAtPeer(AMiniPlayerController* Controller,
	AMiniCharacter* Pawn, AMiniCharacter* Peer) const
{
	UMiniCameraComponent* Camera = Pawn ? Pawn->GetMiniCameraComponent() : nullptr;
	if (!Controller || !Camera || !Peer)
	{
		return false;
	}
	for (int32 Iteration = 0; Iteration < 4; ++Iteration)
	{
		FMinimalViewInfo View;
		Camera->ResetCamera();
		Camera->GetCameraView(0.0f, View);
		Controller->SetControlRotation(
			(Peer->GetActorLocation() + FVector(0.0, 0.0, 45.0) - View.Location).Rotation());
	}
	Camera->ResetCamera();
	FMinimalViewInfo FinalView;
	Camera->GetCameraView(0.0f, FinalView);
	UE_LOG(LogMiniInit, Display,
		TEXT("MiniTask17Probe CLIENT_AIM: Controller=%s ControlRotation=%s CameraOrigin=%s CameraRotation=%s Pawn=%s Peer=%s"),
		*GetPathNameSafe(Controller), *Controller->GetControlRotation().ToString(),
		*FinalView.Location.ToString(), *FinalView.Rotation.ToString(),
		*Pawn->GetActorLocation().ToString(), *Peer->GetActorLocation().ToString());
	return true;
}

void UMiniTask17ProbeSubsystem::HandleEmptyMagazine(FGuid ItemId)
{
	++EmptyFeedbackCount;
	LastEmptyFeedbackItem = ItemId;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask17Probe CLIENT_EMPTY_FEEDBACK: Item=%s Count=%d"),
		*ItemId.ToString(), EmptyFeedbackCount);
}

void UMiniTask17ProbeSubsystem::AcknowledgeClient(AMiniTask17ProbeActor* Probe, const TCHAR* Marker)
{
	if (!Probe || bClientAcknowledged)
	{
		return;
	}
	bClientAcknowledged = true;
	const AMiniPlayerController* Controller = Cast<AMiniPlayerController>(Probe->GetOwner());
	const UMiniInventoryItemInstance* Rifle = GetItem(Controller, 0);
	const UMiniInventoryItemInstance* Pistol = GetItem(Controller, 1);
	UE_LOG(LogMiniInit, Display,
		TEXT("MiniTask17Probe CLIENT_%s: Owner=%d Rifle=%d/%d Pistol=%d/%d"),
		Marker, Probe->GetOwnerIndex(),
		Rifle ? Rifle->GetStat(MiniInventoryTags::AmmoInMagazine) : -1,
		Rifle ? Rifle->GetStat(MiniInventoryTags::ReserveAmmo) : -1,
		Pistol ? Pistol->GetStat(MiniInventoryTags::AmmoInMagazine) : -1,
		Pistol ? Pistol->GetStat(MiniInventoryTags::ReserveAmmo) : -1);
	Probe->ServerAcknowledge(Probe->GetPhase());
}

void UMiniTask17ProbeSubsystem::TickClient(float DeltaTime)
{
	AMiniTask17ProbeActor* Probe = nullptr;
	for (TActorIterator<AMiniTask17ProbeActor> It(GetWorld()); It; ++It)
	{
		Probe = *It;
		break;
	}
	if (!Probe || Probe->GetOwnerIndex() < 1 || Probe->GetOwnerIndex() > 2 ||
		!Probe->GetOwnerPawn() || !Probe->GetPeerPawn())
	{
		return;
	}
	AMiniPlayerController* Controller = Cast<AMiniPlayerController>(Probe->GetOwner());
	AMiniCharacter* Pawn = Probe->GetOwnerPawn();
	AMiniCharacter* Peer = Probe->GetPeerPawn();
	if (!Controller || !Controller->IsLocalController() || Controller->GetPawn() != Pawn)
	{
		// The final death checkpoint retains the old Pawn while its controller awaits respawn.
		if (Probe->GetPhase() != EMiniTask17Phase::Death)
		{
			return;
		}
	}
	const int32 Owner = Probe->GetOwnerIndex();
	const bool bShooter = Owner == 1;
	PistolDamage = GetDefault<UMiniPistolEquipmentDefinition>()->GetFireDamage();
	const EMiniTask17Phase Phase = Probe->GetPhase();
	if (ClientPhase != Phase)
	{
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask17Probe CLIENT_PHASE: Owner=%d %d -> %d"),
			Owner, static_cast<int32>(ClientPhase), static_cast<int32>(Phase));
		ClientPhase = Phase;
		ClientPhaseSeconds = 0.0f;
		LastClientDiagnosticSecond = INDEX_NONE;
		ClientInputSeconds = 0.0f;
		bClientAimSet = false;
		bClientInputPressed = false;
		bClientInputReleased = false;
		bClientBlockedFirePressed = false;
		bClientBlockedFireReleased = false;
		BlockedFireSeconds = 0.0f;
		bClientAcknowledged = false;
	}
	ClientPhaseSeconds += DeltaTime;
	if (Phase == EMiniTask17Phase::Complete)
	{
		return;
	}
	if (ClientPhaseSeconds > 30.0f)
	{
		Fail(TEXT("owning client checkpoint timed out"));
		return;
	}
	UMiniInventoryItemInstance* Rifle = GetItem(Controller, 0);
	UMiniInventoryItemInstance* Pistol = GetItem(Controller, 1);
	UMiniRangedWeaponComponent* Weapon = Pawn ? Pawn->GetRangedWeaponComponent() : nullptr;
	const UMiniHealthSet* VictimHealth = GetHealth(bShooter ? Peer : Pawn);
	const bool bInputReady = Pawn && Pawn->GetHeroComponent() && Pawn->GetHeroComponent()->IsInputActive();
	const int32 ReadyWeaponSpecCount = GetReadyWeaponSpecCount(Pawn);
	if ((Phase == EMiniTask17Phase::Initial || Phase == EMiniTask17Phase::RifleShot) &&
		ReadyWeaponSpecCount != 2)
	{
		const int32 Second = FMath::FloorToInt(ClientPhaseSeconds);
		if (Second != LastClientDiagnosticSecond)
		{
			LastClientDiagnosticSecond = Second;
			UMiniAbilitySystemComponent* ASC = GetASC(Pawn);
			const UMiniEquipmentInstance* Equipment = Pawn && Pawn->GetEquipmentManager()
				? Pawn->GetEquipmentManager()->GetCurrentEquipment() : nullptr;
			const UMiniInventoryItemInstance* SourceItem = Equipment ? Equipment->GetSourceItem() : nullptr;
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask17Probe CLIENT_WAIT_SPECS: Owner=%d ASC=%d Ready=%d Equipment=%s ItemId=%s SourceItem=%s Input=%d"),
				Owner, ASC ? ASC->GetActivatableAbilities().Num() : -1,
				ReadyWeaponSpecCount, *GetPathNameSafe(Equipment),
				Equipment ? *Equipment->GetSourceItemId().ToString() : TEXT("None"),
				*GetPathNameSafe(SourceItem), bInputReady ? 1 : 0);
			if (ASC)
			{
				for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
				{
					if (Spec.Ability && Spec.Ability->IsA<UMiniGameplayAbility_RangedFire>())
					{
						UE_LOG(LogMiniInit, Display,
							TEXT("MiniTask17Probe CLIENT_FIRE_SPEC: Owner=%d Ability=%s Source=%s Current=%s InputTag=%d"),
							Owner, *GetNameSafe(Spec.Ability), *GetPathNameSafe(Spec.SourceObject.Get()),
							*GetPathNameSafe(Equipment),
							Spec.GetDynamicSpecSourceTags().HasTagExact(MiniGameplayTags::InputTag_Fire) ? 1 : 0);
					}
				}
			}
		}
	}
	if (bShooter && Weapon && !bEmptyDelegateBound)
	{
		Weapon->OnEmptyMagazine.AddUniqueDynamic(this, &ThisClass::HandleEmptyMagazine);
		bEmptyDelegateBound = true;
	}
	if (bClientAcknowledged)
	{
		return;
	}
	switch (Phase)
	{
	case EMiniTask17Phase::Initial:
		if (bInputReady && ReadyWeaponSpecCount == 2 &&
			HasAmmo(Rifle, 30, 90) && HasAmmo(Pistol, 12, 36) &&
			HasWeapon(Pawn, UMiniRifleEquipmentDefinition::StaticClass(), 0) &&
			FVector::Dist(Pawn->GetActorLocation(), bShooter ? ShooterPosition : TargetPosition) < 30.0 &&
			FVector::Dist(Peer->GetActorLocation(), bShooter ? TargetPosition : ShooterPosition) < 30.0)
		{
			AcknowledgeClient(Probe, TEXT("INITIAL"));
		}
		return;
	case EMiniTask17Phase::RifleShot:
		if (bShooter && bInputReady && ReadyWeaponSpecCount == 2 &&
			!bClientAimSet && ClientPhaseSeconds >= 0.15f)
		{
			if (!AimClientAtPeer(Controller, Pawn, Peer))
			{
				Fail(TEXT("rifle camera unavailable"));
				return;
			}
			bClientAimSet = true;
		}
		if (bShooter && bClientAimSet && !bClientInputPressed &&
			ClientPhaseSeconds >= 0.60f)
		{
			SendKey(Controller, EKeys::LeftMouseButton, IE_Pressed);
			bClientInputPressed = true;
		}
		if (bShooter && bClientInputPressed && !bClientInputReleased)
		{
			ClientInputSeconds += DeltaTime;
			if (ClientInputSeconds >= 0.05f)
			{
				SendKey(Controller, EKeys::LeftMouseButton, IE_Released);
				bClientInputReleased = true;
			}
		}
		if ((!bShooter || bClientInputReleased) &&
			HasAmmo(Rifle, bShooter ? 29 : 30, 90) && HasAmmo(Pistol, 12, 36) &&
			VictimHealth && FMath::IsNearlyEqual(VictimHealth->GetHealth(), 75.0f, 0.01f))
		{
			AcknowledgeClient(Probe, TEXT("RIFLE_SHOT"));
		}
		return;
	case EMiniTask17Phase::FullReload:
		if (!bShooter && bInputReady && !bClientInputPressed)
		{
			SendKey(Controller, EKeys::R, IE_Pressed);
			bClientInputPressed = true;
		}
		if (!bShooter && bClientInputPressed && !bClientInputReleased)
		{
			ClientInputSeconds += DeltaTime;
			if (ClientInputSeconds >= 0.05f)
			{
				SendKey(Controller, EKeys::R, IE_Released);
				bClientInputReleased = true;
			}
		}
		if (ClientPhaseSeconds >= 0.25f && (bShooter || bClientInputReleased) &&
			HasAmmo(Rifle, bShooter ? 29 : 30, 90))
		{
			AcknowledgeClient(Probe, TEXT("FULL_RELOAD"));
		}
		return;
	case EMiniTask17Phase::EmptyFire:
		if (!bShooter)
		{
			AcknowledgeClient(Probe, TEXT("EMPTY_OBSERVER"));
			return;
		}
		if (bInputReady && HasAmmo(Rifle, 0, 90) && !bClientInputPressed)
		{
			SendKey(Controller, EKeys::LeftMouseButton, IE_Pressed);
			bClientInputPressed = true;
		}
		if (bClientInputPressed && !bClientInputReleased)
		{
			ClientInputSeconds += DeltaTime;
			if (ClientInputSeconds >= 0.05f)
			{
				SendKey(Controller, EKeys::LeftMouseButton, IE_Released);
				bClientInputReleased = true;
			}
		}
		if (bClientInputReleased && EmptyFeedbackCount == 1 && Rifle &&
			LastEmptyFeedbackItem == Rifle->GetInstanceId() && HasAmmo(Rifle, 0, 90))
		{
			AcknowledgeClient(Probe, TEXT("EMPTY_FEEDBACK"));
		}
		return;
	case EMiniTask17Phase::RifleReload:
		if (!bShooter)
		{
			AcknowledgeClient(Probe, TEXT("RIFLE_RELOAD_OBSERVER"));
			return;
		}
		if (bInputReady && HasAmmo(Rifle, 0, 90) && !bClientInputPressed)
		{
			SendKey(Controller, EKeys::R, IE_Pressed);
			bClientInputPressed = true;
		}
		if (bClientInputPressed && !bClientInputReleased)
		{
			ClientInputSeconds += DeltaTime;
			if (ClientInputSeconds >= 0.05f)
			{
				SendKey(Controller, EKeys::R, IE_Released);
				bClientInputReleased = true;
			}
		}
		if (bClientInputReleased && !bClientBlockedFirePressed && GetASC(Pawn) &&
			GetASC(Pawn)->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading))
		{
			SendKey(Controller, EKeys::LeftMouseButton, IE_Pressed);
			bClientBlockedFirePressed = true;
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask17Probe CLIENT_BLOCKED_FIRE_PRESSED: Owner=1 Reloading=1"));
		}
		if (bClientBlockedFirePressed && !bClientBlockedFireReleased)
		{
			BlockedFireSeconds += DeltaTime;
			if (BlockedFireSeconds >= 0.05f)
			{
				SendKey(Controller, EKeys::LeftMouseButton, IE_Released);
				bClientBlockedFireReleased = true;
			}
		}
		if (bClientBlockedFireReleased && HasAmmo(Rifle, 30, 60) && HasAmmo(Pistol, 12, 36))
		{
			AcknowledgeClient(Probe, TEXT("RIFLE_RELOAD"));
		}
		return;
	case EMiniTask17Phase::PartialReload:
		if (bShooter)
		{
			AcknowledgeClient(Probe, TEXT("PARTIAL_OBSERVER"));
			return;
		}
		if (bInputReady && HasAmmo(Rifle, 25, 3) && !bClientInputPressed)
		{
			SendKey(Controller, EKeys::R, IE_Pressed);
			bClientInputPressed = true;
		}
		if (bClientInputPressed && !bClientInputReleased)
		{
			ClientInputSeconds += DeltaTime;
			if (ClientInputSeconds >= 0.05f)
			{
				SendKey(Controller, EKeys::R, IE_Released);
				bClientInputReleased = true;
			}
		}
		if (bClientInputReleased && HasAmmo(Rifle, 28, 0) && HasAmmo(Pistol, 12, 36))
		{
			AcknowledgeClient(Probe, TEXT("PARTIAL_RELOAD"));
		}
		return;
	case EMiniTask17Phase::NoReserveReload:
		if (!bShooter && bInputReady && !bClientInputPressed)
		{
			SendKey(Controller, EKeys::R, IE_Pressed);
			bClientInputPressed = true;
		}
		if (!bShooter && bClientInputPressed && !bClientInputReleased)
		{
			ClientInputSeconds += DeltaTime;
			if (ClientInputSeconds >= 0.05f)
			{
				SendKey(Controller, EKeys::R, IE_Released);
				bClientInputReleased = true;
			}
		}
		if (ClientPhaseSeconds >= 0.25f && (bShooter || bClientInputReleased) &&
			HasAmmo(Rifle, bShooter ? 30 : 28, bShooter ? 60 : 0))
		{
			AcknowledgeClient(Probe, TEXT("NO_RESERVE"));
		}
		return;
	case EMiniTask17Phase::RifleReloadCancelStart:
		if (!bShooter)
		{
			AcknowledgeClient(Probe, TEXT("RIFLE_CANCEL_OBSERVER"));
			return;
		}
		if (bInputReady && HasAmmo(Rifle, 20, 60) && !bClientInputPressed)
		{
			SendKey(Controller, EKeys::R, IE_Pressed);
			bClientInputPressed = true;
		}
		if (bClientInputPressed && !bClientInputReleased)
		{
			ClientInputSeconds += DeltaTime;
			if (ClientInputSeconds >= 0.05f)
			{
				SendKey(Controller, EKeys::R, IE_Released);
				bClientInputReleased = true;
				AcknowledgeClient(Probe, TEXT("RIFLE_CANCEL_START"));
			}
		}
		return;
	case EMiniTask17Phase::RifleReloadCancelSwitch:
		if (!bShooter)
		{
			AcknowledgeClient(Probe, TEXT("SWITCH_OBSERVER"));
			return;
		}
		if (bInputReady && !bClientInputPressed)
		{
			SendKey(Controller, EKeys::Q, IE_Pressed);
			bClientInputPressed = true;
		}
		if (bClientInputPressed && !bClientInputReleased)
		{
			ClientInputSeconds += DeltaTime;
			if (ClientInputSeconds >= 0.05f)
			{
				SendKey(Controller, EKeys::Q, IE_Released);
				bClientInputReleased = true;
			}
		}
		if (bClientInputReleased && HasWeapon(Pawn, UMiniPistolEquipmentDefinition::StaticClass(), 1) &&
			HasAmmo(Rifle, 20, 60) && HasAmmo(Pistol, 12, 36) &&
			!GetASC(Pawn)->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading))
		{
			AcknowledgeClient(Probe, TEXT("SWITCH_CANCEL"));
		}
		return;
	case EMiniTask17Phase::PistolShot:
		if (bShooter && bInputReady && !bClientAimSet && ClientPhaseSeconds >= 0.15f)
		{
			if (!AimClientAtPeer(Controller, Pawn, Peer))
			{
				Fail(TEXT("pistol camera unavailable"));
				return;
			}
			bClientAimSet = true;
		}
		if (bShooter && bClientAimSet && !bClientInputPressed &&
			ClientPhaseSeconds >= 0.60f)
		{
			SendKey(Controller, EKeys::LeftMouseButton, IE_Pressed);
			bClientInputPressed = true;
		}
		if (bShooter && bClientInputPressed && !bClientInputReleased)
		{
			ClientInputSeconds += DeltaTime;
			if (ClientInputSeconds >= 0.55f)
			{
				SendKey(Controller, EKeys::LeftMouseButton, IE_Released);
				bClientInputReleased = true;
			}
		}
		if ((!bShooter || bClientInputReleased) && HasAmmo(Rifle, bShooter ? 20 : 28, bShooter ? 60 : 0) &&
			HasAmmo(Pistol, bShooter ? 11 : 12, 36) && VictimHealth &&
			FMath::IsNearlyEqual(VictimHealth->GetHealth(), 75.0f - PistolDamage, 0.01f))
		{
			AcknowledgeClient(Probe, TEXT("PISTOL_SHOT"));
		}
		return;
	case EMiniTask17Phase::PistolReload:
		if (!bShooter)
		{
			AcknowledgeClient(Probe, TEXT("PISTOL_RELOAD_OBSERVER"));
			return;
		}
		if (bInputReady && HasAmmo(Pistol, 11, 36) && !bClientInputPressed)
		{
			SendKey(Controller, EKeys::R, IE_Pressed);
			bClientInputPressed = true;
		}
		if (bClientInputPressed && !bClientInputReleased)
		{
			ClientInputSeconds += DeltaTime;
			if (ClientInputSeconds >= 0.05f)
			{
				SendKey(Controller, EKeys::R, IE_Released);
				bClientInputReleased = true;
			}
		}
		if (bClientInputReleased && HasAmmo(Rifle, 20, 60) && HasAmmo(Pistol, 12, 35))
		{
			AcknowledgeClient(Probe, TEXT("PISTOL_RELOAD"));
		}
		return;
	case EMiniTask17Phase::PistolReloadCancelStart:
		if (!bShooter)
		{
			AcknowledgeClient(Probe, TEXT("DEATH_CANCEL_OBSERVER"));
			return;
		}
		if (bInputReady && HasAmmo(Pistol, 5, 35) && !bClientInputPressed)
		{
			SendKey(Controller, EKeys::R, IE_Pressed);
			bClientInputPressed = true;
		}
		if (bClientInputPressed && !bClientInputReleased)
		{
			ClientInputSeconds += DeltaTime;
			if (ClientInputSeconds >= 0.05f)
			{
				SendKey(Controller, EKeys::R, IE_Released);
				bClientInputReleased = true;
				AcknowledgeClient(Probe, TEXT("DEATH_CANCEL_START"));
			}
		}
		return;
	case EMiniTask17Phase::Death:
		if (Pawn && Pawn->GetHealthComponent() && Pawn->GetHealthComponent()->IsDead() &&
			!Pawn->GetEquipmentManager()->GetCurrentEquipment() &&
			!GetASC(Pawn)->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading))
		{
			AcknowledgeClient(Probe, TEXT("DEATH_CANCEL"));
		}
		else if (!bShooter && Peer && Peer->GetHealthComponent() && Peer->GetHealthComponent()->IsDead() &&
			!Peer->GetEquipmentManager()->GetCurrentEquipment())
		{
			AcknowledgeClient(Probe, TEXT("DEATH_OBSERVER"));
		}
		return;
	case EMiniTask17Phase::Complete:
		return;
	}
}
