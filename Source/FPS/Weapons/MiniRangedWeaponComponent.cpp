#include "MiniRangedWeaponComponent.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
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
#include "GameModes/MiniGameMode.h"
#include "HAL/IConsoleManager.h"
#include "Inventory/MiniInventoryItemDefinition.h"
#include "Inventory/MiniInventoryItemInstance.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniGameplayTags.h"
#include "System/MiniLogChannels.h"

namespace
{
TAutoConsoleVariable<int32> CVarMiniWeaponDrawTraces(
	TEXT("mini.Weapon.DrawTraces"), 0,
	TEXT("Draw long authoritative camera/muzzle rifle traces when nonzero."));
TAutoConsoleVariable<int32> CVarMiniWeaponLocalTracer(
	TEXT("mini.Weapon.LocalTracer"), 1,
	TEXT("Draw a short provisional owning-client rifle tracer when nonzero."));

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

	FMinimalViewInfo View;
	Camera->GetCameraView(0.0f, View);
	const FVector AimDirection = View.Rotation.Vector().GetSafeNormal();
	if (!IsFiniteVector(View.Location) || !IsFiniteVector(AimDirection) || AimDirection.IsNearlyZero())
	{
		return;
	}

	const uint32 ShotSequence = ++NextLocalSequence;
	if (CVarMiniWeaponLocalTracer.GetValueOnGameThread() != 0)
	{
		DrawDebugLine(GetWorld(), View.Location, View.Location + AimDirection * 1500.0,
			FColor::Cyan, false, 0.12f, 0, 1.5f);
	}
	if (Pawn->HasAuthority())
	{
		TryFireOnServer(View.Location, AimDirection, ShotSequence);
	}
	else
	{
		ServerFire(View.Location, AimDirection, ShotSequence);
	}
}

void UMiniRangedWeaponComponent::ServerFire_Implementation(
	FVector_NetQuantize CameraOrigin, FVector_NetQuantizeNormal AimDirection, uint32 ShotSequence)
{
	TryFireOnServer(CameraOrigin, AimDirection, ShotSequence);
}

const UMiniRifleEquipmentDefinition* UMiniRangedWeaponComponent::GetUsableRifle(AMiniCharacter* Pawn) const
{
	const UMiniEquipmentManagerComponent* Manager = Pawn ? Pawn->GetEquipmentManager() : nullptr;
	const UMiniEquipmentInstance* Equipment = Manager ? Manager->GetCurrentEquipment() : nullptr;
	const UMiniRifleEquipmentDefinition* Rifle = Equipment
		? Cast<UMiniRifleEquipmentDefinition>(Equipment->GetEquipmentDefinition().GetDefaultObject()) : nullptr;
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
				Spec->Ability && Spec->Ability->IsA<UMiniGameplayAbility_RifleFire>();
		}
	}
	if (!Pawn || !Pawn->HasAuthority() || !Pawn->GetController() ||
		Pawn->GetController()->GetPawn() != Pawn || !Pawn->GetHealthComponent() ||
		Pawn->GetHealthComponent()->IsDead() || !ASC || ASC->GetAvatarActor() != Pawn ||
		ASC->HasMatchingGameplayTag(MiniGameplayTags::State_Dead) ||
		ASC->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading) ||
		ASC->IsAbilityInputBlocked() || !ASC->AreAbilityTagRequirementsMet(FireTags) ||
		!Equipment || !Rifle || !Item || !bHasFireGrant ||
		!Item->GetInstanceId().IsValid() || Item->GetInstanceId() != Equipment->GetSourceItemId() ||
		Item->GetStat(MiniInventoryTags::AmmoInMagazine) <= 0)
	{
		return nullptr;
	}
	return Rifle;
}

bool UMiniRangedWeaponComponent::IsPlausibleView(const AMiniCharacter* Pawn,
	const FVector& CameraOrigin, const FVector& AimDirection) const
{
	if (!Pawn || !GetWorld() || !IsFiniteVector(CameraOrigin) || !IsFiniteVector(AimDirection) ||
		!FMath::IsNearlyEqual(AimDirection.SizeSquared(), 1.0, 0.04))
	{
		return false;
	}
	const AController* Controller = Pawn->GetController();
	if (!Controller)
	{
		return false;
	}
	const FRotator ControlRotation = Controller->GetControlRotation();
	const FVector ServerDirection = ControlRotation.Vector().GetSafeNormal();
	// The control rotation is independently replicated and can lag a shot RPC.
	// This still forbids backward and sharply unrelated aim claims.
	if (FVector::DotProduct(AimDirection, ServerDirection) < 0.65)
	{
		return false;
	}

	const FVector Pivot = Pawn->GetActorLocation();
	if (FVector::DistSquared(CameraOrigin, Pivot) > FMath::Square(430.0))
	{
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
		return false;
	}
	FCollisionQueryParams CameraArmParams(SCENE_QUERY_STAT(MiniWeaponCameraArm), false, Pawn);
	FHitResult CameraArmHit;
	if (GetWorld()->SweepSingleByChannel(CameraArmHit, Pivot, CameraOrigin, FQuat::Identity,
		ECC_Camera, FCollisionShape::MakeSphere(6.0f), CameraArmParams) &&
		CameraArmHit.Time < 0.96f)
	{
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
	AMiniCharacter* Pawn = Cast<AMiniCharacter>(GetOwner());
	if (!Pawn || !Pawn->HasAuthority() || !GetWorld() || ShotSequence == 0 ||
		ShotSequence <= LastProcessedSequence)
	{
		return false;
	}
	// Consume even rejected request identifiers so retransmission cannot later cause damage.
	LastProcessedSequence = ShotSequence;
	const UMiniRifleEquipmentDefinition* Rifle = GetUsableRifle(Pawn);
	if (!Rifle || !IsPlausibleView(Pawn, CameraOrigin, AimDirection))
	{
		return false;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastAcceptedFireTime + 0.0001 < Rifle->GetFireInterval())
	{
		return false;
	}
	UMiniEquipmentInstance* Equipment = Pawn->GetEquipmentManager()->GetCurrentEquipment();
	UMiniInventoryItemInstance* Item = Equipment ? Equipment->GetSourceItem() : nullptr;
	const int32 Ammo = Item ? Item->GetStat(MiniInventoryTags::AmmoInMagazine) : 0;
	if (!Item || Ammo <= 0 || !Item->SetStat(MiniInventoryTags::AmmoInMagazine, Ammo - 1))
	{
		return false;
	}
	LastAcceptedFireTime = Now;
	++AcceptedShotCount;
	LastHitCharacter.Reset();
	LastAcceptedCameraOrigin = CameraOrigin;
	LastAcceptedAimDirection = AimDirection.GetSafeNormal();

	const FVector Direction = AimDirection.GetSafeNormal();
	const float Range = Rifle->GetFireRange();
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
		if (GameMode && GameMode->TryApplyDamage(Pawn, Target, Rifle->GetFireDamage()))
		{
			LastHitCharacter = Target;
		}
	}
	UE_LOG(LogMiniInit, Display,
		TEXT("MiniWeapon FIRE_ACCEPTED: Pawn=%s Sequence=%u Ammo=%d Hit=%s CameraHit=%s MuzzleHit=%s"),
		*Pawn->GetPathName(), ShotSequence, Ammo - 1, *GetNameSafe(LastHitCharacter.Get()),
		*GetNameSafe(CameraHit.GetActor()), *GetNameSafe(MuzzleHit.GetActor()));
	return true;
}
