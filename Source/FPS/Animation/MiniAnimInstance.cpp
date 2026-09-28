#include "MiniAnimInstance.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

void UMiniAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	GroundSpeed = 0.0f;
	bIsMoving = false;
	bIsInAir = false;
	bIsAscending = false;
	bRecentlyLanded = false;
	LandingTimeRemaining = 0.0f;
}

void UMiniAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	const ACharacter* Character = Cast<ACharacter>(TryGetPawnOwner());
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Character || !Movement)
	{
		GroundSpeed = 0.0f;
		bIsMoving = bIsInAir = bIsAscending = bRecentlyLanded = false;
		LandingTimeRemaining = 0.0f;
		return;
	}

	const bool bWasInAir = bIsInAir;
	GroundSpeed = Character->GetVelocity().Size2D();
	bIsMoving = GroundSpeed > 8.0f;
	bIsInAir = Movement->IsFalling();
	bIsAscending = bIsInAir && Character->GetVelocity().Z > 15.0f;
	if (bWasInAir && !bIsInAir)
	{
		LandingTimeRemaining = 0.22f;
	}
	else
	{
		LandingTimeRemaining = FMath::Max(0.0f, LandingTimeRemaining - DeltaSeconds);
	}
	bRecentlyLanded = !bIsInAir && LandingTimeRemaining > 0.0f;
}
