#include "MiniRangedWeaponComponent.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "AbilitySystem/MiniHealthSet.h"
#include "AbilitySystem/MiniGameplayAbility_RangedFire.h"
#include "AbilitySystem/MiniGameplayAbility_RifleFire.h"
#include "Camera/MiniCameraComponent.h"
#include "Camera/MiniCameraMode.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHealthComponent.h"
#include "Character/MiniPawnData.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Equipment/MiniEquipmentDefinition.h"
#include "Equipment/MiniEquipmentInstance.h"
#include "Equipment/MiniEquipmentManagerComponent.h"
#include "Feedback/MiniCombatFeedbackComponent.h"
#include "GameModes/MiniGameMode.h"
#include "HAL/IConsoleManager.h"
#include "Inventory/MiniInventoryItemDefinition.h"
#include "Inventory/MiniInventoryItemInstance.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniGameplayTags.h"
#include "System/MiniLogChannels.h"
#include "TimerManager.h"

namespace
{
TAutoConsoleVariable<int32> CVarMiniWeaponDrawTraces(
	TEXT("mini.Weapon.DrawTraces"), 0,
	TEXT("Draw long authoritative camera/muzzle weapon traces when nonzero."));
TAutoConsoleVariable<int32> CVarMiniWeaponLocalTracer(
	TEXT("mini.Weapon.LocalTracer"), 1,
	TEXT("Draw a short provisional owning-client weapon tracer when nonzero."));

bool IsFiniteVector(const FVector& Value)
{
	return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
}
}

UMiniRangedWeaponComponent::UMiniRangedWeaponComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
}

void UMiniRangedWeaponComponent::RequestFire()
{
	AMiniCharacter* Pawn = Cast<AMiniCharacter>(GetOwner());
	UMiniCameraComponent* Camera = Pawn ? Pawn->GetMiniCameraComponent() : nullptr;
	if (!Pawn || !Pawn->IsLocallyControlled() || !Camera || !GetWorld())
	{
		return;
	}
	const UMiniEquipmentManagerComponent* Manager = Pawn->GetEquipmentManager();
	UMiniEquipmentInstance* Equipment = Manager ? Manager->GetCurrentEquipment() : nullptr;
	const FGuid SourceItemId = Equipment ? Equipment->GetSourceItemId() : FGuid();
	if (!SourceItemId.IsValid())
	{
		return;
	}

	FMinimalViewInfo View;
	Camera->GetCameraView(0.0f, View);
	const FVector AimDirection = View.Rotation.Vector().GetSafeNormal();
	if (!IsFiniteVector(View.Location) || !IsFiniteVector(AimDirection) || AimDirection.IsNearlyZero())
	{
		return;
	}

	const uint32 ShotSequence = ++NextLocalSequence;
	const UClass* DefinitionClass = Equipment->GetEquipmentDefinition().Get();
	const bool bPistol = DefinitionClass &&
		DefinitionClass->IsChildOf(UMiniPistolEquipmentDefinition::StaticClass());
	const UMiniInventoryItemInstance* LocalItem = Equipment->GetSourceItem();
	if (LocalItem && LocalItem->GetStat(MiniInventoryTags::AmmoInMagazine) > 0)
	{
		if (UMiniCombatFeedbackComponent* Feedback = Pawn->GetCombatFeedbackComponent())
		{
			Feedback->PlayPredictedFire(ShotSequence, bPistol, GetMuzzleLocation(Pawn));
		}
	}
	if (CVarMiniWeaponLocalTracer.GetValueOnGameThread() != 0)
	{
		DrawDebugLine(GetWorld(), View.Location, View.Location + AimDirection * 1500.0,
			FColor::Cyan, false, 0.12f, 0, 1.5f);
	}
	if (Pawn->HasAuthority())
	{
		TryFireOnServer(View.Location, AimDirection, ShotSequence, SourceItemId);
	}
	else
	{
		ServerFire(View.Location, AimDirection, ShotSequence, SourceItemId, Equipment);
	}
}

