#include "MiniTask16ProbeSubsystem.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "AbilitySystem/MiniGameplayAbility_RifleFire.h"
#include "AbilitySystem/MiniHealthSet.h"
#include "Camera/MiniCameraComponent.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHealthComponent.h"
#include "Character/MiniHeroComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Equipment/MiniEquipmentDefinition.h"
#include "Equipment/MiniEquipmentInstance.h"
#include "Equipment/MiniEquipmentManagerComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Inventory/MiniInventoryItemDefinition.h"
#include "Inventory/MiniInventoryItemInstance.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MiniPlayerController.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniLogChannels.h"
#include "Weapons/MiniRangedWeaponComponent.h"

namespace
{
const FVector ShooterPosition(0.0, 0.0, 3000.0);
const FVector TargetPosition(800.0, 0.0, 3000.0);

UMiniInventoryItemInstance* GetRifleItem(const AMiniCharacter* Pawn)
{
	UMiniEquipmentManagerComponent* Manager = Pawn ? Pawn->GetEquipmentManager() : nullptr;
	UMiniEquipmentInstance* Equipment = Manager ? Manager->GetCurrentEquipment() : nullptr;
	return Equipment ? Equipment->GetSourceItem() : nullptr;
}

const UMiniHealthSet* GetHealth(const AMiniCharacter* Pawn)
{
	const AMiniPlayerState* State = Pawn ? Pawn->GetPlayerState<AMiniPlayerState>() : nullptr;
	return State ? State->GetHealthSet() : nullptr;
}

void SendFireKey(AMiniPlayerController* Controller, EInputEvent Event)
{
	Controller->InputKey(FInputKeyEventArgs::CreateSimulated(
		EKeys::LeftMouseButton, Event, Event == IE_Released ? 0.0f : 1.0f));
}
}

bool UMiniTask16ProbeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if !UE_BUILD_SHIPPING
	return Super::ShouldCreateSubsystem(Outer) &&
		FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask16"));
#else
	return false;
#endif
}

TStatId UMiniTask16ProbeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMiniTask16ProbeSubsystem, STATGROUP_Tickables);
}

void UMiniTask16ProbeSubsystem::Fail(const TCHAR* Reason)
{
	if (!bFailed)
	{
		bFailed = true;
		UE_LOG(LogMiniInit, Error, TEXT("MiniTask16Probe FAIL: Stage=%d Reason=%s"),
			static_cast<int32>(Stage), Reason);
	}
}

void UMiniTask16ProbeSubsystem::Advance(EStage NewStage)
{
	Stage = NewStage;
	StageSeconds = 0.0f;
}

void UMiniTask16ProbeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bFailed || Stage == EStage::Done || !GetWorld() || !GetWorld()->HasBegunPlay())
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

bool UMiniTask16ProbeSubsystem::CheckState(int32 Shots, int32 Ammo, float Health) const
{
	const AMiniCharacter* Source = Shooter.Get();
	const AMiniCharacter* Victim = Target.Get();
	const UMiniRangedWeaponComponent* Weapon = Source ? Source->GetRangedWeaponComponent() : nullptr;
	const UMiniInventoryItemInstance* Item = GetRifleItem(Source);
	const UMiniHealthSet* HealthSet = GetHealth(Victim);
	return Weapon && Item && HealthSet && Weapon->GetAcceptedShotCount() == static_cast<uint32>(Shots) &&
		Item->GetStat(MiniInventoryTags::AmmoInMagazine) == Ammo &&
		FMath::IsNearlyEqual(HealthSet->GetHealth(), Health, 0.01f);
}

bool UMiniTask16ProbeSubsystem::FireAccepted(uint32 Sequence, int32 ExpectedShots,
	int32 ExpectedAmmo, float ExpectedHealth)
{
	UMiniRangedWeaponComponent* Weapon = Shooter.IsValid() ? Shooter->GetRangedWeaponComponent() : nullptr;
	return Weapon && Weapon->TryFireOnServer(TestOrigin, TestDirection, Sequence) &&
		CheckState(ExpectedShots, ExpectedAmmo, ExpectedHealth);
}

