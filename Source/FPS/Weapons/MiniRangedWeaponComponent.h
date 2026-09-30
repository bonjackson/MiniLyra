#pragma once

#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "Misc/Guid.h"
#include "MiniRangedWeaponComponent.generated.h"

class AMiniCharacter;
class UMiniEquipmentInstance;
class UMiniRangedWeaponEquipmentDefinition;

/** Server-side reason for the last fire request, useful for diagnostics. */
enum class EMiniFireRejectionReason : uint8
{
	None,
	InvalidSequence,
	WrongEquipment,
	UnusableWeapon,
	InvalidView,
	FireRate,
	EmptyMagazine,
	AmmoMutationFailed
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMiniEmptyMagazineEvent, FGuid, ItemId);

/** A small, server-authoritative hitscan path shared by equipped ranged weapons. */
UCLASS(ClassGroup = (Mini), meta = (BlueprintSpawnableComponent))
class FPS_API UMiniRangedWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMiniRangedWeaponComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Called by the equipped Fire ability on the locally controlled Pawn. */
	UFUNCTION(BlueprintCallable, Category = "Mini|Weapon")
	void RequestFire();

	/** Owner-only empty-magazine notice, after the server rejects a shot. */
	UPROPERTY(BlueprintAssignable, Category = "Mini|Weapon")
	FMiniEmptyMagazineEvent OnEmptyMagazine;

	/** Authority-side observations for diagnostics. These values are never client authority. */
	uint32 GetAcceptedShotCount() const { return AcceptedShotCount; }
	uint32 GetLastProcessedSequence() const { return LastProcessedSequence; }
	AMiniCharacter* GetLastHitCharacter() const { return LastHitCharacter.Get(); }
	FVector GetLastAcceptedCameraOrigin() const { return LastAcceptedCameraOrigin; }
	FVector GetLastAcceptedAimDirection() const { return LastAcceptedAimDirection; }
	uint32 GetEmptyMagazineRejectionCount() const { return EmptyMagazineRejectionCount; }
	EMiniFireRejectionReason GetLastFireRejectionReason() const { return LastFireRejectionReason; }
	FVector GetMuzzleLocation(const AMiniCharacter* Pawn) const;

	/** Diagnostic entry point: applies the same validation as an owner RPC. */
	bool TryFireOnServer(const FVector& CameraOrigin, const FVector& AimDirection, uint32 ShotSequence);
	bool TryFireOnServer(const FVector& CameraOrigin, const FVector& AimDirection,
		uint32 ShotSequence, const FGuid& SourceItemId);

private:
	UFUNCTION(Server, Reliable)
	void ServerFire(FVector_NetQuantize CameraOrigin, FVector_NetQuantizeNormal AimDirection,
		uint32 ShotSequence, FGuid SourceItemId, UMiniEquipmentInstance* SourceEquipment);

	UFUNCTION(Client, Reliable)
	void ClientNotifyEmptyMagazine(UMiniEquipmentInstance* SourceEquipment);

	bool IsPlausibleView(const AMiniCharacter* Pawn, const FVector& CameraOrigin,
		const FVector& AimDirection) const;
	const UMiniRangedWeaponEquipmentDefinition* GetUsableRangedWeapon(AMiniCharacter* Pawn) const;
	bool RejectFire(EMiniFireRejectionReason Reason);
	void CancelActiveFireAbilityForEmptyMagazine(UMiniEquipmentInstance* SourceEquipment);

	uint32 NextLocalSequence = 0;
	/** Diagnostic maximum; replay decisions use the current item's entry below. */
	uint32 LastProcessedSequence = 0;
	TMap<FGuid, uint32> LastProcessedSequenceByItem;
	uint32 AcceptedShotCount = 0;
	uint32 EmptyMagazineRejectionCount = 0;
	/** One owner notice per depleted magazine, including delayed in-flight shots. */
	TSet<FGuid> EmptyMagazineNotifiedItems;
	EMiniFireRejectionReason LastFireRejectionReason = EMiniFireRejectionReason::None;
	TMap<FGuid, double> LastAcceptedFireTimeByItem;
	TWeakObjectPtr<AMiniCharacter> LastHitCharacter;
	FVector LastAcceptedCameraOrigin = FVector::ZeroVector;
	FVector LastAcceptedAimDirection = FVector::ZeroVector;
};