void UMiniRangedWeaponComponent::ServerFire_Implementation(
	FVector_NetQuantize CameraOrigin, FVector_NetQuantizeNormal AimDirection,
	uint32 ShotSequence, FGuid SourceItemId, UMiniEquipmentInstance* SourceEquipment)
{
	const AMiniCharacter* Pawn = Cast<AMiniCharacter>(GetOwner());
	const UMiniEquipmentManagerComponent* Manager = Pawn ? Pawn->GetEquipmentManager() : nullptr;
	const UMiniEquipmentInstance* CurrentEquipment = Manager ? Manager->GetCurrentEquipment() : nullptr;
	// An inventory item may be unequipped and re-equipped. Its item ID remains the
	// same, but each equipment grant has a distinct replicated subobject identity.
	if (!IsValid(SourceEquipment) || CurrentEquipment != SourceEquipment ||
		SourceEquipment->GetSourceItemId() != SourceItemId)
	{
		RejectFire(EMiniFireRejectionReason::WrongEquipment);
		return;
	}
	TryFireOnServer(CameraOrigin, AimDirection, ShotSequence, SourceItemId);
}

void UMiniRangedWeaponComponent::ClientNotifyEmptyMagazine_Implementation(
	UMiniEquipmentInstance* SourceEquipment)
{
	const AMiniCharacter* Pawn = Cast<AMiniCharacter>(GetOwner());
	const UMiniEquipmentManagerComponent* Manager = Pawn ? Pawn->GetEquipmentManager() : nullptr;
	const UMiniEquipmentInstance* Equipment = Manager ? Manager->GetCurrentEquipment() : nullptr;
	if (Pawn && Pawn->IsLocallyControlled() && IsValid(SourceEquipment) &&
		Equipment == SourceEquipment)
	{
		OnEmptyMagazine.Broadcast(SourceEquipment->GetSourceItemId());
		// On a listen host this RPC executes inside RequestFire/ActivateAbility. Defer the
		// cancellation so the rifle cannot install a timer after EndAbility has run.
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(
				this, &ThisClass::CancelActiveFireAbilityForEmptyMagazine, SourceEquipment));
		}
	}
}

void UMiniRangedWeaponComponent::ClientNotifyHitConfirmed_Implementation(
	uint32 ShotSequence, float AppliedDamage, bool bKilled)
{
	AMiniCharacter* Pawn = Cast<AMiniCharacter>(GetOwner());
	if (Pawn && Pawn->IsLocallyControlled())
	{
		if (UMiniCombatFeedbackComponent* Feedback = Pawn->GetCombatFeedbackComponent())
		{
			Feedback->NotifyConfirmedHit(ShotSequence, AppliedDamage, bKilled);
		}
	}
}

void UMiniRangedWeaponComponent::CancelActiveFireAbilityForEmptyMagazine(
	UMiniEquipmentInstance* SourceEquipment)
{
	const AMiniCharacter* Pawn = Cast<AMiniCharacter>(GetOwner());
	const UMiniEquipmentManagerComponent* Manager = Pawn ? Pawn->GetEquipmentManager() : nullptr;
	const UMiniEquipmentInstance* Equipment = Manager ? Manager->GetCurrentEquipment() : nullptr;
	const AMiniPlayerState* State = Pawn ? Pawn->GetPlayerState<AMiniPlayerState>() : nullptr;
	UMiniAbilitySystemComponent* ASC = State ? State->GetMiniAbilitySystemComponent() : nullptr;
	if (Pawn && Pawn->IsLocallyControlled() && IsValid(SourceEquipment) &&
		Equipment == SourceEquipment && ASC)
	{
		// GrantedHandles are server-only. The owning client finds its active spec
		// through the replicated SourceObject instead. Pistol fire already ended.
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			if (Spec.SourceObject.Get() == Equipment && Spec.IsActive() && Spec.Ability &&
				Spec.Ability->IsA<UMiniGameplayAbility_RifleFire>())
			{
				ASC->CancelAbilityHandle(Spec.Handle);
				break;
			}
		}
	}
}