bool UMiniTask16ProbeSubsystem::BuildMuzzleOnlyBarrier()
{
	AMiniCharacter* Source = Shooter.Get();
	AActor* Wall = Barrier.Get();
	UMiniRangedWeaponComponent* Weapon = Source ? Source->GetRangedWeaponComponent() : nullptr;
	UBoxComponent* Box = Wall ? Cast<UBoxComponent>(Wall->GetRootComponent()) : nullptr;
	if (!Source || !Wall || !Weapon || !Box)
	{
		return false;
	}
	FCollisionQueryParams Query(SCENE_QUERY_STAT(MiniTask16Muzzle), false, Source);
	FHitResult CameraHit;
	const FVector End = TestOrigin + TestDirection * 10000.0;
	Box->SetBoxExtent(FVector(10.0));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	const FVector Muzzle = Weapon->GetMuzzleLocation(Source);
	for (const float Distance : { 35.0f, 55.0f, 80.0f, 120.0f })
	{
		Box->SetWorldLocation(Muzzle + (TargetPosition - Muzzle).GetSafeNormal() * Distance);
		GetWorld()->LineTraceSingleByChannel(CameraHit, TestOrigin, End, ECC_Visibility, Query);
		if (CameraHit.GetActor() != Target.Get())
		{
			continue;
		}
		FHitResult MuzzleHit;
		GetWorld()->LineTraceSingleByChannel(MuzzleHit, Muzzle,
			CameraHit.ImpactPoint, ECC_Visibility, Query);
		if (MuzzleHit.GetActor() == Wall)
		{
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask16Probe MUZZLE_ONLY_READY: Distance=%.1f"), Distance);
			return true;
		}
	}
	return false;
}

