#pragma once

#include "Diagnostics/MiniTask17ProbeActor.h"
#include "Subsystems/WorldSubsystem.h"
#include "MiniTask17ProbeSubsystem.generated.h"

class AMiniCharacter;
class AMiniPlayerController;
class UMiniInventoryItemInstance;
class UMiniRangedWeaponComponent;

/** Three-process acceptance of private ammo, both weapon grants, reload cancellation and pistol fire. */
UCLASS()
class FPS_API UMiniTask17ProbeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	void TickServer(float DeltaTime);
	void TickClient(float DeltaTime);
	void Fail(const TCHAR* Reason);
	void Advance(EMiniTask17Phase NewPhase);
	bool StartServer();
	bool AllAcknowledged() const;
	bool CheckServerAmmo(int32 Owner, int32 RifleMag, int32 RifleReserve,
		int32 PistolMag, int32 PistolReserve) const;
	void AcknowledgeClient(AMiniTask17ProbeActor* Probe, const TCHAR* Marker);
	bool AimClientAtPeer(AMiniPlayerController* Controller, AMiniCharacter* Pawn,
		AMiniCharacter* Peer) const;
	UFUNCTION()
	void HandleEmptyMagazine(FGuid ItemId);

	EMiniTask17Phase ServerPhase = EMiniTask17Phase::Initial;
	float StageSeconds = 0.0f;
	bool bFailed = false;
	bool bServerStarted = false;
	bool bSawRifleReload = false;
	bool bSawPartialReload = false;
	bool bSawPistolReload = false;
	bool bServerTestedFireBlock = false;
	TWeakObjectPtr<AMiniPlayerController> Controllers[2];
	TWeakObjectPtr<AMiniCharacter> Pawns[2];
	TWeakObjectPtr<AMiniTask17ProbeActor> Probes[2];
	TWeakObjectPtr<UMiniInventoryItemInstance> RifleItem;
	TWeakObjectPtr<UMiniInventoryItemInstance> PistolItem;
	FGuid RifleItemId;
	float PistolDamage = 0.0f;
	float RifleReloadDuration = 0.0f;
	float PistolReloadDuration = 0.0f;

	EMiniTask17Phase ClientPhase = EMiniTask17Phase::Complete;
	float ClientPhaseSeconds = 0.0f;
	int32 LastClientDiagnosticSecond = INDEX_NONE;
	float ClientInputSeconds = 0.0f;
	bool bClientAimSet = false;
	bool bClientInputPressed = false;
	bool bClientInputReleased = false;
	bool bClientBlockedFirePressed = false;
	bool bClientBlockedFireReleased = false;
	float BlockedFireSeconds = 0.0f;
	bool bClientAcknowledged = false;
	bool bEmptyDelegateBound = false;
	int32 EmptyFeedbackCount = 0;
	FGuid LastEmptyFeedbackItem;
};