const UMiniRangedWeaponEquipmentDefinition* UMiniRangedWeaponComponent::GetUsableRangedWeapon(
	AMiniCharacter* Pawn) const
{
	const UMiniEquipmentManagerComponent* Manager = Pawn ? Pawn->GetEquipmentManager() : nullptr;
	const UMiniEquipmentInstance* Equipment = Manager ? Manager->GetCurrentEquipment() : nullptr;
	const UMiniRangedWeaponEquipmentDefinition* WeaponDefinition = Equipment
		? Cast<UMiniRangedWeaponEquipmentDefinition>(Equipment->GetEquipmentDefinition().GetDefaultObject()) : nullptr;
	const UMiniInventoryItemInstance* Item = Equipment ? Equipment->GetSourceItem() : nullptr;
	const AMiniPlayerState* State = Pawn ? Pawn->GetPlayerState<AMiniPlayerState>() : nullptr;
	const UMiniAbilitySystemComponent* ASC = State ? State->GetMiniAbilitySystemComponent() : nullptr;
	FGameplayTagContainer FireTags;
	FireTags.AddTag(MiniGameplayTags::Ability_Fire);
	bool bHasFireGrant = false;
	if (ASC && Equipment)
	{
		for (const FGameplayAbilitySpecHandle& Handle : Equipment->GetGrantedHandles().AbilityHandles)
		{
			const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Handle);
			bHasFireGrant |= Spec && Spec->SourceObject.Get() == Equipment &&
				Spec->Ability && Spec->Ability->IsA<UMiniGameplayAbility_RangedFire>();
		}
	}
	if (!Pawn || !Pawn->HasAuthority() || !Pawn->GetController() ||
		Pawn->GetController()->GetPawn() != Pawn || !Pawn->GetHealthComponent() ||
		Pawn->GetHealthComponent()->IsDead() || !ASC || ASC->GetAvatarActor() != Pawn ||
		ASC->HasMatchingGameplayTag(MiniGameplayTags::State_Dead) ||
		ASC->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading) ||
		ASC->IsAbilityInputBlocked() || !ASC->AreAbilityTagRequirementsMet(FireTags) ||
		!Equipment || !WeaponDefinition || !Item || !bHasFireGrant ||
		!Item->GetInstanceId().IsValid() || Item->GetInstanceId() != Equipment->GetSourceItemId())
	{
		return nullptr;
	}
	return WeaponDefinition;
}

bool UMiniRangedWeaponComponent::IsPlausibleView(const AMiniCharacter* Pawn,
	const FVector& CameraOrigin, const FVector& AimDirection) const
{
	const bool bProbeDiagnostic = FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask17"));
	if (!Pawn || !GetWorld() || !IsFiniteVector(CameraOrigin) || !IsFiniteVector(AimDirection) ||
		!FMath::IsNearlyEqual(AimDirection.SizeSquared(), 1.0, 0.04))
	{
		if (bProbeDiagnostic)
		{
			UE_LOG(LogMiniInit, Warning,
				TEXT("MiniTask17Probe INVALID_VIEW: InvalidInput Pawn=%d World=%d Origin=%s Aim=%s AimSizeSq=%.3f"),
				Pawn ? 1 : 0, GetWorld() ? 1 : 0, *CameraOrigin.ToString(),
				*AimDirection.ToString(), AimDirection.SizeSquared());
		}
		return false;
	}
	const AController* Controller = Pawn->GetController();
	if (!Controller)
	{
		if (bProbeDiagnostic)
		{
			UE_LOG(LogMiniInit, Warning, TEXT("MiniTask17Probe INVALID_VIEW: NoController"));
		}
		return false;
	}
	const FRotator ControlRotation = Controller->GetControlRotation();
	const FVector ServerDirection = ControlRotation.Vector().GetSafeNormal();
	// The control rotation is independently replicated and can lag a shot RPC.
	// This still forbids backward and sharply unrelated aim claims.
	if (FVector::DotProduct(AimDirection, ServerDirection) < 0.65)
	{
		if (bProbeDiagnostic)
		{
			UE_LOG(LogMiniInit, Warning,
				TEXT("MiniTask17Probe INVALID_VIEW: AimDot Dot=%.3f Aim=%s ServerRotation=%s Pawn=%s Origin=%s"),
				FVector::DotProduct(AimDirection, ServerDirection), *AimDirection.ToString(),
				*ControlRotation.ToString(), *Pawn->GetActorLocation().ToString(), *CameraOrigin.ToString());
		}
		return false;
	}

	const FVector Pivot = Pawn->GetActorLocation();
	if (FVector::DistSquared(CameraOrigin, Pivot) > FMath::Square(430.0))
	{
		if (bProbeDiagnostic)
		{
			UE_LOG(LogMiniInit, Warning,
				TEXT("MiniTask17Probe INVALID_VIEW: PivotDistance Distance=%.1f Origin=%s Pivot=%s"),
				FVector::Dist(CameraOrigin, Pivot), *CameraOrigin.ToString(), *Pivot.ToString());
		}
		return false;
	}
	const UMiniPawnData* PawnData = Pawn->GetPawnData();
	const AMiniPlayerState* State = Pawn->GetPlayerState<AMiniPlayerState>();
	const UMiniAbilitySystemComponent* ASC = State ? State->GetMiniAbilitySystemComponent() : nullptr;
	TSubclassOf<UMiniCameraMode> ModeClass = (PawnData && ASC &&
		ASC->HasMatchingGameplayTag(MiniGameplayTags::State_Aiming)) ? PawnData->AimCameraMode : nullptr;
	if (!ModeClass && PawnData)
	{
		ModeClass = PawnData->DefaultCameraMode;
	}
	const UMiniCameraMode* Mode = ModeClass ? ModeClass.GetDefaultObject() :
		GetDefault<UMiniCameraMode_ThirdPerson>();
	const FVector IdealCamera = Pivot + ControlRotation.RotateVector(Mode->CameraOffset);
	const FVector ClosestOnCameraArm = FMath::ClosestPointOnSegment(CameraOrigin, Pivot, IdealCamera);
	// The actual camera can move inward around a wall and lag a quick turn.
	if (FVector::DistSquared(CameraOrigin, ClosestOnCameraArm) > FMath::Square(220.0))
	{
		if (bProbeDiagnostic)
		{
			UE_LOG(LogMiniInit, Warning,
				TEXT("MiniTask17Probe INVALID_VIEW: CameraArm Distance=%.1f Origin=%s Pivot=%s Ideal=%s Rotation=%s"),
				FVector::Dist(CameraOrigin, ClosestOnCameraArm), *CameraOrigin.ToString(),
				*Pivot.ToString(), *IdealCamera.ToString(), *ControlRotation.ToString());
		}
		return false;
	}
	FCollisionQueryParams CameraArmParams(SCENE_QUERY_STAT(MiniWeaponCameraArm), false, Pawn);
	FHitResult CameraArmHit;
	if (GetWorld()->SweepSingleByChannel(CameraArmHit, Pivot, CameraOrigin, FQuat::Identity,
		ECC_Camera, FCollisionShape::MakeSphere(6.0f), CameraArmParams) &&
		CameraArmHit.Time < 0.96f)
	{
		if (bProbeDiagnostic)
		{
			UE_LOG(LogMiniInit, Warning,
				TEXT("MiniTask17Probe INVALID_VIEW: Obstructed Time=%.3f Hit=%s Origin=%s Pivot=%s"),
				CameraArmHit.Time, *GetPathNameSafe(CameraArmHit.GetActor()),
				*CameraOrigin.ToString(), *Pivot.ToString());
		}
		return false;
	}
	return true;
}

