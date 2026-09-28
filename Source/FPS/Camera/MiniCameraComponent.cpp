#include "MiniCameraComponent.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "Camera/MiniCameraMode.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHeroComponent.h"
#include "Character/MiniPawnData.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniGameplayTags.h"

UMiniCameraComponent::UMiniCameraComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bUsePawnControlRotation = false;
	bAutoActivate = true;
}

void UMiniCameraComponent::ResetCamera()
{
	bHasView = false;
	CurrentCameraMode = nullptr;
	bLastCollisionBlocked = false;
}

void UMiniCameraComponent::GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView)
{
	Super::GetCameraView(DeltaTime, DesiredView);
	const AMiniCharacter* MiniPawn = Cast<AMiniCharacter>(GetOwner());
	if (!MiniPawn || !GetWorld())
	{
		return;
	}

	const UMiniPawnData* PawnData = MiniPawn->GetPawnData();
	const AMiniPlayerState* MiniState = MiniPawn->GetPlayerState<AMiniPlayerState>();
	const UMiniAbilitySystemComponent* ASC = MiniState ? MiniState->GetMiniAbilitySystemComponent() : nullptr;
	const UMiniHeroComponent* Hero = MiniPawn->GetHeroComponent();
	const bool bAiming = ASC && ASC->GetAvatarActor() == MiniPawn && !ASC->IsAbilityInputBlocked() &&
		Hero && Hero->OwnsTemporaryAimTag(ASC) && ASC->HasMatchingGameplayTag(MiniGameplayTags::State_Aiming);
	TSubclassOf<UMiniCameraMode> ModeClass = bAiming && PawnData ? PawnData->AimCameraMode : nullptr;
	if (!ModeClass && PawnData)
	{
		ModeClass = PawnData->DefaultCameraMode;
	}
	if (!ModeClass)
	{
		ModeClass = UMiniCameraMode_ThirdPerson::StaticClass();
	}
	const UMiniCameraMode* Mode = ModeClass->GetDefaultObject<UMiniCameraMode>();
	if (!Mode)
	{
		return;
	}
	CurrentCameraMode = ModeClass;

	const AController* Controller = MiniPawn->GetController();
	const FRotator TargetRotation = Controller ? Controller->GetControlRotation() : MiniPawn->GetActorRotation();
	// Aim the third-person frame at the capsule center, not the eye height.
	// An eye-height pivot pushes the entire mannequin into the bottom of the view.
	const FVector Pivot = MiniPawn->GetActorLocation();
	if (!bHasView)
	{
		SmoothedRotation = TargetRotation;
		SmoothedFOV = Mode->FieldOfView;
	}
	else
	{
		SmoothedRotation = FMath::RInterpTo(SmoothedRotation, TargetRotation, DeltaTime, Mode->RotationLagSpeed);
		SmoothedFOV = FMath::FInterpTo(SmoothedFOV, Mode->FieldOfView, DeltaTime, Mode->BlendSpeed);
	}
	LastIdealLocation = Pivot + SmoothedRotation.RotateVector(Mode->CameraOffset);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MiniCamera), false, MiniPawn);
	FHitResult Hit;
	const FCollisionShape Probe = FCollisionShape::MakeSphere(Mode->CollisionProbeRadius);
	const bool bHit = GetWorld()->SweepSingleByChannel(Hit, Pivot, LastIdealLocation,
		FQuat::Identity, ECC_Camera, Probe, QueryParams);
	const FVector SafeLocation = bHit ? Hit.Location : LastIdealLocation;
	if (!bHasView || FVector::DistSquared(Pivot, SafeLocation) < FVector::DistSquared(Pivot, SmoothedLocation))
	{
		// Move inward immediately when a wall is encountered.
		SmoothedLocation = SafeLocation;
	}
	else
	{
		SmoothedLocation = FMath::VInterpTo(SmoothedLocation, SafeLocation, DeltaTime, Mode->RecoveryLagSpeed);
	}
	// The recovery path can cross a new obstacle; clamp it with a second sweep.
	FHitResult RecoveryHit;
	if (GetWorld()->SweepSingleByChannel(RecoveryHit, Pivot, SmoothedLocation,
		FQuat::Identity, ECC_Camera, Probe, QueryParams))
	{
		SmoothedLocation = RecoveryHit.Location;
	}
	bLastCollisionBlocked = bHit || RecoveryHit.bBlockingHit;
	bHasView = true;
	DesiredView.Location = SmoothedLocation;
	DesiredView.Rotation = SmoothedRotation;
	DesiredView.FOV = SmoothedFOV;
}
