#pragma once

#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "MiniTask20ProbeTravelSubsystem.generated.h"

class AMiniPracticeTarget;
class AMiniPracticeSupply;
class UMiniHUDLayout;
class UMiniHUDViewModel;
class UMiniHUDDataWidget;
class UMiniDebugMenuWidget;
class UMiniPrimaryGameLayout;
class UMiniGameFeatureAction_AddActors;

/** One GameInstance survives three rounds of real OpenLevel + ServerTravel. */
UCLASS()
class FPS_API UMiniTask20ProbeTravelSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override;
private:
	void HandlePostWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	void Fail(const TCHAR* Reason);
	bool ReadyWorld(UWorld* World);
	bool PrepareAndTravel(UWorld* World);
	bool OldCallbacksStayedSilent() const;
	void ReleaseRetainedObjects();
	FDelegateHandle CleanupHandle;
	float WaitSeconds = 0;
	double CleanupWallTime = 0;
	int32 TravelsRequested = 0;
	int32 CleanupsVerified = 0;
	int32 WorldsVerified = 0;
	int32 SupplyRefillAtTravel = 0;
	bool bAwaitingTravel = false;
	bool bInitialized = false;
	bool bCleanupVerified = false;
	bool bFailed = false;
	bool bDone = false;
	UPROPERTY(Transient) TObjectPtr<UWorld> OldWorld;
	UPROPERTY(Transient) TArray<TObjectPtr<AMiniPracticeTarget>> OldTargets;
	UPROPERTY(Transient) TObjectPtr<AMiniPracticeSupply> OldSupply;
	UPROPERTY(Transient) TObjectPtr<UMiniGameFeatureAction_AddActors> OldActorAction;
	UPROPERTY(Transient) TObjectPtr<UMiniHUDLayout> OldHUD;
	UPROPERTY(Transient) TObjectPtr<UMiniHUDViewModel> OldVM;
	UPROPERTY(Transient) TArray<TObjectPtr<UMiniHUDDataWidget>> OldWidgets;
	UPROPERTY(Transient) TObjectPtr<UMiniPrimaryGameLayout> OldRoot;
	UPROPERTY(Transient) TObjectPtr<UMiniDebugMenuWidget> OldMenu;
};
