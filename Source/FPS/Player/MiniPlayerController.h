#pragma once

#include "CommonPlayerController.h"
#include "TimerManager.h"
#include "MiniPlayerController.generated.h"

class AMiniCharacter;
class UMiniInventoryManagerComponent;
class UMiniQuickBarComponent;

/** Extension receiver for a local or remote Mini player. */
UCLASS()
class FPS_API AMiniPlayerController : public ACommonPlayerController
{
	GENERATED_BODY()

public:
	AMiniPlayerController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnRep_PlayerState() override;
	virtual void OnUnPossess() override;
	virtual void PostProcessInput(const float DeltaTime, const bool bGamePaused) override;
	virtual void SetupInputComponent() override;
	virtual void ClientReturnToMainMenuWithTextReason_Implementation(const FText& ReturnReason) override;
	void SetMiniInputBlocked(bool bBlocked);
	void SetMiniUIInputBlocked(bool bBlocked);
	bool IsMiniInputBlocked() const { return bMiniInputBlocked || bMiniUIInputBlocked; }
	UMiniInventoryManagerComponent* GetInventoryManager() const { return InventoryManager; }
	UMiniQuickBarComponent* GetQuickBar() const { return QuickBar; }

	/** Local console view of this controller's private inventory. */
	UFUNCTION(Exec)
	void MiniDumpInventory() const;
	UFUNCTION(Exec)
	void MiniDumpQuickBar() const;
	UFUNCTION(Exec)
	void MiniToggleMenu();

private:
	UPROPERTY(VisibleAnywhere, Category = "Mini|Inventory")
	TObjectPtr<UMiniInventoryManagerComponent> InventoryManager;

	UPROPERTY(VisibleAnywhere, Category = "Mini|Equipment")
	TObjectPtr<UMiniQuickBarComponent> QuickBar;

	UFUNCTION(Server, Reliable)
	void ServerAdvanceTask10Probe(int32 CompletedCycle);
	void FinishTask10ProbeRespawn();
	void AdvanceTask10Probe();
	void RefreshMiniInputBlock();

	enum class ETask10ProbeStage : uint8
	{
		WaitReady, WaitMove, WaitFireActive, WaitFireEnded, WaitUnpossessed, WaitNextPawn,
		WaitMenuFireActive, WaitMenuJumpActive, WaitMenuBlocked, WaitMenuNoFire, WaitMenuResumed,
		WaitMenuReactivated, WaitMenuReleased, WaitActionFireActive,
		WaitActionSuspended, WaitActionNoFire, WaitActionResumed,
		WaitActionReactivated, WaitActionReleased, Complete
	};

	bool bMiniInputBlocked = false;
	bool bMiniUIInputBlocked = false;
	bool bTask10ProbeEnabled = false;
	int32 Task10ProbeCycle = 0;
	int32 Task10ServerCompletedCycle = 0;
	TWeakObjectPtr<AMiniCharacter> Task10ServerOldPawn;
	ETask10ProbeStage Task10ProbeStage = ETask10ProbeStage::WaitReady;
	TWeakObjectPtr<AMiniCharacter> Task10ProbePawn;
	FVector Task10ProbeStartLocation = FVector::ZeroVector;
	float Task10ProbeOriginalJumpMaxHoldTime = 0.0f;
	FTimerHandle Task10ProbeTimer;
	FTimerHandle Task10RespawnTimer;
};