FVector UMiniRangedWeaponComponent::GetMuzzleLocation(const AMiniCharacter* Pawn) const
{
	if (Pawn)
	{
		const USkeletalMeshComponent* WeaponMesh = Pawn->GetPracticeRifleMesh();
		if (WeaponMesh && WeaponMesh->DoesSocketExist(TEXT("Muzzle")))
		{
			const FVector SocketLocation = WeaponMesh->GetSocketLocation(TEXT("Muzzle"));
			if (IsFiniteVector(SocketLocation) &&
				FVector::DistSquared(SocketLocation, Pawn->GetActorLocation()) < FMath::Square(250.0))
			{
				return SocketLocation;
			}
		}
		return Pawn->GetActorLocation() + FVector(0.0, 0.0, 55.0) +
			Pawn->GetControlRotation().Vector() * 55.0;
	}
	return FVector::ZeroVector;
}

bool UMiniRangedWeaponComponent::TryFireOnServer(const FVector& CameraOrigin,
	const FVector& AimDirection, uint32 ShotSequence)
{
	const AMiniCharacter* Pawn = Cast<AMiniCharacter>(GetOwner());
	const UMiniEquipmentManagerComponent* Manager = Pawn ? Pawn->GetEquipmentManager() : nullptr;
	const UMiniEquipmentInstance* Equipment = Manager ? Manager->GetCurrentEquipment() : nullptr;
	return TryFireOnServer(CameraOrigin, AimDirection, ShotSequence,
		Equipment ? Equipment->GetSourceItemId() : FGuid());
}

bool UMiniRangedWeaponComponent::RejectFire(EMiniFireRejectionReason Reason)
{
	LastFireRejectionReason = Reason;
	return false;
}

