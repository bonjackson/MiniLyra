#pragma once

#include "UI/MiniHUDMessages.h"
#include "MiniHUDViewModel.generated.h"

class AMiniPlayerController;
class APlayerController;
class APlayerState;
class APawn;
class UCommonLocalPlayer;
class UMiniAbilitySystemComponent;
class UMiniCombatFeedbackComponent;
class UMiniInventoryItemInstance;
class UMiniInventoryManagerComponent;
class UMiniQuickBarComponent;
class UMiniRangedWeaponComponent;
struct FOnAttributeChangeData;
struct FActorInitStateChangedParams;

/** A HUD-layout-owned local bridge. Start/Stop have no network authority. */
UCLASS()
class FPS_API UMiniHUDViewModel : public UObject
{
	GENERATED_BODY()

public:
	virtual UWorld* GetWorld() const override { return BoundWorld.Get(); }
	virtual void BeginDestroy() override;
	void Start(ULocalPlayer* LocalPlayer, UWorld* World);
	void Stop();
	const FMiniHUDSnapshot& GetSnapshot() const { return Snapshot; }
	bool IsRunning() const { return bRunning; }
	int32 GetBindingCount() const;
	int32 GetStateRefreshCount() const { return StateRefreshCount; }
	int32 GetHitMessageCount() const { return HitMessageCount; }
	int32 GetEmptyMessageCount() const { return EmptyMessageCount; }

private:
	void RebindSources();
	void UnbindSources();
	void BindActiveItem();
	void RefreshSnapshot();
	void HandleLocalController(UCommonLocalPlayer* LocalPlayer, APlayerController* Controller);
	void HandleLocalPlayerState(UCommonLocalPlayer* LocalPlayer, APlayerState* PlayerState);
	void HandleLocalPawn(UCommonLocalPlayer* LocalPlayer, APawn* Pawn);
	void HandlePawnInitState(const FActorInitStateChangedParams& Params);
	void HandleAttributeChanged(const FOnAttributeChangeData& Data);
	void HandleStateTagChanged(FGameplayTag Tag, int32 NewCount);
	void HandleInventoryChanged();
	void HandleItemChanged();
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	UFUNCTION()
	void HandleHitConfirmed(int32 ShotSequence, float AppliedDamage, bool bKilled);
	UFUNCTION()
	void HandleEmptyMagazine(FGuid ItemId);

	UPROPERTY(Transient)
	FMiniHUDSnapshot Snapshot;
	TWeakObjectPtr<UCommonLocalPlayer> BoundLocalPlayer;
	TWeakObjectPtr<UWorld> BoundWorld;
	TWeakObjectPtr<AMiniPlayerController> BoundController;
	TWeakObjectPtr<AMiniCharacter> BoundPawn;
	TWeakObjectPtr<UMiniAbilitySystemComponent> BoundASC;
	TWeakObjectPtr<UMiniInventoryManagerComponent> BoundInventory;
	TWeakObjectPtr<UMiniQuickBarComponent> BoundQuickBar;
	TWeakObjectPtr<UMiniInventoryItemInstance> BoundItem;
	TWeakObjectPtr<UMiniCombatFeedbackComponent> BoundFeedback;
	TWeakObjectPtr<UMiniRangedWeaponComponent> BoundWeapon;

	FDelegateHandle ControllerSetHandle;
	FDelegateHandle PlayerStateSetHandle;
	FDelegateHandle PawnSetHandle;
	FDelegateHandle WorldCleanupHandle;
	FDelegateHandle PawnInitHandle;
	FDelegateHandle HealthHandle;
	FDelegateHandle MaxHealthHandle;
	FDelegateHandle ReloadHandle;
	FDelegateHandle DeathHandle;
	FDelegateHandle InventoryHandle;
	FDelegateHandle QuickBarHandle;
	FDelegateHandle ItemHandle;
	bool bHitDelegateBound = false;
	bool bEmptyDelegateBound = false;
	bool bRunning = false;
	int32 StateRefreshCount = 0;
	int32 HitMessageCount = 0;
	int32 EmptyMessageCount = 0;
};