void UMiniTask16ProbeSubsystem::TickServer(float DeltaTime)
{
	StageSeconds += DeltaTime;
	if (StageSeconds > (Stage == EStage::WaitPlayers ? 90.0f : 30.0f))
	{
		if (Stage == EStage::WaitClientShot && Shooter.IsValid())
		{
			const UMiniRangedWeaponComponent* TimedOutWeapon = Shooter->GetRangedWeaponComponent();
			UE_LOG(LogMiniInit, Error,
				TEXT("MiniTask16Probe SERVER_FIRE_TIMEOUT: Shots=%u Sequence=%u Reason=%d ControlRotation=%s"),
				TimedOutWeapon ? TimedOutWeapon->GetAcceptedShotCount() : 0,
				TimedOutWeapon ? TimedOutWeapon->GetLastProcessedSequence() : 0,
				TimedOutWeapon ? static_cast<int32>(TimedOutWeapon->GetLastFireRejectionReason()) : -1,
				*Shooter->GetControlRotation().ToString());
		}
		Fail(TEXT("server stage timed out"));
		return;
	}
	UMiniRangedWeaponComponent* Weapon = Shooter.IsValid() ? Shooter->GetRangedWeaponComponent() : nullptr;
	UMiniInventoryItemInstance* Item = GetRifleItem(Shooter.Get());
	switch (Stage)
	{
	case EStage::WaitPlayers:
	{
		TArray<AMiniCharacter*> Ready;
		for (TActorIterator<AMiniPlayerController> It(GetWorld()); It; ++It)
		{
			AMiniPlayerController* Controller = *It;
			AMiniCharacter* Pawn = Controller && !Controller->IsLocalController()
				? Cast<AMiniCharacter>(Controller->GetPawn()) : nullptr;
			AMiniPlayerState* State = Controller ? Controller->GetPlayerState<AMiniPlayerState>() : nullptr;
			UMiniAbilitySystemComponent* ASC = State ? State->GetMiniAbilitySystemComponent() : nullptr;
			UMiniEquipmentInstance* Equipment = Pawn && Pawn->GetEquipmentManager()
				? Pawn->GetEquipmentManager()->GetCurrentEquipment() : nullptr;
			if (Pawn && ASC && ASC->GetAvatarActor() == Pawn && Equipment &&
				Equipment->GetEquipmentDefinition() == UMiniRifleEquipmentDefinition::StaticClass() &&
				Pawn->GetRangedWeaponComponent() && GetRifleItem(Pawn))
			{
				Ready.Add(Pawn);
			}
		}
		if (Ready.Num() != 2)
		{
			return;
		}
		Shooter = Ready[0];
		Target = Ready[1];
		for (int32 Index = 0; Index < 2; ++Index)
		{
			AMiniCharacter* Pawn = Ready[Index];
			// Keep movement replication active: it uploads the owner's aim to
			// the server. MOVE_None would freeze the server's control rotation.
			UCharacterMovementComponent* Movement = Pawn->GetCharacterMovement();
			Movement->StopMovementImmediately();
			Movement->SetMovementMode(MOVE_Flying);
			Pawn->SetActorLocation(Index == 0 ? ShooterPosition : TargetPosition,
				false, nullptr, ETeleportType::TeleportPhysics);
			Pawn->ForceNetUpdate();
		}
		if (!CheckState(0, 30, 100.0f))
		{
			Fail(TEXT("initial rifle, ammo or health mismatch"));
			return;
		}
		const AMiniPlayerState* SourceState = Shooter->GetPlayerState<AMiniPlayerState>();
		const UMiniAbilitySystemComponent* ASC = SourceState ? SourceState->GetMiniAbilitySystemComponent() : nullptr;
		const UMiniEquipmentInstance* Equipment = Shooter->GetEquipmentManager()->GetCurrentEquipment();
		bool bHasFireGrant = false;
		for (const FGameplayAbilitySpecHandle& Handle : Equipment->GetGrantedHandles().AbilityHandles)
		{
			const FGameplayAbilitySpec* Spec = ASC ? ASC->FindAbilitySpecFromHandle(Handle) : nullptr;
			bHasFireGrant |= Spec && Spec->SourceObject.Get() == Equipment &&
				Spec->Ability->IsA<UMiniGameplayAbility_RifleFire>();
		}
		if (!bHasFireGrant)
		{
			Fail(TEXT("rifle Fire GA is not granted by current equipment"));
			return;
		}
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask16Probe SERVER_READY: Shooter=%s Target=%s"),
			*Shooter->GetPathName(), *Target->GetPathName());
		Advance(EStage::WaitClientShot);
		return;
	}
	case EStage::WaitClientShot:
		if (!Weapon || Weapon->GetAcceptedShotCount() == 0)
		{
			return;
		}
		if (!CheckState(1, 29, 75.0f) || Weapon->GetLastHitCharacter() != Target.Get() ||
			Weapon->GetLastProcessedSequence() != 1)
		{
			Fail(TEXT("client input did not produce one authoritative 25 damage hit"));
			return;
		}
		TestOrigin = Weapon->GetLastAcceptedCameraOrigin();
		TestDirection = Weapon->GetLastAcceptedAimDirection();
		if (Weapon->TryFireOnServer(TestOrigin, TestDirection, 1) ||
			Weapon->TryFireOnServer(TestOrigin, -TestDirection, 2) ||
			Weapon->TryFireOnServer(TestOrigin + FVector(1000.0, 0.0, 0.0), TestDirection, 3) ||
			Weapon->TryFireOnServer(TestOrigin, TestDirection, 4) || !CheckState(1, 29, 75.0f))
		{
			Fail(TEXT("replay, forged view or fire interval was accepted"));
			return;
		}
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask16Probe REJECTED: Replay=1 Direction=1 Origin=1 Rate=1"));
		Advance(EStage::WaitWallDelay);
		return;
	case EStage::WaitWallDelay:
		if (StageSeconds < 0.18f || !Weapon || !Item)
		{
			return;
		}
		{
			AActor* Wall = GetWorld()->SpawnActor<AActor>();
			UBoxComponent* Box = Wall ? NewObject<UBoxComponent>(Wall, TEXT("ProbeWall")) : nullptr;
			if (!Wall || !Box)
			{
				Fail(TEXT("could not create wall collider"));
				return;
			}
			Barrier = Wall;
			Wall->SetRootComponent(Box);
			Box->SetBoxExtent(FVector(18.0, 250.0, 250.0));
			Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Box->SetCollisionResponseToAllChannels(ECR_Ignore);
			Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
			Box->RegisterComponent();
			Wall->SetActorLocation(TestOrigin + TestDirection * 550.0);
			FHitResult Hit;
			FCollisionQueryParams Query(SCENE_QUERY_STAT(MiniTask16Wall), false, Shooter.Get());
			GetWorld()->LineTraceSingleByChannel(Hit, TestOrigin,
				TestOrigin + TestDirection * 10000.0, ECC_Visibility, Query);
			if (Hit.GetActor() != Wall || !FireAccepted(NextSequence++, 2, 28, 75.0f) ||
				Weapon->GetLastHitCharacter())
			{
				Fail(TEXT("wall did not block authoritative hit"));
				return;
			}
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask16Probe WALL_BLOCKED: Health=75 Ammo=28"));
			Advance(EStage::WaitMuzzleDelay);
			return;
		}
	case EStage::WaitMuzzleDelay:
		if (StageSeconds < 0.18f || !Weapon || !Item)
		{
			return;
		}
		if (!Item->SetStat(MiniInventoryTags::AmmoInMagazine, 0) ||
			Weapon->TryFireOnServer(TestOrigin, TestDirection, NextSequence++) ||
			!CheckState(2, 0, 75.0f))
		{
			Fail(TEXT("empty magazine produced a shot"));
			return;
		}
		if (!Item->SetStat(MiniInventoryTags::AmmoInMagazine, 4) ||
			!BuildMuzzleOnlyBarrier() ||
			!FireAccepted(NextSequence++, 3, 3, 75.0f) || Weapon->GetLastHitCharacter())
		{
			Fail(TEXT("muzzle-only obstruction did not block damage"));
			return;
		}
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask16Probe MUZZLE_BLOCKED: Health=75 Ammo=3 EmptyRejected=1"));
		if (AActor* Wall = Barrier.Get())
		{
			Wall->SetActorEnableCollision(false);
		}
		Advance(EStage::WaitLethalDelay);
		return;
	case EStage::WaitLethalDelay:
		if (StageSeconds < 0.18f)
		{
			return;
		}
		if (!FireAccepted(NextSequence++, 4 + LethalShots,
			2 - LethalShots, 50.0f - 25.0f * LethalShots))
		{
			Fail(TEXT("lethal series shot failed"));
			return;
		}
		++LethalShots;
		if (LethalShots < 3)
		{
			StageSeconds = 0.0f;
			return;
		}
		if (!Target.IsValid() || !Target->GetHealthComponent()->IsDead() ||
			Target->GetEquipmentManager()->GetCurrentEquipment())
		{
			Fail(TEXT("zero health did not enter death or revoke equipment"));
			return;
		}
		UE_LOG(LogMiniInit, Display,
			TEXT("MiniTask16Probe SERVER_PASS: InputGA=1 DamageGE=1 Replay=1 BadView=1 Rate=1 Empty=1 Wall=1 Muzzle=1 Death=1"));
		Advance(EStage::WaitClientDeath);
		return;
	case EStage::WaitClientDeath:
		// The process harness verifies both clients' replicated death observations.
		Advance(EStage::Done);
		return;
	case EStage::Done:
		return;
	}
}