bool UMiniRangedWeaponComponent::TryFireOnServer(const FVector& CameraOrigin,
	const FVector& AimDirection, uint32 ShotSequence, const FGuid& SourceItemId)
{
	AMiniCharacter* Pawn = Cast<AMiniCharacter>(GetOwner());
	if (!Pawn || !Pawn->HasAuthority() || !GetWorld() || ShotSequence == 0)
	{
		return RejectFire(EMiniFireRejectionReason::InvalidSequence);
	}
	const UMiniEquipmentManagerComponent* Manager = Pawn->GetEquipmentManager();
	UMiniEquipmentInstance* CurrentEquipment = Manager ? Manager->GetCurrentEquipment() : nullptr;
	if (!SourceItemId.IsValid() || !CurrentEquipment ||
		CurrentEquipment->GetSourceItemId() != SourceItemId)
	{
		return RejectFire(EMiniFireRejectionReason::WrongEquipment);
	}
	const uint32* LastItemSequence = LastProcessedSequenceByItem.Find(SourceItemId);
	if (LastItemSequence && ShotSequence <= *LastItemSequence)
	{
		return RejectFire(EMiniFireRejectionReason::InvalidSequence);
	}
	// Consume even rejected request identifiers for this item, preventing replay after re-equip.
	LastProcessedSequenceByItem.Add(SourceItemId, ShotSequence);
	LastProcessedSequence = FMath::Max(LastProcessedSequence, ShotSequence);
	const UMiniRangedWeaponEquipmentDefinition* WeaponDefinition = GetUsableRangedWeapon(Pawn);
	if (!WeaponDefinition)
	{
		return RejectFire(EMiniFireRejectionReason::UnusableWeapon);
	}
	if (!IsPlausibleView(Pawn, CameraOrigin, AimDirection))
	{
		return RejectFire(EMiniFireRejectionReason::InvalidView);
	}
	UMiniInventoryItemInstance* Item = CurrentEquipment->GetSourceItem();
	const int32 Ammo = Item ? Item->GetStat(MiniInventoryTags::AmmoInMagazine) : 0;
	if (!Item)
	{
		return RejectFire(EMiniFireRejectionReason::UnusableWeapon);
	}
	if (Ammo <= 0)
	{
		++EmptyMagazineRejectionCount;
		if (!EmptyMagazineNotifiedItems.Contains(SourceItemId))
		{
			EmptyMagazineNotifiedItems.Add(SourceItemId);
			ClientNotifyEmptyMagazine(CurrentEquipment);
		}
		return RejectFire(EMiniFireRejectionReason::EmptyMagazine);
	}
	// The first positive magazine observed after a reload rearms empty feedback.
	EmptyMagazineNotifiedItems.Remove(SourceItemId);
	const double Now = GetWorld()->GetTimeSeconds();
	const double* LastAcceptedFireTime = LastAcceptedFireTimeByItem.Find(SourceItemId);
	if (LastAcceptedFireTime && Now - *LastAcceptedFireTime + 0.0001 < WeaponDefinition->GetFireInterval())
	{
		return RejectFire(EMiniFireRejectionReason::FireRate);
	}
	if (!Item->SetStat(MiniInventoryTags::AmmoInMagazine, Ammo - 1))
	{
		return RejectFire(EMiniFireRejectionReason::AmmoMutationFailed);
	}
	LastAcceptedFireTimeByItem.Add(SourceItemId, Now);
	LastFireRejectionReason = EMiniFireRejectionReason::None;
	++AcceptedShotCount;
	LastHitCharacter.Reset();
	LastAcceptedCameraOrigin = CameraOrigin;
	LastAcceptedAimDirection = AimDirection.GetSafeNormal();
	AMiniPlayerState* ShooterState = Pawn->GetPlayerState<AMiniPlayerState>();
	UMiniAbilitySystemComponent* ShooterASC = ShooterState
		? ShooterState->GetMiniAbilitySystemComponent() : nullptr;
	if (ShooterASC)
	{
		FGameplayCueParameters FireCue;
		FireCue.Location = GetMuzzleLocation(Pawn);
		// RawMagnitude is an exactly representable float for a normal play session.
		// It identifies the locally predicted shot so its server echo is suppressed.
		FireCue.RawMagnitude = static_cast<float>(ShotSequence);
		FireCue.Instigator = Pawn;
		FireCue.EffectCauser = Pawn;
		ShooterASC->ExecuteGameplayCue(
			WeaponDefinition->IsA<UMiniPistolEquipmentDefinition>()
				? MiniGameplayTags::GameplayCue_Mini_PistolFire
				: MiniGameplayTags::GameplayCue_Mini_RifleFire, FireCue);
	}

	const FVector Direction = AimDirection.GetSafeNormal();
	const float Range = WeaponDefinition->GetFireRange();
	const FVector CameraEnd = CameraOrigin + Direction * Range;
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(MiniWeaponFire), false, Pawn);
	FHitResult CameraHit;
	const bool bCameraHit = GetWorld()->LineTraceSingleByChannel(
		CameraHit, CameraOrigin, CameraEnd, ECC_Visibility, TraceParams);
	const FVector Focus = bCameraHit ? CameraHit.ImpactPoint : CameraEnd;
	const FVector Muzzle = GetMuzzleLocation(Pawn);
	const FVector MuzzleToFocus = Focus - Muzzle;
	FHitResult MuzzleHit;
	bool bMuzzleHit = false;
	if (!MuzzleToFocus.IsNearlyZero())
	{
		const float MuzzleRange = FMath::Min(Range, MuzzleToFocus.Size() + 1.0f);
		bMuzzleHit = GetWorld()->LineTraceSingleByChannel(MuzzleHit, Muzzle,
			Muzzle + MuzzleToFocus.GetSafeNormal() * MuzzleRange, ECC_Visibility, TraceParams);
	}
	if (CVarMiniWeaponDrawTraces.GetValueOnGameThread() != 0)
	{
		DrawDebugLine(GetWorld(), CameraOrigin, Focus, FColor::Yellow, false, 2.0f, 0, 1.5f);
		DrawDebugLine(GetWorld(), Muzzle, bMuzzleHit ? MuzzleHit.ImpactPoint : Focus,
			bMuzzleHit && MuzzleHit.GetActor() == CameraHit.GetActor() ? FColor::Green : FColor::Red,
			false, 2.0f, 0, 2.0f);
	}

	// Both the crosshair ray and physical muzzle path must see the same character.
	// A shoulder camera that sees around a corner cannot shoot through the wall.
	AMiniCharacter* Target = bCameraHit ? Cast<AMiniCharacter>(CameraHit.GetActor()) : nullptr;
	if (Target && Target != Pawn && bMuzzleHit && MuzzleHit.GetActor() == Target)
	{
		AMiniGameMode* GameMode = GetWorld()->GetAuthGameMode<AMiniGameMode>();
		const AMiniPlayerState* TargetState = Target->GetPlayerState<AMiniPlayerState>();
		const UMiniHealthSet* TargetHealthSet = TargetState ? TargetState->GetHealthSet() : nullptr;
		const float OldHealth = TargetHealthSet ? TargetHealthSet->GetHealth() : 0.0f;
		if (GameMode && GameMode->TryApplyDamage(Pawn, Target, WeaponDefinition->GetFireDamage()))
		{
			LastHitCharacter = Target;
			const float AppliedDamage = TargetHealthSet
				? FMath::Max(0.0f, OldHealth - TargetHealthSet->GetHealth())
				: WeaponDefinition->GetFireDamage();
			ClientNotifyHitConfirmed(ShotSequence, AppliedDamage,
				Target->GetHealthComponent() && Target->GetHealthComponent()->IsDead());
			if (UMiniAbilitySystemComponent* TargetASC = TargetState
				? TargetState->GetMiniAbilitySystemComponent() : nullptr)
			{
				FGameplayCueParameters DamageCue;
				DamageCue.Location = MuzzleHit.ImpactPoint;
				DamageCue.RawMagnitude = AppliedDamage;
				DamageCue.Instigator = Pawn;
				DamageCue.EffectCauser = Pawn;
				TargetASC->ExecuteGameplayCue(MiniGameplayTags::GameplayCue_Mini_Damage, DamageCue);
			}
		}
	}
	if (bMuzzleHit && ShooterASC)
	{
		FGameplayCueParameters ImpactCue;
		ImpactCue.Location = MuzzleHit.ImpactPoint;
		ImpactCue.Normal = MuzzleHit.ImpactNormal;
		ImpactCue.Instigator = Pawn;
		ImpactCue.EffectCauser = Pawn;
		ShooterASC->ExecuteGameplayCue(MiniGameplayTags::GameplayCue_Mini_Impact, ImpactCue);
	}
	UE_LOG(LogMiniInit, Display,
		TEXT("MiniWeapon FIRE_ACCEPTED: Pawn=%s Sequence=%u Ammo=%d Hit=%s CameraHit=%s MuzzleHit=%s"),
		*Pawn->GetPathName(), ShotSequence, Ammo - 1, *GetNameSafe(LastHitCharacter.Get()),
		*GetNameSafe(CameraHit.GetActor()), *GetNameSafe(MuzzleHit.GetActor()));
	return true;
}
