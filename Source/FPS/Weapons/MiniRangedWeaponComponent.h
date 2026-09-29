#pragma once

#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "MiniRangedWeaponComponent.generated.h"

class AMiniCharacter;
class UMiniRifleEquipmentDefinition;

/** A small, server-authoritative hitscan path for the equipped rifle. */
UCLASS(ClassGroup = (Mini), meta = (BlueprintSpawnableComponent))
class FPS_API UMiniRangedWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMiniRangedWeaponComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Called by the equipped Fire ability on the locally controlled Pawn. */
	UFUNCTION(BlueprintCallable, Category = "Mini|Weapon")
	void RequestFire();

	/** Authority-side observations for diagnostics. These values are never client authority. */
	uint32 GetAcceptedShotCount() const { return AcceptedShotCount; }
	uint32 GetLastProcessedSequence() const { return LastProcessedSequence; }
	AMiniCharacter* GetLastHitCharacter() const { return LastHitCharacter.Get(); }
	FVector GetLastAcceptedCameraOrigin() const { return LastAcceptedCameraOrigin; }
	FVector GetLastAcceptedAimDirection() const { return LastAcceptedAimDirection; }
	FVector GetMuzzleLocation(const AMiniCharacter* Pawn) const;

	/** Diagnostic entry point: applies the same validation as an owner RPC. */
	bool TryFireOnServer(const FVector& CameraOrigin, const FVector& AimDirection, uint32 ShotSequence);

private:
	UFUNCTION(Server, Reliable)
	void ServerFire(FVector_NetQuantize CameraOrigin, FVector_NetQuantizeNormal AimDirection, uint32 ShotSequence);

	bool IsPlausibleView(const AMiniCharacter* Pawn, const FVector& CameraOrigin,
		const FVector& AimDirection) const;
	const UMiniRifleEquipmentDefinition* GetUsableRifle(AMiniCharacter* Pawn) const;

	uint32 NextLocalSequence = 0;
	uint32 LastProcessedSequence = 0;
	uint32 AcceptedShotCount = 0;
	double LastAcceptedFireTime = -1000000.0;
	TWeakObjectPtr<AMiniCharacter> LastHitCharacter;
	FVector LastAcceptedCameraOrigin = FVector::ZeroVector;
	FVector LastAcceptedAimDirection = FVector::ZeroVector;
};