void UMiniTask16ProbeSubsystem::TickClient(float DeltaTime)
{
	StageSeconds += DeltaTime;
	if (StageSeconds > 90.0f)
	{
		Fail(TEXT("client never reached fire/death observation"));
		return;
	}
	AMiniPlayerController* Controller = nullptr;
	for (TActorIterator<AMiniPlayerController> It(GetWorld()); It; ++It)
	{
		if (It->IsLocalController())
		{
			Controller = *It;
			break;
		}
	}
	AMiniCharacter* Pawn = Controller ? Cast<AMiniCharacter>(Controller->GetPawn()) : nullptr;
	if (!Pawn)
	{
		return;
	}
	const bool bShooter = FVector::Dist(Pawn->GetActorLocation(), ShooterPosition) < 30.0;
	const bool bTarget = FVector::Dist(Pawn->GetActorLocation(), TargetPosition) < 30.0;
	if (!bShooter && !bTarget)
	{
		return;
	}
	AMiniCharacter* Peer = nullptr;
	for (TActorIterator<AMiniCharacter> It(GetWorld()); It; ++It)
	{
		if (*It != Pawn && FVector::Dist(It->GetActorLocation(),
			bShooter ? TargetPosition : ShooterPosition) < 30.0)
		{
			Peer = *It;
			break;
		}
	}
	if (!Peer)
	{
		return;
	}
	if (!bClientRoleLogged)
	{
		bClientRoleLogged = true;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask16Probe CLIENT_READY: Role=%s Own=%s Peer=%s"),
			bShooter ? TEXT("Shooter") : TEXT("Target"), *Pawn->GetPathName(), *Peer->GetPathName());
	}
	ClientReadySeconds += DeltaTime;
	if (bClientAimSet && !bClientPressed)
	{
		ClientAimSeconds += DeltaTime;
	}
	if (bClientPressed)
	{
		ClientPressedSeconds += DeltaTime;
	}
	if (bShooter && !bClientPressed)
	{
		if (!Pawn->GetHeroComponent() || !Pawn->GetHeroComponent()->IsInputActive())
		{
			return;
		}
		if (ClientReadySeconds < 0.5f)
		{
			return;
		}
		UMiniCameraComponent* Camera = Pawn->GetMiniCameraComponent();
		if (!Camera)
		{
			Fail(TEXT("local camera missing"));
			return;
		}
		if (!bClientAimSet)
		{
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
			bClientAimSet = true;
			ClientAimSeconds = 0.0f;
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask16Probe CLIENT_AIM: ControlRotation=%s CameraOrigin=%s CameraRotation=%s"),
				*Controller->GetControlRotation().ToString(),
				*FinalView.Location.ToString(), *FinalView.Rotation.ToString());
			return;
		}
		// A shot RPC can arrive before the movement packet carrying this aim.
		// Wait a full interval after camera setup rather than firing in its frame.
		if (ClientAimSeconds < 0.45f)
		{
			return;
		}
		SendFireKey(Controller, IE_Pressed);
		bClientPressed = true;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask16Probe CLIENT_FIRE_PRESSED: Input=LeftMouseButton"));
		return;
	}
	// Task 17 makes the rifle automatic while held; this legacy probe verifies one press/one shot.
	if (bShooter && bClientPressed && !bClientReleased && ClientPressedSeconds >= 0.05f)
	{
		SendFireKey(Controller, IE_Released);
		bClientReleased = true;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask16Probe CLIENT_FIRE_RELEASED: Input=LeftMouseButton"));
	}
	const UMiniHealthSet* PeerHealth = GetHealth(bShooter ? Peer : Pawn);
	if (!bClientDeathLogged && PeerHealth && PeerHealth->GetHealth() <= 0.0f &&
		(bShooter ? Peer : Pawn)->GetHealthComponent()->IsDead())
	{
		bClientDeathLogged = true;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask16Probe CLIENT_DEATH: Role=%s Health=0 Dead=1"),
			bShooter ? TEXT("Shooter") : TEXT("Target"));
		Advance(EStage::Done);
	}
}
